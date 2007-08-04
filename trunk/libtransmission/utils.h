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

#ifndef TR_UTILS_H
#define TR_UTILS_H 1

#include <inttypes.h>
#include <stdarg.h>

void tr_msgInit( void );

#define tr_err( a... ) tr_msg( TR_MSG_ERR, ## a )
#define tr_inf( a... ) tr_msg( TR_MSG_INF, ## a )
#define tr_dbg( a... ) tr_msg( TR_MSG_DBG, ## a )
void tr_msg  ( int level, char * msg, ... );

int  tr_rand ( int );

void * tr_memmem( const void *, size_t, const void *, size_t );

/***********************************************************************
 * tr_mkdirp
 ***********************************************************************
 * Create a directory and any needed parent directories.
 * Note that the string passed in must be writable!
 **********************************************************************/
int tr_mkdirp( char * path, int permissions );

int tr_mkdir( const char * path, int permissions );

/***********************************************************************
 * tr_strcasecmp
 ***********************************************************************
 * A case-insensitive strncmp()
 **********************************************************************/
#define tr_strcasecmp( ff, ss ) ( tr_strncasecmp( (ff), (ss), ULONG_MAX ) )
int tr_strncasecmp( const char * first, const char * second, size_t len );

/***********************************************************************
 * tr_sprintf
 ***********************************************************************
 * Appends to the end of a buffer using printf formatting,
 * growing the buffer if needed
 **********************************************************************/
int tr_sprintf( char ** buf, int * used, int * max,
                const char * format, ... );
/* gee, it sure would be nice if BeOS had va_copy() */
int tr_vsprintf( char **, int *, int *, const char *, va_list, va_list );
/* this concatenates some binary data onto the end of a buffer */
int tr_concat( char ** buf, int * used, int * max,
               const char * data, int len );

/* creates a filename from a series of elements using the
   correct separator for filenames. */
void tr_buildPath ( char* buf, size_t buflen,
                    const char * first_element, ... );


int    tr_ioErrorFromErrno( void );

char * tr_errorString( int code );

/* return the current date in milliseconds */
uint64_t tr_date( void );

/* wait the specified number of milliseconds */
void tr_wait( uint64_t delay_milliseconds );

/***
****
***/

/* Sometimes the system defines MAX/MIN, sometimes not. In the latter
   case, define those here since we will use them */
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef MIN
#define MIN(a,b) ((a)>(b)?(b):(a))
#endif

/***
****
***/

#define tr_new(struct_type, n_structs)           \
    ((struct_type *) tr_malloc (((size_t) sizeof (struct_type)) * ((size_t) (n_structs))))
#define tr_new0(struct_type, n_structs)          \
    ((struct_type *) tr_malloc0 (((size_t) sizeof (struct_type)) * ((size_t) (n_structs))))
#define tr_renew(struct_type, mem, n_structs)    \
    ((struct_type *) realloc ((mem), ((size_t) sizeof (struct_type)) * ((size_t) (n_structs))))

void* tr_malloc  ( size_t );
void* tr_malloc0 ( size_t );
void* tr_calloc  ( size_t nmemb, size_t size );
void  tr_free    ( void* );

char* tr_strdup( const char * str );
char* tr_strndup( const char * str, int len );

/***
****
***/

typedef struct tr_bitfield_s
{
    uint8_t * bits;
    size_t len;
}
tr_bitfield_t;

tr_bitfield_t* tr_bitfieldNew( size_t bitcount );
tr_bitfield_t* tr_bitfieldDup( const tr_bitfield_t* );
void tr_bitfieldFree( tr_bitfield_t*);

void tr_bitfieldClear( tr_bitfield_t* );
void tr_bitfieldAdd( tr_bitfield_t*, size_t bit );
void tr_bitfieldRem( tr_bitfield_t*, size_t bit );
void tr_bitfieldAddRange( tr_bitfield_t *, size_t begin, size_t end );
void tr_bitfieldRemRange ( tr_bitfield_t*, size_t begin, size_t end );

int    tr_bitfieldIsEmpty( const tr_bitfield_t* );
size_t tr_bitfieldCountTrueBits( const tr_bitfield_t* );

tr_bitfield_t* tr_bitfieldNegate( tr_bitfield_t* );
tr_bitfield_t* tr_bitfieldAnd( tr_bitfield_t*, const tr_bitfield_t* );

#define tr_bitfieldHas(bitfield,nth) \
    ( ( bitfield ) && ( (bitfield)->bits[(nth)>>3] & 128 >>( (nth) & 7 ) ) )

#endif
