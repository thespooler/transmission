/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef SYS_DARWIN
#include <fcntl.h>
#endif

#ifdef HAVE_FALLOCATE
 #include <linux/falloc.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h> /* O_LARGEFILE */

#include <event.h>
#include <evutil.h>

#include "transmission.h"
#include "fdlimit.h"
#include "list.h"
#include "net.h"
#include "platform.h" /* tr_lock */
#include "utils.h"

#define dbgmsg( ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, NULL, __VA_ARGS__ ); \
    } while( 0 )

/**
***
**/

enum
{
    TR_MAX_OPEN_FILES = 16, /* real files, not sockets */

    NOFILE_BUFFER = 512, /* the process' number of open files is
                            globalMaxPeers + NOFILE_BUFFER */
};

struct tr_openfile
{
    unsigned int    isCheckedOut  : 1;
    unsigned int    isWritable    : 1;
    unsigned int    closeWhenDone : 1;
    char            filename[MAX_PATH_LENGTH];
    int             fd;
    uint64_t        date;
};

struct tr_fd_s
{
    int                   socketCount;
    int                   socketMax;
    tr_lock *             lock;
    struct tr_openfile    open[TR_MAX_OPEN_FILES];
};

static struct tr_fd_s * gFd = NULL;

/***
****
****  Local Files
****
***/

static int
preallocateFile( int fd UNUSED, uint64_t length UNUSED )
{
#ifdef HAVE_FALLOCATE

    return fallocate( fd, FALLOC_FL_KEEP_SIZE, 0, length );

#elif defined(HAVE_POSIX_FALLOCATE)

    return posix_fallocate( fd, 0, length );

#elif defined(SYS_DARWIN) 

    fstore_t fst;
    fst.fst_flags = F_ALLOCATECONTIG;
    fst.fst_posmode = F_PEOFPOSMODE;
    fst.fst_offset = 0;
    fst.fst_length = length;
    fst.fst_bytesalloc = 0;
    return fcntl( fd, F_PREALLOCATE, &fst );

#else

    #warning no known method to preallocate files on this platform
    return -1;

#endif
}

/**
 * returns 0 on success, or an errno value on failure.
 * errno values include ENOENT if the parent folder doesn't exist,
 * plus the errno values set by tr_mkdirp() and open().
 */
static int
TrOpenFile( int          i,
            const char * folder,
            const char * torrentFile,
            int          doWrite,
            int          doPreallocate,
            uint64_t     desiredFileSize )
{
    struct tr_openfile * file = &gFd->open[i];
    int                  flags;
    char               * filename;
    struct stat          sb;
    int                  alreadyExisted;

    /* confirm the parent folder exists */
    if( stat( folder, &sb ) || !S_ISDIR( sb.st_mode ) )
        return ENOENT;

    /* create subfolders, if any */
    filename = tr_buildPath( folder, torrentFile, NULL );
    if( doWrite )
    {
        char * tmp = tr_dirname( filename );
        const int err = tr_mkdirp( tmp, 0777 ) ? errno : 0;
        tr_free( tmp );
        if( err ) {
            tr_free( filename );
            return err;
        }
    }

    alreadyExisted = !stat( filename, &sb ) && S_ISREG( sb.st_mode );
    
    /* open the file */
    flags = doWrite ? ( O_RDWR | O_CREAT ) : O_RDONLY;
#ifdef O_LARGEFILE
    flags |= O_LARGEFILE;
#endif
#ifdef WIN32
    flags |= O_BINARY;
#endif
    file->fd = open( filename, flags, 0666 );
    if( file->fd == -1 )
    {
        const int err = errno;
        tr_err( _( "Couldn't open \"%1$s\": %2$s" ), filename,
               tr_strerror( err ) );
        tr_free( filename );
        return err;
    }

    if( ( file->fd >= 0 ) && !alreadyExisted && doPreallocate )
        if( !preallocateFile( file->fd, desiredFileSize ) )
            tr_inf( _( "Preallocated file \"%s\"" ), filename );
       
    tr_free( filename );
    return 0;
}

static int
fileIsOpen( const struct tr_openfile * o )
{
    return o->fd >= 0;
}

