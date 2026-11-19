/*
 * Copyright (c) 2026 Axmol contributors.
 * Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "runtime/JsEngine.h"
#include "axmol/base/WeakPtr.h"
#include "axmol/base/Value.h"
#include "axmol/base/Vector.h"
#include "axmol/base/Map.h"
#include "axmol/base/Types.h"
#include "axmol/math/Vec2.h"
#include "axmol/math/Vec3.h"
#include "axmol/math/Vec4.h"
#include "axmol/math/Rect.h"
#include "axmol/math/Color.h"
#include "axmol/math/Mat4.h"
#include "axmol/math/Quat.h"
#include "axmol/math/AABB.h"
#include "axmol/math/OBB.h"
#include "axmol/math/Ray.h"
#include "axmol/math/AffineTransform.h"
#include "axmol/physics/2d/PhysicsMaterial2D.h"
#if defined(AX_ENABLE_3D)
#include "axmol/3d/Terrain.h"
#endif
#if defined(AX_ENABLE_NAVMESH)
#include "axmol/navmesh/NavMeshAgent.h"
#endif
#if defined(AX_ENABLE_PHYSICS_3D)
namespace ax { struct PhysicsMaterial; }
#endif
#include <array>
#include <algorithm>
#include <atomic>
#include <concepts>
#include <charconv>
#include <stdexcept>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace axjs
{
// SM33 NumberValue/setNumber(double) attempts an int32 cast before testing
// equality. Keep out-of-range values out of that path, and canonicalize NaNs
// before they can overlap the NaN-boxed pointer/type tags.
inline JS::Value nativeNumberValue(double value)
{
    if(value>=static_cast<double>(std::numeric_limits<int32_t>::min()) &&
       value<=static_cast<double>(std::numeric_limits<int32_t>::max()))
        return JS::NumberValue(value); // Includes the original negative-zero handling.
    return JS::DoubleValue(JS::CanonicalizeNaN(value));
}
enum class NativeOwnership { Borrowed, Factory, Constructor };

/** Invalidates non-reference-counted pointers independently of their addresses. */
class LifetimeToken
{
public:
    LifetimeToken();
    explicit LifetimeToken(ax::Object* owner);
    explicit LifetimeToken(std::function<bool()> validator);
    LifetimeToken(ax::Object* owner, std::function<bool()> validator);
    void expire() const;
    bool isAlive() const;
private:
    friend struct NativeHandle;
    std::shared_ptr<std::atomic<bool>> _alive;
    ax::WeakPtr<ax::Object> _owner;
    bool _hasOwner{false};
    std::function<bool()> _validator;
};

/** Callback arguments such as Event must not escape their callback scope. */
class ScopedNative
{
public:
    ScopedNative() = default;
    ~ScopedNative() { _token.expire(); }
    ScopedNative(const ScopedNative&) = delete;
    ScopedNative& operator=(const ScopedNative&) = delete;
    const LifetimeToken& token() const { return _token; }
private:
    LifetimeToken _token;
};

struct RuntimeState
{
    ax::JsEngine* engine{};
    JSContext* context{};
    void* nativeRegistry{};
    uint64_t generation{};
    std::thread::id owner;
    std::atomic<bool> alive{false};
    std::mutex queueMutex;
    std::vector<std::function<void()>> releaseQueue;
    bool onOwnerThread() const { return owner == std::this_thread::get_id(); }
    /** All JS root operations execute on the VM owner thread. */
    void enqueue(std::function<void()> task);
    void drain();
};
std::shared_ptr<RuntimeState> runtimeState(JSContext* cx);
ax::JsEngine* engine(JSContext* cx);
/** Transfer the canonical Object binding to a pre-existing JS subclass
 * receiver. The ordinary receiver is rooted until native invalidation. */
bool associateObjectWithNative(JSContext*, unsigned argc, JS::Value* vp);

namespace detail
{
using CastFunction = void* (*)(void*);
bool registerTypeInfo(JSContext*, std::type_index type, std::type_index base, CastFunction,
                      JS::HandleObject prototype, JSClass* jsClass);
JSObject* getPrototypeInfo(JSContext*, std::type_index);
JSObject* getClassObjectInfo(JSContext*, std::type_index);
JSClass* getClassInfo(JSContext*, std::type_index);
void setStaticLifetimeInfo(JSContext*, std::type_index);
bool bindNativeInfo(JSContext*, JS::HandleObject wrapper, void* pointer, ax::Object* object,
                    std::type_index type, NativeOwnership, const LifetimeToken*, std::function<void(void*)> deleter);
bool rebindNativeInfo(JSContext*, JS::HandleObject wrapper, void* pointer, ax::Object* object, std::type_index type);
bool wrapNativeInfo(JSContext*, void* pointer, ax::Object* object, std::type_index type,
                    NativeOwnership, const LifetimeToken*, JS::MutableHandleValue);
void* unwrapNativeInfo(JSContext*, JS::HandleValue, std::type_index type, bool reportInvalid);
JSObject* findNativeObject(JSContext*, const void* pointer);
// Exact non-Object key only; does not resolve base types or create/root peers.
JSObject* findNativeValueObject(JSContext*, const void* pointer, std::type_index exactType);
/** Ephemeron edge also supports ordinary JS receivers without reserved slots. */
bool setOwnerDependency(JSContext*, JS::HandleObject owner, JS::HandleValue dependency);
JSObject* getNativePrototype(JSContext*, JS::HandleValue wrapper);
bool isNativeValid(JSContext*, JS::HandleValue);
void invalidateNative(JSContext*, const void* pointer);
void invalidateExpiredNative(JSContext*, const void* pointer);
ax::Object* nativeObjectBase(JSContext*, JS::HandleValue);
bool getNativeLifetime(JSContext*, JS::HandleValue, LifetimeToken&);
bool createRegistry(JSContext*);
void destroyRegistry(JSContext*);
void releaseCallbackRoots(JSContext*);
void collectExpiredNativeHandles(JSContext*);
bool stringFromValue(JSContext*, JS::HandleValue, std::string&);
bool stringToValue(JSContext*, std::string_view, JS::MutableHandleValue);
bool readProperty(JSContext*, JS::HandleValue, const char* name, JS::MutableHandleValue);
}

