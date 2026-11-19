/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#include <jsapi.h>
#include "manual/ReflectionTypes.h"
#include "runtime/JsRuntime.h"
#include <cstring>
#include <vector>

namespace axjs
{
namespace
{
bool error(JSContext* cx, const char* message) { JS_ReportError(cx, "%s", message); return false; }

bool jsString(JSContext* cx, JS::HandleValue value, NSString* __strong& result)
{
    if (!value.isString()) return error(cx, "Objective-C class, selector, and String values require JavaScript strings");
    JS::RootedString string(cx, value.toString());
    auto length = JS_GetStringLength(string);
    std::vector<jschar> chars(length);
    if (!JS_CopyStringChars(cx, mozilla::Range<jschar>(chars.data(), length), string)) return false;
    result = [[NSString alloc] initWithCharacters:reinterpret_cast<const unichar*>(chars.data()) length:length];
    return result != nil || error(cx, "Cannot convert JavaScript string to NSString");
}

bool nativeString(JSContext* cx, NSString* text, JS::MutableHandleValue result)
{
    std::vector<unichar> chars(text.length);
    [text getCharacters:chars.data() range:NSMakeRange(0, text.length)];
    JS::RootedString string(cx, JS_NewUCStringCopyN(cx, reinterpret_cast<const jschar*>(chars.data()), chars.size()));
    if (!string) return false;
    result.setString(string); return true;
}

bool toObject(JSContext* cx, JS::HandleValue value, id __strong& result, JS::AutoObjectVector& seen, unsigned depth)
{
    if (depth > 64) return error(cx, "Objective-C reflection object nesting exceeds 64 levels");
    if (value.isNullOrUndefined()) { result = nil; return true; }
    if (value.isBoolean()) { result = @(value.toBoolean()); return true; }
    if (value.isNumber()) { result = @(value.toNumber()); return true; }
    if (value.isString()) { NSString* string = nil; if (!jsString(cx, value, string)) return false; result = string; return true; }
    if (!value.isObject()) return error(cx, "Unsupported JavaScript value in Objective-C reflection");
    JS::RootedObject object(cx, &value.toObject());
    if (JS_ObjectIsCallable(cx, object)) return error(cx, "JavaScript functions cannot be passed as Objective-C object values");
    for (std::size_t i = 0; i < seen.length(); ++i)
        if (seen[i] == object) return error(cx, "Cyclic JavaScript object in Objective-C reflection");
    if (!seen.append(object)) return false;
    struct Pop { JS::AutoObjectVector& seen; ~Pop() { seen.popBack(); } } pop{seen};
    JS::RootedValue element(cx);
    if (JS_IsArrayObject(cx, object))
    {
        uint32_t count = 0;
        if (!JS_GetArrayLength(cx, object, &count)) return false;
        NSMutableArray* array = [NSMutableArray arrayWithCapacity:count];
        for (uint32_t i = 0; i < count; ++i)
        {
            id item = nil;
            if (!JS_GetElement(cx, object, i, &element) || !toObject(cx, element, item, seen, depth + 1)) return false;
            [array addObject:item ?: [NSNull null]];
        }
        result = array; return true;
    }
    JS::AutoIdArray keys(cx, JS_Enumerate(cx, object));
    if (!keys) return false;
    NSMutableDictionary* dictionary = [NSMutableDictionary dictionaryWithCapacity:keys.length()];
    JS::RootedValue key(cx);
    for (std::size_t i = 0; i < keys.length(); ++i)
    {
        JS::RootedId idKey(cx, keys[i]);
        if (!JS_IdToValue(cx, idKey, &key)) return false;
        JS::RootedString keyString(cx, JS::ToString(cx, key));
        if (!keyString) return false;
        key.setString(keyString);
        NSString* name = nil;
        id item = nil;
        if (!jsString(cx, key, name) || !JS_GetPropertyById(cx, object, idKey, &element) ||
            !toObject(cx, element, item, seen, depth + 1)) return false;
        dictionary[name] = item ?: [NSNull null];
    }
    result = dictionary; return true;
}

bool fromObject(JSContext* cx, id object, JS::MutableHandleValue result, std::vector<const void*>& seen, unsigned depth)
{
    if (depth > 64) return error(cx, "Objective-C return object nesting exceeds 64 levels");
    if (!object || object == [NSNull null]) { result.setNull(); return true; }
    if ([object isKindOfClass:[NSString class]]) return nativeString(cx, object, result);
    if ([object isKindOfClass:[NSNumber class]])
    {
        if (CFGetTypeID((__bridge CFTypeRef)object) == CFBooleanGetTypeID())
            result.setBoolean([object boolValue]);
        else
        {
            const char* type = [object objCType];
            double value = [object doubleValue];
            if (std::strchr("qQlL", type[0]) && (value < -9007199254740991.0 || value > 9007199254740991.0))
                return error(cx, "Objective-C integer result exceeds JavaScript safe integer range");
            result.set(nativeNumberValue(value));
        }
        return true;
    }
    bool array = [object isKindOfClass:[NSArray class]], dictionary = [object isKindOfClass:[NSDictionary class]];
    if (!array && !dictionary) return nativeString(cx, [object description], result);
    auto identity = (__bridge const void*)object;
    for (auto previous : seen) if (previous == identity) return error(cx, "Cyclic Objective-C return object");
    seen.push_back(identity);
    struct Pop { std::vector<const void*>& seen; ~Pop() { seen.pop_back(); } } pop{seen};
    JS::RootedObject output(cx, array ? JS_NewArrayObject(cx, [object count]) : JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
    if (!output) return false;
    JS::RootedValue element(cx), key(cx);
    if (array)
    {
        NSArray* values = object;
        if (values.count > std::numeric_limits<uint32_t>::max()) return error(cx, "Objective-C array result is too large");
        for (NSUInteger i = 0; i < values.count; ++i)
            if (!fromObject(cx, values[i], &element, seen, depth + 1) || !JS_SetElement(cx, output, static_cast<uint32_t>(i), element)) return false;
    }
    else
    {
        NSDictionary* values = object;
        for (id name in values)
        {
            if (![name isKindOfClass:[NSString class]]) return error(cx, "Objective-C dictionary return keys must be strings");
            JS::RootedId idKey(cx);
            if (!nativeString(cx, name, &key) || !JS_ValueToId(cx, key, &idKey) ||
                !fromObject(cx, values[name], &element, seen, depth + 1) ||
                !JS_DefinePropertyById(cx, output, idKey, element, JSPROP_ENUMERATE)) return false;
        }
    }
    result.setObject(*output); return true;
}

const char* unqualified(const char* type)
{
    while (*type && std::strchr("rnNoORV", *type)) ++type;
    return type;
}
template<class T>
bool setInteger(JSContext* cx, NSInvocation* invocation, NSUInteger index, JS::HandleValue value)
{
    T converted{};
    if (!value.isNumber() || !reflection::checkedInteger(value.toNumber(), converted))
        return error(cx, "Objective-C integer argument is fractional, out of range, or not a safe integer");
    [invocation setArgument:&converted atIndex:index]; return true;
}
template<class T>
bool getInteger(JSContext* cx, NSInvocation* invocation, JS::MutableHandleValue result)
{
    T value{}; [invocation getReturnValue:&value];
    if constexpr (sizeof(T) >= 8)
        if (static_cast<long double>(value) < -9007199254740991.0L || static_cast<long double>(value) > 9007199254740991.0L)
            return error(cx, "Objective-C integer result exceeds JavaScript safe integer range");
    result.set(nativeNumberValue(static_cast<double>(value))); return true;
}
}

bool callStaticMethodApple(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc < 2) return error(cx, "callStaticMethod requires Objective-C class, selector, and arguments");
    @autoreleasepool
    {
        @try
        {
            NSString* className = nil;
            NSString* methodName = nil;
            if (!jsString(cx, args[0], className) || !jsString(cx, args[1], methodName)) return false;
            Class klass = NSClassFromString(className);
            if (!klass) return error(cx, "Objective-C reflection class not found");
            SEL selector = NSSelectorFromString(methodName);
            NSMethodSignature* signature = [klass methodSignatureForSelector:selector];
            if (!signature || ![klass respondsToSelector:selector]) return error(cx, "Objective-C static selector not found");
            if (signature.numberOfArguments != argc) return error(cx, "Objective-C selector argument count does not match JavaScript arguments");
            const char* returnType = unqualified(signature.methodReturnType);
            if (!std::strchr("v@BcCsSiIlLqQfd", returnType[0]) || (returnType[0] == '@' && returnType[1] == '?'))
                return error(cx, "Unsupported Objective-C reflection return encoding");
            NSInvocation* invocation = [NSInvocation invocationWithMethodSignature:signature];
            invocation.target = klass;
            invocation.selector = selector;
            NSMutableArray* objects = [NSMutableArray array];
            JS::AutoObjectVector seen(cx);
            for (unsigned i = 2; i < argc; ++i)
            {
                const char* type = unqualified([signature getArgumentTypeAtIndex:i]);
                switch (*type)
                {
                case '@':
                {
                    if (type[1] == '?') return error(cx, "Objective-C blocks are not supported by reflection");
                    id object = nil;
                    if (!toObject(cx, args[i], object, seen, 0)) return false;
                    if (object) [objects addObject:object];
                    [invocation setArgument:&object atIndex:i]; break;
                }
                case 'B':
                {
                    if (!args[i].isBoolean()) return error(cx, "Objective-C BOOL argument requires a boolean");
                    bool value = args[i].toBoolean(); [invocation setArgument:&value atIndex:i]; break;
                }
                case 'c':
                    if (args[i].isBoolean()) { signed char value = args[i].toBoolean(); [invocation setArgument:&value atIndex:i]; }
                    else if (!setInteger<signed char>(cx, invocation, i, args[i])) return false;
                    break;
                case 'C': if (!setInteger<unsigned char>(cx, invocation, i, args[i])) return false; break;
                case 's': if (!setInteger<short>(cx, invocation, i, args[i])) return false; break;
                case 'S': if (!setInteger<unsigned short>(cx, invocation, i, args[i])) return false; break;
                case 'i': if (!setInteger<int>(cx, invocation, i, args[i])) return false; break;
                case 'I': if (!setInteger<unsigned int>(cx, invocation, i, args[i])) return false; break;
                case 'l': if (!setInteger<long>(cx, invocation, i, args[i])) return false; break;
                case 'L': if (!setInteger<unsigned long>(cx, invocation, i, args[i])) return false; break;
                case 'q': if (!setInteger<long long>(cx, invocation, i, args[i])) return false; break;
                case 'Q': if (!setInteger<unsigned long long>(cx, invocation, i, args[i])) return false; break;
                case 'f':
                {
                    if (!args[i].isNumber()) return error(cx, "Objective-C float argument requires a number");
                    double number = args[i].toNumber(); float value = static_cast<float>(number);
                    if (std::isfinite(number) && !std::isfinite(value)) return error(cx, "Objective-C float argument is out of range");
                    [invocation setArgument:&value atIndex:i]; break;
                }
                case 'd':
                {
                    if (!args[i].isNumber()) return error(cx, "Objective-C double argument requires a number");
                    double value = args[i].toNumber(); [invocation setArgument:&value atIndex:i]; break;
                }
                default: return error(cx, "Unsupported Objective-C reflection argument encoding");
                }
            }
            [invocation retainArguments];
            [invocation invoke];
            const char* type = unqualified(signature.methodReturnType);
            if (std::strcmp(type, @encode(BOOL)) == 0)
            {
                BOOL value = NO; [invocation getReturnValue:&value]; args.rval().setBoolean(value); return true;
            }
            switch (*type)
            {
            case 'v': args.rval().setUndefined(); return true;
            case 'B': { bool value{}; [invocation getReturnValue:&value]; args.rval().setBoolean(value); return true; }
            case '@':
            {
                __unsafe_unretained id object = nil;
                [invocation getReturnValue:&object];
                id retained = object;
                std::vector<const void*> visited;
                return fromObject(cx, retained, args.rval(), visited, 0);
            }
            case 'c': return getInteger<signed char>(cx, invocation, args.rval());
            case 'C': return getInteger<unsigned char>(cx, invocation, args.rval());
            case 's': return getInteger<short>(cx, invocation, args.rval());
            case 'S': return getInteger<unsigned short>(cx, invocation, args.rval());
            case 'i': return getInteger<int>(cx, invocation, args.rval());
            case 'I': return getInteger<unsigned int>(cx, invocation, args.rval());
            case 'l': return getInteger<long>(cx, invocation, args.rval());
            case 'L': return getInteger<unsigned long>(cx, invocation, args.rval());
            case 'q': return getInteger<long long>(cx, invocation, args.rval());
            case 'Q': return getInteger<unsigned long long>(cx, invocation, args.rval());
            case 'f': { float value{}; [invocation getReturnValue:&value]; args.rval().set(nativeNumberValue(static_cast<double>(value))); return true; }
            case 'd': { double value{}; [invocation getReturnValue:&value]; args.rval().set(nativeNumberValue(value)); return true; }
            default: return error(cx, "Unsupported Objective-C reflection return encoding");
            }
        }
        @catch (NSException* exception)
        {
            JS_ReportError(cx, "Objective-C exception: %s: %s", exception.name.UTF8String, exception.reason.UTF8String);
            return false;
        }
    }
}
}  // namespace axjs
