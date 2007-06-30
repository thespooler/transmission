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

#include <ctype.h>
#include "transmission.h"

#define SPRINTF_BUFSIZE         100

static tr_lock_t      * messageLock = NULL;
static int              messageLevel = 0;
static int              messageQueuing = 0;
static tr_msg_list_t *  messageQueue = NULL;
static tr_msg_list_t ** messageQueueTail = &messageQueue;

void tr_msgInit( void )
{
    if( NULL == messageLock )
    {
        messageLock = calloc( 1, sizeof( *messageLock ) );
        tr_lockInit( messageLock );
    }
}

void tr_setMessageLevel( int level )
{
    tr_msgInit();
    tr_lockLock( messageLock );
    messageLevel = MAX( 0, level );
    tr_lockUnlock( messageLock );
}

int tr_getMessageLevel( void )
{
    int ret;

    tr_msgInit();
    tr_lockLock( messageLock );
    ret = messageLevel;
    tr_lockUnlock( messageLock );

    return ret;
}

void tr_setMessageQueuing( int enabled )
{
    tr_msgInit();
    tr_lockLock( messageLock );
    messageQueuing = enabled;
    tr_lockUnlock( messageLock );
}

tr_msg_list_t * tr_getQueuedMessages( void )
{
    tr_msg_list_t * ret;

    assert( NULL != messageLock );
    tr_lockLock( messageLock );
    ret = messageQueue;
    messageQueue = NULL;
    messageQueueTail = &messageQueue;
    tr_lockUnlock( messageLock );

    return ret;
}

void tr_freeMessageList( tr_msg_list_t * list )
{
    tr_msg_list_t * next;

    while( NULL != list )
    {
        next = list->next;
        free( list->message );
        free( list );
        list = next;
    }
}

void tr_msg( int level, char * msg, ... )
{
    va_list         args1, args2;
    tr_msg_list_t * newmsg;
    int             len1, len2;

    assert( NULL != messageLock );
    tr_lockLock( messageLock );

    if( !messageLevel )
    {
        char * env;
        env          = getenv( "TR_DEBUG" );
        messageLevel = ( env ? atoi( env ) : 0 ) + 1;
        messageLevel = MAX( 1, messageLevel );
    }

    if( messageLevel >= level )
    {
        va_start( args1, msg );
        if( messageQueuing )
        {
            newmsg = calloc( 1, sizeof( *newmsg ) );
            if( NULL != newmsg )
            {
                newmsg->level = level;
                newmsg->when = time( NULL );
                len1 = len2 = 0;
                va_start( args2, msg );
                tr_vsprintf( &newmsg->message, &len1, &len2, msg,
                             args1, args2 );
                va_end( args2 );
                if( NULL == newmsg->message )
                {
                    free( newmsg );
                }
                else
                {
                    *messageQueueTail = newmsg;
                    messageQueueTail = &newmsg->next;
                }
            }
        }
        else
        {
            vfprintf( stderr, msg, args1 );
            fputc( '\n', stderr );
        }
        va_end( args1 );
    }

    tr_lockUnlock( messageLock );
}

int tr_rand( int sup )
{
    static int init = 0;
    if( !init )
    {
        srand( tr_date() );
        init = 1;
    }
    return rand() % sup;
}


void*
tr_memmem( const void* haystack, size_t hl,
           const void* needle,   size_t nl)
{
    const char *walk, *end;

    if( !nl )
        return (void*) haystack;

    if( hl < nl )
        return NULL;

    for (walk=(const char*)haystack, end=walk+hl-nl; walk!=end; ++walk)
        if( !memcmp( walk, needle, nl ) )
            return (void*) walk;

    return NULL;
}

