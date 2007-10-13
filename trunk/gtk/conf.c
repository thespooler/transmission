/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <libtransmission/transmission.h>

#include "conf.h"
#include "util.h"

static char * gl_confdir = NULL;
static char * gl_lockpath = NULL;

/* errstr may be NULL, this might be called before GTK is initialized */
gboolean
cf_init(const char *dir, char **errstr)
{
    if( errstr != NULL )
        *errstr = NULL;

    gl_confdir = g_build_filename( dir, "gtk", NULL );

    if( mkdir_p(gl_confdir, 0755 ) )
        return TRUE;

    if( errstr != NULL )
        *errstr = g_strdup_printf( _("Failed to create the directory %s:\n%s"),
                                   gl_confdir, g_strerror(errno) );

    return FALSE;
}

/***
****
****  Lockfile
****
***/

/* errstr may be NULL, this might be called before GTK is initialized */
static int
lockfile(const char *file, char **errstr)
{
    int fd;
    struct flock lk;

    if( errstr )
        *errstr = NULL;

    fd = open( file, O_RDWR | O_CREAT, 0666 );
    if( fd < 0 )
    { 
        const int savederr = errno;
        if( errstr )
            *errstr = g_strdup_printf(
                          _("Failed to open the file %s for writing:\n%s"),
                          file, g_strerror( errno ) );
        errno = savederr;
        return -1;
    }

    memset( &lk, 0,  sizeof( lk ) );
    lk.l_start = 0;
    lk.l_len = 0;
    lk.l_type = F_WRLCK;
    lk.l_whence = SEEK_SET;
    if( -1 == fcntl( fd, F_SETLK, &lk ) )
    {
        const int savederr = errno;
        if( errstr )
            *errstr = ( errno == EAGAIN )
                ? g_strdup_printf( _( "Another copy of %s is already running." ),
                                   g_get_application_name( ) )
                : g_strdup_printf( _( "Failed to lock the file %s:\n%s" ),
                                  file, g_strerror( errno ) );
        close( fd );
        errno = savederr;
        return -1;
    }

    return fd;
}

static char*
getLockFilename( void )
{
    assert( gl_confdir != NULL );
    return g_build_filename( gl_confdir, "lock", NULL );
}

static void
cf_removelocks( void )
{
    g_unlink( gl_lockpath );
    g_free( gl_lockpath );
}

/* errstr may be NULL, this might be called before GTK is initialized */
gboolean
cf_lock( char ** errstr )
{
    const char * path = getLockFilename( );
    int fd = lockfile( path, errstr );
    if( fd >= 0 )
        gl_lockpath = g_strdup( path );
    g_atexit( cf_removelocks );
    return fd >= 0;
}

char*
cf_sockname( void )
{
    assert( gl_confdir != NULL );
    return g_build_filename( gl_confdir, "socket", NULL );
}

/***
****
****  Preferences
****
***/

#define GROUP "general"

static char*
getPrefsFilename( void )
{
    assert( gl_confdir != NULL );
    return g_build_filename( gl_confdir, "prefs.ini", NULL );
}

static GKeyFile*
getPrefsKeyFile( void )
{
    static GKeyFile * myKeyFile = NULL;

    if( myKeyFile == NULL )
    {
        char * filename = getPrefsFilename( );
        myKeyFile = g_key_file_new( );
        g_key_file_load_from_file( myKeyFile, filename, 0, NULL );
        g_free( filename );
    }

    return myKeyFile;
}

int
pref_int_get( const char * key ) {
    return g_key_file_get_integer( getPrefsKeyFile( ), GROUP, key, NULL );
}
void
pref_int_set( const char * key, int value ) {
    g_key_file_set_integer( getPrefsKeyFile( ), GROUP, key, value );
}
void
pref_int_set_default( const char * key, int value ) {
    if( !g_key_file_has_key( getPrefsKeyFile( ), GROUP, key, NULL ) )
        pref_int_set( key, value );
}

gboolean
pref_flag_get ( const char * key ) {
    return g_key_file_get_boolean( getPrefsKeyFile( ), GROUP, key, NULL );
}
void
pref_flag_set( const char * key, gboolean value ) {
    g_key_file_set_boolean( getPrefsKeyFile( ), GROUP, key, value );
}
void
pref_flag_set_default( const char * key, gboolean value ) {
    if( !g_key_file_has_key( getPrefsKeyFile( ), GROUP, key, NULL ) )
        pref_flag_set( key, value );
}

char*
pref_string_get( const char * key ) {
    return g_key_file_get_string( getPrefsKeyFile( ), GROUP, key, NULL );
}
void
pref_string_set( const char * key, const char * value ) {
    g_key_file_set_string( getPrefsKeyFile( ), GROUP, key, value );
}
void
pref_string_set_default( const char * key, const char * value ) {
    if( !g_key_file_has_key( getPrefsKeyFile( ), GROUP, key, NULL ) )
        pref_string_set( key, value );
}

void
pref_save(char **errstr)
{
    gsize datalen;
    GError * err = NULL;
    char * data;
    char * filename;
    char * path;

    filename = getPrefsFilename( );
    path = g_path_get_dirname( filename );
    mkdir_p( path, 0755 );

    data = g_key_file_to_data( getPrefsKeyFile(), &datalen, &err );
    if( !err ) {
        GIOChannel * out = g_io_channel_new_file( filename, "w+", &err );
        g_io_channel_write_chars( out, data, datalen, NULL, &err );
        g_io_channel_unref( out );
    }

    if( errstr != NULL )
        *errstr = err ? g_strdup( err->message ) : NULL;

    g_clear_error( &err );
    g_free( data );
    g_free( path );
    g_free( filename );
}
