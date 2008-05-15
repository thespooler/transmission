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
#include <ctype.h> /* isalpha */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* strerror */

#include <libgen.h> /* basename */
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h> /* usleep, stat */

#include "event.h"

#ifdef WIN32
    #include <windows.h> /* for Sleep */
#elif defined(__BEOS__)
    #include <kernel/OS.h>
#endif

#include "transmission.h"
#include "trcompat.h"
#include "utils.h"
#include "platform.h"

static tr_lock      * messageLock = NULL;
static int            messageLevel = 0;
static int            messageQueuing = FALSE;
static tr_msg_list *  messageQueue = NULL;
static tr_msg_list ** messageQueueTail = &messageQueue;

void tr_msgInit( void )
{
    if( !messageLock )
         messageLock = tr_lockNew( );
}

FILE*
tr_getLog( void )
{
    static int initialized = FALSE;
    static FILE * file= NULL;

    if( !initialized )
    {
        const char * str = getenv( "TR_DEBUG_FD" );
        int fd = 0;
        if( str && *str )
            fd = atoi( str );
        switch( fd ) {
            case 1: file = stdout; break;
            case 2: file = stderr; break;
            default: file = NULL; break;
        }
        initialized = TRUE;
    }

    return file;
}

void
tr_setMessageLevel( int level )
{
    tr_msgInit();
    tr_lockLock( messageLock );
    messageLevel = MAX( 0, level );
    tr_lockUnlock( messageLock );
}

int
tr_getMessageLevel( void )
{
    int ret;

    tr_msgInit();
    tr_lockLock( messageLock );
    ret = messageLevel;
    tr_lockUnlock( messageLock );

    return ret;
}

void
tr_setMessageQueuing( int enabled )
{
    tr_msgInit();
    tr_lockLock( messageLock );
    messageQueuing = enabled;
    tr_lockUnlock( messageLock );
}

tr_msg_list *
tr_getQueuedMessages( void )
{
    tr_msg_list * ret;

    assert( NULL != messageLock );
    tr_lockLock( messageLock );
    ret = messageQueue;
    messageQueue = NULL;
    messageQueueTail = &messageQueue;
    tr_lockUnlock( messageLock );

    return ret;
}

void
tr_freeMessageList( tr_msg_list * list )
{
    tr_msg_list * next;

    while( NULL != list )
    {
        next = list->next;
        free( list->message );
        free( list->name );
        free( list );
        list = next;
    }
}

/**
***
**/

char*
tr_getLogTimeStr( char * buf, int buflen )
{
    char tmp[64];
    time_t now;
    struct tm now_tm;
    struct timeval tv;
    int milliseconds;

    now = time( NULL );
    gettimeofday( &tv, NULL );

#ifdef WIN32
    now_tm = *localtime( &now );
#else
    localtime_r( &now, &now_tm );
#endif
    strftime( tmp, sizeof(tmp), "%H:%M:%S", &now_tm );
    milliseconds = (int)(tv.tv_usec / 1000);
    snprintf( buf, buflen, "%s.%03d", tmp, milliseconds );

    return buf;
}

void
tr_deepLog( const char * file, int line, const char * name, const char * fmt, ... )
{
    FILE * fp = tr_getLog( );
    if( fp != NULL )
    {
        va_list args;
        char timestr[64];
        struct evbuffer * buf = evbuffer_new( );
        char * myfile = tr_strdup( file );

        evbuffer_add_printf( buf, "[%s] ", tr_getLogTimeStr( timestr, sizeof(timestr) ) );
        if( name )
            evbuffer_add_printf( buf, "%s ", name );
        va_start( args, fmt );
        evbuffer_add_vprintf( buf, fmt, args );
        va_end( args );
        evbuffer_add_printf( buf, " (%s:%d)\n", basename(myfile), line );
        fwrite( EVBUFFER_DATA(buf), 1, EVBUFFER_LENGTH(buf), fp );

        tr_free( myfile );
        evbuffer_free( buf );
    }
}

