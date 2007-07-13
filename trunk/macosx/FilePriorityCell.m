#import "FilePriorityCell.h"
#import "InfoWindowController.h"
#import "Torrent.h"

@implementation FilePriorityCell

- (id) init
{
    if ((self = [super init]))
    {
        [self setTrackingMode: NSSegmentSwitchTrackingSelectAny];
        [self setControlSize: NSMiniControlSize];
        [self setSegmentCount: 3];
        
        int i;
        for (i = 0; i < [self segmentCount]; i++)
        {
            [self setLabel: @"" forSegment: i];
            [self setWidth: 6.0 forSegment: i];
        }
    }
    return self;
}

- (void) setItem: (NSMutableDictionary *) item
{
    fItem = item;
}

- (void) setSelected: (BOOL) flag forSegment: (int) segment
{
    [super setSelected: flag forSegment: segment];
    
    //only for when clicking manually
    Torrent * torrent = [[[[self controlView] window] windowController] selectedTorrent];
    NSIndexSet * indexes = [fItem objectForKey: @"Indexes"];
    if (![torrent canChangeDownloadCheckForFiles: indexes])
        return;
    
    int priority;
    if (segment == 0)
        priority = TR_PRI_LOW;
    else if (segment == 2)
        priority = TR_PRI_HIGH;
    else
        priority = TR_PRI_NORMAL;
    
    [torrent setFilePriority: priority forIndexes: indexes];
    [(FileOutlineView *)[self controlView] reloadData];
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    Torrent * torrent = [(InfoWindowController *)[[[self controlView] window] windowController] selectedTorrent];
    NSSet * priorities = [torrent filePrioritiesForIndexes: [fItem objectForKey: @"Indexes"]];
    
    if ([priorities count] == 0)
        return;
    
    BOOL low = [priorities containsObject: [NSNumber numberWithInt: TR_PRI_LOW]],
        normal = [priorities containsObject: [NSNumber numberWithInt: TR_PRI_NORMAL]],
        high = [priorities containsObject: [NSNumber numberWithInt: TR_PRI_HIGH]];
    
    FileOutlineView * view = (FileOutlineView *)[self controlView];
    int row = [view hoverRow];
    if (row != -1 && [view itemAtRow: row] == fItem)
    {
        [super setSelected: low forSegment: 0];
        [super setSelected: normal forSegment: 1];
        [super setSelected: high forSegment: 2];
        
        [super drawWithFrame: cellFrame inView: controlView];
    }
    else
    {
        if (high || low)
        {
            BOOL highlighted = [self isHighlighted] && [[self highlightColorWithFrame: cellFrame inView: controlView]
                                        isEqual: [NSColor alternateSelectedControlColor]];
            NSDictionary * attributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                            highlighted ? [NSColor whiteColor] : [NSColor controlTextColor], NSForegroundColorAttributeName,
                            [NSFont messageFontOfSize: 18.0], NSFontAttributeName, nil];
            
            NSString * text;
            if (low && !normal && !high)
              text = @"-";
            else if (!low && !normal && high)
                text = @"+";
            else
                text = @"*";
            
            NSSize textSize = [text sizeWithAttributes: attributes];
            NSRect textRect = NSMakeRect(cellFrame.origin.x + (cellFrame.size.width - textSize.width) * 0.5,
                                    cellFrame.origin.y + (cellFrame.size.height - textSize.height) * 0.5,
                                    textSize.width, textSize.height);
            
            [text drawInRect: textRect withAttributes: attributes];
        }
    }
}

@end
