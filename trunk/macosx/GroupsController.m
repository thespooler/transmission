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

#import "GroupsController.h"
#import "CTGradient.h"
#import "NSBezierPathAdditions.h"

#define ICON_WIDTH 16.0
#define ICON_WIDTH_SMALL 12.0

@interface GroupsController (Private)

- (void) saveGroups;

- (NSImage *) imageForGroup: (NSMutableDictionary *) dict;

- (BOOL) torrent: (Torrent *) torrent doesMatchRulesForGroupAtIndex: (NSInteger) index;

@end

@implementation GroupsController

GroupsController * fGroupsInstance = nil;
+ (GroupsController *) groups
{
    if (!fGroupsInstance)
        fGroupsInstance = [[GroupsController alloc] init];
    return fGroupsInstance;
}

- (id) init
{
    if ((self = [super init]))
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

- (void) dealloc
{
    [fGroups release];
    [super dealloc];
}

- (NSInteger) numberOfGroups
{
    return [fGroups count];
}

- (NSInteger) rowValueForIndex: (NSInteger) index
{
    if (index != -1)
    {
        for (NSInteger i = 0; i < [fGroups count]; i++)
            if (index == [[[fGroups objectAtIndex: i] objectForKey: @"Index"] intValue])
                return i;
    }
    return -1;
}

- (NSInteger) indexForRow: (NSInteger) row
{
    return [[[fGroups objectAtIndex: row] objectForKey: @"Index"] intValue];
}

- (NSString *) nameForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? [[fGroups objectAtIndex: orderIndex] objectForKey: @"Name"] : nil;
}

- (void) setName: (NSString *) name forIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    [[fGroups objectAtIndex: orderIndex] setObject: name forKey: @"Name"];
    [self saveGroups];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
}

- (NSImage *) imageForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? [self imageForGroup: [fGroups objectAtIndex: orderIndex]]
                            : [NSImage imageNamed: @"GroupsNoneTemplate.png"];
}

- (NSColor *) colorForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? [[fGroups objectAtIndex: orderIndex] objectForKey: @"Color"] : nil;
}

- (void) setColor: (NSColor *) color forIndex: (NSInteger) index
{
    NSMutableDictionary * dict = [fGroups objectAtIndex: [self rowValueForIndex: index]];
    [dict removeObjectForKey: @"Icon"];
    
    [dict setObject: color forKey: @"Color"];
    
    [[GroupsController groups] saveGroups];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
}

- (BOOL) usesCustomDownloadLocationForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? [[[fGroups objectAtIndex: orderIndex] objectForKey: @"UsesCustomDownloadLocation"] boolValue] : NO;
}

- (void) setUsesCustomDownloadLocation: (BOOL) useCustomLocation forIndex: (NSInteger) index
{
    NSMutableDictionary * dict = [fGroups objectAtIndex: [self rowValueForIndex: index]];
    
    [dict setObject: [NSNumber numberWithBool: useCustomLocation] forKey: @"UsesCustomDownloadLocation"];
    
    [[GroupsController groups] saveGroups];
}

- (NSString *) customDownloadLocationForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? [[fGroups objectAtIndex: orderIndex] objectForKey: @"CustomDownloadLocation"] : nil;
}

- (void) setCustomDownloadLocation: (NSString *) location forIndex: (NSInteger) index
{
    NSMutableDictionary * dict = [fGroups objectAtIndex: [self rowValueForIndex: index]];
    
    if (location)
        [dict setObject: location forKey: @"CustomDownloadLocation"];
    else
    {
        [dict removeObjectForKey: @"CustomDownloadLocation"];
        [self setUsesCustomDownloadLocation: NO forIndex: index];
    }
    
    [[GroupsController groups] saveGroups];
}

- (BOOL) usesAutoAssignRulesForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    if (orderIndex == -1)
        return NO;
    
    NSNumber * assignRules = [[fGroups objectAtIndex: orderIndex] objectForKey: @"UsesAutoAssignRules"];
    return assignRules && [assignRules boolValue];
}

