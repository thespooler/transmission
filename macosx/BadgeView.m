/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007-2009 Transmission authors and contributors
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

#import "BadgeView.h"
#import "NSStringAdditions.h"

#define BETWEEN_PADDING 2.0f

@interface BadgeView (Private)

- (void) badge: (NSImage *) badge string: (NSString *) string atHeight: (CGFloat) height adjustForQuit: (BOOL) quit;

@end

@implementation BadgeView

- (id) initWithFrame: (NSRect) frame lib: (tr_session *) lib
{
    if ((self = [super initWithFrame: frame]))
    {
        fLib = lib;
        
        fDownloadRate = 0.0;
        fUploadRate = 0.0;
        fQuitting = NO;
    }
    return self;
}

- (void) dealloc
{
    [fAttributes release];
    [super dealloc];
}

- (BOOL) setRatesWithDownload: (CGFloat) downloadRate upload: (CGFloat) uploadRate
{
    //only needs update if the badges were displayed or are displayed now
    if (fDownloadRate == downloadRate && fUploadRate == uploadRate)
        return NO;
    
    fDownloadRate = downloadRate;
    fUploadRate = uploadRate;
    return YES;
}

- (void) setQuitting
{
    fQuitting = YES;
}

- (void) drawRect: (NSRect) rect
{
    [[NSApp applicationIconImage] drawInRect: rect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
    
    if (fQuitting)
    {
        NSImage * quitBadge = [NSImage imageNamed: @"QuitBadge.png"];
        [self badge: quitBadge string: NSLocalizedString(@"Quitting", "Dock Badger -> quit")
                atHeight: (rect.size.height - [quitBadge size].height) * 0.5f adjustForQuit: YES];
        return;
    }
    
    const BOOL upload = fUploadRate >= 0.1f,
            download = fDownloadRate >= 0.1f;
    CGFloat bottom = 0.0f;
    if (upload)
    {
        NSImage * uploadBadge = [NSImage imageNamed: @"UploadBadge.png"];
        [self badge: uploadBadge string: [NSString stringForSpeedAbbrev: fUploadRate] atHeight: bottom adjustForQuit: NO];
        if (download)
            bottom += [uploadBadge size].height + BETWEEN_PADDING; //download rate above upload rate
    }
    if (download)
        [self badge: [NSImage imageNamed: @"DownloadBadge.png"] string: [NSString stringForSpeedAbbrev: fDownloadRate]
                atHeight: bottom adjustForQuit: NO];
}

@end

@implementation BadgeView (Private)

//dock icon must have locked focus
- (void) badge: (NSImage *) badge string: (NSString *) string atHeight: (CGFloat) height adjustForQuit: (BOOL) quit
{
    if (!fAttributes)
    {
        NSShadow * stringShadow = [[NSShadow alloc] init];
        [stringShadow setShadowOffset: NSMakeSize(2.0f, -2.0f)];
        [stringShadow setShadowBlurRadius: 4.0f];
        
        fAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
            [NSColor whiteColor], NSForegroundColorAttributeName,
            [NSFont boldSystemFontOfSize: 26.0f], NSFontAttributeName, stringShadow, NSShadowAttributeName, nil];
        
        [stringShadow release];
    }
    
    NSRect badgeRect = NSZeroRect;
    badgeRect.size = [badge size];
    badgeRect.origin.y = height;
    
    [badge drawInRect: badgeRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0f];
    
    //string is in center of image
    NSSize stringSize = [string sizeWithAttributes: fAttributes];
    
    NSRect stringRect = badgeRect;
    stringRect.origin.x += (badgeRect.size.width - stringSize.width) * 0.5f;
    stringRect.origin.y += (badgeRect.size.height - stringSize.height) * 0.5f + (quit ? 2.0f : 1.0f); //adjust for shadow, extra for quit
    stringRect.size = stringSize;
    
    [string drawInRect: stringRect withAttributes: fAttributes];
}

@end
