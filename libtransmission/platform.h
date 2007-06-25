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
#define TR_PLATFORM_H 1

#ifdef SYS_BEOS
  #include <kernel/OS.h>
  typedef thread_id tr_thread_id_t;
  typedef sem_id    tr_lock_t;
  typedef int       tr_cond_t;
#else
  #include <pthread.h>
  typedef pthread_t       tr_thread_id_t;
  typedef pthread_mutex_t tr_lock_t;
  typedef pthread_cond_t  tr_cond_t;
#endif
typedef struct tr_thread_s
{
    void          (* func ) ( void * );
    void           * arg;
    char           * name;
    tr_thread_id_t thread;;
}
tr_thread_t;

const char * tr_getHomeDirectory( void );
const char * tr_getCacheDirectory( void );
const char * tr_getTorrentsDirectory( void );

/**
 * When instantiating a thread with a deferred call to tr_threadCreate(),
 * initializing it to THREAD_EMPTY makes calls tr_threadJoin() safe.
 */ 
const tr_thread_t THREAD_EMPTY;

void tr_threadCreate ( tr_thread_t *, void (*func)(void *),
                       void * arg, const char * name );
void tr_threadJoin   ( tr_thread_t * );
void tr_lockInit     ( tr_lock_t * );
void tr_lockClose    ( tr_lock_t * );
int  tr_lockTryLock  ( tr_lock_t * );
void tr_lockLock     ( tr_lock_t * );
void tr_lockUnlock   ( tr_lock_t * );

void tr_condInit      ( tr_cond_t * );
void tr_condSignal    ( tr_cond_t * );
void tr_condBroadcast ( tr_cond_t * );
void tr_condClose     ( tr_cond_t * );
void tr_condWait      ( tr_cond_t *, tr_lock_t * );

/***
**** RW lock:
**** The lock can be had by one writer or any number of readers.
***/

typedef struct tr_rwlock_s
{
    tr_lock_t lock;
    tr_cond_t readCond;
    tr_cond_t writeCond;
    size_t readCount;
    size_t wantToRead;
    size_t wantToWrite;
    int haveWriter;
}
tr_rwlock_t;

void  tr_rwInit          ( tr_rwlock_t * );
void  tr_rwClose         ( tr_rwlock_t * );
void  tr_rwReaderLock    ( tr_rwlock_t * );
int   tr_rwReaderTrylock ( tr_rwlock_t * );
void  tr_rwReaderUnlock  ( tr_rwlock_t * );
void  tr_rwWriterLock    ( tr_rwlock_t * );
int   tr_rwWriterTrylock ( tr_rwlock_t * );
void  tr_rwWriterUnlock  ( tr_rwlock_t * );


struct in_addr; /* forward declaration to calm gcc down */
int
tr_getDefaultRoute( struct in_addr * addr );

#endif