void finalizeNative(JSFreeOp*, JSObject*);
bool getOrCreateNamespace(JSContext*, JS::HandleObject global, const char* name, JS::MutableHandleObject);
/** Preserve a JS subclass on native clone/reverse results of the same actual
 * registered C++ type; the native method remains responsible for its state. */
bool inheritNativeSubclass(JSContext*, JS::HandleValue source, JS::HandleValue result);

template<class T, class Base = void>
bool registerType(JSContext* cx, JS::HandleObject prototype, JSClass* jsClass)
{
    if constexpr (std::is_void_v<Base>)
        return detail::registerTypeInfo(cx, typeid(T), typeid(void), nullptr, prototype, jsClass);
    else
    {
        static_assert(std::is_base_of_v<Base, T>);
        return detail::registerTypeInfo(cx, typeid(T), typeid(Base),
            [](void* p) -> void* { return static_cast<Base*>(static_cast<T*>(p)); }, prototype, jsClass);
    }
}
template<class T> JSObject* getPrototype(JSContext* cx) { return detail::getPrototypeInfo(cx, typeid(T)); }
template<class T> JSObject* getClassObject(JSContext* cx) { return detail::getClassObjectInfo(cx, typeid(T)); }
template<class T> void setStaticLifetime(JSContext* cx) { detail::setStaticLifetimeInfo(cx, typeid(T)); }

template<class T>
bool bindNative(JSContext* cx, JS::HandleObject wrapper, T* pointer,
                NativeOwnership ownership = NativeOwnership::Borrowed, const LifetimeToken* token = nullptr)
{
    using U = std::remove_const_t<T>;
    ax::Object* object = nullptr;
    if constexpr (std::is_base_of_v<ax::Object, U>) object = const_cast<U*>(pointer);
    std::function<void(void*)> deleter;
    if constexpr (!std::is_base_of_v<ax::Object, U> && std::is_destructible_v<U>)
        if (ownership == NativeOwnership::Constructor) deleter = [](void* p) { delete static_cast<U*>(p); };
    return detail::bindNativeInfo(cx, wrapper, const_cast<U*>(pointer), object, typeid(U), ownership, token, std::move(deleter));
}
template<class T>
bool wrapNative(JSContext* cx, T* pointer, JS::MutableHandleValue result,
                NativeOwnership ownership = NativeOwnership::Borrowed)
{
    using U = std::remove_const_t<T>;
    if (!pointer) { result.setNull(); return true; }
    ax::Object* object = nullptr;
    if constexpr (std::is_base_of_v<ax::Object, U>) object = const_cast<U*>(pointer);
    auto type = std::type_index(typeid(U));
    if constexpr (std::is_polymorphic_v<U>)
        if (detail::getClassInfo(cx, typeid(*pointer))) type = typeid(*pointer);
    // Dynamic type lookup must use its complete-object address, even with
    // multiple inheritance and an adjusted base pointer.
    void* raw = const_cast<U*>(pointer);
    if constexpr (std::is_polymorphic_v<U>)
        if (type != typeid(U)) raw = dynamic_cast<void*>(const_cast<U*>(pointer));
    return detail::wrapNativeInfo(cx, raw, object, type, ownership, nullptr, result);
}
/** Rebind a legacy fluent action to its new outer native allocation while
 * preserving the original JS object and a checked peer for the inner object.
 * The caller creates/initializes/autoreleases the outer native action once. */
template<class T> bool rebindNative(JSContext* cx,JS::HandleObject wrapper,T* outer)
{
    using U=std::remove_const_t<T>;
    static_assert(std::is_base_of_v<ax::Object,U>);
    if(!outer) { JS_ReportError(cx,"Cannot rebind to a null native object"); return false; }
    auto type=std::type_index(typeid(U));
    void* pointer=const_cast<U*>(outer);
    if constexpr(std::is_polymorphic_v<U>)
        if(detail::getClassInfo(cx,typeid(*outer)))
        { type=typeid(*outer); pointer=dynamic_cast<void*>(const_cast<U*>(outer)); }
    return detail::rebindNativeInfo(cx,wrapper,pointer,const_cast<U*>(outer),type);
}
template<class T>
bool wrapBorrowed(JSContext* cx, T* pointer, JS::MutableHandleValue result, const LifetimeToken& token)
{
    using U = std::remove_const_t<T>;
    ax::Object* object = nullptr;
    if constexpr (std::is_base_of_v<ax::Object, U>) object = const_cast<U*>(pointer);
    return detail::wrapNativeInfo(cx, const_cast<U*>(pointer), object, typeid(U), NativeOwnership::Borrowed, &token, result);
}
template<class T>
bool toNative(JSContext* cx, JS::HandleValue value, T** result)
{
    *result = nullptr;
    if (value.isNullOrUndefined()) return true;
    using U = std::remove_const_t<T>;
    *result = static_cast<T*>(detail::unwrapNativeInfo(cx, value, typeid(U), true));
    if constexpr(std::is_class_v<U> && std::is_polymorphic_v<U>)
        if(!*result) if(auto* base=detail::nativeObjectBase(cx,value)) *result=dynamic_cast<T*>(base);
    return *result != nullptr;
}
template<class T> bool fromNative(JSContext* cx, JS::MutableHandleValue result, T* pointer)
{ return wrapNative(cx, pointer, result); }

