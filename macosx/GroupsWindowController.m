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

#import "GroupsWindowController.h"
#import "GradientCell.h"
#import "CTGradient.h"
#import "NSBezierPathAdditions.h"
#import "NSApplicationAdditions.h"

typedef enum
{
    ADD_TAG = 0,
    REMOVE_TAG = 1
} controlTag;

@interface GroupsWindowController (Private)

- (void) saveGroups;

- (CTGradient *) gradientForColor: (NSColor *) color;
- (void) changeColor: (id) sender;

@end

@implementation GroupsWindowController

GroupsWindowController * fGroupsWindowInstance = nil;
+ (GroupsWindowController *) groupsController
{
    if (!fGroupsWindowInstance)
        fGroupsWindowInstance = [[GroupsWindowController alloc] init];
    return fGroupsWindowInstance;
}

- (id) init
{
    if ((self = [super initWithWindowNibName: @"GroupsWindow"]))
    {
        NSData * data;
        if ((data = [[NSUserDefaults standardUserDefaults] dataForKey: @"Groups"]))
            fGroups = [[NSUnarchiver unarchiveObjectWithData: data] retain];
        else
        {
            //default groups
            NSMutableDictionary * red = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor redColor], @"Color",
                                            NSLocalizedString(@"Red", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInt: 0], @"Index", nil];
            
            NSMutableDictionary * orange = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor orangeColor], @"Color",
                                            NSLocalizedString(@"Orange", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInt: 1], @"Index", nil];
            
            NSMutableDictionary * yellow = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor yellowColor], @"Color",
                                            NSLocalizedString(@"Yellow", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInt: 2], @"Index", nil];
            
            NSMutableDictionary * green = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor greenColor], @"Color",
                                            NSLocalizedString(@"Green", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInt: 3], @"Index", nil];
            
            NSMutableDictionary * blue = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor blueColor], @"Color",
                                            NSLocalizedString(@"Blue", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInt: 4], @"Index", nil];
            
            NSMutableDictionary * purple = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor purpleColor], @"Color",
                                            NSLocalizedString(@"Purple", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInt: 5], @"Index", nil];
            
            NSMutableDictionary * gray = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor grayColor], @"Color",
                                            NSLocalizedString(@"Gray", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInt: 6], @"Index", nil];
            
            fGroups = [[NSMutableArray alloc] initWithObjects: red, orange, yellow, green, blue, purple, gray, nil];
            [self saveGroups]; //make sure this is saved right away
        }
    }
    
    return self;
}

- (void) awakeFromNib
{
    GradientCell * cell = [[GradientCell alloc] init];
    [[fTableView tableColumnWithIdentifier: @"Color"] setDataCell: cell];
    [cell release];
    
    if ([NSApp isOnLeopardOrBetter])
        [[self window] setContentBorderThickness: [[fTableView enclosingScrollView] frame].origin.y forEdge: NSMinYEdge];
    
    [fAddRemoveControl setEnabled: NO forSegment: REMOVE_TAG];
}

- (void) dealloc
{
    [fGroups release];
    [super dealloc];
}

- (CTGradient *) gradientForIndex: (int) index
{
    if (index < 0)
        return nil;
    
    NSEnumerator * enumerator = [fGroups objectEnumerator];
    NSDictionary * dict;
    while ((dict = [enumerator nextObject]))
        if ([[dict objectForKey: @"Index"] intValue] == index)
            return [self gradientForColor: [dict objectForKey: @"Color"]];
    
    return nil;
}

