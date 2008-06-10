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

#import "PiecesView.h"
#import "InfoWindowController.h"
#import "CTGradient.h"

#define MAX_ACROSS 18
#define BETWEEN 1.0

#define HIGH_PEERS 15

#define FINISHED 1

@implementation PiecesView

- (void) awakeFromNib
{
        //back image
        fBack = [[NSImage alloc] initWithSize: [self bounds].size];
        
        [fBack lockFocus];
        CTGradient * gradient = [CTGradient gradientWithBeginningColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.4]
                                    endingColor: [NSColor colorWithCalibratedWhite: 0.2 alpha: 0.4]];
        [gradient fillRect: [self bounds] angle: 90.0];
        [fBack unlockFocus];
        
        //store box colors
        fGreenAvailabilityColor = [[NSColor colorWithCalibratedRed: 0.0 green: 1.0 blue: 0.4 alpha: 1.0] retain];
        fBluePieceColor = [[NSColor colorWithCalibratedRed: 0.0 green: 0.4 blue: 0.8 alpha: 1.0] retain];
                
        //actually draw the box
        [self setTorrent: nil];
}

- (void) dealloc
{
    tr_free(fPieces);
    
    [fBack release];
    
    [fGreenAvailabilityColor release];
    [fBluePieceColor release];
    
    [super dealloc];
}

- (void) setTorrent: (Torrent *) torrent
{
    [self clearView];
    
    fTorrent = torrent;
    if (fTorrent)
    {
        //determine relevant values
        fNumPieces = MIN([fTorrent pieceCount], MAX_ACROSS * MAX_ACROSS);
        fAcross = ceil(sqrt(fNumPieces));
        
        float width = [self bounds].size.width;
        fWidth = (width - (fAcross + 1) * BETWEEN) / fAcross;
        fExtraBorder = (width - ((fWidth + BETWEEN) * fAcross + BETWEEN)) / 2;
    }
    
    //reset the view to blank
    NSImage * newBack = [fBack copy];
    [self setImage: newBack];
    [newBack release];
    
    [self setNeedsDisplay];
}

- (void) clearView
{
    tr_free(fPieces);
    fPieces = NULL;
}

- (void) updateView
{
    if (!fTorrent)
        return;
    
    //determine if first time
    BOOL first = NO;
    if (!fPieces)
    {
        fPieces = (int8_t *)tr_malloc(fNumPieces * sizeof(int8_t));
        first = YES;
    }

    int8_t * pieces = NULL;
    float * piecesPercent = NULL;
    
    BOOL showAvailablity = [[NSUserDefaults standardUserDefaults] boolForKey: @"PiecesViewShowAvailability"];
    if (showAvailablity)
    {   
        pieces = (int8_t *)tr_malloc(fNumPieces * sizeof(int8_t));
        [fTorrent getAvailability: pieces size: fNumPieces];
    }
    else
    {   
        piecesPercent = (float *)tr_malloc(fNumPieces * sizeof(float));
        [fTorrent getAmountFinished: piecesPercent size: fNumPieces];
    }
    
    int i, j, index = -1;
    NSRect rect = NSMakeRect(0, 0, fWidth, fWidth);
    
    NSImage * image = [self image];
    [image lockFocus];
    
    for (i = 0; i < fAcross; i++)
        for (j = 0; j < fAcross; j++)
        {
            index++;
            if (index >= fNumPieces)
            {
                i = fAcross;
                break;
            }
            
            NSColor * pieceColor;
            if (showAvailablity)
            {
                if (pieces[index] == -1)
                {
                    pieceColor = !first && fPieces[index] != FINISHED ? [NSColor orangeColor] : fBluePieceColor;
                    fPieces[index] = FINISHED;
                }
                else
                {
                    float percent = MIN(1.0, (float)pieces[index]/HIGH_PEERS);
                    pieceColor = [[NSColor whiteColor] blendedColorWithFraction: percent ofColor: fGreenAvailabilityColor];
                    fPieces[index] = 0;
                }
            }
            else
            {
                if (piecesPercent[index] == 1.0)
                {
                    pieceColor = !first && fPieces[index] != FINISHED ? [NSColor orangeColor] : fBluePieceColor;
                    fPieces[index] = FINISHED;
                }
                else
                {
                    pieceColor = [[NSColor whiteColor] blendedColorWithFraction: piecesPercent[index] ofColor: fBluePieceColor];
                    fPieces[index] = 0;
                }
            }
            
            rect.origin = NSMakePoint(j * (fWidth + BETWEEN) + BETWEEN + fExtraBorder,
                                    [image size].width - (i + 1) * (fWidth + BETWEEN) - fExtraBorder);
            
            [pieceColor set];
            NSRectFill(rect);
        }
    
    [image unlockFocus];
    [self setNeedsDisplay];
    
    tr_free(pieces);
    tr_free(piecesPercent);
}

- (BOOL) acceptsFirstMouse: (NSEvent *) event
{
    return YES;
}

- (void) mouseDown: (NSEvent *) event
{
    if (fTorrent)
        [[[self window] windowController] setPiecesViewForAvailable:
            ![[NSUserDefaults standardUserDefaults] boolForKey: @"PiecesViewShowAvailability"]];
    [super mouseDown: event];
}

@end
