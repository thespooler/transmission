/******************************************************************************
 * Copyright (c) 2005-2006 Transmission authors and contributors
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

#import "StringAdditions.h"
#import "Utils.h"

@implementation NSString (StringAdditions)

+ (NSString *) stringWithInt: (int) value
{
    return [NSString stringWithFormat: @"%d", value];
}

+ (NSString *) stringForFileSize: (uint64_t) size
{
    if (size < 1024)
        return [NSString stringWithFormat: @"%lld bytes", size];
    else if (size < 1048576)
        return [NSString stringWithFormat: @"%lld.%lld KB",
                size / 1024, ( size % 1024 ) / 103];
    else if (size < 1073741824)
        return [NSString stringWithFormat: @"%lld.%lld MB",
                size / 1048576, ( size % 1048576 ) / 104858];
    else
        return [NSString stringWithFormat: @"%lld.%lld GB",
                size / 1073741824, ( size % 1073741824 ) / 107374183];
}

+ (NSString *) stringForSpeed: (float) speed
{
    return [[self stringForSpeedAbbrev: speed]
        stringByAppendingString: @"B/s"];
}

+ (NSString *) stringForSpeedAbbrev: (float) speed
{
    if (speed < 1000)         /* 0.0 K to 999.9 K */
        return [NSString stringWithFormat: @"%.1f K", speed];
    else if (speed < 102400)  /* 0.98 M to 99.99 M */
        return [NSString stringWithFormat: @"%.2f M", speed / 1024];
    else if (speed < 1024000) /* 100.0 M to 999.9 M */
        return [NSString stringWithFormat: @"%.1f M", speed / 1024];
    else                      /* Insane speeds */
        return [NSString stringWithFormat: @"%.2f G", speed / 1048576];
}

+ (NSString *) stringForRatio: (uint64_t) down upload: (uint64_t) up
{
    if( !down && !up )
        return @"N/A";
    if( !down )
        return [NSString stringWithUTF8String: "\xE2\x88\x9E"];

    float ratio = (float) up / (float) down;
    if( ratio < 10.0 )
        return [NSString stringWithFormat: @"%.2f", ratio];
    else if( ratio < 100.0 )
        return [NSString stringWithFormat: @"%.1f", ratio];
    else
        return [NSString stringWithFormat: @"%.0f", ratio];
}

- (NSString *) stringFittingInWidth: (float) width
                    withAttributes: (NSDictionary *) attributes
{
    int i;
    float realWidth = [self sizeWithAttributes: attributes].width;
    
    /* The whole string fits */
    if( realWidth <= width )
        return self;
        
    /* Width is too small */
    if ( [NS_ELLIPSIS sizeWithAttributes: attributes].width > width )
        return @"";

    /* Don't worry about ellipsis until the end */
    width -= [NS_ELLIPSIS sizeWithAttributes: attributes].width;

    /* Approximate how many characters we'll need to drop... */
    i = [self length] * (width / realWidth);

    /* ... then refine it */
    NSString * newString = [self substringToIndex: i];
    realWidth = [newString sizeWithAttributes: attributes].width;

    if( realWidth < width )
    {
        NSString * smallerString;
        do
        {
            smallerString = newString;
            newString = [self substringToIndex: ++i];
        } while ([newString sizeWithAttributes: attributes].width <= width);
        
        newString = smallerString;
    }
    else if( realWidth > width )
    {
        do
        {
            newString = [self substringToIndex: --i];
        } while ([newString sizeWithAttributes: attributes].width > width);
    }
    else;

    return [newString stringByAppendingString: NS_ELLIPSIS];
}

@end