/***
****
***/

void
tr_msg( const char * file, int line, int level,
        const char * name,
        const char * fmt, ... )
{
    FILE * fp;

    if( messageLock )
        tr_lockLock( messageLock );

    fp = tr_getLog( );

    if( !messageLevel )
    {
        char * env = getenv( "TR_DEBUG" );
        messageLevel = ( env ? atoi( env ) : 0 ) + 1;
        messageLevel = MAX( 1, messageLevel );
    }

    if( messageLevel >= level )
    {
        va_list ap;
        struct evbuffer * buf = evbuffer_new( );

        /* build the text message */
        va_start( ap, fmt );
        evbuffer_add_vprintf( buf, fmt, ap );
        va_end( ap );

        if( EVBUFFER_LENGTH( buf ) )
        {
            if( messageQueuing )
            {
                tr_msg_list * newmsg;
                newmsg = tr_new0( tr_msg_list, 1 );
                newmsg->level = level;
                newmsg->when = time( NULL );
                newmsg->message = tr_strdup( (char*)EVBUFFER_DATA( buf ) );
                newmsg->file = file;
                newmsg->line = line;
                newmsg->name = tr_strdup( name );

                *messageQueueTail = newmsg;
                messageQueueTail = &newmsg->next;
            }
            else
            {
                if( fp == NULL )
                    fp = stderr;
                if( name )
                    fprintf( fp, "%s: %s\n", name, (char*)EVBUFFER_DATA(buf) );
                else
                    fprintf( fp, "%s\n", (char*)EVBUFFER_DATA(buf) );
                fflush( fp );
            }

            evbuffer_free( buf );
        }
    }

    if( messageLock )
        tr_lockUnlock( messageLock );
}

int tr_rand( int sup )
{
    static int init = 0;

    assert( sup > 0 );

    if( !init )
    {
        srand( tr_date() );
        init = 1;
    }
    return rand() % sup;
}

/***
****
***/

void
tr_set_compare( const void * va, size_t aCount,
                const void * vb, size_t bCount,
                int compare( const void * a, const void * b ),
                size_t elementSize,
                tr_set_func in_a_cb,
                tr_set_func in_b_cb,
                tr_set_func in_both_cb,
                void * userData )
{
    const uint8_t * a = (const uint8_t *) va;
    const uint8_t * b = (const uint8_t *) vb;
    const uint8_t * aend = a + elementSize*aCount;
    const uint8_t * bend = b + elementSize*bCount;

    while( a!=aend || b!=bend )
    {
        if( a==aend )
        {
            (*in_b_cb)( (void*)b, userData );
            b += elementSize;
        }
        else if ( b==bend )
        {
            (*in_a_cb)( (void*)a, userData );
            a += elementSize;
        }
        else
        {
            const int val = (*compare)( a, b );

            if( !val )
            {
                (*in_both_cb)( (void*)a, userData );
                a += elementSize;
                b += elementSize;
            }
            else if( val < 0 )
            {
                (*in_a_cb)( (void*)a, userData );
                a += elementSize;
            }
            else if( val > 0 )
            {
                (*in_b_cb)( (void*)b, userData );
                b += elementSize;
            }
        }
    }
}

/***
****
***/

int
tr_compareUint16( uint16_t a, uint16_t b )
{
    if( a < b ) return -1;
    if( a > b ) return 1;
    return 0;
}

int
tr_compareUint32( uint32_t a, uint32_t b )
{
    if( a < b ) return -1;
    if( a > b ) return 1;
    return 0;
}

/**
***
**/

struct timeval
tr_timevalMsec( uint64_t milliseconds )
{
    struct timeval ret;
    const uint64_t microseconds = milliseconds * 1000;
    ret.tv_sec  = microseconds / 1000000;
    ret.tv_usec = microseconds % 1000000;
    return ret;
}

