#import "FilePriorityCell.h"
#import "Torrent.h"

@implementation FilePriorityCell

- (id) initForParentView: (FileOutlineView *) parentView
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
        
        #warning better way?
        fParentView = parentView;
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
    Torrent * torrent = [fItem objectForKey: @"Torrent"];
    if (![torrent canChangeDownloadCheckForFiles: [fItem objectForKey: @"Indexes"]])
        return;
    
    int priority = segment, actualPriority;
    if (priority == 0)
        actualPriority = PRIORITY_LOW;
    else if (priority == 2)
        actualPriority = PRIORITY_HIGH;
    else
        actualPriority = PRIORITY_NORMAL;
    
    [torrent setFilePriority: actualPriority forIndexes: [fItem objectForKey: @"Indexes"]];
    [fParentView reloadData];
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    Torrent * torrent = [fItem objectForKey: @"Torrent"];
    NSIndexSet * indexSet = [fItem objectForKey: @"Indexes"];
    
    if (![torrent canChangeDownloadCheckForFiles: indexSet])
        return;
    
    BOOL low = [torrent hasFilePriority: PRIORITY_LOW forIndexes: indexSet],
        normal = [torrent hasFilePriority: PRIORITY_NORMAL forIndexes: indexSet],
        high = [torrent hasFilePriority: PRIORITY_HIGH forIndexes: indexSet];
    
    int row = [fParentView hoverRow];
    if (row != -1 && [fParentView itemAtRow: row] == fItem)
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