namespace tests
{
struct CallbackRootsWatch;
using CallbackRootsWatchPtr = std::shared_ptr<CallbackRootsWatch>;
struct CallbackRootsSnapshot
{
    uint64_t generation{};
    bool functionRootCaptured{}, receiverRootCaptured{};
    uint32_t functionRootReleaseCount{}, receiverRootReleaseCount{};
    bool ownerBoundCaptured{};
    uint32_t ownerBoundReleaseCount{};
};
/** Reads only immutable/atomic scalar facts; safe after the VM has stopped. */
bool snapshotCallbackRoots(const CallbackRootsWatchPtr&, CallbackRootsSnapshot&);
}

class Callback
{
public:
    static std::shared_ptr<Callback> create(JSContext*, JS::HandleValue function, JS::HandleObject thisObject);
    static std::shared_ptr<Callback> create(JSContext*, JS::HandleValue function);
    /** The caller must store record in its owner's traced JS graph before
     * releasing this rooted output. C++ holds only a weak reference to record. */
    static std::shared_ptr<Callback> createOwnerBound(JSContext*, JS::HandleValue function,
                                                    JS::HandleObject receiver, JS::MutableHandleValue record);
    ~Callback();
    bool isValid() const;
    /** Pin before native argument conversion, which can allocate/collect. */
    bool pinOwnerRecord(JSContext*, JS::MutableHandleObject) const;
    bool call(const JS::HandleValueArray&, JS::MutableHandleValue);
    bool call(const JS::HandleValueArray&);
    void cancel();
    std::shared_ptr<RuntimeState> state() const;
    /** Test-only weak observation; does not extend Callback/Roots/JS lifetime.
     * Capture requires this callback's live generation and owner thread. */
    tests::CallbackRootsWatchPtr watchRootsForTests() const;
private:
    struct Roots;
    static std::shared_ptr<Roots> makeRoots(JSContext*);
    explicit Callback(std::shared_ptr<Roots> roots) : _roots(std::move(roots)) {}
    std::shared_ptr<Roots> _roots;
};

template<class T, class Enable = void> struct Converter;
template<class T> bool fromJsValue(JSContext* cx, JS::HandleValue value, T& result)
{ return Converter<std::remove_cvref_t<T>>::read(cx, value, result); }
template<class T> bool toJsValue(JSContext* cx, const T& value, JS::MutableHandleValue result)
{ return Converter<std::remove_cvref_t<T>>::write(cx, value, result); }
inline bool toJsValue(JSContext*, std::nullptr_t, JS::MutableHandleValue result) { result.setNull(); return true; }
inline bool toJsValue(JSContext* cx, const char* value, JS::MutableHandleValue result)
{ return value ? detail::stringToValue(cx, value, result) : (result.setNull(), true); }
template<size_t N> bool toJsValue(JSContext* cx, const char (&value)[N], JS::MutableHandleValue result)
{ return detail::stringToValue(cx, std::string_view(value, N-1), result); }

