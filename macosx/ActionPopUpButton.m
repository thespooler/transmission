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

#import "ActionPopUpButton.h"

@implementation ActionPopUpButton

#warning look into ellimination

- (id) initWithCoder: (NSCoder *) coder
{
	if ((self = [super initWithCoder: coder]))
    {
        fImage = [NSImage imageNamed: @"ActionPopUp.png"];
        [fImage setFlipped: YES];
	}
	return self;
}

- (void) drawRect: (NSRect) rect
{
    [super drawRect: rect];
    
    NSSize imageSize = [fImage size];
    NSRect imageRect = NSMakeRect(rect.origin.x + (rect.size.width - imageSize.width) * 0.5,
                        rect.origin.y + (rect.size.height - imageSize.height) * 0.5, imageSize.width, imageSize.height);
    
	[fImage drawInRect: imageRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
}

@end
