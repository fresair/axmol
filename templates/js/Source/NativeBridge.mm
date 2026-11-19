// Copyright (c) 2019-present Simdsoft Limited.
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
#import <Foundation/Foundation.h>

@interface AxmolJsBridge : NSObject
+ (NSString*)hello:(NSString*)name;
+ (int)add:(int)left to:(int)right;
@end

@implementation AxmolJsBridge
+ (NSString*)hello:(NSString*)name
{
    return [@"Hello, " stringByAppendingString:name ?: @"JavaScript"];
}
+ (int)add:(int)left to:(int)right
{
    return left + right;
}
@end