template<class T>
struct Converter<T, std::enable_if_t<std::is_arithmetic_v<T>>>
{
    static bool read(JSContext*, JS::HandleValue value, T& result)
    {
        // The v3 jsval_to_boolean contract accepts JS truthiness, including
        // numeric flags in MenuItem.setEnabled and Sprite.setFlippedY.
        if constexpr (std::is_same_v<T, bool>) result = JS::ToBoolean(value);
        else
        {
            if (!value.isNumber()) return false;
            const double number = value.toNumber();
            if constexpr (std::is_integral_v<T>)
            {
                // Old JSB byte parameters (notably opacity) truncate finite
                // fractional values. Wider integers keep their exact checks.
                if constexpr(std::is_same_v<T,unsigned char>)
                {
                    if(!std::isfinite(number)) return false;
                    const double truncated=std::trunc(number);
                    if(truncated<0||truncated>255) return false;
                    result=static_cast<T>(truncated);
                    return true;
                }
                // JS numbers represent integer values exactly only through 2^53.
                // Reject lossy 64-bit identifiers rather than silently truncate.
                if (!std::isfinite(number) || std::trunc(number) != number ||
                    static_cast<long double>(number) < static_cast<long double>(std::numeric_limits<T>::lowest()) ||
                    static_cast<long double>(number) > static_cast<long double>(std::numeric_limits<T>::max()) ||
                    (sizeof(T) > 4 && std::abs(number) > 9007199254740991.0)) return false;
            }
            result = static_cast<T>(number);
        }
        return true;
    }
    static bool write(JSContext*, T value, JS::MutableHandleValue result)
    {
        if constexpr (std::is_same_v<T, bool>) result.setBoolean(value);
        else result.set(nativeNumberValue(static_cast<double>(value)));
        return true;
    }
};
template<class T>
struct Converter<T, std::enable_if_t<std::is_enum_v<T>>>
{
    static bool read(JSContext* cx, JS::HandleValue value, T& result)
    { std::underlying_type_t<T> n{}; if (!fromJsValue(cx,value,n)) return false; result = static_cast<T>(n); return true; }
    static bool write(JSContext* cx, T value, JS::MutableHandleValue result)
    { return toJsValue(cx,static_cast<std::underlying_type_t<T>>(value),result); }
};
template<> struct Converter<std::string>
{
    static bool read(JSContext* cx, JS::HandleValue value, std::string& result)
    { return detail::stringFromValue(cx,value,result); }
    static bool write(JSContext* cx, const std::string& value, JS::MutableHandleValue result)
    { return detail::stringToValue(cx,value,result); }
};
template<> struct Converter<std::string_view>
{
    // Reading a string_view is deliberately only available through Argument,
    // which owns the backing string through the complete native call.
    static bool write(JSContext* cx, std::string_view value, JS::MutableHandleValue result)
    { return detail::stringToValue(cx,value,result); }
};
template<class T> struct Converter<T*, std::enable_if_t<!std::is_same_v<std::remove_cv_t<T>,char>>>
{
    static bool read(JSContext* cx, JS::HandleValue value, T*& result)
    {
        result=nullptr; if(value.isNullOrUndefined()) return true;
        using U=std::remove_const_t<T>;
        result=static_cast<T*>(detail::unwrapNativeInfo(cx,value,typeid(U),false));
        if constexpr(std::is_class_v<U> && std::is_polymorphic_v<U>)
            if(!result) if(auto* base=detail::nativeObjectBase(cx,value)) result=dynamic_cast<T*>(base);
        return result!=nullptr;
    }
    static bool write(JSContext* cx, T* value, JS::MutableHandleValue result) { return wrapNative(cx,value,result); }
};

#define AXJS_DECLARE_CONVERTER(Type) \
    template<> struct Converter<Type> { \
        static bool read(JSContext*,JS::HandleValue,Type&); \
        static bool write(JSContext*,const Type&,JS::MutableHandleValue); };
AXJS_DECLARE_CONVERTER(ax::Vec2)
AXJS_DECLARE_CONVERTER(ax::Vec3)
AXJS_DECLARE_CONVERTER(ax::Vec4)
AXJS_DECLARE_CONVERTER(ax::Rect)
AXJS_DECLARE_CONVERTER(ax::Color32)
AXJS_DECLARE_CONVERTER(ax::Color)
AXJS_DECLARE_CONVERTER(ax::BlendFunc)
AXJS_DECLARE_CONVERTER(ax::rhi::BlendFactor)
AXJS_DECLARE_CONVERTER(ax::AffineTransform)
AXJS_DECLARE_CONVERTER(ax::Mat4)
AXJS_DECLARE_CONVERTER(ax::Quat)
AXJS_DECLARE_CONVERTER(ax::AABB)
AXJS_DECLARE_CONVERTER(ax::OBB)
AXJS_DECLARE_CONVERTER(ax::Ray)
AXJS_DECLARE_CONVERTER(ax::Viewport)
AXJS_DECLARE_CONVERTER(ax::PhysicsMaterial2D)
#if defined(AX_ENABLE_3D)
AXJS_DECLARE_CONVERTER(ax::Terrain::DetailMap)
AXJS_DECLARE_CONVERTER(ax::Terrain::TerrainData)
#endif
#if defined(AX_ENABLE_NAVMESH)
AXJS_DECLARE_CONVERTER(ax::NavMeshAgentParam)
AXJS_DECLARE_CONVERTER(ax::OffMeshLinkData)
#endif
#if defined(AX_ENABLE_PHYSICS_3D)
AXJS_DECLARE_CONVERTER(ax::PhysicsMaterial)
#endif
AXJS_DECLARE_CONVERTER(ax::Value)
#undef AXJS_DECLARE_CONVERTER

template<class T>
class Argument
{
    using Value = std::remove_cvref_t<T>;
    Value _value{};
public:
    static constexpr bool needsReceiver = requires(JSContext* cx, JS::HandleValue v, Value& out, JS::HandleObject r) { Converter<Value>::read(cx,v,out,r); };
    bool read(JSContext* cx, JS::HandleValue v) { return fromJsValue(cx,v,_value); }
    bool read(JSContext* cx, JS::HandleValue v, JS::HandleObject receiver)
    {
        if constexpr(needsReceiver)
            return Converter<Value>::read(cx,v,_value,receiver);
        else return fromJsValue(cx,v,_value);
    }
    decltype(auto) get()
    { if constexpr (std::is_reference_v<T>) return static_cast<T>(_value); else return _value; }
};
/** A JS array is copied before entry into native code; its backing storage
 * remains owned by the argument throughout the complete native invocation. */