uint8_t *
tr_loadFile( const char * path, size_t * size )
{
    uint8_t    * buf;
    struct stat  sb;
    FILE       * file;
    const char * err_fmt = _( "Couldn't read \"%1$s\": %2$s" );

    /* try to stat the file */
    errno = 0;
    if( stat( path, &sb ) )
    {
        tr_dbg( err_fmt, path, tr_strerror(errno) );
        return NULL;
    }

    if( ( sb.st_mode & S_IFMT ) != S_IFREG )
    {
        tr_err( err_fmt, path, _( "Not a regular file" ) );
        return NULL;
    }

    /* Load the torrent file into our buffer */
    file = fopen( path, "rb" );
    if( !file )
    {
        tr_err( err_fmt, path, tr_strerror(errno) );
        return NULL;
    }
    buf = malloc( sb.st_size );
    if( NULL == buf )
    {
        tr_err( err_fmt, path, _( "Memory allocation failed" ) );
        fclose( file );
        return NULL;
    }
    fseek( file, 0, SEEK_SET );
    if( fread( buf, sb.st_size, 1, file ) != 1 )
    {
        tr_err( err_fmt, path, tr_strerror(errno) );
        free( buf );
        fclose( file );
        return NULL;
    }
    fclose( file );

    *size = sb.st_size;

    return buf;
}

int
tr_mkdir( const char * path, int permissions 
#ifdef WIN32
                                             UNUSED
#endif
                                                    )
{
#ifdef WIN32
    if( path && isalpha(path[0]) && path[1]==':' && !path[2] )
        return 0;
    return mkdir( path );
#else
    return mkdir( path, permissions );
#endif
}

int
tr_mkdirp( const char * path_in, int permissions )
{
    char * path = tr_strdup( path_in );
    char * p, * pp;
    struct stat sb;
    int done;

    /* walk past the root */
    p = path;
    while( *p == TR_PATH_DELIMITER )
        ++p;

    pp = p;
    done = 0;
    while( ( p = strchr( pp, TR_PATH_DELIMITER ) ) || ( p = strchr( pp, '\0' ) ) )
    {
        if( !*p )
            done = 1;
        else
            *p = '\0';

        if( stat( path, &sb ) )
        {
            /* Folder doesn't exist yet */
            if( tr_mkdir( path, permissions ) ) {
                const int err = errno;
                tr_err( _( "Couldn't create \"%1$s\": %2$s" ), path, tr_strerror( err ) );
                tr_free( path );
                errno = err;
                return -1;
            }
        }
        else if( ( sb.st_mode & S_IFMT ) != S_IFDIR )
        {
            /* Node exists but isn't a folder */
            char buf[MAX_PATH_LENGTH];
            snprintf( buf, sizeof( buf ), _( "File \"%s\" is in the way" ), path );
            tr_err( _( "Couldn't create \"%1$s\": %2$s" ), path_in, buf );
            tr_free( path );
            errno = ENOTDIR;
            return -1;
        }

        if( done )
            break;

        *p = TR_PATH_DELIMITER;
        p++;
        pp = p;
    }

    tr_free( path );
    return 0;
}

void
tr_buildPath ( char *buf, size_t buflen, const char *first_element, ... )
{
    struct evbuffer * evbuf = evbuffer_new( );
    const char * element = first_element;
    va_list vl;
    va_start( vl, first_element );
    while( element ) {
        if( EVBUFFER_LENGTH(evbuf) )
            evbuffer_add_printf( evbuf, "%c", TR_PATH_DELIMITER );
        evbuffer_add_printf( evbuf, "%s", element );
        element = (const char*) va_arg( vl, const char* );
    }
    if( EVBUFFER_LENGTH(evbuf) )
        strlcpy( buf, (char*)EVBUFFER_DATA(evbuf), buflen );
    else
        *buf = '\0';
    evbuffer_free( evbuf );
}

