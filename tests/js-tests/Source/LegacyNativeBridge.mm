/*
 * Copyright (c) 2013-2017 Chukong Technologies Inc.
 * Copyright (c) 2026 Axmol contributors.
 * Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
// Adapted from cocos2d-x/v3 tests/js-tests/project/proj.ios/NativeOcClass.m
// at e4b6a5ef8fcdc99a7ebea606312b67fe4c534b9f. Preserve the original selectors
// while creating real native alerts on the main UI thread of current SDKs.
#import <Foundation/Foundation.h>
#import <TargetConditionals.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#else
#import <UIKit/UIKit.h>
#endif

@interface NativeOcClass : NSObject
+ (float)callNative:(NSNumber*)number andInt:(NSString*)text;
+ (void)callNativeWithParam:(NSString*)text;
+ (NSString*)callNativeWithReturnString;
+ (BOOL)callNativeWithReturnBool;
+ (int)callNativeWithAdd:(NSNumber*)first and:(NSNumber*)second;
+ (BOOL)callNativeUIWithTitle:(NSString*)title andContent:(NSString*)content;
+ (BOOL)callNativeUIWithTitle:(NSString*)title andContent:(NSString*)content addBool:(BOOL)value;
@end

namespace
{
void showLegacyAlert(NSString* title, NSString* content, NSString* button)
{
    dispatch_async(dispatch_get_main_queue(), ^{
#if TARGET_OS_OSX
        NSAlert* alert = [[NSAlert alloc] init];
        [alert addButtonWithTitle:button];
        [alert addButtonWithTitle:@"Cancel"];
        alert.messageText = title ?: @"";
        alert.informativeText = content ?: @"";
        alert.alertStyle = NSAlertStyleWarning;
        if (NSApp.keyWindow)
            [alert beginSheetModalForWindow:NSApp.keyWindow completionHandler:nil];
        else
            [alert runModal];
#else
        UIWindow* window = nil;
        for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
        {
            if (![scene isKindOfClass:UIWindowScene.class] ||
                scene.activationState != UISceneActivationStateForegroundActive)
                continue;
            for (UIWindow* candidate in ((UIWindowScene*)scene).windows)
                if (candidate.isKeyWindow) { window = candidate; break; }
            if (window) break;
        }
        UIViewController* controller = window.rootViewController;
        while (controller.presentedViewController) controller = controller.presentedViewController;
        if (!controller)
        {
            NSLog(@"NativeOcClass: no native controller available to present the reflection alert");
            return;
        }
        UIAlertController* alert = [UIAlertController alertControllerWithTitle:title message:content
            preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
        [alert addAction:[UIAlertAction actionWithTitle:button style:UIAlertActionStyleDefault handler:nil]];
        [controller presentViewController:alert animated:YES completion:nil];
#endif
    });
}
}

@implementation NativeOcClass
+ (float)callNative:(NSNumber*)number andInt:(NSString*)text
{
    float result = number.floatValue + 111.3333F;
    NSLog(@"callNative string is %@ and int value is %f", text, result);
    return result;
}
+ (void)callNativeWithParam:(NSString*)text { NSLog(@"callNativeWithParam: str is %@", text); }
+ (NSString*)callNativeWithReturnString { return @"yes is a return string form objective-c"; }
+ (BOOL)callNativeWithReturnBool { return YES; }
+ (int)callNativeWithAdd:(NSNumber*)first and:(NSNumber*)second { return first.intValue + second.intValue; }
+ (BOOL)callNativeUIWithTitle:(NSString*)title andContent:(NSString*)content
{
    showLegacyAlert(title, content, @"OK");
    return YES;
}
+ (BOOL)callNativeUIWithTitle:(NSString*)title andContent:(NSString*)content addBool:(BOOL)value
{
    showLegacyAlert(title, content, value ? @"it's true" : @"it's false");
    return YES;
}
@end
