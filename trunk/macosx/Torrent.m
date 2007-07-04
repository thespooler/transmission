/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2007 Transmission authors and contributors
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

#import "Torrent.h"
#import "StringAdditions.h"

#define BAR_HEIGHT 12.0

#define MAX_PIECES 324
#define BLANK_PIECE -99

static int static_lastid = 0;

@interface Torrent (Private)

- (id) initWithHash: (NSString *) hashString path: (NSString *) path lib: (tr_handle_t *) lib
        publicTorrent: (NSNumber *) publicTorrent
        downloadFolder: (NSString *) downloadFolder
        useIncompleteFolder: (NSNumber *) useIncompleteFolder incompleteFolder: (NSString *) incompleteFolder
        dateAdded: (NSDate *) dateAdded dateCompleted: (NSDate *) dateCompleted
        dateActivity: (NSDate *) dateActivity
        ratioSetting: (NSNumber *) ratioSetting ratioLimit: (NSNumber *) ratioLimit
        limitSpeedCustom: (NSNumber *) limitCustom
        checkUpload: (NSNumber *) checkUpload uploadLimit: (NSNumber *) uploadLimit
        checkDownload: (NSNumber *) checkDownload downloadLimit: (NSNumber *) downloadLimit
		pex: (NSNumber *) pex
        waitToStart: (NSNumber *) waitToStart orderValue: (NSNumber *) orderValue
        filesShouldDownload: (NSArray *) filesShouldDownload filePriorities: (NSArray *) filePriorities;
- (void) historyFilePriorities: (NSMutableArray *) history forItems: (NSArray *) items;

- (BOOL) shouldUseIncompleteFolderForName: (NSString *) name;
- (void) updateDownloadFolder;

- (void) createFileListShouldDownload: (NSArray *) filesShouldDownload priorities: (NSArray *) filePriorities;
- (void) insertPath: (NSMutableArray *) components forSiblings: (NSMutableArray *) siblings
            withParent: (NSMutableDictionary *) parent previousPath: (NSString *) previousPath
            flatList: (NSMutableArray *) flatList fileSize: (uint64_t) size index: (int) index priority: (int) priority;
- (NSImage *) advancedBar;
- (void) trashFile: (NSString *) path;

@end

@implementation Torrent

// Used to optimize drawing. They contain packed RGBA pixels for every color needed.
#define BE OSSwapBigToHostConstInt32

static uint32_t kRed   = BE(0xFF6450FF), //255, 100, 80
                kBlue = BE(0x50A0FFFF), //80, 160, 255
                kBlue2 = BE(0x1E46B4FF), //30, 70, 180
                kGray  = BE(0x969696FF), //150, 150, 150
                kGreen1 = BE(0x99FFCCFF), //153, 255, 204
                kGreen2 = BE(0x66FF99FF), //102, 255, 153
                kGreen3 = BE(0x00FF66FF), //0, 255, 102
                kWhite = BE(0xFFFFFFFF); //255, 255, 255

- (id) initWithPath: (NSString *) path location: (NSString *) location forceDeleteTorrent: (BOOL) delete lib: (tr_handle_t *) lib
{
    self = [self initWithHash: nil path: path lib: lib
            publicTorrent: delete ? [NSNumber numberWithBool: NO] : nil
            downloadFolder: location
            useIncompleteFolder: nil incompleteFolder: nil
            dateAdded: nil dateCompleted: nil
            dateActivity: nil
            ratioSetting: nil ratioLimit: nil
            limitSpeedCustom: nil
            checkUpload: nil uploadLimit: nil
            checkDownload: nil downloadLimit: nil
			pex: nil
            waitToStart: nil orderValue: nil
            filesShouldDownload: nil filePriorities: nil];
    
    if (self)
    {
        if (!fPublicTorrent)
            [self trashFile: path];
    }
    return self;
}

- (id) initWithHistory: (NSDictionary *) history lib: (tr_handle_t *) lib
{
    self = [self initWithHash: [history objectForKey: @"TorrentHash"]
                path: [history objectForKey: @"TorrentPath"] lib: lib
                publicTorrent: [history objectForKey: @"PublicCopy"]
                downloadFolder: [history objectForKey: @"DownloadFolder"]
                useIncompleteFolder: [history objectForKey: @"UseIncompleteFolder"]
                incompleteFolder: [history objectForKey: @"IncompleteFolder"]
                dateAdded: [history objectForKey: @"Date"]
				dateCompleted: [history objectForKey: @"DateCompleted"]
                dateActivity: [history objectForKey: @"DateActivity"]
                ratioSetting: [history objectForKey: @"RatioSetting"]
                ratioLimit: [history objectForKey: @"RatioLimit"]
                limitSpeedCustom: [history objectForKey: @"LimitSpeedCustom"]
                checkUpload: [history objectForKey: @"CheckUpload"]
                uploadLimit: [history objectForKey: @"UploadLimit"]
                checkDownload: [history objectForKey: @"CheckDownload"]
                downloadLimit: [history objectForKey: @"DownloadLimit"]
				pex: [history objectForKey: @"Pex"]
                waitToStart: [history objectForKey: @"WaitToStart"]
                orderValue: [history objectForKey: @"OrderValue"]
                filesShouldDownload: [history objectForKey: @"FilesShouldDownload"]
                filePriorities: [history objectForKey: @"FilePriorities"]];
    
    if (self)
    {
        //start transfer
        NSNumber * active;
        if ((active = [history objectForKey: @"Active"]) && [active boolValue])
        {
            fStat = tr_torrentStat(fHandle);
            [self startTransfer];
        }
    }
    return self;
}

- (NSDictionary *) history
{
    NSMutableDictionary * history = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                    [NSNumber numberWithBool: fPublicTorrent], @"PublicCopy",
                    [self hashString], @"TorrentHash",
                    fDownloadFolder, @"DownloadFolder",
                    [NSNumber numberWithBool: fUseIncompleteFolder], @"UseIncompleteFolder",
                    [NSNumber numberWithBool: [self isActive]], @"Active",
                    fDateAdded, @"Date",
                    [NSNumber numberWithInt: fRatioSetting], @"RatioSetting",
                    [NSNumber numberWithFloat: fRatioLimit], @"RatioLimit",
                    [NSNumber numberWithInt: fCheckUpload], @"CheckUpload",
                    [NSNumber numberWithInt: fUploadLimit], @"UploadLimit",
                    [NSNumber numberWithInt: fCheckDownload], @"CheckDownload",
                    [NSNumber numberWithInt: fDownloadLimit], @"DownloadLimit",
                    [NSNumber numberWithBool: fWaitToStart], @"WaitToStart",
                    [self orderValue], @"OrderValue",
                    nil];
    
    if (fIncompleteFolder)
        [history setObject: fIncompleteFolder forKey: @"IncompleteFolder"];
    
    //set file should download
    int fileCount = [self fileCount];
    NSMutableArray * filesShouldDownload = [NSMutableArray arrayWithCapacity: fileCount];
    
    tr_priority_t * priorities = tr_torrentGetFilePriorities(fHandle);
    int i;
    for (i = 0; i < fileCount; i++)
        [filesShouldDownload addObject: [NSNumber numberWithBool: priorities[i] != TR_PRI_DND]];
    free(priorities);
    [history setObject: filesShouldDownload forKey: @"FilesShouldDownload"];
    
    //set file priorities
    NSMutableArray * filePriorities = [NSMutableArray arrayWithCapacity: fileCount];
    [self historyFilePriorities: filePriorities forItems: fFileList];
    [history setObject: filePriorities forKey: @"FilePriorities"];

    if (fPublicTorrent)
        [history setObject: [self publicTorrentLocation] forKey: @"TorrentPath"];
	
	if (![self privateTorrent])
		[history setObject: [NSNumber numberWithBool: fPex] forKey: @"Pex"];
	
	if (fDateCompleted)
		[history setObject: fDateCompleted forKey: @"DateCompleted"];
    
    NSDate * dateActivity = [self dateActivity];
    if (dateActivity)
		[history setObject: dateActivity forKey: @"DateActivity"];
	
    return history;
}