static void
TrCloseFile( int i )
{
    struct tr_openfile * o = &gFd->open[i];

    assert( i >= 0 );
    assert( i < TR_MAX_OPEN_FILES );
    assert( fileIsOpen( o ) );

    close( o->fd );
    o->fd = -1;
    o->isCheckedOut = 0;
}

static int
fileIsCheckedOut( const struct tr_openfile * o )
{
    return fileIsOpen( o ) && o->isCheckedOut;
}

/* returns an fd on success, or a -1 on failure and sets errno */
int
tr_fdFileCheckout( const char * folder,
                   const char * torrentFile,
                   int          doWrite,
                   int          doPreallocate,
                   uint64_t     desiredFileSize )
{
    int                  i, winner = -1;
    struct tr_openfile * o;
    char               * filename;

    assert( folder && *folder );
    assert( torrentFile && *torrentFile );
    assert( doWrite == 0 || doWrite == 1 );

    filename = tr_buildPath( folder, torrentFile, NULL );
    dbgmsg( "looking for file '%s', writable %c", filename,
            doWrite ? 'y' : 'n' );

    tr_lockLock( gFd->lock );

    /* Is it already open? */
    for( i = 0; i < TR_MAX_OPEN_FILES; ++i )
    {
        o = &gFd->open[i];

        if( !fileIsOpen( o ) )
            continue;

        if( strcmp( filename, o->filename ) )
            continue;

        if( fileIsCheckedOut( o ) )
        {
            dbgmsg( "found it!  it's open, but checked out.  waiting..." );
            tr_lockUnlock( gFd->lock );
            tr_wait( 200 );
            tr_lockLock( gFd->lock );
            i = -1; /* reloop */
            continue;
        }

        if( doWrite && !o->isWritable )
        {
            dbgmsg(
                "found it!  it's open and available, but isn't writable. closing..." );
            TrCloseFile( i );
            break;
        }

        dbgmsg( "found it!  it's ready for use!" );
        winner = i;
        break;
    }

    dbgmsg(
        "it's not already open.  looking for an open slot or an old file." );
    while( winner < 0 )
    {
        uint64_t date = tr_date( ) + 1;

        /* look for the file that's been open longest */
        for( i = 0; i < TR_MAX_OPEN_FILES; ++i )
        {
            o = &gFd->open[i];

            if( !fileIsOpen( o ) )
            {
                winner = i;
                dbgmsg( "found an empty slot in %d", winner );
                break;
            }

            if( date > o->date )
            {
                date = o->date;
                winner = i;
            }
        }

        if( winner >= 0 )
        {
            if( fileIsOpen( &gFd->open[winner] ) )
            {
                dbgmsg( "closing file '%s', slot #%d",
                        gFd->open[winner].filename,
                        winner );
                TrCloseFile( winner );
            }
        }
        else
        {
            dbgmsg(
                "everything's full!  waiting for someone else to finish something" );
            tr_lockUnlock( gFd->lock );
            tr_wait( 200 );
            tr_lockLock( gFd->lock );
        }
    }

    assert( winner >= 0 );
    o = &gFd->open[winner];
    if( !fileIsOpen( o ) )
    {
        const int err = TrOpenFile( winner, folder, torrentFile, doWrite, doPreallocate, desiredFileSize );
        if( err ) {
            tr_lockUnlock( gFd->lock );
            tr_free( filename );
            errno = err;
            return -1;
        }

        dbgmsg( "opened '%s' in slot %d, doWrite %c", filename, winner,
                doWrite ? 'y' : 'n' );
        tr_strlcpy( o->filename, filename, sizeof( o->filename ) );
        o->isWritable = doWrite;
    }

    dbgmsg( "checking out '%s' in slot %d", filename, winner );
    o->isCheckedOut = 1;
    o->closeWhenDone = 0;
    o->date = tr_date( );
    tr_free( filename );
    tr_lockUnlock( gFd->lock );
    return o->fd;
}