- (NSInteger) numberOfRowsInTableView: (NSTableView *) tableview
{
    return [fGroups count];
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn: (NSTableColumn *) tableColumn row: (NSInteger) row
{
    #warning consider color column ?
    NSString * identifier = [tableColumn identifier];
    if ([identifier isEqualToString: @"Color"])
        return [self gradientForColor: [[fGroups objectAtIndex: row] objectForKey: @"Color"]];
    else
        return [[fGroups objectAtIndex: row] objectForKey: @"Name"];
}

- (void) tableView: (NSTableView *) tableView setObjectValue: (id) object forTableColumn: (NSTableColumn *) tableColumn
    row: (NSInteger) row
{
    NSString * identifier = [tableColumn identifier];
    if ([identifier isEqualToString: @"Name"])
    {
        [[fGroups objectAtIndex: row] setObject: object forKey: @"Name"];
        [self saveGroups];
    }
    else if ([identifier isEqualToString: @"Button"])
    {
        fCurrentColorDict = [fGroups objectAtIndex: row];
        
        NSColorPanel * colorPanel = [NSColorPanel sharedColorPanel];
        [colorPanel setContinuous: YES];
        [colorPanel setColor: [[fGroups objectAtIndex: row] objectForKey: @"Color"]];
        
        [colorPanel setTarget: self];
        [colorPanel setAction: @selector(changeColor:)];
        
        [colorPanel orderFront: self];
    }
    else;
}

- (void) tableViewSelectionDidChange: (NSNotification *) notification
{
    [fAddRemoveControl setEnabled: [fTableView numberOfSelectedRows] > 0 forSegment: REMOVE_TAG];
}

- (void) addRemoveGroup: (id) sender
{
    NSEnumerator * enumerator;
    NSDictionary * dict;
    int index;
    BOOL found;
    NSIndexSet * rowIndexes;
    NSMutableIndexSet * indexes;
    
    switch ([[sender cell] tagForSegment: [sender selectedSegment]])
    {
        case ADD_TAG:
            
            //find the lowest index
            for (index = 0; index < [fGroups count]; index++)
            {
                found = NO;
                enumerator = [fGroups objectEnumerator];
                while ((dict = [enumerator nextObject]))
                    if ([[dict objectForKey: @"Index"] intValue] == index)
                    {
                        found = YES;
                        break;
                    }
                
                if (!found)
                    break;
            }
            
            [fGroups addObject: [NSMutableDictionary dictionaryWithObjectsAndKeys: [NSNumber numberWithInt: index], @"Index",
                                    [NSColor cyanColor], @"Color", @"", @"Name", nil]];
            [fTableView reloadData];
            [fTableView deselectAll: self];
            
            [fTableView editColumn: [fTableView columnWithIdentifier: @"Name"] row: [fTableView numberOfRows]-1 withEvent: nil
                        select: NO];
            break;
        
        case REMOVE_TAG:
            
            rowIndexes = [fTableView selectedRowIndexes];
            indexes = [NSMutableIndexSet indexSet];
            for (index = [rowIndexes firstIndex]; index != NSNotFound; index = [rowIndexes indexGreaterThanIndex: index])
                [indexes addIndex: [[[fGroups objectAtIndex: index] objectForKey: @"Index"] intValue]];
            
            [fGroups removeObjectsAtIndexes: rowIndexes];
            [fTableView deselectAll: self];
            [fTableView reloadData];
            
            [[NSNotificationCenter defaultCenter] postNotificationName: @"GroupValueRemoved" object: self userInfo:
                [NSDictionary dictionaryWithObject: indexes forKey: @"Indexes"]];
            break;
        
        default:
            return;
    }
    
    [self saveGroups];
}

- (NSMenu *) groupMenuWithTarget: (id) target action: (SEL) action
{
    NSMenu * menu = [[NSMenu alloc] initWithTitle: @"Groups"];
    
    NSMenuItem * item = [[NSMenuItem alloc] initWithTitle: @"None" action: action keyEquivalent: @""];
    [item setTarget: target];
    [item setRepresentedObject: [NSNumber numberWithInt: -1]];
    [menu addItem: item];
    [item release];
    
    [menu addItem: [NSMenuItem separatorItem]];
    
    NSBezierPath * bp = [NSBezierPath bezierPathWithRoundedRect: NSMakeRect(0.0, 0.0, 16.0, 16.0) radius: 4.0];
    
    NSEnumerator * enumerator = [fGroups objectEnumerator];
    NSDictionary * dict;
    while ((dict = [enumerator nextObject]))
    {
        item = [[NSMenuItem alloc] initWithTitle: [dict objectForKey: @"Name"] action: action keyEquivalent: @""];
        [item setTarget: target];
        
        NSImage * icon = [[NSImage alloc] initWithSize: NSMakeSize(16.0, 16.0)];
        
        [icon lockFocus];
        [[self gradientForColor: [dict objectForKey: @"Color"]] fillBezierPath: bp angle: 90];
        [icon unlockFocus];
        
        [item setImage: icon];
        [icon release];
        
        [item setRepresentedObject: [dict objectForKey: @"Index"]];
        
        [menu addItem: item];
        [item release];
    }
    
    return [menu autorelease];
}

@end

@implementation GroupsWindowController (Private)

- (void) saveGroups
{
    [[NSUserDefaults standardUserDefaults] setObject: [NSArchiver archivedDataWithRootObject: fGroups] forKey: @"Groups"];
}

- (CTGradient *) gradientForColor: (NSColor *) color
{
    return [CTGradient gradientWithBeginningColor: [color blendedColorWithFraction: 0.7 ofColor: [NSColor whiteColor]]
            endingColor: [color blendedColorWithFraction: 0.4 ofColor: [NSColor whiteColor]]];
}

- (void) changeColor: (id) sender
{
    [fCurrentColorDict setObject: [sender color] forKey: @"Color"];
    [fTableView reloadData];
    
    [self saveGroups];
}

@end
