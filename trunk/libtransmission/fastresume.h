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

#ifndef TR_FAST_RESUME_H
#define TR_FAST_RESUME_H

void tr_fastResumeSave( const tr_torrent * tor );

enum
{
  TR_FR_DOWNLOADED   = (1<<0),
  TR_FR_UPLOADED     = (1<<1),
  TR_FR_CORRUPT      = (1<<2),
  TR_FR_PEERS        = (1<<3),
  TR_FR_PROGRESS     = (1<<4),
  TR_FR_PRIORITY     = (1<<5),
  TR_FR_SPEEDLIMIT   = (1<<6),
  TR_FR_RUN          = (1<<7),
  TR_FR_DESTINATION  = (1<<8),
  TR_FR_MAX_PEERS    = (1<<9)
};

/**
 * Returns a bitwise-or'ed set of the data loaded from fastresume
 */
uint64_t tr_fastResumeLoad( tr_torrent          * tor,
                            uint64_t              fieldsToLoad,
                            const tr_ctor       * ctor );

void tr_fastResumeRemove( const tr_torrent * tor );

#endif
