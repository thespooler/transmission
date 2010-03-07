/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2010 Transmission authors and contributors
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

#import "InfoActivityViewController.h"
#import "NSStringAdditions.h"
#import "PiecesView.h"
#import "Torrent.h"
#include "utils.h" //tr_getRatio()

#define PIECES_CONTROL_PROGRESS 0
#define PIECES_CONTROL_AVAILABLE 1

@implementation InfoActivityViewController

- (id) init
{
    self = [super initWithNibName: @"InfoActivityView" bundle: nil];
    return self;
}

- (void) dealloc
{
    [fTorrents release];
    
    [super dealloc];
}

- (void) setInfoForTorrents: (NSArray *) torrents
{
    if (fTorrents && [fTorrents isEqualToArray: torrents])
        return;
    
    [fTorrents release];
    fTorrents = [torrents retain];
    
    const NSUInteger count = [fTorrents count];
    if (count != 1)
    {
        if (count == 0)
        {
            [fHaveField setStringValue: @""];
            [fDownloadedTotalField setStringValue: @""];
            [fUploadedTotalField setStringValue: @""];
            [fFailedHashField setStringValue: @""];
            [fDateActivityField setStringValue: @""];
            [fRatioField setStringValue: @""];
        }
    
        [fStateField setStringValue: @""];
        [fProgressField setStringValue: @""];
        
        [fErrorMessageView setString: @""];
        
        [fDateAddedField setStringValue: @""];
        [fDateCompletedField setStringValue: @""];
        
        [fPiecesControl setSelected: NO forSegment: PIECES_CONTROL_AVAILABLE];
        [fPiecesControl setSelected: NO forSegment: PIECES_CONTROL_PROGRESS];
        [fPiecesControl setEnabled: NO];
        [fPiecesView setTorrent: nil];
    }
    else
    {
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        [fDateAddedField setObjectValue: [torrent dateAdded]];
    }
}

- (void) updateInfo
{
    const NSInteger numberSelected = [fTorrents count];
    if (numberSelected == 0)
        return;
    
    uint64_t have = 0, haveVerified = 0, downloadedTotal = 0, uploadedTotal = 0, failedHash = 0;
    NSDate * lastActivity = nil;
    for (Torrent * torrent in fTorrents)
    {
        have += [torrent haveTotal];
        haveVerified += [torrent haveVerified];
        downloadedTotal += [torrent downloadedTotal];
        uploadedTotal += [torrent uploadedTotal];
        failedHash += [torrent failedHash];
        
        NSDate * nextLastActivity;
        if ((nextLastActivity = [torrent dateActivity]))
            lastActivity = lastActivity ? [lastActivity laterDate: nextLastActivity] : nextLastActivity;
    }
    
    if (have == 0)
        [fHaveField setStringValue: [NSString stringForFileSize: 0]];
    else
    {
        NSString * verifiedString = [NSString stringWithFormat: NSLocalizedString(@"%@ verified", "Inspector -> Activity tab -> have"),
                                        [NSString stringForFileSize: haveVerified]];
        if (have == haveVerified)
            [fHaveField setStringValue: verifiedString];
        else
            [fHaveField setStringValue: [NSString stringWithFormat: @"%@ (%@)", [NSString stringForFileSize: have], verifiedString]];
    }
    
    [fDownloadedTotalField setStringValue: [NSString stringForFileSize: downloadedTotal]];
    [fUploadedTotalField setStringValue: [NSString stringForFileSize: uploadedTotal]];
    [fFailedHashField setStringValue: [NSString stringForFileSize: failedHash]];
    
    [fDateActivityField setObjectValue: lastActivity];
    
    if (numberSelected == 1)
    {
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        
        [fStateField setStringValue: [torrent stateString]];
        
        if ([torrent isFolder])
            [fProgressField setStringValue: [NSString localizedStringWithFormat: NSLocalizedString(@"%.2f%% (%.2f%% selected)",
                "Inspector -> Activity tab -> progress"), 100.0 * [torrent progress], 100.0 * [torrent progressDone]]];
        else
            [fProgressField setStringValue: [NSString localizedStringWithFormat: @"%.2f%%", 100.0 * [torrent progress]]];
            
        [fRatioField setStringValue: [NSString stringForRatio: [torrent ratio]]];
        
        NSString * errorMessage = [torrent errorMessage];
        if (![errorMessage isEqualToString: [fErrorMessageView string]])
            [fErrorMessageView setString: errorMessage];
        
        [fDateCompletedField setObjectValue: [torrent dateCompleted]];
        
        [fPiecesView updateView];
    }
    else if (numberSelected > 1)
    {
        [fRatioField setStringValue: [NSString stringForRatio: tr_getRatio(uploadedTotal, downloadedTotal)]];
    }
    else;
}

- (void) setPiecesView: (id) sender
{
    [self setPiecesViewForAvailable: [sender selectedSegment] == PIECES_CONTROL_AVAILABLE];
}

- (void) setPiecesViewForAvailable: (BOOL) available
{
    [fPiecesControl setSelected: available forSegment: PIECES_CONTROL_AVAILABLE];
    [fPiecesControl setSelected: !available forSegment: PIECES_CONTROL_PROGRESS];
    
    [[NSUserDefaults standardUserDefaults] setBool: available forKey: @"PiecesViewShowAvailability"];
    [fPiecesView updateView];
}

- (void) clearPiecesView
{
    [fPiecesView clearView];
}

@end