int tr_mkdir( char * path )
{
    char      * p, * pp;
    struct stat sb;
    int done;

    p = path;
    while( '/' == *p )
      p++;
    pp = p;
    done = 0;
    while( ( p = strchr( pp, '/' ) ) || ( p = strchr( pp, '\0' ) ) )
    {
        if( '\0' == *p)
        {
            done = 1;
        }
        else
        {
            *p = '\0';
        }
        if( stat( path, &sb ) )
        {
            /* Folder doesn't exist yet */
            if( mkdir( path, 0777 ) )
            {
                tr_err( "Could not create directory %s (%s)", path,
                        strerror( errno ) );
                *p = '/';
                return 1;
            }
        }
        else if( ( sb.st_mode & S_IFMT ) != S_IFDIR )
        {
            /* Node exists but isn't a folder */
            tr_err( "Remove %s, it's in the way.", path );
            *p = '/';
            return 1;
        }
        if( done )
        {
            break;
        }
        *p = '/';
        p++;
        pp = p;
    }

    return 0;
}

int
tr_strncasecmp( const char * s1, const char * s2, size_t n )
{
    if ( !n )
        return 0;

    while( n-- != 0 && tolower( *s1 ) == tolower( *s2 ) ) {
        if( !n || !*s1 || !*s2 )
	    break;
        ++s1;
        ++s2;
    }

    return tolower(*(unsigned char *) s1) - tolower(*(unsigned char *) s2);
}

int tr_sprintf( char ** buf, int * used, int * max, const char * format, ... )
{
    va_list ap1, ap2;
    int     ret;

    va_start( ap1, format );
    va_start( ap2, format );
    ret = tr_vsprintf( buf, used, max, format, ap1, ap2 );
    va_end( ap2 );
    va_end( ap1 );

    return ret;
}

int tr_vsprintf( char ** buf, int * used, int * max, const char * fmt,
                 va_list ap1, va_list ap2 )
{
    int     want;

    want = vsnprintf( NULL, 0, fmt, ap1 );

    if( tr_concat( buf, used, max, NULL, want ) )
    {
        return 1;
    }
    assert( *used + want + 1 <= *max );

    *used += vsnprintf( *buf + *used, *max - *used, fmt, ap2 );

    return 0;
}

int tr_concat( char ** buf, int * used, int * max, const char * data, int len )
{
    int     newmax;
    char  * newbuf;

    newmax = *max;
    while( *used + len + 1 > newmax )
    {
        newmax += SPRINTF_BUFSIZE;
    }
    if( newmax > *max )
    {
        newbuf = realloc( *buf, newmax );
        if( NULL == newbuf )
        {
            return 1;
        }
        *buf = newbuf;
        *max = newmax;
    }

    if( NULL != data )
    {
        memcpy( *buf + *used, data, len );
        *used += len;
    }

    return 0;
}

void
tr_buildPath ( char *buf, size_t buflen, const char *first_element, ... )
{
    va_list vl;
    char* walk = buf;
    const char * element = first_element;
    va_start( vl, first_element );
    for( ;; ) {
        const size_t n = strlen( element );
        memcpy( walk, element, n );
        walk += n;
        element = (const char*) va_arg( vl, const char* );
        if( element == NULL )
            break;
        *walk++ = TR_PATH_DELIMITER;
    }
    *walk = '\0';
    assert( walk-buf <= (int)buflen );
}

int
tr_ioErrorFromErrno( void )
{
    switch( errno )
    {
        case EACCES:
        case EROFS:
            return TR_ERROR_IO_PERMISSIONS;
        case ENOSPC:
            return TR_ERROR_IO_SPACE;
        case EMFILE:
        case EFBIG:
            return TR_ERROR_IO_RESOURCES;
        default:
            tr_dbg( "generic i/o errno from errno: %s", strerror( errno ) );
            return TR_ERROR_IO_OTHER;
    }
}