- (void) dealloc
{
    [fDownloadFolder release];
    [fIncompleteFolder release];
    
    [fPublicTorrentLocation release];
    
    [fDateAdded release];
    [fDateCompleted release];
    [fDateActivity release];
    
    [fAnnounceDate release];
    
    [fIcon release];
    [fIconFlipped release];
    [fIconSmall release];
    
    [fProgressString release];
    [fStatusString release];
    [fShortStatusString release];
    [fRemainingTimeString release];
    
    [fFileList release];
    [fFlatFileList release];
    
    [fBitmap release];
    
    if (fPieces)
        free(fPieces);
    
    [super dealloc];
}

- (void) closeTorrent
{
    tr_torrentClose(fHandle);
}

- (void) closeRemoveTorrent
{
    tr_torrentRemoveSaved(fHandle);
    [self closeTorrent];
}

- (void) changeIncompleteDownloadFolder: (NSString *) folder
{
    fUseIncompleteFolder = folder != nil;
    if (fIncompleteFolder)
    {
        [fIncompleteFolder release];
        fIncompleteFolder = nil;
    }
    
    if (folder)
        fIncompleteFolder = [folder retain];
    
    [self updateDownloadFolder];
}

- (void) changeDownloadFolder: (NSString *) folder
{
    if (fDownloadFolder)
        [fDownloadFolder release];
    fDownloadFolder = [folder retain];
    
    [self updateDownloadFolder];
}

- (NSString *) downloadFolder
{
    return [NSString stringWithUTF8String: tr_torrentGetFolder(fHandle)];
}

- (void) getAvailability: (int8_t *) tab size: (int) size
{
    tr_torrentAvailability(fHandle, tab, size);
}

- (void) getAmountFinished: (float *) tab size: (int) size
{
    tr_torrentAmountFinished(fHandle, tab, size);
}

