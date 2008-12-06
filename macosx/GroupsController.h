/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007-2008 Transmission authors and contributors
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

#import <Cocoa/Cocoa.h>

@interface GroupsController : NSObject
{
    NSMutableArray * fGroups;
}

+ (GroupsController *) groups;

- (NSInteger) numberOfGroups;

- (NSInteger) rowValueForIndex: (NSInteger) index;
- (NSInteger) indexForRow: (NSInteger) row;

- (NSString *) nameForIndex: (NSInteger) index;
- (void) setName: (NSString *) name forIndex: (NSInteger) index;

- (NSImage *) imageForIndex: (NSInteger) index;

- (NSColor *) colorForIndex: (NSInteger) index;
- (void) setColor: (NSColor *) color forIndex: (NSInteger) index;

- (BOOL) usesCustomDownloadLocationForIndex: (NSInteger) index;
- (void) setUsesCustomDownloadLocation: (BOOL) useCustomLocation forIndex: (NSInteger) index;

- (NSString *) customDownloadLocationForIndex: (NSInteger) index;
- (void) setCustomDownloadLocation: (NSString *) location forIndex: (NSInteger) index;

- (void) addNewGroup;
- (void) removeGroupWithRowIndex: (NSInteger) row;

- (void) moveGroupAtRow: (NSInteger) oldRow toRow: (NSInteger) newRow;

- (NSMenu *) groupMenuWithTarget: (id) target action: (SEL) action isSmall: (BOOL) small;

@end