template<class T,size_t Extent> class Argument<std::span<T,Extent>>
{
    std::vector<std::remove_const_t<T>> _storage;
public:
    static constexpr bool needsReceiver = false;
    bool read(JSContext* cx,JS::HandleValue value)
    {
        if(!fromJsValue(cx,value,_storage)) return false;
        return Extent==std::dynamic_extent || _storage.size()==Extent;
    }
    bool read(JSContext* cx,JS::HandleValue value,JS::HandleObject) { return read(cx,value); }
    std::span<T,Extent> get() { return std::span<T,Extent>(_storage.data(),_storage.size()); }
};
template<class T,size_t Extent> class Argument<const std::span<T,Extent>&> : public Argument<std::span<T,Extent>> {};
template<> class Argument<std::string_view>
{
    std::string _value;
public:
    static constexpr bool needsReceiver = false;
    bool read(JSContext* cx,JS::HandleValue v) { return detail::stringFromValue(cx,v,_value); }
    bool read(JSContext* cx,JS::HandleValue v,JS::HandleObject) { return read(cx,v); }
    std::string_view get() const { return _value; }
};
template<> class Argument<const std::string_view&> : public Argument<std::string_view> {};
template<> class Argument<const char*>
{
    std::string _value;
    bool _null{};
public:
    static constexpr bool needsReceiver = false;
    bool read(JSContext* cx,JS::HandleValue v) { _null=v.isNullOrUndefined(); return _null || detail::stringFromValue(cx,v,_value); }
    bool read(JSContext* cx,JS::HandleValue v,JS::HandleObject) { return read(cx,v); }
    const char* get() const { return _null ? nullptr : _value.c_str(); }
};

