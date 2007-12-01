/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006 Transmission authors and contributors
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

#ifndef TR_NATPMP_H
#define TR_NATPMP_H 1

typedef struct tr_natpmp tr_natpmp; 

tr_natpmp * tr_natpmpInit();
void        tr_natpmpStart( tr_natpmp * );
void        tr_natpmpStop( tr_natpmp * );
int         tr_natpmpStatus( tr_natpmp * );
void        tr_natpmpForwardPort( tr_natpmp *, int );
void        tr_natpmpRemoveForwarding( tr_natpmp * );
void        tr_natpmpPulse( tr_natpmp *, int * );
void        tr_natpmpClose( tr_natpmp * );

#define PMP_MCAST_ADDR "224.0.0.1"

#endif