int
tr_ioErrorFromErrno( int err )
{
    switch( err )
    {
        case 0:
            return TR_OK;
        case EACCES:
        case EROFS:
            return TR_ERROR_IO_PERMISSIONS;
        case ENOSPC:
            return TR_ERROR_IO_SPACE;
        case EMFILE:
            return TR_ERROR_IO_OPEN_FILES;
        case EFBIG:
            return TR_ERROR_IO_FILE_TOO_BIG;
        default:
            tr_dbg( "generic i/o errno from errno: %s", tr_strerror( errno ) );
            return TR_ERROR_IO_OTHER;
    }
}

const char *
tr_errorString( int code )
{
    switch( code )
    {
        case TR_OK:
            return _( "No error" );

        case TR_ERROR:
            return _( "Unspecified error" );
        case TR_ERROR_ASSERT:
            return _( "Assert error" );

        case TR_ERROR_IO_PARENT:
            return _( "Destination folder doesn't exist" );
        case TR_ERROR_IO_PERMISSIONS:
            return tr_strerror( EACCES );
        case TR_ERROR_IO_SPACE:
            return tr_strerror( ENOSPC );
        case TR_ERROR_IO_FILE_TOO_BIG:
            return tr_strerror( EFBIG );
        case TR_ERROR_IO_OPEN_FILES:
            return tr_strerror( EMFILE );
        case TR_ERROR_IO_DUP_DOWNLOAD:
            return _( "A torrent with this name and destination folder already exists." );
        case TR_ERROR_IO_CHECKSUM:
            return _( "Checksum failed" );
        case TR_ERROR_IO_OTHER:
            return _( "Unspecified I/O error" );

        case TR_ERROR_TC_ERROR:
            return _( "Tracker error" );
        case TR_ERROR_TC_WARNING:
            return _( "Tracker warning" );

        case TR_ERROR_PEER_MESSAGE:
            return _( "Peer sent a bad message" );

        default:
            return _( "Unknown error" );
    }
}

/****
*****
****/

char*
tr_strdup( const char * in )
{
    return tr_strndup( in, in ? strlen(in) : 0 );
}

char*
tr_strndup( const char * in, int len )
{
    char * out = NULL;

    if( len < 0 )
    {
        out = tr_strdup( in );
    }
    else if( in != NULL )
    {
        out = tr_malloc( len+1 );
        memcpy( out, in, len );
        out[len] = '\0';
    }
    return out;
}

char*
tr_strdup_printf( const char * fmt, ... )
{
    char * ret = NULL;
    struct evbuffer * buf;
    va_list ap;

    buf = evbuffer_new( );
    va_start( ap, fmt );
    if( evbuffer_add_vprintf( buf, fmt, ap ) != -1 )
        ret = tr_strdup( (char*)EVBUFFER_DATA( buf ) );
    evbuffer_free( buf );

    return ret;
}

void*
tr_calloc( size_t nmemb, size_t size )
{
    return nmemb && size ? calloc( nmemb, size ) : NULL;
}

void*
tr_malloc( size_t size )
{
    return size ? malloc( size ) : NULL;
}

void*
tr_malloc0( size_t size )
{
    void * ret = tr_malloc( size );
    memset( ret, 0, size );
    return ret;
}

void
tr_free( void * p )
{
    if( p )
        free( p );
}

const char*
tr_strerror( int i )
{
    const char * ret = strerror( i );
    if( ret == NULL )
        ret = "Unknown Error";
    return ret;
}

/****
*****
****/

/* note that the argument is how many bits are needed, not bytes */
tr_bitfield*
tr_bitfieldNew( size_t bitcount )
{
    tr_bitfield * ret = calloc( 1, sizeof(tr_bitfield) );
    if( NULL == ret )
        return NULL;

    ret->len = (bitcount+7u) / 8u;
    ret->bits = calloc( ret->len, 1 );
    if( NULL == ret->bits ) {
        free( ret );
        return NULL;
    }

    return ret;
}

