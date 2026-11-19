/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/Bindings.h"
#include "manual/ReflectionTypes.h"
#include "runtime/JsRuntime.h"
#include "axmol/platform/PlatformConfig.h"

#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
#    include "axmol/platform/android/jni/JniHelper.h"
#    include <algorithm>
#endif

namespace axjs
{
#if defined(__APPLE__)
bool callStaticMethodApple(JSContext*, unsigned, JS::Value*);
#endif
namespace
{
bool fail(JSContext* cx, const char* message) { JS_ReportError(cx, "%s", message); return false; }

#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
bool javaException(JSContext* cx, JNIEnv* env)
{
    if (!env->ExceptionCheck()) return false;
    jthrowable exception = env->ExceptionOccurred();
    env->ExceptionClear();
    std::string message = "Java exception in jsb.reflection.callStaticMethod";
    auto klass = env->GetObjectClass(exception);
    auto method = klass ? env->GetMethodID(klass, "toString", "()Ljava/lang/String;") : nullptr;
    if (method)
    {
        auto text = static_cast<jstring>(env->CallObjectMethod(exception, method));
        if (!env->ExceptionCheck() && text) message += ": " + ax::JniHelper::jstring2string(text);
        if (text) env->DeleteLocalRef(text);
    }
    env->ExceptionClear();
    if (klass) env->DeleteLocalRef(klass);
    env->DeleteLocalRef(exception);
    JS_ReportError(cx, "%s", message.c_str());
    return true;
}

bool jsToJava(JSContext* cx, JNIEnv* env, JS::HandleValue value, const std::string& type, jvalue& result);

bool jsString(JSContext* cx, JNIEnv* env, JS::HandleValue value, jstring& result)
{
    if (value.isNull()) { result = nullptr; return true; }
    if (!value.isString()) return fail(cx, "JNI String argument requires a string or null");
    JS::RootedString str(cx, value.toString());
    auto length = JS_GetStringLength(str);
    if (length > static_cast<std::size_t>(std::numeric_limits<jsize>::max())) return fail(cx, "JNI String is too large");
    std::vector<jschar> chars(length);
    if (!JS_CopyStringChars(cx, mozilla::Range<jschar>(chars.data(), length), str)) return false;
    static_assert(sizeof(jschar) == sizeof(jchar));
    result = env->NewString(reinterpret_cast<const jchar*>(chars.data()), static_cast<jsize>(length));
    return result != nullptr && !javaException(cx, env);
}

bool jsArray(JSContext* cx, JNIEnv* env, JS::HandleValue value, const std::string& type, jobject& result)
{
    if (value.isNull()) { result = nullptr; return true; }
    if (!value.isObject() || !JS_IsArrayObject(cx, value)) return fail(cx, "JNI array argument requires an array or null");
    JS::RootedObject object(cx, &value.toObject());
    uint32_t count = 0;
    if (!JS_GetArrayLength(cx, object, &count)) return false;
    if (count > static_cast<uint32_t>(std::numeric_limits<jsize>::max())) return fail(cx, "JNI array is too large");
    auto component = type.substr(1);
    JS::RootedValue element(cx);
#    define AXJS_INPUT_ARRAY(CODE, TYPE, NAME, FIELD) \
    if (component == CODE) { \
        std::vector<TYPE> items(count); \
        for (uint32_t i = 0; i < count; ++i) { jvalue converted{}; \
            if (!JS_GetElement(cx, object, i, &element) || !jsToJava(cx, env, element, component, converted)) return false; \
            items[i] = converted.FIELD; } \
        auto array = env->New##NAME##Array(static_cast<jsize>(count)); \
        if (!array) return !javaException(cx, env) && fail(cx, "Cannot allocate JNI array"); \
        env->Set##NAME##ArrayRegion(array, 0, static_cast<jsize>(count), items.data()); \
        result = array; return !javaException(cx, env); }
    AXJS_INPUT_ARRAY("Z", jboolean, Boolean, z)
    AXJS_INPUT_ARRAY("B", jbyte, Byte, b)
    AXJS_INPUT_ARRAY("C", jchar, Char, c)
    AXJS_INPUT_ARRAY("S", jshort, Short, s)
    AXJS_INPUT_ARRAY("I", jint, Int, i)
    AXJS_INPUT_ARRAY("J", jlong, Long, j)
    AXJS_INPUT_ARRAY("F", jfloat, Float, f)
    AXJS_INPUT_ARRAY("D", jdouble, Double, d)
#    undef AXJS_INPUT_ARRAY
    if (component == "Ljava/lang/String;")
    {
        auto klass = env->FindClass("java/lang/String");
        if (!klass) return !javaException(cx, env) && fail(cx, "Cannot resolve java.lang.String");
        auto array = env->NewObjectArray(static_cast<jsize>(count), klass, nullptr);
        env->DeleteLocalRef(klass);
        if (!array) return !javaException(cx, env) && fail(cx, "Cannot allocate JNI String array");
        for (uint32_t i = 0; i < count; ++i)
        {
            jstring converted = nullptr;
            if (!JS_GetElement(cx, object, i, &element) || !jsString(cx, env, element, converted)) return false;
            env->SetObjectArrayElement(array, static_cast<jsize>(i), converted);
            if (converted) env->DeleteLocalRef(converted);
            if (javaException(cx, env)) return false;
        }
        result = array;
        return true;
    }
    return fail(cx, "JNI reflection supports primitive arrays and String arrays");
}

bool jsToJava(JSContext* cx, JNIEnv* env, JS::HandleValue value, const std::string& type, jvalue& result)
{
    if (type == "Ljava/lang/String;")
    {
        jstring str = nullptr; if (!jsString(cx, env, value, str)) return false; result.l = str; return true;
    }
    if (type[0] == '[') return jsArray(cx, env, value, type, result.l);
    if (type == "Z")
    {
        if (!value.isBoolean()) return fail(cx, "JNI boolean argument requires a boolean");
        result.z = value.toBoolean() ? JNI_TRUE : JNI_FALSE; return true;
    }
    if (!value.isNumber()) return fail(cx, "JNI numeric argument requires a number");
    double number = value.toNumber();
    bool ok = false;
    switch (type[0])
    {
    case 'B': ok = reflection::checkedInteger(number, result.b); break;
    case 'C': ok = reflection::checkedInteger(number, result.c); break;
    case 'S': ok = reflection::checkedInteger(number, result.s); break;
    case 'I': ok = reflection::checkedInteger(number, result.i); break;
    case 'J': ok = reflection::checkedInteger(number, result.j); break;
    case 'F': result.f = static_cast<jfloat>(number); ok = !std::isfinite(number) || std::isfinite(result.f); break;
    case 'D': result.d = number; ok = true; break;
    default: return fail(cx, "Unsupported JNI reflection object type");
    }
    return ok || fail(cx, "JNI numeric argument is fractional, out of range, or not a safe integer");
}

bool javaString(JSContext* cx, JNIEnv* env, jstring str, JS::MutableHandleValue result)
{
    if (!str) { result.setNull(); return true; }
    auto length = env->GetStringLength(str);
    auto chars = env->GetStringChars(str, nullptr);
    if (!chars) return !javaException(cx, env) && fail(cx, "Cannot read JNI String");
    JS::RootedString text(cx, JS_NewUCStringCopyN(cx, reinterpret_cast<const jschar*>(chars), static_cast<std::size_t>(length)));
    env->ReleaseStringChars(str, chars);
    if (!text) return false;
    result.setString(text); return true;
}

bool javaArray(JSContext* cx, JNIEnv* env, jobject input, const std::string& type, JS::MutableHandleValue result)
{
    if (!input) { result.setNull(); return true; }
    auto count = env->GetArrayLength(static_cast<jarray>(input));
    if (javaException(cx, env)) return false;
    JS::RootedObject output(cx, JS_NewArrayObject(cx, static_cast<size_t>(count)));
    if (!output) return false;
    auto component = type.substr(1);
    JS::RootedValue element(cx);
#    define AXJS_OUTPUT_ARRAY(CODE, TYPE, NAME, CONVERT) \
    if (component == CODE) { \
        std::vector<TYPE> items(static_cast<size_t>(count)); \
        env->Get##NAME##ArrayRegion(static_cast<TYPE##Array>(input), 0, count, items.data()); \
        if (javaException(cx, env)) return false; \
        for (jsize i = 0; i < count; ++i) { auto native = items[static_cast<size_t>(i)]; CONVERT; \
            if (!JS_SetElement(cx, output, static_cast<uint32_t>(i), element)) return false; } \
        result.setObject(*output); return true; }
    AXJS_OUTPUT_ARRAY("Z", jboolean, Boolean, element.setBoolean(native != JNI_FALSE))
    AXJS_OUTPUT_ARRAY("B", jbyte, Byte, element.setInt32(native))
    AXJS_OUTPUT_ARRAY("C", jchar, Char, element.setInt32(native))
    AXJS_OUTPUT_ARRAY("S", jshort, Short, element.setInt32(native))
    AXJS_OUTPUT_ARRAY("I", jint, Int, element.setInt32(native))
    AXJS_OUTPUT_ARRAY("J", jlong, Long, if (native < -9007199254740991LL || native > 9007199254740991LL) return fail(cx, "JNI long result exceeds JavaScript safe integer range"); element.set(nativeNumberValue(static_cast<double>(native))))
    AXJS_OUTPUT_ARRAY("F", jfloat, Float, element.set(nativeNumberValue(static_cast<double>(native))))
    AXJS_OUTPUT_ARRAY("D", jdouble, Double, element.set(nativeNumberValue(native)))
#    undef AXJS_OUTPUT_ARRAY
    if (component == "Ljava/lang/String;")
    {
        for (jsize i = 0; i < count; ++i)
        {
            auto str = static_cast<jstring>(env->GetObjectArrayElement(static_cast<jobjectArray>(input), i));
            bool ok = !javaException(cx, env) && javaString(cx, env, str, &element) &&
                JS_SetElement(cx, output, static_cast<uint32_t>(i), element);
            if (str) env->DeleteLocalRef(str);
            if (!ok) return false;
        }
        result.setObject(*output); return true;
    }
    return fail(cx, "JNI reflection supports primitive arrays and String arrays");
}

bool callStaticMethodAndroid(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string className, methodName, signature;
    if (argc < 3 || !args[0].isString() || !args[1].isString() || !args[2].isString() ||
        !fromJsValue(cx, args[0], className) || !fromJsValue(cx, args[1], methodName) || !fromJsValue(cx, args[2], signature))
        return fail(cx, "callStaticMethod requires class, method, JNI signature, and arguments");
    reflection::MethodDescriptor descriptor;
    if (!reflection::parseMethodDescriptor(signature, descriptor)) return fail(cx, "Invalid JNI method descriptor");
    if (descriptor.arguments.size() != argc - 3) return fail(cx, "JNI signature argument count does not match JavaScript arguments");
    auto supported = [](const std::string& type) {
        return (type.size() == 1 && std::string_view("VZBCSIJFD").find(type[0]) != std::string_view::npos) ||
            type == "Ljava/lang/String;" || type == "[Ljava/lang/String;" ||
            (type.size() == 2 && type[0] == '[' && std::string_view("ZBCSIJFD").find(type[1]) != std::string_view::npos);
    };
    if (!supported(descriptor.result) || !std::all_of(descriptor.arguments.begin(), descriptor.arguments.end(), supported))
        return fail(cx, "JNI reflection supports primitive types, String, primitive arrays, and String arrays");
    std::replace(className.begin(), className.end(), '.', '/');
    auto* env = ax::JniHelper::getEnv();
    if (!env) return fail(cx, "Android JNI environment is unavailable");
    if (env->PushLocalFrame(32) < 0) return !javaException(cx, env) && fail(cx, "Cannot allocate JNI local frame");
    struct LocalFrame { JNIEnv* env; ~LocalFrame() { env->PopLocalFrame(nullptr); } } frame{env};
    ax::JniMethodInfo info{};
    if (!ax::JniHelper::getStaticMethodInfo(info, className.c_str(), methodName.c_str(), signature.c_str()))
    {
        if (javaException(cx, env)) return false;
        JS_ReportError(cx, "Java static method not found: %s.%s%s", className.c_str(), methodName.c_str(), signature.c_str());
        return false;
    }
    std::vector<jvalue> values(descriptor.arguments.size());
    for (unsigned i = 0; i < values.size(); ++i)
        if (!jsToJava(cx, env, args[i + 3], descriptor.arguments[i], values[i])) return false;
    jvalue returned{};
    const auto& type = descriptor.result;
    switch (type[0])
    {
    case 'V': env->CallStaticVoidMethodA(info.classID, info.methodID, values.data()); break;
    case 'Z': returned.z = env->CallStaticBooleanMethodA(info.classID, info.methodID, values.data()); break;
    case 'B': returned.b = env->CallStaticByteMethodA(info.classID, info.methodID, values.data()); break;
    case 'C': returned.c = env->CallStaticCharMethodA(info.classID, info.methodID, values.data()); break;
    case 'S': returned.s = env->CallStaticShortMethodA(info.classID, info.methodID, values.data()); break;
    case 'I': returned.i = env->CallStaticIntMethodA(info.classID, info.methodID, values.data()); break;
    case 'J': returned.j = env->CallStaticLongMethodA(info.classID, info.methodID, values.data()); break;
    case 'F': returned.f = env->CallStaticFloatMethodA(info.classID, info.methodID, values.data()); break;
    case 'D': returned.d = env->CallStaticDoubleMethodA(info.classID, info.methodID, values.data()); break;
    case 'L': case '[': returned.l = env->CallStaticObjectMethodA(info.classID, info.methodID, values.data()); break;
    default: return fail(cx, "Unsupported JNI return type");
    }
    if (javaException(cx, env)) return false;
    switch (type[0])
    {
    case 'V': args.rval().setUndefined(); break;
    case 'Z': args.rval().setBoolean(returned.z != JNI_FALSE); break;
    case 'B': args.rval().setInt32(returned.b); break;
    case 'C': args.rval().setInt32(returned.c); break;
    case 'S': args.rval().setInt32(returned.s); break;
    case 'I': args.rval().setInt32(returned.i); break;
    case 'J':
        if (returned.j < -9007199254740991LL || returned.j > 9007199254740991LL) return fail(cx, "JNI long result exceeds JavaScript safe integer range");
        args.rval().set(nativeNumberValue(static_cast<double>(returned.j))); break;
    case 'F': args.rval().set(nativeNumberValue(static_cast<double>(returned.f))); break;
    case 'D': args.rval().set(nativeNumberValue(returned.d)); break;
    case 'L':
        if (type != "Ljava/lang/String;") return fail(cx, "JNI reflection supports String object results");
        return javaString(cx, env, static_cast<jstring>(returned.l), args.rval());
    case '[': return javaArray(cx, env, returned.l, type, args.rval());
    }
    return true;
}
#endif

bool callStaticMethod(JSContext* cx, unsigned argc, JS::Value* vp)
{
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    return callStaticMethodAndroid(cx, argc, vp);
#elif defined(__APPLE__)
    return callStaticMethodApple(cx, argc, vp);
#else
    return fail(cx, "Native reflection is supported on Android and Apple platforms");
#endif
}
}  // namespace

bool registerPlatform(JSContext* cx, JS::HandleObject global)
{
    JS::RootedObject jsb(cx), object(cx);
    if (!getOrCreateNamespace(cx, global, "jsb", &jsb) || !getOrCreateNamespace(cx, jsb, "reflection", &object)) return false;
    return JS_DefineFunction(cx, object, "callStaticMethod", callStaticMethod, 2, JSPROP_ENUMERATE) != nullptr;
}
}  // namespace axjs
