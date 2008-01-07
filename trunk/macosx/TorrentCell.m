/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2008 Transmission authors and contributors
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

#import "TorrentCell.h"
#import "TorrentTableView.h"
#import "GroupsWindowController.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "NSBezierPathAdditions.h"
#import "CTGradientAdditions.h"

#define BAR_HEIGHT 12.0

#define IMAGE_SIZE_REG 32.0
#define IMAGE_SIZE_MIN 16.0

//end up being larger than font height
#define HEIGHT_TITLE 16.0
#define HEIGHT_STATUS 12.0

#define PADDING_HORIZONAL 2.0
#define PADDING_ABOVE_IMAGE_REG 9.0
#define PADDING_BETWEEN_IMAGE_AND_TITLE 5.0
#define PADDING_BETWEEN_IMAGE_AND_BAR 7.0
#define PADDING_ABOVE_TITLE 3.0
#define PADDING_ABOVE_MIN_STATUS 4.0
#define PADDING_BETWEEN_TITLE_AND_MIN_STATUS 2.0
#define PADDING_BETWEEN_TITLE_AND_PROGRESS 1.0
#define PADDING_BETWEEN_PROGRESS_AND_BAR 2.0
#define PADDING_BETWEEN_TITLE_AND_BAR_MIN 3.0
#define PADDING_BETWEEN_BAR_AND_STATUS 2.0

#define WIDTH_GROUP 40.0
#define WIDTH_GROUP_MIN 24.0

#define MAX_PIECES 324

@interface TorrentCell (Private)

- (void) drawBar: (NSRect) barRect;
- (void) drawRegularBar: (NSRect) barRect;
- (void) drawPiecesBar: (NSRect) barRect;

- (NSRect) rectForMinimalStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds;
- (NSRect) rectForTitleWithString: (NSAttributedString *) string basedOnMinimalStatusRect: (NSRect) statusRect
            inBounds: (NSRect) bounds;
- (NSRect) rectForProgressWithString: (NSAttributedString *) string inBounds: (NSRect) bounds;
- (NSRect) rectForStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds;

- (NSAttributedString *) attributedTitleWithColor: (NSColor *) color;
- (NSAttributedString *) attributedStatusString: (NSString *) string withColor: (NSColor *) color;

@end

@implementation TorrentCell

//only called once, so don't worry about releasing
- (id) init
{
    if ((self = [super init]))
	{
        fDefaults = [NSUserDefaults standardUserDefaults];
        
        NSMutableParagraphStyle * paragraphStyle = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
        [paragraphStyle setLineBreakMode: NSLineBreakByTruncatingTail];
    
        fTitleAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                                [NSFont messageFontOfSize: 12.0], NSFontAttributeName,
                                paragraphStyle, NSParagraphStyleAttributeName, nil];
        fStatusAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                                [NSFont messageFontOfSize: 9.0], NSFontAttributeName,
                                paragraphStyle, NSParagraphStyleAttributeName, nil];
        [paragraphStyle release];
        
        //store box colors
        fGrayColor = [[NSColor colorWithCalibratedRed: 0.9 green: 0.9 blue: 0.9 alpha: 1.0] retain];
        fBlue1Color = [[NSColor colorWithCalibratedRed: 0.8 green: 1.0 blue: 1.0 alpha: 1.0] retain];
        fBlue2Color = [[NSColor colorWithCalibratedRed: 0.6 green: 1.0 blue: 1.0 alpha: 1.0] retain];
        fBlue3Color = [[NSColor colorWithCalibratedRed: 0.6 green: 0.8 blue: 1.0 alpha: 1.0] retain];
        fBlue4Color = [[NSColor colorWithCalibratedRed: 0.4 green: 0.6 blue: 1.0 alpha: 1.0] retain];
        fBlueColor = [[NSColor colorWithCalibratedRed: 0.0 green: 0.4 blue: 0.8 alpha: 1.0] retain];
        fOrangeColor = [[NSColor orangeColor] retain];
        
        fBarOverlayColor = [[NSColor colorWithDeviceWhite: 0.0 alpha: 0.2] retain];
    }
	return self;
}