tr_bitfield*
tr_bitfieldDup( const tr_bitfield * in )
{
    tr_bitfield * ret = calloc( 1, sizeof(tr_bitfield) );
    ret->len = in->len;
    ret->bits = malloc( ret->len );
    memcpy( ret->bits, in->bits, ret->len );
    return ret;
}

void tr_bitfieldFree( tr_bitfield * bitfield )
{
    if( bitfield )
    {
        free( bitfield->bits );
        free( bitfield );
    }
}

void
tr_bitfieldClear( tr_bitfield * bitfield )
{
    memset( bitfield->bits, 0, bitfield->len );
}

int
tr_bitfieldIsEmpty( const tr_bitfield * bitfield )
{
    size_t i;

    for( i=0; i<bitfield->len; ++i )
        if( bitfield->bits[i] )
            return 0;

    return 1;
}

int
tr_bitfieldHas( const tr_bitfield * bitfield, size_t nth )
{
    static const uint8_t ands[8] = { 128, 64, 32, 16, 8, 4, 2, 1 };
    const size_t i = nth >> 3u;
    return ( bitfield != NULL )
        && ( bitfield->bits != NULL )
        && ( i < bitfield->len )
        && ( ( bitfield->bits[i] & ands[nth&7u] ) != 0 );
}

int
tr_bitfieldAdd( tr_bitfield  * bitfield, size_t nth )
{
    static const uint8_t ands[8] = { 128, 64, 32, 16, 8, 4, 2, 1 };
    const size_t i = nth >> 3u;

    assert( bitfield != NULL );
    assert( bitfield->bits != NULL );

    if( i >= bitfield->len )
        return -1;

    bitfield->bits[i] |= ands[nth&7u];
    assert( tr_bitfieldHas( bitfield, nth ) );
    return 0;
}

int
tr_bitfieldAddRange( tr_bitfield  * bitfield,
                     size_t         begin,
                     size_t         end )
{
    int err = 0;
    size_t i;
    for( i=begin; i<end; ++i )
        if(( err = tr_bitfieldAdd( bitfield, i )))
            break;
    return err;
}

int
tr_bitfieldRem( tr_bitfield   * bitfield,
                size_t          nth )
{
    static const uint8_t rems[8] = { 127, 191, 223, 239, 247, 251, 253, 254 };
    const size_t i = nth >> 3u;

    assert( bitfield != NULL );
    assert( bitfield->bits != NULL );

    if( i >= bitfield->len )
        return -1;

    bitfield->bits[i] &= rems[nth&7u];
    assert( !tr_bitfieldHas( bitfield, nth ) );
    return 0;
}

int
tr_bitfieldRemRange ( tr_bitfield  * b,
                      size_t         begin,
                      size_t         end )
{
    int err = 0;
    size_t i;
    for( i=begin; i<end; ++i )
        if(( err = tr_bitfieldRem( b, i )))
            break;
    return err;
}

tr_bitfield*
tr_bitfieldOr( tr_bitfield * a, const tr_bitfield * b )
{
    uint8_t *ait;
    const uint8_t *aend, *bit;

    assert( a->len == b->len );

    for( ait=a->bits, bit=b->bits, aend=ait+a->len; ait!=aend; )
        *ait++ |= *bit++;

    return a;
}

/* set 'a' to all the flags that were in 'a' but not 'b' */
void
tr_bitfieldDifference( tr_bitfield * a, const tr_bitfield * b )
{
    uint8_t *ait;
    const uint8_t *aend, *bit;

    assert( a->len == b->len );

    for( ait=a->bits, bit=b->bits, aend=ait+a->len; ait!=aend; )
        *ait++ &= ~(*bit++);
}


size_t
tr_bitfieldCountTrueBits( const tr_bitfield* b )
{
    size_t ret = 0;
    const uint8_t *it, *end;
    static const int trueBitCount[512] = {
        0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4,1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,
        1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
        1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
        2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
        1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
        2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
        2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
        3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8,
        1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
        2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
        2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
        3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8,
        2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
        3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8,
        3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8,
        4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8,5,6,6,7,6,7,7,8,6,7,7,8,7,8,8,9
    };

    if( !b )
        return 0;

    for( it=b->bits, end=it+b->len; it!=end; ++it )
        ret += trueBitCount[*it];

    return ret;
}