- (void) update
{
    fStat = tr_torrentStat(fHandle);
    
    //notification when downloading finished
    if (tr_getComplete(fHandle) || tr_getDone(fHandle))
    {
        BOOL canMove = YES;
        
        //move file from incomplete folder to download folder
        if (fUseIncompleteFolder && ![[self downloadFolder] isEqualToString: fDownloadFolder]
            && (canMove = [self alertForMoveFolderAvailable]))
        {
            //pause without actually stopping
            tr_setUseCustomUpload(fHandle, 1);
            tr_setUploadLimit(fHandle, 0);
            
            tr_setUseCustomDownload(fHandle, 1);
            tr_setDownloadLimit(fHandle, 0);
            
            if ([[NSFileManager defaultManager] movePath: [[self downloadFolder] stringByAppendingPathComponent: [self name]]
                                    toPath: [fDownloadFolder stringByAppendingPathComponent: [self name]] handler: nil])
                [self updateDownloadFolder];
            
            [self updateSpeedSetting];
        }
        
        if (!canMove)
        {
            fUseIncompleteFolder = NO;
            
            [fDownloadFolder release];
            fDownloadFolder = fIncompleteFolder;
            fIncompleteFolder = nil;
        }
		
		if (fDateCompleted)
			[fDateCompleted release];
		fDateCompleted = [[NSDate alloc] init];
        
        fStat = tr_torrentStat(fHandle);
        [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentFinishedDownloading" object: self];
    }
    else if (tr_getIncomplete(fHandle))
        [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentRestartedDownloading" object: self];
    else;
    
    //check to stop for ratio
    float stopRatio;
    if ([self isSeeding] && (stopRatio = [self actualStopRatio]) != INVALID && [self ratio] >= stopRatio)
    {
        [self stopTransfer];
        fStat = tr_torrentStat(fHandle);
        
        fFinishedSeeding = YES;
        
        [self setRatioSetting: NSOffState];
        [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentStoppedForRatio" object: self];
    }
	
	NSMutableString * progressString = [NSMutableString stringWithString: @""],
					* remainingTimeString = [NSMutableString stringWithString: @""],
					* statusString = [NSMutableString string],
					* shortStatusString = [NSMutableString string];

    if (![self allDownloaded])
        [progressString appendFormat: NSLocalizedString(@"%@ of %@ (%.2f%%)", "Torrent -> progress string"),
                            [NSString stringForFileSize: [self downloadedValid]],
                            [NSString stringForFileSize: [self size]], 100.0 * [self progress]];
    else if ([self progress] < 1.0)
        [progressString appendFormat: NSLocalizedString(@"%@ of %@ (%.2f%%), uploaded %@ (Ratio: %@)",
                "Torrent -> progress string"),
                [NSString stringForFileSize: [self downloadedValid]], [NSString stringForFileSize: [self size]],
                100.0 * [self progress], [NSString stringForFileSize: [self uploadedTotal]],
                [NSString stringForRatio: [self ratio]]];
    else
        [progressString appendFormat: NSLocalizedString(@"%@, uploaded %@ (Ratio: %@)", "Torrent -> progress string"),
                [NSString stringForFileSize: [self size]], [NSString stringForFileSize: [self uploadedTotal]],
                [NSString stringForRatio: [self ratio]]];

    BOOL wasChecking = fChecking;
    fChecking = NO;
    switch (fStat->status)
    {
        NSString * tempString;
        
        case TR_STATUS_STOPPED:
            if (fWaitToStart)
            {
                tempString = ![self allDownloaded]
                        ? [NSLocalizedString(@"Waiting to download", "Torrent -> status string") stringByAppendingEllipsis]
                        : [NSLocalizedString(@"Waiting to seed", "Torrent -> status string") stringByAppendingEllipsis];
            }
            else if (fFinishedSeeding)
                tempString = NSLocalizedString(@"Seeding complete", "Torrent -> status string");
            else
                tempString = NSLocalizedString(@"Paused", "Torrent -> status string");
            
            [statusString setString: tempString];
            [shortStatusString setString: tempString];
            
            break;

        case TR_STATUS_CHECK_WAIT:
            tempString = [NSLocalizedString(@"Waiting to check existing data", "Torrent -> status string")
                            stringByAppendingEllipsis];
            
            [statusString setString: tempString];
            [shortStatusString setString: tempString];
            [remainingTimeString setString: tempString];
            
            fChecking = YES;
            
            break;

        case TR_STATUS_CHECK:
            tempString = [NSLocalizedString(@"Checking existing data", "Torrent -> status string") stringByAppendingEllipsis];
            
            [statusString setString: tempString];
            [shortStatusString setString: tempString];
            [remainingTimeString setString: tempString];
            
            fChecking = YES;
            
            break;

        case TR_STATUS_DOWNLOAD:
            [statusString setString: @""];
            if ([self totalPeers] != 1)
                [statusString appendFormat: NSLocalizedString(@"Downloading from %d of %d peers",
                                                "Torrent -> status string"), [self peersUploading], [self totalPeers]];
            else
                [statusString appendFormat: NSLocalizedString(@"Downloading from %d of 1 peer",
                                                "Torrent -> status string"), [self peersUploading]];
            
            int eta = [self eta];
            if (eta < 0)
            {
                [remainingTimeString setString: NSLocalizedString(@"Unknown", "Torrent -> remaining time")];
                [progressString appendString: NSLocalizedString(@" - remaining time unknown", "Torrent -> progress string")];
            }
            else
            {
                if (eta < 60)
                    [remainingTimeString appendFormat: NSLocalizedString(@"%d sec", "Torrent -> remaining time"), eta];
                else if (eta < 3600) //60 * 60
                    [remainingTimeString appendFormat: NSLocalizedString(@"%d min %d sec", "Torrent -> remaining time"),
                                                            eta / 60, eta % 60];
                else if (eta < 86400) //24 * 60 * 60
                    [remainingTimeString appendFormat: NSLocalizedString(@"%d hr %d min", "Torrent -> remaining time"),
                                                            eta / 3600, (eta / 60) % 60];
                else
                {
					int days = eta / 86400;
                    if (days > 1)
                        [remainingTimeString appendFormat: NSLocalizedString(@"%d days %d hr", "Torrent -> remaining time"),
                                                                days, (eta / 3600) % 24];
                    else
                        [remainingTimeString appendFormat: NSLocalizedString(@"1 day %d hr", "Torrent -> remaining time"),
                                                                (eta / 3600) % 24];
                }
                
                [progressString appendFormat: NSLocalizedString(@" - %@ remaining", "Torrent -> progress string"),
                                                                    remainingTimeString];
            }
            
            break;

        case TR_STATUS_SEED:
        case TR_STATUS_DONE:
            [statusString setString: @""];
            if ([self totalPeers] != 1)
                [statusString appendFormat: NSLocalizedString(@"Seeding to %d of %d peers", "Torrent -> status string"),
                                                [self peersDownloading], [self totalPeers]];
            else
                [statusString appendFormat: NSLocalizedString(@"Seeding to %d of 1 peer", "Torrent -> status string"),
                                                [self peersDownloading]];
            
            break;

        case TR_STATUS_STOPPING:
            tempString = [NSLocalizedString(@"Stopping", "Torrent -> status string") stringByAppendingEllipsis];
        
            [statusString setString: tempString];
            [shortStatusString setString: tempString];
            
            break;
    }
    
    //check for error
    BOOL wasError = fError;
    fError = [self isError];
    
    //check if stalled
    BOOL wasStalled = fStalled;
    fStalled = [self isActive] && [fDefaults boolForKey: @"CheckStalled"]
                && [fDefaults integerForKey: @"StalledMinutes"] < [self stalledMinutes];
    
    //create strings for error or stalled
    if (fError)
        [statusString setString: [NSLocalizedString(@"Error: ", "Torrent -> status string")
                                    stringByAppendingString: [self errorMessage]]];
    else if (fStalled)
        [statusString setString: [NSLocalizedString(@"Stalled, ", "Torrent -> status string")
                                    stringByAppendingString: statusString]];
    else;
    
    //update queue for checking (from downloading to seeding), stalled, or error
    if ((wasChecking && !fChecking) || (!wasStalled && fStalled) || (!wasError && fError && [self isActive]))
        [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateQueue" object: self];

    if ([self isActive] && ![self isChecking])
    {
        NSString * stringToAppend = @"";
        if (![self allDownloaded])
        {
            stringToAppend = [NSString stringWithFormat: NSLocalizedString(@"DL: %@, ", "Torrent -> status string"),
                                [NSString stringForSpeed: [self downloadRate]]];
            [shortStatusString setString: @""];
        }
        else
        {
            NSString * ratioString = [NSString stringForRatio: [self ratio]];
        
            [shortStatusString setString: [NSString stringWithFormat: NSLocalizedString(@"Ratio: %@, ",
                                            "Torrent -> status string"), ratioString]];
            [remainingTimeString setString: [NSLocalizedString(@"Ratio: ", "Torrent -> status string")
                                                stringByAppendingString: ratioString]];
        }
        
        stringToAppend = [stringToAppend stringByAppendingString: [NSLocalizedString(@"UL: ", "Torrent -> status string")
                                            stringByAppendingString: [NSString stringForSpeed: [self uploadRate]]]];

        [statusString appendFormat: @" - %@", stringToAppend];
        [shortStatusString appendString: stringToAppend];
    }
	
	[fProgressString setString: progressString];
	[fStatusString setString: statusString];
	[fShortStatusString setString: shortStatusString];
	[fRemainingTimeString setString: remainingTimeString];
}

- (NSDictionary *) infoForCurrentView
{
    NSMutableDictionary * info = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                    [self name], @"Name",
                                    [NSNumber numberWithFloat: [self progress]], @"Progress",
                                    [NSNumber numberWithFloat: (float)fStat->left/[self size]], @"Left",
                                    [NSNumber numberWithBool: [self isActive]], @"Active",
                                    [NSNumber numberWithBool: [self isSeeding]], @"Seeding",
                                    [NSNumber numberWithBool: [self isChecking]], @"Checking",
                                    [NSNumber numberWithBool: fWaitToStart], @"Waiting",
                                    [NSNumber numberWithBool: [self isError]], @"Error", nil];
    
    if ([self isSeeding])
        [info setObject: [NSNumber numberWithFloat: [self progressStopRatio]] forKey: @"ProgressStopRatio"];
    
    if (![fDefaults boolForKey: @"SmallView"])
    {
        [info setObject: [self iconFlipped] forKey: @"Icon"];
        [info setObject: [self progressString] forKey: @"ProgressString"];
        [info setObject: [self statusString] forKey: @"StatusString"];
    }
    else
    {
        [info setObject: [self iconSmall] forKey: @"Icon"];
        [info setObject: [self remainingTimeString] forKey: @"RemainingTimeString"];
        [info setObject: [self shortStatusString] forKey: @"ShortStatusString"];
    }
    
    if ([fDefaults boolForKey: @"UseAdvancedBar"])
        [info setObject: [self advancedBar] forKey: @"AdvancedBar"];
    
    return info;
}

- (void) startTransfer
{
    fWaitToStart = NO;
    fFinishedSeeding = NO;
    
    if (![self isActive] && [self alertForFolderAvailable] && [self alertForRemainingDiskSpace])
    {
        tr_torrentStart(fHandle);
        [self update];
    }
}

- (void) stopTransfer
{
    fError = NO;
    fWaitToStart = NO;
    
    if ([self isActive])
    {
        tr_torrentStop(fHandle);
        [self update];

        [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateQueue" object: self];
    }
}

- (void) sleep
{
    if ((fResumeOnWake = [self isActive]))
        tr_torrentStop(fHandle);
}

- (void) wakeUp
{
    if (fResumeOnWake)
        tr_torrentStart(fHandle);
}

- (void) announce
{
    if (![self isActive])
        return;
    
    tr_manualUpdate(fHandle);
    
    if (fAnnounceDate)
        [fAnnounceDate release];
    fAnnounceDate = [[NSDate date] retain];
}

- (NSDate *) announceDate
{
    return fAnnounceDate;
}

- (void) resetCache
{
    tr_torrentRecheck(fHandle);
    [self update];
}

- (float) ratio
{
    return fStat->ratio;
}

- (int) ratioSetting
{
    return fRatioSetting;
}

- (void) setRatioSetting: (int) setting
{
    fRatioSetting = setting;
}

- (float) ratioLimit
{
    return fRatioLimit;
}

- (void) setRatioLimit: (float) limit
{
    if (limit >= 0)
        fRatioLimit = limit;
}

- (float) actualStopRatio
{
    if (fRatioSetting == NSOnState)
        return fRatioLimit;
    else if (fRatioSetting == NSMixedState && [fDefaults boolForKey: @"RatioCheck"])
        return [fDefaults floatForKey: @"RatioLimit"];
    else
        return INVALID;
}

- (float) progressStopRatio
{
    float stopRatio, ratio;
    if ((stopRatio = [self actualStopRatio]) == INVALID || (ratio = [self ratio]) >= stopRatio)
        return 1.0;
    else if (ratio > 0 && stopRatio > 0)
        return ratio / stopRatio;
    else
        return 0;
}

- (int) checkUpload
{
    return fCheckUpload;
}

- (void) setCheckUpload: (int) setting
{
    fCheckUpload = setting;
    [self updateSpeedSetting];
}

- (int) uploadLimit
{
    return fUploadLimit;
}

- (void) setUploadLimit: (int) limit
{
    fUploadLimit = limit;
    [self updateSpeedSetting];
}

- (int) checkDownload
{
    return fCheckDownload;
}

- (void) setCheckDownload: (int) setting
{
    fCheckDownload = setting;
    [self updateSpeedSetting];
}

- (int) downloadLimit
{
    return fDownloadLimit;
}

- (void) setDownloadLimit: (int) limit
{
    fDownloadLimit = limit;
    [self updateSpeedSetting];
}

- (void) updateSpeedSetting
{
    tr_setUseCustomUpload(fHandle, fCheckUpload != NSMixedState);
    tr_setUploadLimit(fHandle, fCheckUpload == NSOnState ? fUploadLimit : -1);
    
    tr_setUseCustomDownload(fHandle, fCheckDownload != NSMixedState);
    tr_setDownloadLimit(fHandle, fCheckDownload == NSOnState ? fDownloadLimit : -1);
}

- (void) setWaitToStart: (BOOL) wait
{
    fWaitToStart = wait;
}

- (BOOL) waitingToStart
{
    return fWaitToStart;
}

- (void) revealData
{
    [[NSWorkspace sharedWorkspace] selectFile: [self dataLocation] inFileViewerRootedAtPath: nil];
}

- (void) revealPublicTorrent
{
    if (fPublicTorrent)
        [[NSWorkspace sharedWorkspace] selectFile: fPublicTorrentLocation inFileViewerRootedAtPath: nil];
}

- (void) trashData
{
    [self trashFile: [self dataLocation]];
}

- (void) trashTorrent
{
    if (fPublicTorrent)
        [self trashFile: [self publicTorrentLocation]];
}

- (void) moveTorrentDataFileTo: (NSString *) folder
{
    NSString * oldFolder = [self downloadFolder];
    if (![oldFolder isEqualToString: folder] || ![fDownloadFolder isEqualToString: folder])
    {
        //check if moving inside itself
        NSArray * oldComponents = [oldFolder pathComponents],
                * newComponents = [folder pathComponents];
        int count;
        
        if ((count = [oldComponents count]) < [newComponents count]
                && [[newComponents objectAtIndex: count] isEqualToString: [self name]]
                && [oldComponents isEqualToArray:
                        [newComponents objectsAtIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, count)]]])
        {
            NSAlert * alert = [[NSAlert alloc] init];
            [alert setMessageText: NSLocalizedString(@"A folder cannot be moved to inside itself.",
                                                        "Move inside itself alert -> title")];
            [alert setInformativeText: [NSString stringWithFormat:
                            NSLocalizedString(@"The move operation of \"%@\" cannot be done.",
                                                "Move inside itself alert -> message"), [self name]]];
            [alert addButtonWithTitle: NSLocalizedString(@"OK", "Move inside itself alert -> button")];
            
            [alert runModal];
            [alert release];
            
            return;
        }
        
        //pause without actually stopping
        tr_setDownloadLimit(fHandle, 0);
        tr_setUploadLimit(fHandle, 0);
        
        if ([[NSFileManager defaultManager] movePath: [oldFolder stringByAppendingPathComponent: [self name]]
                            toPath: [folder stringByAppendingPathComponent: [self name]] handler: nil])
        {
            //get rid of both incomplete folder and old download folder, even if move failed
            fUseIncompleteFolder = NO;
            if (fIncompleteFolder)
            {
                [fIncompleteFolder release];
                fIncompleteFolder = nil;
            }
            [self changeDownloadFolder: folder];
            
            [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateInfoSettings" object: nil];
            
            [self updateSpeedSetting];
        }
        else
        {
            [self updateSpeedSetting]; //restart before showing the alert
        
            NSAlert * alert = [[NSAlert alloc] init];
            [alert setMessageText: NSLocalizedString(@"There was an error moving the data file.", "Move error alert -> title")];
            [alert setInformativeText: [NSString stringWithFormat:
                            NSLocalizedString(@"The move operation of \"%@\" cannot be done.",
                                                "Move error alert -> message"), [self name]]];
            [alert addButtonWithTitle: NSLocalizedString(@"OK", "Move error alert -> button")];
            
            [alert runModal];
            [alert release];
        }
    }
}

- (void) copyTorrentFileTo: (NSString *) path
{
    [[NSFileManager defaultManager] copyPath: [self torrentLocation] toPath: path handler: nil];
}

- (BOOL) alertForRemainingDiskSpace
{
    if ([self allDownloaded] || ![fDefaults boolForKey: @"WarningRemainingSpace"])
        return YES;
    
    NSString * volumeName;
    if ((volumeName = [[[NSFileManager defaultManager] componentsToDisplayForPath: [self downloadFolder]] objectAtIndex: 0]))
    {
        NSDictionary * fsAttributes = [[NSFileManager defaultManager] fileSystemAttributesAtPath: [self downloadFolder]];
        uint64_t remainingSpace = [[fsAttributes objectForKey: NSFileSystemFreeSize] unsignedLongLongValue];
        
        if (remainingSpace <= fStat->left)
        {
            NSAlert * alert = [[NSAlert alloc] init];
            [alert setMessageText: [NSString stringWithFormat:
                                    NSLocalizedString(@"Not enough remaining disk space to download \"%@\" completely.",
                                        "Torrent file disk space alert -> title"), [self name]]];
            [alert setInformativeText: [NSString stringWithFormat:
                            NSLocalizedString(@"The transfer will be paused. Clear up space on \"%@\" to continue.",
                                                "Torrent file disk space alert -> message"), volumeName]];
            [alert addButtonWithTitle: NSLocalizedString(@"OK", "Torrent file disk space alert -> button")];
            [alert addButtonWithTitle: NSLocalizedString(@"Download Anyway", "Torrent file disk space alert -> button")];
            [alert addButtonWithTitle: NSLocalizedString(@"Always Download", "Torrent file disk space alert -> button")];
            
            int result = [alert runModal];
            [alert release];
            
            if (result == NSAlertThirdButtonReturn)
                [fDefaults setBool: NO forKey: @"WarningRemainingSpace"];
            
            return result != NSAlertFirstButtonReturn;
        }
    }
    return YES;
}

- (BOOL) alertForFolderAvailable
{
    if (access(tr_torrentGetFolder(fHandle), 0))
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert setMessageText: [NSString stringWithFormat:
                                NSLocalizedString(@"The folder for downloading \"%@\" cannot be found.",
                                    "Folder cannot be found alert -> title"), [self name]]];
        [alert setInformativeText: [NSString stringWithFormat:
                        NSLocalizedString(@"\"%@\" cannot be found. The transfer will be paused.",
                                            "Folder cannot be found alert -> message"), [self downloadFolder]]];
        [alert addButtonWithTitle: NSLocalizedString(@"OK", "Folder cannot be found alert -> button")];
        [alert addButtonWithTitle: [NSLocalizedString(@"Choose New Location",
                                    "Folder cannot be found alert -> location button") stringByAppendingEllipsis]];
        
        if ([alert runModal] != NSAlertFirstButtonReturn)
        {
            NSOpenPanel * panel = [NSOpenPanel openPanel];
            
            [panel setPrompt: NSLocalizedString(@"Select", "Folder cannot be found alert -> prompt")];
            [panel setAllowsMultipleSelection: NO];
            [panel setCanChooseFiles: NO];
            [panel setCanChooseDirectories: YES];
            [panel setCanCreateDirectories: YES];

            [panel setMessage: [NSString stringWithFormat: NSLocalizedString(@"Select the download folder for \"%@\"",
                                "Folder cannot be found alert -> select destination folder"), [self name]]];
            
            [[NSNotificationCenter defaultCenter] postNotificationName: @"MakeWindowKey" object: nil];
            [panel beginSheetForDirectory: nil file: nil types: nil modalForWindow: [NSApp keyWindow] modalDelegate: self
                    didEndSelector: @selector(destinationChoiceClosed:returnCode:contextInfo:) contextInfo: nil];
        }
        
        [alert release];
        
        return NO;
    }
    
    return YES;
}

- (void) destinationChoiceClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) context
{
    if (code != NSOKButton)
        return;
    
    NSString * folder = [[openPanel filenames] objectAtIndex: 0];
    if (fUseIncompleteFolder)
        [self changeDownloadFolder: folder];
    else
        [self changeDownloadFolder: folder];
    
    [self startTransfer];
    [self update];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateInfoSettings" object: nil];
}