- (id) copyWithZone: (NSZone *) zone
{
    TorrentCell * copy = [super copyWithZone: zone];
    
    copy->fTitleAttributes = [fTitleAttributes retain];
    copy->fStatusAttributes = [fStatusAttributes retain];
    
    copy->fBitmap = nil;
    
    copy->fGrayColor = [fGrayColor retain];
    copy->fBlue1Color = [fBlue1Color retain];
    copy->fBlue2Color = [fBlue2Color retain];
    copy->fBlue3Color = [fBlue3Color retain];
    copy->fBlue4Color = [fBlue4Color retain];
    copy->fBlueColor = [fBlueColor retain];
    copy->fOrangeColor = [fOrangeColor retain];
    
    copy->fBarOverlayColor = [fBarOverlayColor retain];
    
    copy->fGrayGradient = [fGrayGradient retain];
    copy->fLightGrayGradient = [fLightGrayGradient retain];
    copy->fBlueGradient = [fBlueGradient retain];
    copy->fDarkBlueGradient = [fDarkBlueGradient retain];
    copy->fGreenGradient = [fGreenGradient retain];
    copy->fLightGreenGradient = [fLightGreenGradient retain];
    copy->fDarkGreenGradient = [fDarkGreenGradient retain];
    copy->fYellowGradient = [fYellowGradient retain];
    copy->fRedGradient = [fRedGradient retain];
    copy->fTransparentGradient = [fTransparentGradient retain];
    
    return copy;
}

- (void) dealloc
{
    [fTitleAttributes release];
    [fStatusAttributes release];
    
    [fBitmap release];
    
    [fGrayColor release];
    [fBlue1Color release];
    [fBlue2Color release];
    [fBlue3Color release];
    [fBlue4Color release];
    [fBlueColor release];
    [fOrangeColor release];
    
    [fBarOverlayColor release];
    
    [fGrayGradient release];
    [fLightGrayGradient release];
    [fBlueGradient release];
    [fDarkBlueGradient release];
    [fGreenGradient release];
    [fLightGreenGradient release];
    [fDarkGreenGradient release];
    [fYellowGradient release];
    [fRedGradient release];
    [fTransparentGradient release];
    
    [super dealloc];
}

- (NSRect) iconRectForBounds: (NSRect) bounds
{
    NSRect result = bounds;
    
    result.origin.x += PADDING_HORIZONAL;
    
    float imageSize;
    if ([fDefaults boolForKey: @"SmallView"])
    {
        imageSize = IMAGE_SIZE_MIN;
        result.origin.y += (result.size.height - imageSize) * 0.5;
    }
    else
    {
        imageSize = IMAGE_SIZE_REG;
        result.origin.y += PADDING_ABOVE_IMAGE_REG;
    }
    
    result.size = NSMakeSize(imageSize, imageSize);
    
    return result;
}

- (NSRect) titleRectForBounds: (NSRect) bounds
{
    return [self rectForTitleWithString: [self attributedTitleWithColor: nil]
            basedOnMinimalStatusRect: [self minimalStatusRectForBounds: bounds] inBounds: bounds];
}

- (NSRect) minimalStatusRectForBounds: (NSRect) bounds
{
    Torrent * torrent = [self representedObject];
    NSString * string = [fDefaults boolForKey: @"DisplaySmallStatusRegular"]
                            ? [torrent shortStatusString] : [torrent remainingTimeString];
    return [self rectForMinimalStatusWithString: [self attributedStatusString: string withColor: nil] inBounds: bounds];
}

- (NSRect) progressRectForBounds: (NSRect) bounds
{
    return [self rectForProgressWithString: [self attributedStatusString: [[self representedObject] progressString] withColor: nil]
                    inBounds: bounds];
}

- (NSRect) barRectForBounds: (NSRect) bounds
{
    BOOL minimal = [fDefaults boolForKey: @"SmallView"];
    
    NSRect result = bounds;
    result.size.height = BAR_HEIGHT;
    result.origin.x = PADDING_HORIZONAL + (minimal ? IMAGE_SIZE_MIN : IMAGE_SIZE_REG) + PADDING_BETWEEN_IMAGE_AND_BAR;
    
    result.origin.y += PADDING_ABOVE_TITLE + HEIGHT_TITLE;
    if (minimal)
        result.origin.y += PADDING_BETWEEN_TITLE_AND_BAR_MIN;
    else
        result.origin.y += PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS + PADDING_BETWEEN_PROGRESS_AND_BAR;
    
    result.size.width = round(NSMaxX(bounds) - result.origin.x - PADDING_HORIZONAL - BUTTONS_TOTAL_WIDTH);
    
    return result;
}

- (NSRect) statusRectForBounds: (NSRect) bounds
{
    return [self rectForStatusWithString: [self attributedStatusString: [[self representedObject] statusString] withColor: nil]
                    inBounds: bounds];
}