void
tr_fdFileReturn( int fd )
{
    int i;

    tr_lockLock( gFd->lock );

    for( i = 0; i < TR_MAX_OPEN_FILES; ++i )
    {
        struct tr_openfile * o = &gFd->open[i];
        if( o->fd != fd )
            continue;

        dbgmsg( "releasing file '%s' in slot #%d", o->filename, i );
        o->isCheckedOut = 0;
        if( o->closeWhenDone )
            TrCloseFile( i );

        break;
    }

    tr_lockUnlock( gFd->lock );
}

void
tr_fdFileClose( const char * filename )
{
    int i;

    tr_lockLock( gFd->lock );

    for( i = 0; i < TR_MAX_OPEN_FILES; ++i )
    {
        struct tr_openfile * o = &gFd->open[i];
        if( !fileIsOpen( o ) || strcmp( filename, o->filename ) )
            continue;

        dbgmsg( "tr_fdFileClose closing '%s'", filename );

        if( !o->isCheckedOut )
        {
            dbgmsg( "not checked out, so closing it now... '%s'", filename );
            TrCloseFile( i );
        }
        else
        {
            dbgmsg(
                "flagging file '%s', slot #%d to be closed when checked in",
                gFd->open[i].filename, i );
            o->closeWhenDone = 1;
        }
    }

    tr_lockUnlock( gFd->lock );
}

/***
****
****  Sockets
****
***/

static int
getSocketMax( struct tr_fd_s * gFd )
{
    return gFd->socketMax;
}

int
tr_fdSocketCreate( int type )
{
    int s = -1;

    tr_lockLock( gFd->lock );

    if( gFd->socketCount < getSocketMax( gFd ) )
        if( ( s = socket( AF_INET, type, 0 ) ) < 0 )
            tr_err( _( "Couldn't create socket: %s" ),
                   tr_strerror( sockerrno ) );

    if( s > -1 )
        ++gFd->socketCount;

    assert( gFd->socketCount >= 0 );

    tr_lockUnlock( gFd->lock );
    return s;
}

int
tr_fdSocketAccept( int              b,
                   struct in_addr * addr,
                   tr_port_t *      port )
{
    int                s = -1;
    unsigned int       len;
    struct sockaddr_in sock;

    assert( addr );
    assert( port );

    tr_lockLock( gFd->lock );
    if( gFd->socketCount < getSocketMax( gFd ) )
    {
        len = sizeof( sock );
        s = accept( b, (struct sockaddr *) &sock, &len );
    }
    if( s > -1 )
    {
        *addr = sock.sin_addr;
        *port = sock.sin_port;
        ++gFd->socketCount;
    }
    tr_lockUnlock( gFd->lock );

    return s;
}

static void
socketClose( int fd )
{
#ifdef BEOS_NETSERVER
    closesocket( fd );
#else
    EVUTIL_CLOSESOCKET( fd );
#endif
}

void
tr_fdSocketClose( int s )
{
    tr_lockLock( gFd->lock );

    if( s >= 0 )
    {
        socketClose( s );
        --gFd->socketCount;
    }

    assert( gFd->socketCount >= 0 );

    tr_lockUnlock( gFd->lock );
}

/***
****
****  Startup / Shutdown
****
***/

void
tr_fdInit( int globalPeerLimit )
{
    int i;

    assert( gFd == NULL );
    gFd = tr_new0( struct tr_fd_s, 1 );
    gFd->lock = tr_lockNew( );
    gFd->socketMax = globalPeerLimit;
    tr_dbg( "%d usable file descriptors", globalPeerLimit );

    for( i = 0; i < TR_MAX_OPEN_FILES; ++i )
        gFd->open[i].fd = -1;
}

void
tr_fdClose( void )
{
    int i = 0;

    for( i = 0; i < TR_MAX_OPEN_FILES; ++i )
        if( fileIsOpen( &gFd->open[i] ) )
            TrCloseFile( i );

    tr_lockFree( gFd->lock );

    tr_free( gFd );
    gFd = NULL;
}

void
tr_fdSetPeerLimit( uint16_t n )
{
    assert( gFd != NULL && "tr_fdInit() must be called first!" );
    gFd->socketMax = n;
}

uint16_t
tr_fdGetPeerLimit( void )
{
    return gFd ? gFd->socketMax : -1;
}