- (void) setUsesAutoAssignRules: (BOOL) useAutoAssignRules forIndex: (NSInteger) index
{
    NSMutableDictionary * dict = [fGroups objectAtIndex: [self rowValueForIndex: index]];
    
    [dict setObject: [NSNumber numberWithBool: useAutoAssignRules] forKey: @"UsesAutoAssignRules"];
    
    [[GroupsController groups] saveGroups];
}

- (NSArray *) autoAssignRulesForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? [[fGroups objectAtIndex: orderIndex] objectForKey: @"AutoAssignRules"] : nil;
}

- (void) setAutoAssignRules: (NSArray *) rules forIndex: (NSInteger) index
{
    NSMutableDictionary * dict = [fGroups objectAtIndex: [self rowValueForIndex: index]];
    
    if (rules && [rules count] > 0)
    {
        [dict setObject: rules forKey: @"AutoAssignRules"];
        
        [[GroupsController groups] saveGroups];
    }
    else
    {
        [dict removeObjectForKey: @"AutoAssignRules"];
        [self setUsesAutoAssignRules: NO forIndex: index];
    }
}

- (void) addNewGroup
{
    //find the lowest index
    NSInteger index;
    for (index = 0; index < [fGroups count]; index++)
    {
        BOOL found = NO;
        NSEnumerator * enumerator = [fGroups objectEnumerator];
        NSDictionary * dict;
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
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
    [self saveGroups];
}

- (void) removeGroupWithRowIndex: (NSInteger) row
{
    NSInteger index = [[[fGroups objectAtIndex: row] objectForKey: @"Index"] intValue];
    [fGroups removeObjectAtIndex: row];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"GroupValueRemoved" object: self userInfo:
        [NSDictionary dictionaryWithObject: [NSNumber numberWithInt: index] forKey: @"Index"]];
    
    if (index == [[NSUserDefaults standardUserDefaults] integerForKey: @"FilterGroup"])
        [[NSUserDefaults standardUserDefaults] setInteger: -2 forKey: @"FilterGroup"];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
    [self saveGroups];
}

- (void) moveGroupAtRow: (NSInteger) oldRow toRow: (NSInteger) newRow
{
    if (oldRow < newRow)
        newRow--;
    
    //remove objects to reinsert
    id movingGroup = [[fGroups objectAtIndex: oldRow] retain];
    [fGroups removeObjectAtIndex: oldRow];
    
    //insert objects at new location
    [fGroups insertObject: movingGroup atIndex: newRow];
    
    [movingGroup release];
    
    [self saveGroups];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
}

- (NSMenu *) groupMenuWithTarget: (id) target action: (SEL) action isSmall: (BOOL) small
{
    NSMenu * menu = [[NSMenu alloc] initWithTitle: @"Groups"];
    
    NSMenuItem * item = [[NSMenuItem alloc] initWithTitle: NSLocalizedString(@"None", "Groups -> Menu") action: action
                            keyEquivalent: @""];
    [item setTarget: target];
    [item setTag: -1];
    
    NSImage * icon = [NSImage imageNamed: @"GroupsNoneTemplate.png"];
    if (small)
    {
        icon = [icon copy];
        [icon setScalesWhenResized: YES];
        [icon setSize: NSMakeSize(ICON_WIDTH_SMALL, ICON_WIDTH_SMALL)];
        
        [item setImage: icon];
        [icon release];
    }
    else
        [item setImage: icon];
    
    [menu addItem: item];
    [item release];
    
    NSEnumerator * enumerator = [fGroups objectEnumerator];
    NSMutableDictionary * dict;
    while ((dict = [enumerator nextObject]))
    {
        item = [[NSMenuItem alloc] initWithTitle: [dict objectForKey: @"Name"] action: action keyEquivalent: @""];
        [item setTarget: target];
        
        [item setTag: [[dict objectForKey: @"Index"] intValue]];
        
        NSImage * icon = [self imageForGroup: dict];
        if (small)
        {
            icon = [icon copy];
            [icon setScalesWhenResized: YES];
            [icon setSize: NSMakeSize(ICON_WIDTH_SMALL, ICON_WIDTH_SMALL)];
            
            [item setImage: icon];
            [icon release];
        }
        else
            [item setImage: icon];
        
        [menu addItem: item];
        [item release];
    }
    
    return [menu autorelease];
}