namespace detail
{
template<class Container> struct SequenceElement { using type = typename Container::value_type; };
template<class T> struct SequenceElement<ax::Vector<T>> { using type = T; };
template<class Container>
bool readSequence(JSContext* cx,JS::HandleValue v,Container& out)
{
    if (!v.isObject()) return false;
    JS::RootedObject array(cx,&v.toObject());
    if (!JS_IsArrayObject(cx,array)) return false;
    uint32_t length{};
    if (!JS_GetArrayLength(cx,array,&length)) return false;
    Container temporary;
    for(uint32_t i=0;i<length;++i)
    {
        JS::RootedValue item(cx);
        typename SequenceElement<Container>::type converted{};
        if (!JS_GetElement(cx,array,i,&item) || !fromJsValue(cx,item,converted)) return false;
        if constexpr (requires { temporary.push_back(converted); }) temporary.push_back(std::move(converted));
        else if constexpr (requires { temporary.pushBack(converted); }) temporary.pushBack(std::move(converted));
        else temporary.insert(std::move(converted));
    }
    out = std::move(temporary); return true;
}
template<class Container>
bool writeSequence(JSContext* cx,const Container& value,JS::MutableHandleValue out)
{
    JS::RootedObject array(cx,JS_NewArrayObject(cx,0));
    if (!array) return false;
    uint32_t index=0;
    for (const auto& item:value)
    {
        JS::RootedValue converted(cx);
        if (!toJsValue(cx,item,&converted) || !JS_SetElement(cx,array,index++,converted)) return false;
    }
    out.setObject(*array); return true;
}
template<class Container>
bool readMap(JSContext* cx,JS::HandleValue value,Container& out)
{
    if (!value.isObject()) return false;
    JS::RootedObject obj(cx,&value.toObject());
    JS::AutoIdArray ids(cx,JS_Enumerate(cx,obj));
    if (!ids) return false;
    Container temporary;
    for(size_t i=0;i<ids.length();++i)
    {
        JS::RootedId id(cx,ids[i]);
        JS::RootedValue key(cx),item(cx);
        if (!JS_IdToValue(cx,id,&key) || !JS_GetPropertyById(cx,obj,id,&item)) return false;
        using Key=typename Container::key_type;
        using Item=typename Container::mapped_type;
        Key k{}; Item v{};
        if constexpr (std::is_same_v<Key,std::string>)
        {
            JS::RootedString text(cx,JS::ToString(cx,key));
            if (!text) return false;
            key.setString(text);
        }
        if constexpr(std::is_integral_v<Key>)
        {
            if(key.isString())
            {
                std::string text;
                if(!stringFromValue(cx,key,text)) return false;
                auto parsed=std::from_chars(text.data(),text.data()+text.size(),k);
                if(parsed.ec!=std::errc() || parsed.ptr!=text.data()+text.size()) return false;
            }
            else if(!fromJsValue(cx,key,k)) return false;
        }
        else if(!fromJsValue(cx,key,k)) return false;
        if(!fromJsValue(cx,item,v)) return false;
        if constexpr(requires { temporary.emplace(k,v); }) temporary.emplace(std::move(k),std::move(v));
        else temporary.insert(k,v);
    }
    out=std::move(temporary); return true;
}
template<class Container>
bool writeMap(JSContext* cx,const Container& value,JS::MutableHandleValue out)
{
    JS::RootedObject obj(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
    if (!obj) return false;
    for(const auto& [key,item]:value)
    {
        JS::RootedValue converted(cx);
        if (!toJsValue(cx,item,&converted)) return false;
        std::string name;
        if constexpr(std::is_integral_v<std::remove_cvref_t<decltype(key)>>) name=std::to_string(key);
        else name=std::string(key);
        if (!JS_SetProperty(cx,obj,name.c_str(),converted)) return false;
    }
    out.setObject(*obj); return true;
}
}

#define AXJS_SEQUENCE_CONVERTER(Template) \
    template<class T,class... Rest> struct Converter<Template<T,Rest...>> { \
        using C=Template<T,Rest...>; \
        static bool read(JSContext* cx,JS::HandleValue v,C& out) { return detail::readSequence(cx,v,out); } \
        static bool write(JSContext* cx,const C& v,JS::MutableHandleValue out) { return detail::writeSequence(cx,v,out); } };
AXJS_SEQUENCE_CONVERTER(std::vector)
AXJS_SEQUENCE_CONVERTER(std::set)
AXJS_SEQUENCE_CONVERTER(std::unordered_set)
#undef AXJS_SEQUENCE_CONVERTER
template<class T,size_t N> struct Converter<std::array<T,N>>
{
    static bool read(JSContext* cx,JS::HandleValue value,std::array<T,N>& result)
    {
        std::vector<T> temporary;
        if(!detail::readSequence(cx,value,temporary) || temporary.size()!=N) return false;
        std::move(temporary.begin(),temporary.end(),result.begin());
        return true;
    }
    static bool write(JSContext* cx,const std::array<T,N>& value,JS::MutableHandleValue result)
    { return detail::writeSequence(cx,value,result); }
};
template<class T,size_t Extent> struct Converter<std::span<T,Extent>>
{
    // Read is deliberately available only through Argument, which owns storage.
    static bool write(JSContext* cx,std::span<T,Extent> value,JS::MutableHandleValue result)
    { return detail::writeSequence(cx,value,result); }
};
template<class T> struct Converter<ax::Vector<T>>
{
    using C=ax::Vector<T>;
    static bool read(JSContext* cx,JS::HandleValue v,C& out) { return detail::readSequence(cx,v,out); }
    static bool write(JSContext* cx,const C& v,JS::MutableHandleValue out) { return detail::writeSequence(cx,v,out); }
};
#define AXJS_MAP_CONVERTER(Template) \
    template<class K,class V,class... Rest> struct Converter<Template<K,V,Rest...>> { \
        using C=Template<K,V,Rest...>; \
        static bool read(JSContext* cx,JS::HandleValue v,C& out) { return detail::readMap(cx,v,out); } \
        static bool write(JSContext* cx,const C& v,JS::MutableHandleValue out) { return detail::writeMap(cx,v,out); } };
AXJS_MAP_CONVERTER(std::map)
AXJS_MAP_CONVERTER(std::unordered_map)
AXJS_MAP_CONVERTER(ax::Map)
#undef AXJS_MAP_CONVERTER

template<class K,class V,class H,class E,class A,bool Store,class G>
struct Converter<tsl::robin_map<K,V,H,E,A,Store,G>>
{
    using C=tsl::robin_map<K,V,H,E,A,Store,G>;
    static bool read(JSContext* cx,JS::HandleValue v,C& out) { return detail::readMap(cx,v,out); }
    static bool write(JSContext* cx,const C& v,JS::MutableHandleValue out) { return detail::writeMap(cx,v,out); }
};

template<class A,class B> struct Converter<std::pair<A,B>>
{
    static bool read(JSContext* cx,JS::HandleValue v,std::pair<A,B>& out)
    {
        if(!v.isObject()) return false;
        JS::RootedObject obj(cx,&v.toObject()); JS::RootedValue a(cx),b(cx);
        return JS_GetElement(cx,obj,0,&a) && JS_GetElement(cx,obj,1,&b) &&
            fromJsValue(cx,a,out.first) && fromJsValue(cx,b,out.second);
    }
    static bool write(JSContext* cx,const std::pair<A,B>& v,JS::MutableHandleValue out)
    {
        JS::RootedObject obj(cx,JS_NewArrayObject(cx,0)); JS::RootedValue a(cx),b(cx);
        if(!obj || !toJsValue(cx,v.first,&a) || !toJsValue(cx,v.second,&b) ||
            !JS_SetElement(cx,obj,0,a) || !JS_SetElement(cx,obj,1,b)) return false;
        out.setObject(*obj); return true;
    }
};

/** Non-Object child pointers inherit the receiver's checked native lifetime.
 * This conversion is also applied recursively to containers returned by native
 * getters. Static functions require an explicit static-lifetime declaration. */
template<class T> bool toJsValueWithOwner(JSContext* cx,const T& value,JS::MutableHandleValue result,
                                        JS::HandleValue receiver)
{
    using U=std::remove_cvref_t<T>;
    if constexpr(std::is_pointer_v<U> && !std::is_void_v<std::remove_pointer_t<U>> &&
                 !std::is_same_v<std::remove_cv_t<std::remove_pointer_t<U>>,char> &&
                 !std::is_base_of_v<ax::Object,std::remove_cv_t<std::remove_pointer_t<U>>>)
    {
        if(!value) { result.setNull(); return true; }
        LifetimeToken token;
        if(detail::getNativeLifetime(cx,receiver,token)) return wrapBorrowed(cx,value,result,token);
        return wrapNative(cx,value,result);
    }
    else if constexpr(requires { typename U::mapped_type; typename U::key_type; value.begin(); value.end(); })
    {
        JS::RootedObject object(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
        if(!object) return false;
        for(const auto& [key,item]:value)
        {
            JS::RootedValue converted(cx);
            if(!toJsValueWithOwner(cx,item,&converted,receiver)) return false;
            std::string name;
            if constexpr(std::is_integral_v<std::remove_cvref_t<decltype(key)>>) name=std::to_string(key);
            else name=std::string(key);
            if(!JS_SetProperty(cx,object,name.c_str(),converted)) return false;
        }
        result.setObject(*object); return true;
    }
    else if constexpr(requires { typename U::value_type; value.begin(); value.end(); } &&
                      !std::is_same_v<U,std::string> && !std::is_same_v<U,std::string_view>)
    {
        JS::RootedObject array(cx,JS_NewArrayObject(cx,0));
        if(!array) return false;
        uint32_t index=0;
        for(const auto& item:value)
        {
            JS::RootedValue converted(cx);
            if(!toJsValueWithOwner(cx,item,&converted,receiver) || !JS_SetElement(cx,array,index++,converted)) return false;
        }
        result.setObject(*array); return true;
    }
    else if constexpr(requires { value.first; value.second; })
    {
        JS::RootedObject array(cx,JS_NewArrayObject(cx,0));
        JS::RootedValue first(cx),second(cx);
        if(!array || !toJsValueWithOwner(cx,value.first,&first,receiver) ||
            !toJsValueWithOwner(cx,value.second,&second,receiver) ||
            !JS_SetElement(cx,array,0,first) || !JS_SetElement(cx,array,1,second)) return false;
        result.setObject(*array); return true;
    }
    else return toJsValue(cx,value,result);
}

namespace detail
{
template<class R,class... Args>
R callCallback(const std::shared_ptr<Callback>& callback,Args&&... args)
{
    auto state=callback->state();
    if(!state || !state->alive || !state->onOwnerThread())
    {
        if constexpr(!std::is_void_v<R>) return R{};
        else return;
    }
    JSContext* cx=state->context;
    JSAutoCompartment compartment(cx,state->engine->getGlobalObject());
    JS::AutoValueVector values(cx);
    ScopedNative scope;
    bool ok=true;
    auto append=[&](auto&& item) {
        JS::RootedValue v(cx);
        using T=std::remove_cvref_t<decltype(item)>;
        if constexpr(std::is_pointer_v<T> && !std::is_same_v<std::remove_cv_t<std::remove_pointer_t<T>>,char> &&
                     !std::is_void_v<std::remove_pointer_t<T>>)
        {
            // Event values, including ax::Event objects, are ephemeral even when
            // their native type participates in reference counting.
            if constexpr(std::is_base_of_v<ax::Event,std::remove_pointer_t<T>>)
                ok=ok && wrapBorrowed(cx,item,&v,scope.token());
            else if constexpr(!std::is_base_of_v<ax::Object,std::remove_pointer_t<T>>)
                ok=ok && wrapBorrowed(cx,item,&v,scope.token());
            else ok=ok && toJsValue(cx,item,&v);
        }
        else ok=ok && toJsValue(cx,item,&v);
        ok=ok && values.append(v);
    };
    (append(std::forward<Args>(args)),...);
    JS::RootedValue result(cx);
    if(ok) ok=callback->call(JS::HandleValueArray(values),&result);
    if constexpr(!std::is_void_v<R>) { R value{}; if(ok) fromJsValue(cx,result,value); return value; }
}
}

template<class R,class... Args> struct Converter<std::function<R(Args...)>>
{
    static bool read(JSContext* cx,JS::HandleValue v,std::function<R(Args...)>& out)
    {
        if(v.isNullOrUndefined()) { out={}; return true; }
        JS::RootedObject global(cx,engine(cx)->getGlobalObject());
        return read(cx,v,out,global);
    }
    static bool read(JSContext* cx,JS::HandleValue v,std::function<R(Args...)>& out,JS::HandleObject receiver)
    {
        if(v.isNullOrUndefined()) { out={}; return true; }
        auto callback=Callback::create(cx,v,receiver);
        if(!callback) return false;
        out=[callback](Args... args)->R { return detail::callCallback<R>(callback,std::forward<Args>(args)...); };
        return true;
    }
};

namespace detail
{
template<class F> struct FunctionTraits;
template<class R,class... A> struct FunctionTraits<R(*)(A...)> { using Return=R; using Arguments=std::tuple<Argument<A>...>; };
template<class R,class T,class... A> struct FunctionTraits<R(T::*)(A...)> : FunctionTraits<R(*)(A...)> { using Class=T; };
template<class R,class T,class... A> struct FunctionTraits<R(T::*)(A...) const> : FunctionTraits<R(T::*)(A...)> {};
template<class R,class T,class... A> struct FunctionTraits<R(T::*)(A...) noexcept> : FunctionTraits<R(T::*)(A...)> {};
template<class R,class T,class... A> struct FunctionTraits<R(T::*)(A...) const noexcept> : FunctionTraits<R(T::*)(A...)> {};
template<class R,class... A> struct FunctionTraits<R(*)(A...) noexcept> : FunctionTraits<R(*)(A...)> {};
template<class Tuple,size_t... I>
bool readArguments(JSContext* cx,JS::CallArgs& args,Tuple& values,std::index_sequence<I...>)
{
    if constexpr(sizeof...(I)==0) return true;
    else if constexpr((std::tuple_element_t<I,Tuple>::needsReceiver || ...))
    {
        JS::RootedObject receiver(cx,args.thisv().isObject()?&args.thisv().toObject():engine(cx)->getGlobalObject());
        return (std::get<I>(values).read(cx,args[I],receiver) && ...);
    }
    else return (std::get<I>(values).read(cx,args[I]) && ...);
}
}

inline bool toJsPoint(JSContext* cx,const ax::Vec2& v,JS::MutableHandleValue out)
{
    JS::RootedObject obj(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
    if(!obj) return false;
    JS::RootedValue x(cx,nativeNumberValue(v.x)),y(cx,nativeNumberValue(v.y));
    if(!JS_DefineProperty(cx,obj,"x",x,JSPROP_ENUMERATE) ||
        !JS_DefineProperty(cx,obj,"y",y,JSPROP_ENUMERATE)) return false;
    out.setObject(*obj); return true;
}

template<auto Function> bool invokePoint(JSContext* cx,JS::CallArgs& args)
{
    using F=detail::FunctionTraits<decltype(Function)>;
    typename F::Arguments arguments;
    constexpr auto count=std::tuple_size_v<typename F::Arguments>;
    if(args.length()!=count || !detail::readArguments(cx,args,arguments,std::make_index_sequence<count>{})) return false;
    if constexpr(std::is_member_function_pointer_v<decltype(Function)>)
    {
        typename F::Class* native=nullptr;
        if(!toNative(cx,args.thisv(),&native) || !native)
        {
            if(!JS_IsExceptionPending(cx)) JS_ReportError(cx,"Invalid native receiver");
            return false;
        }
        try
        {
            decltype(auto) value=std::apply([&](auto&... a)->decltype(auto) { return std::invoke(Function,native,a.get()...); },arguments);
            return toJsPoint(cx,value,args.rval());
        }
        catch(const std::exception& error) { if(!JS_IsExceptionPending(cx)) JS_ReportError(cx,"%s",error.what()); return false; }
    }
    else
    {
        try
        {
            decltype(auto) value=std::apply([&](auto&... a)->decltype(auto) { return std::invoke(Function,a.get()...); },arguments);
            return toJsPoint(cx,value,args.rval());
        }
        catch(const std::exception& error) { if(!JS_IsExceptionPending(cx)) JS_ReportError(cx,"%s",error.what()); return false; }
    }
}

template<auto Function> bool invoke(JSContext* cx,JS::CallArgs& args)
{
    using F=detail::FunctionTraits<decltype(Function)>;
    typename F::Arguments arguments;
    constexpr auto count=std::tuple_size_v<typename F::Arguments>;
    if(args.length()!=count || !detail::readArguments(cx,args,arguments,std::make_index_sequence<count>{})) return false;
    if constexpr(std::is_member_function_pointer_v<decltype(Function)>)
    {
        typename F::Class* native=nullptr;
        if(!toNative(cx,args.thisv(),&native) || !native)
        {
            if(!JS_IsExceptionPending(cx)) JS_ReportError(cx,"Invalid native receiver");
            return false;
        }
        auto call=[&](auto&... a)->decltype(auto) { return std::invoke(Function,native,a.get()...); };
        try
        {
            if constexpr(std::is_void_v<typename F::Return>) { std::apply(call,arguments); args.rval().setUndefined(); return true; }
            else { decltype(auto) value=std::apply(call,arguments); return toJsValueWithOwner(cx,value,args.rval(),args.thisv()); }
        }
        catch(const std::exception& error) { if(!JS_IsExceptionPending(cx)) JS_ReportError(cx,"%s",error.what()); return false; }
    }
    else
    {
        auto call=[&](auto&... a)->decltype(auto) { return std::invoke(Function,a.get()...); };
        try
        {
            if constexpr(std::is_void_v<typename F::Return>) { std::apply(call,arguments); args.rval().setUndefined(); return true; }
            else { decltype(auto) value=std::apply(call,arguments); return toJsValueWithOwner(cx,value,args.rval(),args.thisv()); }
        }
        catch(const std::exception& error) { if(!JS_IsExceptionPending(cx)) JS_ReportError(cx,"%s",error.what()); return false; }
    }
}

template<class T,class... A> bool construct(JSContext* cx,JS::CallArgs& args,A&&... arguments)
{
    JSClass* clasp=detail::getClassInfo(cx,typeid(T));
    if(!clasp) { JS_ReportError(cx,"Native type is not registered"); return false; }
    JS::RootedObject wrapper(cx,JS_NewObjectForConstructor(cx,clasp,args));
    if(!wrapper) return false;
    T* native=nullptr;
    try { native=new T(std::forward<A>(arguments)...); }
    catch(const std::exception& error) { JS_ReportError(cx,"%s",error.what()); return false; }
    if constexpr(sizeof...(A)==0 && requires(T* p) { { p->init() } -> std::convertible_to<bool>; })
        if(!native->init()) { delete native; JS_ReportError(cx,"Native initialization failed"); return false; }
    if(!bindNative(cx,wrapper,native,NativeOwnership::Constructor)) { delete native; return false; }
    args.rval().setObject(*wrapper); return true;
}
/** Internal allocation for legacy native .extend() constructors. The JS ctor
 * initializes this same allocation exactly once with its actual arguments. */
template<class T> bool constructUninitialized(JSContext* cx,JS::CallArgs& args)
{
    static_assert(std::is_default_constructible_v<T>);
    if(args.length()!=0) { JS_ReportError(cx,"Native allocation requires zero arguments"); return false; }
    JSClass* clasp=detail::getClassInfo(cx,typeid(T));
    JS::RootedObject prototype(cx,getPrototype<T>(cx));
    if(!clasp || !prototype) { JS_ReportError(cx,"Native type is not registered"); return false; }
    JS::RootedObject wrapper(cx,JS_NewObject(cx,clasp,prototype,JS::NullPtr()));
    if(!wrapper) return false;
    T* native=nullptr;
    try { native=new T; }
    catch(const std::exception& error) { JS_ReportError(cx,"%s",error.what()); return false; }
    if(!bindNative(cx,wrapper,native,NativeOwnership::Constructor)) { delete native; return false; }
    args.rval().setObject(*wrapper); return true;
}
} // namespace axjs
