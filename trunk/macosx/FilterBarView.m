/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007 Transmission authors and contributors
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

#import "FilterBarView.h"

@implementation FilterBarView

- (void) awakeFromNib
{
    [fNoFilterButton setTitle: NSLocalizedString(@"All", @"Filter Bar Button -> title")];
    [fDownloadFilterButton setTitle: NSLocalizedString(@"Downloading", @"Filter Bar Button -> title")];
    [fSeedFilterButton setTitle: NSLocalizedString(@"Seeding", @"Filter Bar Button -> title")];
    [fPauseFilterButton setTitle: NSLocalizedString(@"Paused", @"Filter Bar Button -> title")];
    
    [fNoFilterButton sizeToFit];
    [fDownloadFilterButton sizeToFit];
    [fSeedFilterButton sizeToFit];
    [fPauseFilterButton sizeToFit];
    
    float padding = 2.0, base = 3.0;
    [fNoFilterButton setFrameOrigin: NSMakePoint(padding + 2.0, base)];
    [fDownloadFilterButton setFrameOrigin: NSMakePoint(NSMaxX([fNoFilterButton frame]) + padding, base)];
    [fSeedFilterButton setFrameOrigin: NSMakePoint(NSMaxX([fDownloadFilterButton frame]) + padding, base)];
    [fPauseFilterButton setFrameOrigin: NSMakePoint(NSMaxX([fSeedFilterButton frame]) + padding, base)];
    
    [self setNeedsDisplay: YES];
}

@end