- (NSInteger) groupIndexForTorrent: (Torrent *) torrent;
{
    NSEnumerator * enumerator = [fGroups objectEnumerator];
    NSMutableDictionary * group;
    while ((group = [enumerator nextObject]))
    {
        NSInteger row = [[group objectForKey: @"Index"] intValue];
        if ([self torrent: torrent doesMatchRulesForGroupAtIndex: row])
            return row;
    }
    return -1; // Default to no group
}

@end

@implementation GroupsController (Private)

- (void) saveGroups
{
    //don't archive the icon
    NSMutableArray * groups = [NSMutableArray arrayWithCapacity: [fGroups count]];
    NSEnumerator * enumerator = [fGroups objectEnumerator];
    NSDictionary * dict;
    while ((dict = [enumerator nextObject]))
    {
        NSMutableDictionary * tempDict = [dict mutableCopy];
        [tempDict removeObjectForKey: @"Icon"];
        [groups addObject: tempDict];
        [tempDict release];
    }
    
    [[NSUserDefaults standardUserDefaults] setObject: [NSArchiver archivedDataWithRootObject: groups] forKey: @"Groups"];
}

- (NSImage *) imageForGroup: (NSMutableDictionary *) dict
{
    NSImage * image;
    if ((image = [dict objectForKey: @"Icon"]))
        return image;
    
    NSRect rect = NSMakeRect(0.0, 0.0, ICON_WIDTH, ICON_WIDTH);
    
    NSBezierPath * bp = [NSBezierPath bezierPathWithRoundedRect: rect radius: 3.0];
    NSImage * icon = [[NSImage alloc] initWithSize: rect.size];
    
    NSColor * color = [dict objectForKey: @"Color"];
    
    [icon lockFocus];
    
    //border
    CTGradient * gradient = [CTGradient gradientWithBeginningColor: [color blendedColorWithFraction: 0.45 ofColor:
                                [NSColor whiteColor]] endingColor: color];
    [gradient fillBezierPath: bp angle: 270.0];
    
    //inside
    bp = [NSBezierPath bezierPathWithRoundedRect: NSInsetRect(rect, 1.0, 1.0) radius: 3.0];
    gradient = [CTGradient gradientWithBeginningColor: [color blendedColorWithFraction: 0.75 ofColor: [NSColor whiteColor]]
                endingColor: [color blendedColorWithFraction: 0.2 ofColor: [NSColor whiteColor]]];
    [gradient fillBezierPath: bp angle: 270.0];
    
    [icon unlockFocus];
    
    [dict setObject: icon forKey: @"Icon"];
    [icon release];
    
    return icon;
}

- (BOOL) torrent: (Torrent *) torrent doesMatchRulesForGroupAtIndex: (NSInteger) index
{
    if (![self usesAutoAssignRulesForIndex: index])
        return NO;
    
    NSArray * rules = [self autoAssignRulesForIndex: index];
    if (!rules || [rules count] == 0)
        return NO;
    
    #warning should rules be dict instead of array?
    NSEnumerator * iterator = [rules objectEnumerator];
    NSArray * rule = nil;
    while ((rule = [iterator nextObject]))
    {
        NSString * type = [rule objectAtIndex: 0], * place = [rule objectAtIndex: 1], * match = [rule objectAtIndex: 2],
                * value = nil;
        if ([type isEqualToString: @"title"])
            value = [torrent name];
        else if ([type isEqualToString: @"tracker"])
        {
            #warning consider all trackers
            value = [torrent trackerAddressAnnounce];
        }
        else
            continue;
        
        NSStringCompareOptions options = NSCaseInsensitiveSearch;
        if ([place isEqualToString: @"ends"])
            options += NSBackwardsSearch;
        
        NSRange result = [value rangeOfString: match options: options];
        if ([place isEqualToString: @"begins"])
        {
            if (result.location != 0)
                return NO;
        }
        else if ([place isEqualToString: @"contains"])
        {
            if (result.location == NSNotFound)
                return NO;
        }
        else if ([place isEqualToString: @"ends"])
        {
            if (NSMaxRange(result) == [value length])
                return NO;
        }
        else
            continue;
    }
    return YES;
}

@end
