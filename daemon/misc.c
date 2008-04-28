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
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libtransmission/transmission.h>
#include <libtransmission/trcompat.h>
#include <libtransmission/utils.h> /* tr_loadFile() */

#include "errors.h"
#include "misc.h"

static char gl_myname[256];

void
setmyname( const char * argv0 )
{
    const char * name;

    name = strrchr( argv0, '/' );
    if( NULL == name || '\0' == *(++name) )
    {
        name = argv0;
    }
    strlcpy( gl_myname, name, sizeof gl_myname );
}

const char *
getmyname( void )
{
    return gl_myname;
}

void
confpath( char               * buf,
          size_t               len,
          const char         * configDir,
          const char         * file,
          enum confpathtype    type )
{
    switch( type )
    {
        case CONF_PATH_TYPE_DAEMON:
            tr_buildPath( buf, len, configDir, "daemon", file, NULL );
            break;
        case CONF_PATH_TYPE_GTK:
            tr_buildPath( buf, len, configDir, "gtk", file, NULL );
            break;
        case CONF_PATH_TYPE_OSX:
            tr_buildPath( buf, len, configDir, file, NULL );
            break;
        default:
            assert( 0 );
            break;
    }
}

void
absolutify( char * buf, size_t len, const char * path )
{
    if( *path == '/' )
        strlcpy( buf, path, len );
    else {
        char cwd[MAX_PATH_LENGTH];
        getcwd( cwd, sizeof( cwd ) );
        tr_buildPath( buf, len, cwd, path, NULL );
    }
}

int
writefile( const char * name, uint8_t * buf, ssize_t len )
{
    int     fd;
    ssize_t res;

    fd = open( name, O_WRONLY | O_CREAT | O_TRUNC, 0666 );
    if( 0 > fd )
    {
        errnomsg( "Couldn't open \"%1$s\": %2$s", name, tr_strerror( errno ) );
        return -1;
    }

    res = write( fd, buf, len );
    if( 0 > res )
    {
        errnomsg( "failed to write to %s", name );
        return -1;
    }
    if( len > res )
    {
        errmsg( "failed to write all data to %s", name );
        return -1;
    }

    close( fd );

    return 0;
}

uint8_t *
readfile( const char * filename, size_t * len )
{
    return tr_loadFile( filename, len );
}

#ifndef HAVE_DAEMON

int
daemon( int nochdir, int noclose )
{
    switch( fork( ) ) {
        case 0:
            break;
        case -1:
            fprintf( stderr, "Error daemonizing (fork)! %d - %s\n", errno, strerror(errno) );
            return -1;
        default:
            _exit(0);
    }

    if( setsid() < 0 ) {
        fprintf( stderr, "Error daemonizing (setsid)! %d - %s\n", errno, strerror(errno) );
        return -1;
    }

    switch( fork( ) ) {
        case 0:
            break;
        case -1:
            fprintf( stderr, "Error daemonizing (fork2)! %d - %s\n", errno, strerror(errno) );
            return -1;
        default:
            _exit(0);
    }

    if( !nochdir && 0 > chdir( "/" ) ) {
        fprintf( stderr, "Error daemonizing (chdir)! %d - %s\n", errno, strerror(errno) );
        return -1;
    }

    if( !noclose ) {
        int fd;
        fd = open("/dev/null", O_RDONLY);
        if( fd != 0 ) {
            dup2( fd,  0 );
            close( fd );
        }
        fd = open("/dev/null", O_WRONLY);
        if( fd != 1 ) {
            dup2( fd, 1 );
            close( fd );
        }
        fd = open("/dev/null", O_WRONLY);
        if( fd != 2 ) {
            dup2( fd, 2 );
            close( fd );
        }
    }

    return 0;
}

#endif