- (BOOL) alertForMoveFolderAvailable
{
    if (access([fDownloadFolder UTF8String], 0))
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert setMessageText: [NSString stringWithFormat:
                                NSLocalizedString(@"The folder for moving the completed \"%@\" cannot be found.",
                                    "Move folder cannot be found alert -> title"), [self name]]];
        [alert setInformativeText: [NSString stringWithFormat:
                                NSLocalizedString(@"\"%@\" cannot be found. The file will remain in its current location.",
                                    "Move folder cannot be found alert -> message"), fDownloadFolder]];
        [alert addButtonWithTitle: NSLocalizedString(@"OK", "Move folder cannot be found alert -> button")];
        
        [alert runModal];
        [alert release];
        
        return NO;
    }
    
    return YES;
}

- (NSImage *) icon
{
    return fIcon;
}

- (NSImage *) iconFlipped
{
    if (!fIconFlipped)
    {
        fIconFlipped = [fIcon copy];
        [fIconFlipped setFlipped: YES];
    }
    return fIconFlipped;
}

- (NSImage *) iconSmall
{
    if (!fIconSmall)
    {
        fIconSmall = [fIcon copy];
        [fIconSmall setFlipped: YES];
        [fIconSmall setScalesWhenResized: YES];
        [fIconSmall setSize: NSMakeSize(16.0, 16.0)];
    }
    return fIconSmall;
}

