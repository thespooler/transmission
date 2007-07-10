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

#import "FileOutlineView.h"
#import "FileBrowserCell.h"
#import "InfoWindowController.h"
#import "Torrent.h"

@implementation FileOutlineView

- (void) awakeFromNib
{
    NSBrowserCell * browserCell = [[[FileBrowserCell alloc] init] autorelease];
    [[self tableColumnWithIdentifier: @"Name"] setDataCell: browserCell];
    
    [self setAutoresizesOutlineColumn: NO];
    [self setIndentationPerLevel: 14.0];
    
    fNormalColor = [self backgroundColor];
    fHighPriorityColor = [[NSColor colorWithCalibratedRed: 0.678 green: 0.937 blue: 0.451 alpha: 1.0] retain];
    fLowPriorityColor = [[NSColor colorWithCalibratedRed: 0.984 green: 0.878 blue: 0.431 alpha: 1.0] retain];
    
    fHoverRow = -1;
}

- (void) dealloc
{
    [fHighPriorityColor release];
    [fLowPriorityColor release];
    
    [super dealloc];
}

- (void) mouseDown: (NSEvent *) event
{
    [[self window] makeKeyWindow];
    [super mouseDown: event];
}

- (NSMenu *) menuForEvent: (NSEvent *) event
{
    int row = [self rowAtPoint: [self convertPoint: [event locationInWindow] fromView: nil]];
    
    if (row >= 0)
    {
        if (![self isRowSelected: row])
            [self selectRowIndexes: [NSIndexSet indexSetWithIndex: row] byExtendingSelection: NO];
    }
    else
        [self deselectAll: self];
    
    return [self menu];
}

- (void) setHoverRowForEvent: (NSEvent *) event
{
    int row = -1;
    if (event)
    {
        NSPoint point = [self convertPoint: [event locationInWindow] fromView: nil];
        if ([self columnAtPoint: point] == [self columnWithIdentifier: @"Priority"])
            row = [self rowAtPoint: point];
    }
    
    if (row != fHoverRow)
    {
        if (fHoverRow != -1)
            [self reloadItem: [self itemAtRow: fHoverRow]];
        fHoverRow = row;
        if (fHoverRow != -1)
            [self reloadItem: [self itemAtRow: fHoverRow]];
    }
}

- (int) hoverRow
{
    return fHoverRow;
}

- (void) drawRow: (int) row clipRect: (NSRect) clipRect
{
    if (![self isRowSelected: row])
    {
        NSDictionary * item = [self itemAtRow: row];
        Torrent * torrent = [(InfoWindowController *)[[self window] windowController] selectedTorrent];
        
        if ([[item objectForKey: @"IsFolder"] boolValue]
                || ![torrent canChangeDownloadCheckForFiles: [item objectForKey: @"Indexes"]])
            [fNormalColor set];
        else
        {
            NSIndexSet * indexSet = [item objectForKey: @"Indexes"];
            if ([torrent hasFilePriority: TR_PRI_HIGH forIndexes: indexSet])
                [fHighPriorityColor set];
            else if ([torrent hasFilePriority: TR_PRI_LOW forIndexes: indexSet])
                [fLowPriorityColor set];
            else
                [fNormalColor set];
        }
        
        NSRect rect = [self rectOfRow: row];
        rect.size.height -= 1.0;
        
        NSRectFill(rect);
    }
    
    [super drawRow: row clipRect: clipRect];
}

- (void) drawRect: (NSRect) r
{
    [super drawRect: r];

    NSDictionary * item;
    NSIndexSet * indexSet;
    int i, priority;
    Torrent * torrent = [(InfoWindowController *)[[self window] windowController] selectedTorrent];
    for (i = 0; i < [self numberOfRows]; i++)
    {
        if ([self isRowSelected: i])
        {
            item = [self itemAtRow: i];
            if ([[item objectForKey: @"IsFolder"] boolValue])
                continue;
            
            indexSet = [item objectForKey: @"Indexes"];
            if ([torrent canChangeDownloadCheckForFiles: indexSet])
            {
                if ([torrent hasFilePriority: TR_PRI_HIGH forIndexes: indexSet])
                    [fHighPriorityColor set];
                else if ([torrent hasFilePriority: TR_PRI_LOW forIndexes: indexSet])
                    [fLowPriorityColor set];
                else
                    continue;
                
                NSRect rect = [self rectOfRow: i];
                float width = 14.0;
                rect.origin.y += (rect.size.height - width) * 0.5;
                rect.origin.x += 3.0;
                rect.size.width = width;
                rect.size.height = width;
                
                [[NSBezierPath bezierPathWithOvalInRect: rect] fill];
            }
        }
    }
}

@end
