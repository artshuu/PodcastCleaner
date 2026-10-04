// Renders the Podcast Cleaner application icon.
//
//   make_icon <pixel-size> <output.png>
//
// Uses nothing but the macOS SDK (CoreGraphics + ImageIO), so the packaging
// script can regenerate the artwork on any machine without shipping a binary
// asset or depending on an image editor.

#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#include <ImageIO/ImageIO.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const CGFloat designSize = 1024.0;

static CGColorRef makeColor (CGColorSpaceRef space, CGFloat r, CGFloat g, CGFloat b)
{
    const CGFloat components[4] = { r, g, b, 1.0 };
    return CGColorCreate (space, components);
}

static CGGradientRef makeGradient (CGColorSpaceRef space, CGColorRef from, CGColorRef to)
{
    const void* colors[2] = { from, to };
    CFArrayRef array = CFArrayCreate (NULL, colors, 2, &kCFTypeArrayCallBacks);
    CGGradientRef gradient = CGGradientCreateWithColors (space, array, NULL);
    CFRelease (array);
    return gradient;
}

/** Fills a rounded rectangle with a top-to-bottom gradient. */
static void fillRoundedRect (CGContextRef context, CGRect rect, CGFloat radius,
                             CGGradientRef gradient)
{
    CGPathRef path = CGPathCreateWithRoundedRect (rect, radius, radius, NULL);
    CGContextSaveGState (context);
    CGContextAddPath (context, path);
    CGContextClip (context);
    CGContextDrawLinearGradient (context, gradient,
                                 CGPointMake (0.0, CGRectGetMaxY (rect)),
                                 CGPointMake (0.0, CGRectGetMinY (rect)), 0);
    CGContextRestoreGState (context);
    CGPathRelease (path);
}

int main (int argc, char** argv)
{
    if (argc != 3)
    {
        fprintf (stderr, "usage: make_icon <pixel-size> <output.png>\n");
        return 2;
    }

    const CGFloat size = (CGFloat) atof (argv[1]);
    if (size < 16.0 || size > 4096.0)
    {
        fprintf (stderr, "error: pixel size out of range\n");
        return 2;
    }

    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate (NULL, (size_t) size, (size_t) size, 8,
                                                  (size_t) size * 4, space,
                                                  kCGImageAlphaPremultipliedLast);
    if (context == NULL)
    {
        fprintf (stderr, "error: could not create a bitmap context\n");
        CGColorSpaceRelease (space);
        return 1;
    }

    CGContextScaleCTM (context, size / designSize, size / designSize);
    CGContextSetAllowsAntialiasing (context, true);

    // Rounded plate, matching the dark window of the app.
    CGColorRef plateTop = makeColor (space, 0.110, 0.149, 0.188);    // #1c2630
    CGColorRef plateBottom = makeColor (space, 0.055, 0.075, 0.098); // #0e1319
    CGGradientRef plate = makeGradient (space, plateTop, plateBottom);

    const CGRect plateRect = CGRectMake (30.0, 30.0, designSize - 60.0, designSize - 60.0);
    fillRoundedRect (context, plateRect, 230.0, plate);

    // A hairline border lifts the plate off light backgrounds.
    CGPathRef border = CGPathCreateWithRoundedRect (plateRect, 230.0, 230.0, NULL);
    CGContextAddPath (context, border);
    CGContextSetStrokeColorWithColor (context, makeColor (space, 0.180, 0.235, 0.290));
    CGContextSetLineWidth (context, 6.0);
    CGContextStrokePath (context);
    CGPathRelease (border);

    // Waveform. Two bars are dimmed to hint at censored words.
    const CGFloat magnitudes[] = { 0.30, 0.52, 0.34, 0.72, 0.95, 0.62, 0.38, 0.80, 0.55, 0.28 };
    const int mutedBars[] = { 2, 6 };
    const int barCount = (int) (sizeof (magnitudes) / sizeof (magnitudes[0]));

    const CGFloat barWidth = 54.0;
    const CGFloat barGap = 36.0;
    const CGFloat totalWidth = barCount * barWidth + (barCount - 1) * barGap;
    const CGFloat midY = designSize * 0.5;

    CGGradientRef bright = makeGradient (space, makeColor (space, 0.345, 0.776, 0.655),
                                         makeColor (space, 0.184, 0.561, 0.471));
    CGGradientRef dim = makeGradient (space, makeColor (space, 0.239, 0.290, 0.337),
                                      makeColor (space, 0.145, 0.180, 0.212));

    CGFloat x = (designSize - totalWidth) * 0.5;
    for (int index = 0; index < barCount; ++index)
    {
        bool muted = false;
        for (unsigned int m = 0; m < sizeof (mutedBars) / sizeof (mutedBars[0]); ++m)
            muted = muted || mutedBars[m] == index;

        const CGFloat halfHeight = 430.0 * magnitudes[index];
        fillRoundedRect (context, CGRectMake (x, midY - halfHeight, barWidth, halfHeight * 2.0),
                         barWidth * 0.5, muted ? dim : bright);
        x += barWidth + barGap;
    }

    CGImageRef image = CGBitmapContextCreateImage (context);
    bool ok = false;

    if (image != NULL)
    {
        CFURLRef url = CFURLCreateFromFileSystemRepresentation (
            NULL, (const UInt8*) argv[2], (CFIndex) strlen (argv[2]), false);

        if (url != NULL)
        {
            CGImageDestinationRef destination =
                CGImageDestinationCreateWithURL (url, CFSTR ("public.png"), 1, NULL);

            if (destination != NULL)
            {
                CGImageDestinationAddImage (destination, image, NULL);
                ok = CGImageDestinationFinalize (destination);
                CFRelease (destination);
            }
            CFRelease (url);
        }
        CGImageRelease (image);
    }

    CGContextRelease (context);
    CGColorSpaceRelease (space);

    if (! ok)
    {
        fprintf (stderr, "error: could not write '%s'\n", argv[2]);
        return 1;
    }

    return 0;
}