/***
****
***/

uint64_t
tr_date( void )
{
    struct timeval tv;
    gettimeofday( &tv, NULL );
    return (uint64_t) tv.tv_sec * 1000 + ( tv.tv_usec / 1000 );
}

void
tr_wait( uint64_t delay_milliseconds )
{
#ifdef __BEOS__
    snooze( 1000 * delay_milliseconds );
#elif defined(WIN32)
    Sleep( (DWORD)delay_milliseconds );
#else
    usleep( 1000 * delay_milliseconds );
#endif
}

/***
****
***/


#ifndef HAVE_STRLCPY

/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t
strlcpy(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;

	assert( s != NULL );
	assert( d != NULL );

	/* Copy as many bytes as will fit */
	if (n != 0) {
		while (--n != 0) {
			if ((*d++ = *s++) == '\0')
				break;
		}
	}

	/* Not enough room in dst, add NUL and traverse rest of src */
	if (n == 0) {
		if (siz != 0)
			*d = '\0';		/* NUL-terminate dst */
		while (*s++)
			;
	}

	return(s - src - 1);	/* count does not include NUL */
}

#endif /* HAVE_STRLCPY */

/***
****
***/

double
tr_getRatio( double numerator, double denominator )
{
    double ratio;

    if( denominator )
        ratio = numerator / denominator;
    else if( numerator )
        ratio = TR_RATIO_INF;
    else
        ratio = TR_RATIO_NA;

    return ratio;
}

void
tr_sha1_to_hex( char * out, const uint8_t * sha1 )
{
    static const char hex[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 20; i++) {
        unsigned int val = *sha1++;
        *out++ = hex[val >> 4];
        *out++ = hex[val & 0xf];
    }
    *out = '\0';
}

/***
****
***/

int
tr_httpIsValidURL( const char * url )
{
    return !tr_httpParseURL( url, -1, NULL, NULL, NULL );
}

int
tr_httpParseURL( const char * url_in, int len,
                 char ** setme_host,
                 int * setme_port,
                 char ** setme_path )
{
    int err;
    int port = 0;
    int n;
    char * tmp;
    char * pch;
    const char * protocol = NULL;
    const char * host = NULL;
    const char * path = NULL;

    tmp = tr_strndup( url_in, len );
    if(( pch = strstr( tmp, "://" )))
    {
       *pch = '\0';
       protocol = tmp;
       pch += 3;
/*fprintf( stderr, "protocol is [%s]... what's left is [%s]\n", protocol, pch );*/
       if(( n = strcspn( pch, ":/" )))
       {
           const int havePort = pch[n] == ':';
           host = pch;
           pch += n;
           *pch++ = '\0';
/*fprintf( stderr, "host is [%s]... what's left is [%s]\n", host, pch );*/
           if( havePort )
           {
               char * end;
               port = strtol( pch, &end, 10 );
               pch = end;
/*fprintf( stderr, "port is [%d]... what's left is [%s]\n", port, pch );*/
           }
           path = pch;
/*fprintf( stderr, "path is [%s]\n", path );*/
       }
    }

    err = !host || !path || !protocol || ( strcmp(protocol,"http") && strcmp(protocol,"https") );

    if( !err && !port ) {
        if( !strcmp(protocol,"http") ) port = 80;
        if( !strcmp(protocol,"https") ) port = 443;
    }

    if( !err ) {
        if( setme_host) { ((char*)host)[-3]=':'; *setme_host = tr_strdup( protocol ); }
        if( setme_path) { ((char*)path)[-1]='/'; *setme_path = tr_strdup( path-1 ); }
        if( setme_port) *setme_port = port;
    }


    tr_free( tmp );
    return err;
}