- (NSString *) name
{
    return [NSString stringWithUTF8String: fInfo->name];
}

- (uint64_t) size
{
    return fInfo->totalSize;
}

- (NSString *) trackerAddress
{
    return [NSString stringWithFormat: @"http://%s:%d", fStat->tracker->address, fStat->tracker->port];
}

- (NSString *) trackerAddressAnnounce
{
    return [NSString stringWithUTF8String: fStat->tracker->announce];
}

- (NSString *) comment
{
    return [NSString stringWithUTF8String: fInfo->comment];
}

- (NSString *) creator
{
    return [NSString stringWithUTF8String: fInfo->creator];
}

- (NSDate *) dateCreated
{
    int date = fInfo->dateCreated;
    return date > 0 ? [NSDate dateWithTimeIntervalSince1970: date] : nil;
}

- (int) pieceSize
{
    return fInfo->pieceSize;
}

- (int) pieceCount
{
    return fInfo->pieceCount;
}

- (NSString *) hashString
{
    return [NSString stringWithUTF8String: fInfo->hashString];
}

- (BOOL) privateTorrent
{
    return TR_FLAG_PRIVATE & fInfo->flags;
}

- (NSString *) torrentLocation
{
    return [NSString stringWithUTF8String: fInfo->torrent];
}

- (NSString *) publicTorrentLocation
{
    return fPublicTorrentLocation;
}

- (NSString *) dataLocation
{
    return [[self downloadFolder] stringByAppendingPathComponent: [self name]];
}

- (BOOL) publicTorrent
{
    return fPublicTorrent;
}

- (NSString *) stateString
{
    switch( fStat->status )
    {
        case TR_STATUS_STOPPED:
            return NSLocalizedString(@"Paused", "Torrent -> status string");
            break;

        case TR_STATUS_CHECK:
            return [NSLocalizedString(@"Checking existing data", "Torrent -> status string") stringByAppendingEllipsis];
            break;

        case TR_STATUS_DOWNLOAD:
            return NSLocalizedString(@"Downloading", "Torrent -> status string");
            break;

        case TR_STATUS_SEED:
        case TR_STATUS_DONE:
            return NSLocalizedString(@"Seeding", "Torrent -> status string");
            break;

        case TR_STATUS_STOPPING:
            return [NSLocalizedString(@"Stopping", "Torrent -> status string") stringByAppendingEllipsis];
            break;
        
        default:
            return NSLocalizedString(@"N/A", "Torrent -> status string");
    }
}

- (float) progress
{
    return fStat->percentComplete;
}

- (float) progressDone
{
    return fStat->percentDone;
}

- (int) eta
{
    return fStat->eta;
}

- (BOOL) isActive
{
    return fStat->status & TR_STATUS_ACTIVE;
}

- (BOOL) isSeeding
{
    return fStat->status == TR_STATUS_SEED || fStat->status == TR_STATUS_DONE;
}

- (BOOL) isPaused
{
    return fStat->status == TR_STATUS_STOPPED;
}

- (BOOL) isChecking
{
    return fStat->status == TR_STATUS_CHECK || fStat->status == TR_STATUS_CHECK_WAIT;
}

- (BOOL) allDownloaded
{
    return fStat->cpStatus != TR_CP_INCOMPLETE;
}

- (BOOL) isError
{
    return fStat->error != 0;
}

- (NSString *) errorMessage
{
    if (![self isError])
        return @"";
    
    NSString * error;
    if (!(error = [NSString stringWithUTF8String: fStat->errorString])
        && !(error = [NSString stringWithCString: fStat->errorString encoding: NSISOLatin1StringEncoding]))
        error = NSLocalizedString(@"(unreadable error)", "Torrent -> error string unreadable");
    
    return error;
}

- (NSArray *) peers
{
    int totalPeers, i;
    tr_peer_stat_t * peers = tr_torrentPeers(fHandle, & totalPeers);
    
    NSMutableArray * peerDics = [NSMutableArray arrayWithCapacity: totalPeers];
    NSMutableDictionary * dic;
    
    tr_peer_stat_t * peer;
    for (i = 0; i < totalPeers; i++)
    {
        peer = &peers[i];
        
        dic = [NSMutableDictionary dictionaryWithObjectsAndKeys:
            [NSNumber numberWithBool: peer->isConnected], @"Connected",
            [NSNumber numberWithInt: peer->from], @"From",
            [NSString stringWithCString: (char *) peer->addr encoding: NSUTF8StringEncoding], @"IP",
            [NSNumber numberWithInt: peer->port], @"Port", nil];
        
        if (peer->isConnected)
        {
            [dic setObject: [NSNumber numberWithFloat: peer->progress] forKey: @"Progress"];
            
            if (peer->isDownloading)
                [dic setObject: [NSNumber numberWithFloat: peer->uploadToRate] forKey: @"UL To Rate"];
            if (peer->isUploading)
                [dic setObject: [NSNumber numberWithFloat: peer->downloadFromRate] forKey: @"DL From Rate"];
            
            [dic setObject: [NSString stringWithCString: (char *) peer->client encoding: NSUTF8StringEncoding] forKey: @"Client"];
        }
        else
            [dic setObject: @"" forKey: @"Client"];
        
        [peerDics addObject: dic];
    }
    
    tr_torrentPeersFree(peers, totalPeers);
    
    return peerDics;
}

