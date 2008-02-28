/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007 Joshua Elsasser
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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <assert.h>
#include <errno.h>
#include <event.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libtransmission/trcompat.h>
#include <libtransmission/platform.h>
#include <libtransmission/version.h>

#include "errors.h"
#include "misc.h"
#include "server.h"
#include "torrents.h"

static void usage       ( const char *, ... );
static void readargs    ( int, char **, int *, int *, char **, char ** );
static int  trylocksock ( const char * );
static int  getsock     ( const char * );
static void exitcleanup ( void );
static void setupsigs   ( struct event_base * );
static void gotsig      ( int, short, void * );
static int  savepid     ( const char * );

static char gl_lockpath[MAXPATHLEN] = "";
static int  gl_sockfd               = -1;
static char gl_sockpath[MAXPATHLEN] = "";
static char gl_pidfile[MAXPATHLEN]  = "";

int
main( int argc, char ** argv )
{
    struct event_base * evbase;
    int                 nofork, debug, sockfd;
    char              * sockpath, * pidfile;

    setmyname( argv[0] );
    readargs( argc, argv, &nofork, &debug, &sockpath, &pidfile );

    if( !nofork )
    {
        if( 0 > daemon( 1, 0 ) )
        {
            errnomsg( "failed to daemonize" );
            exit( 1 );
        }
        errsyslog( 1 );
    }

    atexit( exitcleanup );
    sockfd = trylocksock( sockpath );
    if( 0 > sockfd )
    {
        exit( 1 );
    }
    if( NULL != pidfile && 0 > savepid( pidfile ) )
    {
        exit( 1 );
    }

    evbase = event_init();
    setupsigs( evbase );
    torrent_init( evbase );
    server_init( evbase );
    server_debug( debug );
    server_listen( sockfd );

    event_base_dispatch( evbase );

    return 1;
}

void
usage( const char * msg, ... )
{
    va_list ap;

    if( NULL != msg )
    {
        printf( "%s: ", getmyname() );
        va_start( ap, msg );
        vprintf( msg, ap );
        va_end( ap );
        printf( "\n" );
    }

    printf(
  "usage: %s [-dfh] [-p file] [-s file]\n"
  "\n"
  "Transmission %s http://www.transmissionbt.com/\n"
  "A fast and easy BitTorrent client\n"
  "\n"
  "  -d --debug                Print data send and received, implies -f\n"
  "  -f --foreground           Run in the foreground and log to stderr\n"
  "  -h --help                 Display this message and exit\n"
  "  -p --pidfile <path>       Save the process id in a file at <path>\n"
  "  -s --socket <path>        Place the socket file at <path>\n"
  "\n"
  "To add torrents or set options, use the transmission-remote program.\n",
            getmyname(), LONG_VERSION_STRING );
    exit( 0 );
}

void
readargs( int argc, char ** argv, int * nofork, int * debug, char ** sock,
          char ** pidfile )
{
    char optstr[] = "dfhp:s:";
    struct option longopts[] =
    {
        { "debug",              no_argument,       NULL, 'd' },
        { "foreground",         no_argument,       NULL, 'f' },
        { "help",               no_argument,       NULL, 'h' },
        { "pidfile",            required_argument, NULL, 'p' },
        { "socket",             required_argument, NULL, 's' },
        { NULL, 0, NULL, 0 }
    };
    int opt;

    *nofork    = 0;
    *debug     = 0;
    *sock      = NULL;
    *pidfile   = NULL;

    while( 0 <= ( opt = getopt_long( argc, argv, optstr, longopts, NULL ) ) )
    {
        switch( opt )
        {
            case 'd':
                *debug = 1;
                /* FALLTHROUGH */
            case 'f':
                *nofork = 1;
                break;
            case 'p':
                *pidfile = optarg;
                break;
            case 's':
                *sock   = optarg;
                break;
            default:
                usage( NULL );
                break;
        }
    }
}