- (NSUInteger) hitTestForEvent: (NSEvent *) event inRect: (NSRect) cellFrame ofView: (NSView *) controlView
{
    return NSCellHitContentArea;
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    [super drawWithFrame: cellFrame inView: controlView];
    
    Torrent * torrent = [self representedObject];
    
    BOOL minimal = [fDefaults boolForKey: @"SmallView"];
    
    //group coloring
    NSRect iconRect = [self iconRectForBounds: cellFrame];
    
    int groupValue = [torrent groupValue];
    if (groupValue != -1)
    {
        NSRect groupRect = NSInsetRect(iconRect, -2.0, -3.0);
        if (!minimal)
        {
            groupRect.size.height--;
            groupRect.origin.y--;
        }
        
        [[[GroupsWindowController groups] gradientForIndex: groupValue] fillBezierPath:
            [NSBezierPath bezierPathWithRoundedRect: groupRect radius: 6.0] angle: 90.0];
    }
    
    //error image
    BOOL error = [torrent isError];
    if (error && !fErrorImage)
    {
        fErrorImage = [NSImage imageNamed: @"Error.png"];
        [fErrorImage setFlipped: YES];
    }
    
    //icon
    NSImage * icon = minimal && error ? fErrorImage : [torrent icon];
    [icon drawInRect: iconRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
    
    if (error && !minimal)
    {
        NSRect errorRect = NSMakeRect(NSMaxX(iconRect) - IMAGE_SIZE_MIN, NSMaxY(iconRect) - IMAGE_SIZE_MIN,
                                        IMAGE_SIZE_MIN, IMAGE_SIZE_MIN);
        [fErrorImage drawInRect: errorRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
    }
    
    //text color
    NSColor * titleColor, * statusColor;
    if ([NSApp isOnLeopardOrBetter] ? [self backgroundStyle] == NSBackgroundStyleDark : [self isHighlighted]
        && [[self highlightColorWithFrame: cellFrame inView: controlView] isEqual: [NSColor alternateSelectedControlColor]])
    {
        titleColor = [NSColor whiteColor];
        statusColor = [NSColor whiteColor];
    }
    else
    {
        titleColor = [NSColor controlTextColor];
        statusColor = [NSColor darkGrayColor];
    }
    
    //minimal status
    NSRect minimalStatusRect;
    if (minimal)
    {
        NSString * string = [fDefaults boolForKey: @"DisplaySmallStatusRegular"]
                            ? [torrent shortStatusString] : [torrent remainingTimeString];
        NSAttributedString * minimalString = [self attributedStatusString: string withColor: statusColor];
        minimalStatusRect = [self rectForMinimalStatusWithString: minimalString inBounds: cellFrame];
        
        [minimalString drawInRect: minimalStatusRect];
    }
    
    //title
    NSAttributedString * titleString = [self attributedTitleWithColor: titleColor];
    NSRect titleRect = [self rectForTitleWithString: titleString basedOnMinimalStatusRect: minimalStatusRect inBounds: cellFrame];
    [titleString drawInRect: titleRect];
    
    //progress
    if (!minimal)
    {
        NSAttributedString * progressString = [self attributedStatusString: [torrent progressString] withColor: statusColor];
        NSRect progressRect = [self rectForProgressWithString: progressString inBounds: cellFrame];
        [progressString drawInRect: progressRect];
    }
    
    //bar
    [self drawBar: [self barRectForBounds: cellFrame]];
    
    //status
    if (!minimal)
    {
        NSAttributedString * statusString = [self attributedStatusString: [torrent statusString] withColor: statusColor];
        [statusString drawInRect: [self rectForStatusWithString: statusString inBounds: cellFrame]];
    }
}

@end

@implementation TorrentCell (Private)

- (void) drawBar: (NSRect) barRect
{
    if ([fDefaults boolForKey: @"PiecesBar"])
    {
        NSRect regularBarRect = barRect, piecesBarRect = barRect;
        regularBarRect.size.height /= 2.5;
        piecesBarRect.origin.y += regularBarRect.size.height;
        piecesBarRect.size.height -= regularBarRect.size.height;
        
        [self drawRegularBar: regularBarRect];
        [self drawPiecesBar: piecesBarRect];
    }
    else
    {
        [fBitmap release];
        fBitmap = nil;
        [[self representedObject] setPreviousAmountFinished: NULL];
        
        [self drawRegularBar: barRect];
    }
    
    [fBarOverlayColor set];
    [NSBezierPath strokeRect: NSInsetRect(barRect, 0.5, 0.5)];
}

- (void) drawRegularBar: (NSRect) barRect
{
    Torrent * torrent = [self representedObject];
    
    int leftWidth = barRect.size.width;
    float progress = [torrent progress];
    
    if (progress < 1.0)
    {
        float rightProgress = 1.0 - progress, progressLeft = [torrent progressLeft];
        int rightWidth = leftWidth * rightProgress;
        leftWidth -= rightWidth;
        
        if (progressLeft < rightProgress)
        {
            int rightNoIncludeWidth = rightWidth * ((rightProgress - progressLeft) / rightProgress);
            rightWidth -= rightNoIncludeWidth;
            
            NSRect noIncludeRect = barRect;
            noIncludeRect.origin.x += barRect.size.width - rightNoIncludeWidth;
            noIncludeRect.size.width = rightNoIncludeWidth;
            
            if (!fLightGrayGradient)
                fLightGrayGradient = [[CTGradient progressLightGrayGradient] retain];
            [fLightGrayGradient fillRect: noIncludeRect angle: -90];
        }
        
        if (rightWidth > 0)
        {
            if ([torrent isActive] && ![torrent allDownloaded] && ![torrent isChecking]
                && [fDefaults boolForKey: @"DisplayProgressBarAvailable"])
            {
                int notAvailableWidth = MIN(ceil(barRect.size.width * [torrent notAvailableDesired]), rightWidth);
                rightWidth -= notAvailableWidth;
                
                if (notAvailableWidth > 0)
                {
                    NSRect notAvailableRect = barRect;
                    notAvailableRect.origin.x += leftWidth + rightWidth;
                    notAvailableRect.size.width = notAvailableWidth;
                    
                    if (!fRedGradient)
                        fRedGradient = [[CTGradient progressRedGradient] retain];
                    [fRedGradient fillRect: notAvailableRect angle: -90];
                }
            }
            
            if (rightWidth > 0)
            {
                NSRect includeRect = barRect;
                includeRect.origin.x += leftWidth;
                includeRect.size.width = rightWidth;
                
                if (!fWhiteGradient)
                    fWhiteGradient = [[CTGradient progressWhiteGradient] retain];
                [fWhiteGradient fillRect: includeRect angle: -90];
            }
        }
    }
    
    if (leftWidth > 0)
    {
        NSRect completeRect = barRect;
        completeRect.size.width = leftWidth;
        
        if ([torrent isActive])
        {
            if ([torrent isChecking])
            {
                if (!fYellowGradient)
                    fYellowGradient = [[CTGradient progressYellowGradient] retain];
                [fYellowGradient fillRect: completeRect angle: -90];
            }
            else if ([torrent isSeeding])
            {
                int ratioLeftWidth = leftWidth * (1.0 - [torrent progressStopRatio]);
                leftWidth -= ratioLeftWidth;
                
                if (ratioLeftWidth > 0)
                {
                    NSRect ratioLeftRect = barRect;
                    ratioLeftRect.origin.x += leftWidth;
                    ratioLeftRect.size.width = ratioLeftWidth;
                    
                    if (!fLightGreenGradient)
                        fLightGreenGradient = [[CTGradient progressLightGreenGradient] retain];
                    [fLightGreenGradient fillRect: ratioLeftRect angle: -90];
                }
                
                if (leftWidth > 0)
                {
                    completeRect.size.width = leftWidth;
                    
                    if (!fGreenGradient)
                        fGreenGradient = [[CTGradient progressGreenGradient] retain];
                    [fGreenGradient fillRect: completeRect angle: -90];
                }
            }
            else
            {
                if (!fBlueGradient)
                    fBlueGradient = [[CTGradient progressBlueGradient] retain];
                [fBlueGradient fillRect: completeRect angle: -90];
            }
        }
        else
        {
            if ([torrent waitingToStart])
            {
                if ([torrent progressLeft] <= 0.0)
                {
                    if (!fDarkGreenGradient)
                        fDarkGreenGradient = [[CTGradient progressDarkGreenGradient] retain];
                    [fDarkGreenGradient fillRect: completeRect angle: -90];
                }
                else
                {
                    if (!fDarkBlueGradient)
                        fDarkBlueGradient = [[CTGradient progressDarkBlueGradient] retain];
                    [fDarkBlueGradient fillRect: completeRect angle: -90];
                }
            }
            else
            {
                if (!fGrayGradient)
                    fGrayGradient = [[CTGradient progressGrayGradient] retain];
                [fGrayGradient fillRect: completeRect angle: -90];
            }
        }
    }
}

- (void) drawPiecesBar: (NSRect) barRect
{
    if (!fBitmap)
        fBitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes: nil
            pixelsWide: MAX_PIECES pixelsHigh: barRect.size.height bitsPerSample: 8 samplesPerPixel: 4 hasAlpha: YES
            isPlanar: NO colorSpaceName: NSCalibratedRGBColorSpace bytesPerRow: 0 bitsPerPixel: 0];
    
    Torrent * torrent = [self representedObject];
    
    int pieceCount = MIN([torrent pieceCount], MAX_PIECES);
    float * piecePercent = malloc(pieceCount * sizeof(float)),
        * previousPiecePercent = [torrent getPreviousAmountFinished];
    [torrent getAmountFinished: piecePercent size: pieceCount];
    
    int i, h, index;
    float increment = (float)pieceCount / MAX_PIECES;
    NSColor * pieceColor;
    for (i = 0; i < MAX_PIECES; i++)
    {
        index = i * increment;
        if (piecePercent[index] >= 1.0)
        {
            if (previousPiecePercent != NULL && previousPiecePercent[index] < 1.0)
                pieceColor = fOrangeColor;
            else
                pieceColor = fBlueColor;
        }
        else if (piecePercent[index] <= 0.0)
            pieceColor = fGrayColor;
        else if (piecePercent[index] <= 0.25)
            pieceColor = fBlue1Color;
        else if (piecePercent[index] <= 0.5)
            pieceColor = fBlue2Color;
        else if (piecePercent[index] <= 0.75)
            pieceColor = fBlue3Color;
        else
            pieceColor = fBlue4Color;
        
        if (![pieceColor isEqual: [fBitmap colorAtX: i y: 0]])
            for (h = 0; h < barRect.size.height; h++)
                [fBitmap setColor: pieceColor atX: i y: h];
    }
    
    [torrent setPreviousAmountFinished: piecePercent];
    
    //actually draw image
    [fBitmap drawInRect: barRect];
    
    if (!fTransparentGradient)
        fTransparentGradient = [[CTGradient progressTransparentGradient] retain];
    [fTransparentGradient fillRect: barRect angle: -90];
}

- (NSRect) rectForMinimalStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    if (![fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    NSRect result = bounds;
    result.size = [string size];
    
    result.origin.x += bounds.size.width - result.size.width - PADDING_HORIZONAL;
    result.origin.y += PADDING_ABOVE_MIN_STATUS;
    
    return result;
}

- (NSRect) rectForTitleWithString: (NSAttributedString *) string basedOnMinimalStatusRect: (NSRect) statusRect
            inBounds: (NSRect) bounds
{
    BOOL minimal = [fDefaults boolForKey: @"SmallView"];
    
    NSRect result = bounds;
    result.origin.y += PADDING_ABOVE_TITLE;
    result.origin.x += PADDING_HORIZONAL + (minimal ? IMAGE_SIZE_MIN : IMAGE_SIZE_REG) + PADDING_BETWEEN_IMAGE_AND_TITLE;
    
    result.size = [string size];
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONAL
                            - (minimal ? PADDING_BETWEEN_TITLE_AND_MIN_STATUS + statusRect.size.width : 0));
    
    return result;
}

- (NSRect) rectForProgressWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    if ([fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    NSRect result = bounds;
    result.origin.y += PADDING_ABOVE_TITLE + HEIGHT_TITLE + PADDING_BETWEEN_TITLE_AND_PROGRESS;
    result.origin.x += PADDING_HORIZONAL + IMAGE_SIZE_REG + PADDING_BETWEEN_IMAGE_AND_TITLE;
    
    result.size = [string size];
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONAL);
    
    return result;
}

- (NSRect) rectForStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    if ([fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    NSRect result = bounds;
    result.origin.y += PADDING_ABOVE_TITLE + HEIGHT_TITLE + PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS
                        + PADDING_BETWEEN_PROGRESS_AND_BAR + BAR_HEIGHT + PADDING_BETWEEN_BAR_AND_STATUS;
    result.origin.x += PADDING_HORIZONAL + IMAGE_SIZE_REG + PADDING_BETWEEN_IMAGE_AND_TITLE;
    
    result.size = [string size];
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONAL);
    
    return result;
}

- (NSAttributedString *) attributedTitleWithColor: (NSColor *) color
{
    if (color)
        [fTitleAttributes setObject: color forKey: NSForegroundColorAttributeName];
    
    NSString * title = [[self representedObject] name];
    return [[[NSAttributedString alloc] initWithString: title attributes: fTitleAttributes] autorelease];
}

- (NSAttributedString *) attributedStatusString: (NSString *) string withColor: (NSColor *) color
{
    if (color)
        [fStatusAttributes setObject: color forKey: NSForegroundColorAttributeName];
    
    return [[[NSAttributedString alloc] initWithString: string attributes: fStatusAttributes] autorelease];
}

@end