- (NSString *) progressString
{
    return fProgressString;
}

- (NSString *) statusString
{
    return fStatusString;
}

- (NSString *) shortStatusString
{
    return fShortStatusString;
}

- (NSString *) remainingTimeString
{
    return fRemainingTimeString;
}

- (int) seeders
{
    return fStat->seeders;
}

- (int) leechers
{
    return fStat->leechers;
}

- (int) completedFromTracker
{
    return fStat->completedFromTracker;
}

- (int) totalPeers
{
    return fStat->peersTotal;
}

- (int) totalPeersTracker
{
    return fStat->peersFrom[TR_PEER_FROM_TRACKER];
}

- (int) totalPeersIncoming
{
    return fStat->peersFrom[TR_PEER_FROM_INCOMING];
}

- (int) totalPeersCache
{
    return fStat->peersFrom[TR_PEER_FROM_CACHE];
}

- (int) totalPeersPex
{
    return fStat->peersFrom[TR_PEER_FROM_PEX];
}

//peers uploading to you
- (int) peersUploading
{
    return fStat->peersUploading;
}

//peers downloading from you
- (int) peersDownloading
{
    return fStat->peersDownloading;
}

- (float) downloadRate
{
    return fStat->rateDownload;
}

- (float) uploadRate
{
    return fStat->rateUpload;
}

- (uint64_t) downloadedValid
{
    return fStat->downloadedValid;
}

- (uint64_t) downloadedTotal
{
    return fStat->downloaded;
}

- (uint64_t) uploadedTotal
{
    return fStat->uploaded;
}

- (float) swarmSpeed
{
    return fStat->swarmspeed;
}

- (BOOL) pex
{
	return fPex;
}

- (void) setPex: (BOOL) setting
{
	if (![self privateTorrent])
	{
		fPex = setting;
		tr_torrentDisablePex(fHandle, !setting);
	}
}

- (NSNumber *) orderValue
{
    return [NSNumber numberWithInt: fOrderValue];
}

- (void) setOrderValue: (int) orderValue
{
    fOrderValue = orderValue;
}

- (NSArray *) fileList
{
    return fFileList;
}

- (int) fileCount
{
    return fInfo->fileCount;
}

- (float) fileProgress: (int) index
{
    return tr_torrentFileCompletion(fHandle, index);
}