char *
tr_errorString( int code )
{
    switch( code )
    {
        case TR_OK:
            return "No error";
        case TR_ERROR:
            return "Generic error";
        case TR_ERROR_ASSERT:
            return "Assert error";
        case TR_ERROR_IO_PARENT:
            return "Download folder does not exist";
        case TR_ERROR_IO_PERMISSIONS:
            return "Insufficient permissions";
        case TR_ERROR_IO_SPACE:
            return "Insufficient free space";
        case TR_ERROR_IO_DUP_DOWNLOAD:
            return "Already active transfer with same name and download folder";
        case TR_ERROR_IO_RESOURCES:
            return "Insufficient resources";
        case TR_ERROR_IO_OTHER:
            return "Generic I/O error";
    }
    return "Unknown error";
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
    if( in != NULL )
    {
        out = tr_calloc( len+1, 1 );
        memcpy( out, in, len );
    }
    return out;
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

void tr_free( void * p )
{
    if( p )
        free( p );
}

/****
*****
****/

/* note that the argument is how many bits are needed, not bytes */
tr_bitfield_t*
tr_bitfieldNew( size_t bitcount )
{
    tr_bitfield_t * ret = calloc( 1, sizeof(tr_bitfield_t) );
    if( NULL == ret )
        return NULL;

    ret->len = ( bitcount + 7u ) / 8u;
    ret->bits = calloc( ret->len, 1 );
    if( NULL == ret->bits ) {
        free( ret );
        return NULL;
    }

    return ret;
}

tr_bitfield_t*
tr_bitfieldDup( const tr_bitfield_t * in )
{
    tr_bitfield_t * ret = calloc( 1, sizeof(tr_bitfield_t) );
    ret->len = in->len;
    ret->bits = malloc( ret->len );
    memcpy( ret->bits, in->bits, ret->len );
    return ret;
}

void tr_bitfieldFree( tr_bitfield_t * bitfield )
{
    if( bitfield )
    {
        free( bitfield->bits );
        free( bitfield );
    }
}

void
tr_bitfieldClear( tr_bitfield_t * bitfield )
{
    memset( bitfield->bits, 0, bitfield->len );
}

int
tr_bitfieldIsEmpty( const tr_bitfield_t * bitfield )
{
    unsigned int i;

    for( i=0; i<bitfield->len; ++i )
        if( *bitfield->bits )
            return 0;

    return 1;
}

static const uint8_t bitmask[8] = { 128u, 64u, 32u, 16u, 8u, 4u, 2u, 1u };

int
tr_bitfieldHas( const tr_bitfield_t   * bitfield,
                size_t                  bit )
{
    if ( bitfield == NULL ) return 0;
    assert( bit / 8u < bitfield->len );
    return ( bitfield->bits[ bit/8u ] & bitmask[bit%8] );
}

void
tr_bitfieldAdd( tr_bitfield_t  * bitfield, size_t bit )
{
    assert( bit / 8u < bitfield->len );
    bitfield->bits[ bit/8u ] |= bitmask[bit%8];
}

void
tr_bitfieldAddRange( tr_bitfield_t  * bitfield,
                     size_t           first,
                     size_t           last )
{
    /* TODO: there are faster ways to do this */
    unsigned int i;
    for( i=first; i<=last; ++i )
        tr_bitfieldAdd( bitfield, i );
}

void
tr_bitfieldRem( tr_bitfield_t   * bitfield,
                size_t            bit )
{
    if( bitfield != NULL )
    {
        assert( bit / 8u < bitfield->len );
        bitfield->bits[bit/8u] &= ~bitmask[bit%8];
    }
}

void
tr_bitfieldRemRange ( tr_bitfield_t  * b,
                      size_t           first,
                      size_t           last )
{
    /* TODO: there are faster ways to do this */
    unsigned int i;
    for( i=first; i<=last; ++i )
        tr_bitfieldRem( b, i );
}

tr_bitfield_t*
tr_bitfieldNegate( tr_bitfield_t * b )
{
    uint8_t *it;
    const uint8_t *end;

    for( it=b->bits, end=it+b->len; it!=end; ++it )
        *it = ~*it;

    return b;
}

tr_bitfield_t*
tr_bitfieldAnd( tr_bitfield_t * a, const tr_bitfield_t * b )
{
    uint8_t *ait;
    const uint8_t *aend, *bit;

    assert( a->len == b->len );

    for( ait=a->bits, bit=b->bits, aend=ait+a->len; ait!=aend; ++ait, ++bit )
        *ait &= *bit;

    return a;
}

size_t
tr_bitfieldCountTrueBits( const tr_bitfield_t* b )
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

    for( it=b->bits, end=it+b->len; it!=end; ++it )
        ret += trueBitCount[*it];

    return ret;
}
