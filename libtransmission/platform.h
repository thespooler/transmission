/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005 Transmission authors and contributors
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
#ifndef TR_PLATFORM_H
#define TR_PLATFORM_H

typedef struct tr_lock   tr_lock;
typedef struct tr_cond   tr_cond;
typedef struct tr_thread tr_thread;

const char * tr_getHomeDirectory( void );
const char * tr_getCacheDirectory( void );
const char * tr_getTorrentsDirectory( void );

tr_thread*   tr_threadNew  ( void (*func)(void *), void * arg, const char * name );
void         tr_threadJoin ( tr_thread * );
int          tr_amInThread ( const tr_thread * );
int           tr_amInThread ( const tr_thread_t * );

tr_lock *    tr_lockNew        ( void );
void         tr_lockFree       ( tr_lock * );
int          tr_lockTryLock    ( tr_lock * );
void         tr_lockLock       ( tr_lock * );
void         tr_lockUnlock     ( tr_lock * );

tr_cond *    tr_condNew       ( void );
void         tr_condFree      ( tr_cond * );
void         tr_condSignal    ( tr_cond * );
void         tr_condBroadcast ( tr_cond * );
void         tr_condWait      ( tr_cond *, tr_lock * );

struct in_addr; /* forward declaration to calm gcc down */
int
tr_getDefaultRoute( struct in_addr * addr );

#endif