- (int) checkForFiles: (NSIndexSet *) indexSet
{
    BOOL onState = NO, offState = NO;
    int index;
    for (index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
    {
        if (tr_torrentGetFilePriority(fHandle, index) != TR_PRI_DND
                || ![self canChangeDownloadCheckForFiles: [NSIndexSet indexSetWithIndex: index]])
            onState = YES;
        else
            offState = YES;
        
        if (onState == offState)
            return NSMixedState;
    }
    return onState ? NSOnState : NSOffState;
}

- (BOOL) canChangeDownloadCheckForFiles: (NSIndexSet *) indexSet
{
    if ([self fileCount] <= 1)
        return NO;
    
    int index;
    for (index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
        if ([self fileProgress: index] < 1.0)
            return YES;
    return NO;
}

- (void) setFileCheckState: (int) state forIndexes: (NSIndexSet *) indexSet
{
    int index;
    for (index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
    {
        tr_priority_t actualPriority;
        if (state == NSOnState)
        {
            int priority = [[[fFlatFileList objectAtIndex: index] objectForKey: @"Priority"] intValue];
            if (priority == PRIORITY_HIGH)
                actualPriority = TR_PRI_HIGH;
            else if (priority == PRIORITY_LOW)
                actualPriority = TR_PRI_LOW;
            else
                actualPriority = TR_PRI_NORMAL;
        }
        else
            actualPriority = TR_PRI_DND;
        
        tr_torrentSetFilePriority(fHandle, index, actualPriority);
    }
    
    [self update];
    if ([self isPaused])
        [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateQueue" object: self]; //for paused torrents
}

- (void) setFilePriority: (int) priority forIndexes: (NSIndexSet *) indexSet
{
    NSNumber * priorityValue = [NSNumber numberWithInt: priority];
    
    int index;
    for (index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
    {
        [[fFlatFileList objectAtIndex: index] setObject: priorityValue forKey: @"Priority"];
        
        if ([self checkForFiles: [NSIndexSet indexSetWithIndex: index]] == NSOnState)
        {
            tr_priority_t actualPriority;
            if (priority == PRIORITY_HIGH)
                actualPriority = TR_PRI_HIGH;
            else if (priority == PRIORITY_LOW)
                actualPriority = TR_PRI_LOW;
            else
                actualPriority = TR_PRI_NORMAL;
            tr_torrentSetFilePriority(fHandle, index, actualPriority);
        }
    }
}

- (BOOL) hasFilePriority: (int) priority forIndexes: (NSIndexSet *) indexSet
{
    int index;
    for (index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
        if (priority == [[[fFlatFileList objectAtIndex: index] objectForKey: @"Priority"] intValue]
                && [self canChangeDownloadCheckForFiles: [NSIndexSet indexSetWithIndex: index]])
            return YES;
    return NO;
}

- (NSDate *) dateAdded
{
    return fDateAdded;
}

- (NSDate *) dateCompleted
{
    return fDateCompleted;
}

- (NSDate *) dateActivity
{
    uint64_t date = fStat->activityDate / 1000;
    return date != 0 ? [NSDate dateWithTimeIntervalSince1970: date] : fDateActivity;
}

- (int) stalledMinutes
{
    uint64_t start;
    if ((start = fStat->startDate) == 0)
        return -1;
    
    NSDate * started = [NSDate dateWithTimeIntervalSince1970: start / 1000],
            * activity = [self dateActivity];
    if (!activity || [started compare: activity] == NSOrderedDescending)
        return -1 * [started timeIntervalSinceNow] / 60;
    else
        return -1 * [activity timeIntervalSinceNow] / 60;
}

- (BOOL) isStalled
{
    return fStalled;
}

- (NSNumber *) stateSortKey
{
    if (![self isActive])
        return [NSNumber numberWithInt: 0];
    else if ([self isSeeding])
        return [NSNumber numberWithInt: 1];
    else
        return [NSNumber numberWithInt: 2];
}

- (NSNumber *) progressSortKey
{
    float progress;
    if ((progress = [self progress]) >= 1.0)
       progress += [self progressStopRatio];
    
    return [NSNumber numberWithFloat: progress];
}

- (NSNumber *) ratioSortKey
{
    return [NSNumber numberWithFloat: [self ratio]];
}

- (int) torrentID
{
    return fID;
}

- (const tr_info_t *) torrentInfo
{
    return fInfo;
}

- (tr_stat_t *) torrentStat
{
    return fStat;
}

@end

@implementation Torrent (Private)

//if a hash is given, attempt to load that; otherwise, attempt to open file at path
- (id) initWithHash: (NSString *) hashString path: (NSString *) path lib: (tr_handle_t *) lib
        publicTorrent: (NSNumber *) publicTorrent
        downloadFolder: (NSString *) downloadFolder
        useIncompleteFolder: (NSNumber *) useIncompleteFolder incompleteFolder: (NSString *) incompleteFolder
        dateAdded: (NSDate *) dateAdded dateCompleted: (NSDate *) dateCompleted
        dateActivity: (NSDate *) dateActivity
        ratioSetting: (NSNumber *) ratioSetting ratioLimit: (NSNumber *) ratioLimit
        limitSpeedCustom: (NSNumber *) limitCustom
        checkUpload: (NSNumber *) checkUpload uploadLimit: (NSNumber *) uploadLimit
        checkDownload: (NSNumber *) checkDownload downloadLimit: (NSNumber *) downloadLimit
		pex: (NSNumber *) pex
        waitToStart: (NSNumber *) waitToStart orderValue: (NSNumber *) orderValue
        filesShouldDownload: (NSArray *) filesShouldDownload filePriorities: (NSArray *) filePriorities;
{
    if (!(self = [super init]))
        return nil;
    
    static_lastid++;
    fID = static_lastid;
    
    fLib = lib;
    fDefaults = [NSUserDefaults standardUserDefaults];

    fPublicTorrent = path && (publicTorrent ? [publicTorrent boolValue] : ![fDefaults boolForKey: @"DeleteOriginalTorrent"]);
    if (fPublicTorrent)
        fPublicTorrentLocation = [path retain];
    
    fDownloadFolder = downloadFolder ? downloadFolder : [fDefaults stringForKey: @"DownloadFolder"];
    fDownloadFolder = [[fDownloadFolder stringByExpandingTildeInPath] retain];
    
    fUseIncompleteFolder = useIncompleteFolder ? [useIncompleteFolder boolValue]
                                : [fDefaults boolForKey: @"UseIncompleteDownloadFolder"];
    if (fUseIncompleteFolder)
    {
        fIncompleteFolder = incompleteFolder ? incompleteFolder : [fDefaults stringForKey: @"IncompleteDownloadFolder"];
        fIncompleteFolder = [[fIncompleteFolder stringByExpandingTildeInPath] retain];
    }
    
    NSString * currentDownloadFolder;
    tr_info_t info;
    int error;
    if (hashString)
    {
        if (tr_torrentParseHash(fLib, [hashString UTF8String], NULL, &info) == TR_OK)
        {
            currentDownloadFolder = [self shouldUseIncompleteFolderForName: [NSString stringWithUTF8String: info.name]]
                                        ? fIncompleteFolder : fDownloadFolder;
            fHandle = tr_torrentInitSaved(fLib, [hashString UTF8String], [currentDownloadFolder UTF8String],
                                            TR_FLAG_SAVE | TR_FLAG_PAUSED, &error);
        }
        tr_metainfoFree(&info);
    }
    if (!fHandle && path)
    {
        if (tr_torrentParse(fLib, [path UTF8String], NULL, &info) == TR_OK)
        {
            currentDownloadFolder = [self shouldUseIncompleteFolderForName: [NSString stringWithUTF8String: info.name]]
                                        ? fIncompleteFolder : fDownloadFolder;
            fHandle = tr_torrentInit(fLib, [path UTF8String], [currentDownloadFolder UTF8String],
                                        TR_FLAG_SAVE | TR_FLAG_PAUSED, &error);
        }
        tr_metainfoFree(&info);
    }
    if (!fHandle)
    {
        [self release];
        return nil;
    }
    
    fInfo = tr_torrentInfo(fHandle);
    
    NSNotificationCenter * nc = [NSNotificationCenter defaultCenter];
    [nc addObserver: self selector: @selector(updateSpeedSetting:)
                name: @"UpdateSpeedSetting" object: nil];

    fDateAdded = dateAdded ? [dateAdded retain] : [[NSDate alloc] init];
	if (dateCompleted)
		fDateCompleted = [dateCompleted retain];
    if (dateActivity)
		fDateActivity = [dateActivity retain];
	
    fRatioSetting = ratioSetting ? [ratioSetting intValue] : NSMixedState;
    fRatioLimit = ratioLimit ? [ratioLimit floatValue] : [fDefaults floatForKey: @"RatioLimit"];
    fFinishedSeeding = NO;
    
    fCheckUpload = checkUpload ? [checkUpload intValue] : NSMixedState;
    fUploadLimit = uploadLimit ? [uploadLimit intValue] : [fDefaults integerForKey: @"UploadLimit"];
    fCheckDownload = checkDownload ? [checkDownload intValue] : NSMixedState;
    fDownloadLimit = downloadLimit ? [downloadLimit intValue] : [fDefaults integerForKey: @"DownloadLimit"];
    [self updateSpeedSetting];
	
	if ([self privateTorrent])
		fPex = NO;
	else
		fPex = pex ? [pex boolValue] : YES;
	tr_torrentDisablePex(fHandle, !fPex);
    
    fWaitToStart = waitToStart ? [waitToStart boolValue] : [fDefaults boolForKey: @"AutoStartDownload"];
    fOrderValue = orderValue ? [orderValue intValue] : tr_torrentCount(fLib) - 1;
    fError = NO;
    
    fIcon = [[[NSWorkspace sharedWorkspace] iconForFileType: fInfo->multifile ? NSFileTypeForHFSTypeCode('fldr')
                                                : [[self name] pathExtension]] retain];

    fProgressString = [[NSMutableString alloc] initWithCapacity: 50];
    fStatusString = [[NSMutableString alloc] initWithCapacity: 75];
    fShortStatusString = [[NSMutableString alloc] initWithCapacity: 30];
    fRemainingTimeString = [[NSMutableString alloc] initWithCapacity: 30];
    
    [self createFileListShouldDownload: filesShouldDownload priorities: filePriorities];
    
    //set up advanced bar
    fBitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes: nil
        pixelsWide: MAX_PIECES pixelsHigh: BAR_HEIGHT bitsPerSample: 8 samplesPerPixel: 4 hasAlpha: YES
        isPlanar: NO colorSpaceName: NSCalibratedRGBColorSpace bytesPerRow: 0 bitsPerPixel: 0];
    
    fPieces = malloc(MAX_PIECES);
    int i;
    for (i = 0; i < MAX_PIECES; i++)
        fPieces[i] = BLANK_PIECE;
    
    [self update];
    return self;
}

- (void) createFileListShouldDownload: (NSArray *) filesShouldDownload priorities: (NSArray *) filePriorities
{
    int count = [self fileCount], i;
    tr_file_t * file;
    NSMutableArray * pathComponents;
    NSString * path;
    int priority;
    tr_priority_t actualPriority;
    
    NSMutableArray * fileList = [[NSMutableArray alloc] init],
                    * flatFileList = [[NSMutableArray alloc] initWithCapacity: count];
    
    for (i = 0; i < count; i++)
    {
        file = &fInfo->files[i];
        
        pathComponents = [[[NSString stringWithUTF8String: file->name] pathComponents] mutableCopy];
        if (fInfo->multifile)
        {
            path = [pathComponents objectAtIndex: 0];
            [pathComponents removeObjectAtIndex: 0];
        }
        else
            path = @"";
        
        priority = filePriorities ? [[filePriorities objectAtIndex: i] intValue] : PRIORITY_NORMAL;
        [self insertPath: pathComponents forSiblings: fileList withParent: nil previousPath: path
                flatList: flatFileList fileSize: file->length index: i priority: priority];
        [pathComponents autorelease];
        
        if (!filesShouldDownload || [[filesShouldDownload objectAtIndex: i] boolValue])
        {
            if (priority == PRIORITY_HIGH)
                actualPriority = TR_PRI_HIGH;
            else if (priority == PRIORITY_LOW)
                actualPriority = TR_PRI_LOW;
            else
                actualPriority = TR_PRI_NORMAL;
        }
        else
            actualPriority = TR_PRI_DND;
        
        tr_torrentSetFilePriority(fHandle, i, actualPriority);
    }
    
    fFileList = [[NSArray alloc] initWithArray: fileList];
    [fileList release];
    fFlatFileList = [[NSArray alloc] initWithArray: flatFileList];
    [flatFileList release];
}

- (void) insertPath: (NSMutableArray *) components forSiblings: (NSMutableArray *) siblings
        withParent: (NSMutableDictionary *) parent previousPath: (NSString *) previousPath
        flatList: (NSMutableArray *) flatList fileSize: (uint64_t) size index: (int) index priority: (int) priority
{
    NSString * name = [components objectAtIndex: 0];
    BOOL isFolder = [components count] > 1;
    
    NSMutableDictionary * dict = nil;
    if (isFolder)
    {
        NSEnumerator * enumerator = [siblings objectEnumerator];
        while ((dict = [enumerator nextObject]))
            if ([[dict objectForKey: @"IsFolder"] boolValue] && [[dict objectForKey: @"Name"] isEqualToString: name])
                break;
    }
    
    NSString * currentPath = [previousPath stringByAppendingPathComponent: name];
    
    //create new folder or item if it doesn't already exist
    if (!dict)
    {
        dict = [NSMutableDictionary dictionaryWithObjectsAndKeys: name, @"Name",
                [NSNumber numberWithBool: isFolder], @"IsFolder", currentPath, @"Path", nil];
        [siblings addObject: dict];
        
        if (isFolder)
        {
            [dict setObject: [NSMutableArray array] forKey: @"Children"];
            [dict setObject: [NSMutableIndexSet indexSetWithIndex: index] forKey: @"Indexes"];
        }
        else
        {
            [dict setObject: [NSIndexSet indexSetWithIndex: index] forKey: @"Indexes"];
            [dict setObject: [NSNumber numberWithUnsignedLongLong: size] forKey: @"Size"];
            [dict setObject: [[NSWorkspace sharedWorkspace] iconForFileType: [name pathExtension]] forKey: @"Icon"];
            [dict setObject: [NSNumber numberWithInt: priority] forKey: @"Priority"];
            
            [flatList addObject: dict];
        }
        
        if (parent)
            [dict setObject: parent forKey: @"Parent"];
    }
    else
        [[dict objectForKey: @"Indexes"] addIndex: index];
    
    if (isFolder)
    {
        [components removeObjectAtIndex: 0];
        [self insertPath: components forSiblings: [dict objectForKey: @"Children"]
            withParent: dict previousPath: currentPath flatList: flatList fileSize: size index: index priority: priority];
    }
}

- (void) historyFilePriorities: (NSMutableArray *) history forItems: (NSArray *) items
{
    NSEnumerator * enumerator = [items objectEnumerator];
    NSDictionary * item;
    while ((item = [enumerator nextObject]))
    {
        if (![[item objectForKey: @"IsFolder"] boolValue])
            [history addObject: [item objectForKey: @"Priority"]];
        else
            [self historyFilePriorities: history forItems: [item objectForKey: @"Children"]];
    }
}

- (BOOL) shouldUseIncompleteFolderForName: (NSString *) name
{
    return fUseIncompleteFolder &&
        ![[NSFileManager defaultManager] fileExistsAtPath: [fDownloadFolder stringByAppendingPathComponent: name]];
}

- (void) updateDownloadFolder
{
    NSString * folder = [self shouldUseIncompleteFolderForName: [self name]] ? fIncompleteFolder : fDownloadFolder;
    tr_torrentSetFolder(fHandle, [folder UTF8String]);
}

#warning move?
- (NSImage *) advancedBar
{
    uint32_t * p;
    uint8_t * bitmapData = [fBitmap bitmapData];
    int bytesPerRow = [fBitmap bytesPerRow];
    
    int pieceCount = [self pieceCount];
    int8_t * piecesAvailablity = malloc(pieceCount);
    [self getAvailability: piecesAvailablity size: pieceCount];
    
    //lines 2 to 14: blue, green, or gray depending on piece availability
    int i, h, index = 0;
    float increment = (float)pieceCount / (float)MAX_PIECES, indexValue = 0;
    uint32_t color;
    BOOL change;
    for (i = 0; i < MAX_PIECES; i++)
    {
        change = NO;
        if (piecesAvailablity[index] < 0)
        {
            if (fPieces[i] != -1)
            {
                color = kBlue;
                fPieces[i] = -1;
                change = YES;
            }
        }
        else if (piecesAvailablity[index] == 0)
        {
            if (fPieces[i] != 0)
            {
                color = kGray;
                fPieces[i] = 0;
                change = YES;
            }
        }
        else if (piecesAvailablity[index] <= 4)
        {
            if (fPieces[i] != 1)
            {
                color = kGreen1;
                fPieces[i] = 1;
                change = YES;
            }
        }
        else if (piecesAvailablity[index] <= 8)
        {
            if (fPieces[i] != 2)
            {
                color = kGreen2;
                fPieces[i] = 2;
                change = YES;
            }
        }
        else
        {
            if (fPieces[i] != 3)
            {
                color = kGreen3;
                fPieces[i] = 3;
                change = YES;
            }
        }
        
        if (change)
        {
            //point to pixel (i, 2) and draw "vertically"
            p = (uint32_t *)(bitmapData + 2 * bytesPerRow) + i;
            for (h = 2; h < BAR_HEIGHT; h++)
            {
                p[0] = color;
                p = (uint32_t *)((uint8_t *)p + bytesPerRow);
            }
        }
        
        indexValue += increment;
        index = (int)indexValue;
    }
    
    //determine percentage finished and available
    int have = rintf((float)MAX_PIECES * [self progress]), avail;
    if ([self progress] >= 1.0 || ![self isActive] || [self totalPeers] <= 0)
        avail = 0;
    else
    {
        float * piecesFinished = malloc(pieceCount * sizeof(float));
        [self getAmountFinished: piecesFinished size: pieceCount];
        
        float available = 0;
        for (i = 0; i < pieceCount; i++)
            if (piecesAvailablity[i] > 0)
                available += 1.0 - piecesFinished[i];
        
        avail = rintf((float)MAX_PIECES * available / (float)pieceCount);
        if (have + avail > MAX_PIECES) //case if both end in .5 and all pieces are available
            avail--;
        
        free(piecesFinished);
    }
    
    free(piecesAvailablity);
    
    //first two lines: dark blue to show progression, green to show available
    p = (uint32_t *)bitmapData;
    for (i = 0; i < have; i++)
    {
        p[i] = kBlue2;
        p[i + bytesPerRow / 4] = kBlue2;
    }
    for (; i < avail + have; i++)
    {
        p[i] = kGreen3;
        p[i + bytesPerRow / 4] = kGreen3;
    }
    for (; i < MAX_PIECES; i++)
    {
        p[i] = kWhite;
        p[i + bytesPerRow / 4] = kWhite;
    }
    
    //actually draw image
    NSImage * bar = [[NSImage alloc] initWithSize: [fBitmap size]];
    [bar addRepresentation: fBitmap];
    [bar setScalesWhenResized: YES];
    
    return [bar autorelease];
}

- (void) trashFile: (NSString *) path
{
    //attempt to move to trash
    if (![[NSWorkspace sharedWorkspace] performFileOperation: NSWorkspaceRecycleOperation
            source: [path stringByDeletingLastPathComponent] destination: @""
            files: [NSArray arrayWithObject: [path lastPathComponent]] tag: nil])
    {
        //if cannot trash, just delete it (will work if it is on a remote volume)
        if (![[NSFileManager defaultManager] removeFileAtPath: path handler: nil])
            NSLog(@"Could not trash %@", path);
    }
}

@end