static int
getlock( const char * filename )
{
    const int state = tr_lockfile( filename );
    const int success = state == TR_LOCKFILE_SUCCESS;

    if( !success ) switch( state ) {
        case TR_LOCKFILE_EOPEN:
            errnomsg( "failed to open file: %s", filename );
            break;
        case TR_LOCKFILE_ELOCK:
            errmsg( "another copy of %s is already running", getmyname() );
            break;
        default:
            errmsg( "unhandled tr_lockfile error: %d", state );
            break;
    }

    return success;
}


int
trylocksock( const char * sockpath )
{
    char path[MAXPATHLEN];
    int  fd;

    confpath( path, sizeof path, NULL, CONF_PATH_TYPE_DAEMON );
    if( 0 > mkdir( path, 0777 ) && EEXIST != errno )
    {
        errnomsg( "failed to create directory: %s", path );
        return -1;
    }

    confpath( path, sizeof path, CONF_FILE_LOCK, 0 );
    if( !getlock( path ) )
        return -1;
    strlcpy( gl_lockpath, path, sizeof gl_lockpath );

    if( NULL == sockpath )
    {
        confpath( path, sizeof path, CONF_FILE_SOCKET, 0 );
        sockpath = path;
    }
    fd = getsock( sockpath );
    if( 0 > fd )
    {
        return -1;
    }
    gl_sockfd = fd;
    strlcpy( gl_sockpath, sockpath, sizeof gl_sockpath );

    return fd;
}

int
getsock( const char * path )
{
    struct sockaddr_un sa;
    int                fd;

    fd = socket( PF_LOCAL, SOCK_STREAM, 0 );
    if( 0 > fd )
    {
        errnomsg( "failed to create socket file: %s", path );
        return -1;
    }

    memset( &sa, 0, sizeof sa );
    sa.sun_family = AF_LOCAL;
    strlcpy( sa.sun_path, path, sizeof sa.sun_path );
    unlink( path );
    if( 0 > bind( fd, ( struct sockaddr * )&sa, SUN_LEN( &sa ) ) )
    {
        /* bind can sometimes fail on the first call */
        unlink( path );
        if( 0 > bind( fd, ( struct sockaddr * )&sa, SUN_LEN( &sa ) ) )
        {
            errnomsg( "failed to bind socket file: %s", path );
            close( fd );
            return -1;
        }
    }

    return fd;
}

void
exitcleanup( void )
{
    if( 0 <= gl_sockfd )
    {
        unlink( gl_sockpath );
        close( gl_sockfd );
    }
    if( 0 != gl_pidfile[0] )
    {
        unlink( gl_pidfile );
    }

    if( *gl_lockpath )
        unlink( gl_lockpath );
}

void
setupsigs( struct event_base * base )
{
    static struct event ev_int;
    static struct event ev_quit;
    static struct event ev_term;

    signal_set( &ev_int, SIGINT, gotsig, NULL );
    event_base_set( base, &ev_int );
    signal_add( &ev_int, NULL );

    signal_set( &ev_quit, SIGQUIT, gotsig, NULL );
    event_base_set( base, &ev_quit );
    signal_add( &ev_quit, NULL );

    signal_set( &ev_term, SIGTERM, gotsig, NULL );
    event_base_set( base, &ev_term );
    signal_add( &ev_term, NULL );

    signal( SIGPIPE, SIG_IGN );
    signal( SIGHUP, SIG_IGN );
}

void
gotsig( int sig, short what UNUSED, void * arg UNUSED )
{
    static int exiting = 0;

    if( !exiting )
    {
        exiting = 1;
        errmsg( "received fatal signal %i, attempting to exit cleanly", sig );
        server_quit();
    }
    else
    {
        errmsg( "received fatal signal %i while exiting, exiting immediately",
                sig );
        signal( sig, SIG_DFL );
        raise( sig );
    }
}

int
savepid( const char * file )
{
    FILE * pid;

    pid = fopen( file, "wb" );
    if( NULL == pid )
    {
        errnomsg( "failed to open pid file: %s", file );
        return -1;
    }

    if( 0 > fprintf( pid, "%d\n", (int) getpid() ) )
    {
        errnomsg( "failed to write pid to file: %s", file );
        fclose( pid );
        unlink( file );
        return -1;
    }

    fclose( pid );
    strlcpy( gl_pidfile, file, sizeof gl_pidfile );

    return 0;
}
