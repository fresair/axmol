/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/Bindings.h"
#include "manual/ManualUtils.h"
#include "manual/LegacyActionDurationBindings.h"
#include "axmol/platform/RenderViewCore.h"
#include "axmol/platform/Application.h"
#include "axmol/base/UserDefault.h"
#include "axmol/base/Logging.h"
#include "axmol/2d/ActionInstant.h"
#include "axmol/2d/ActionInterval.h"
#include "axmol/2d/ActionCatmullRom.h"
#include "axmol/2d/Menu.h"
#include "axmol/2d/MenuItem.h"
#include "axmol/2d/Sprite.h"
#include "axmol/2d/SpriteFrameCache.h"
#include "axmol/2d/AnimationCache.h"
#include "axmol/2d/Animation.h"
#include "axmol/2d/Layer.h"
#include "axmol/2d/ParticleSystem.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_map>

namespace axjs
{
namespace
{
using namespace manual;
template <auto Method>
bool legacyNativeMethod(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (invoke<Method>(cx, args))
        return true;
    if (!JS_IsExceptionPending(cx))
        JS_ReportError(cx, "Invalid legacy initializer arguments");
    return false;
}
template<class T>
bool menuFactory(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    ax::ccMenuCallback onClick;
    unsigned count=argc;
    if(argc && args[argc-1].isObject() && JS_ObjectIsCallable(cx,&args[argc-1].toObject()))
    {
        if(!fromJsValue(cx,args[argc-1],onClick)) return false;
        --count;
    }
    T* item=nullptr;
    if constexpr(std::is_same_v<T,ax::MenuItem>)
    {
        if(count) return error(cx,"MenuItem expects an optional callback");
        item=T::create(onClick);
    }
    else if constexpr(std::is_same_v<T,ax::MenuItemFont>)
    {
        std::string label;
        if(count>1 || (count && !argument(cx,args,0,label))) return error(cx,"MenuItemFont expects text and optional callback");
        item=T::create(label,onClick);
    }
    else if constexpr(std::is_same_v<T,ax::MenuItemLabel>)
    {
        ax::Node* label{};
        if(count!=1 || !argument(cx,args,0,label) || !label) return error(cx,"MenuItemLabel expects a native label");
        item=T::create(label,onClick);
    }
    else if constexpr(std::is_same_v<T,ax::MenuItemSprite>)
    {
        ax::Node *normal{},*selected{},*disabled{};
        if(count<2 || count>3 || !argument(cx,args,0,normal) || !argument(cx,args,1,selected) ||
            (count==3 && !argument(cx,args,2,disabled))) return error(cx,"MenuItemSprite expects normal, selected and optional disabled nodes");
        item=T::create(normal,selected,disabled,onClick);
    }
    else if constexpr(std::is_same_v<T,ax::MenuItemImage>)
    {
        std::string normal,selected,disabled;
        if(count==0) item=T::create();
        else
        {
            if(count<2 || count>3 || !argument(cx,args,0,normal) || !argument(cx,args,1,selected) ||
                (count==3 && !argument(cx,args,2,disabled))) return error(cx,"MenuItemImage expects normal, selected and optional disabled image names");
            if(normal.starts_with('#') || selected.starts_with('#') || disabled.starts_with('#'))
            {
                auto sprite=[](const std::string& file)->ax::Sprite* {
                    if(file.empty()) return nullptr;
                    return file.starts_with('#')?ax::Sprite::createWithSpriteFrameName(file.substr(1)):ax::Sprite::create(file);
                };
                auto* normalSprite=sprite(normal); auto* selectedSprite=sprite(selected); auto* disabledSprite=sprite(disabled);
                if((!normal.empty()&&!normalSprite)||(!selected.empty()&&!selectedSprite)||(!disabled.empty()&&!disabledSprite))
                    return error(cx,"MenuItemImage image or sprite frame could not be loaded");
                item=T::create();
                if(item) item->initWithNormalSprite(normalSprite,selectedSprite,disabledSprite,onClick);
            }
            else item=T::create(normal,selected,disabled,onClick);
        }
    }
    else if constexpr(std::is_same_v<T,ax::MenuItemAtlasFont>)
    {
        std::string text,file; int width{},height{}; char start{};
        if(count!=5 || !argument(cx,args,0,text)||!argument(cx,args,1,file)||!argument(cx,args,2,width)||!argument(cx,args,3,height))
            return error(cx,"MenuItemAtlasFont expects text, atlas file, glyph width, height and initial character");
        if(args[4].isString())
        {
            std::string character;if(!argument(cx,args,4,character)||character.empty()) return error(cx,"Initial atlas character cannot be empty");
            start=character[0];
        }
        else if(!argument(cx,args,4,start)) return false;
        item=T::create(text,file,width,height,start,onClick);
    }
    return wrapNative(cx,item,args.rval(),NativeOwnership::Factory);
}
bool toggleFactory(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp); ax::ccMenuCallback onClick; ax::Vector<ax::MenuItem*> items;
    if(argc!=2 || !argument(cx,args,0,onClick) || !argument(cx,args,1,items)) return false;
    return wrapNative(cx,ax::MenuItemToggle::createWithCallback(onClick,items),args.rval(),NativeOwnership::Factory);
}
bool menuArrayFactory(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp); ax::Vector<ax::MenuItem*> items;
    if(argc!=1 || !argument(cx,args,0,items)) return false;
    return wrapNative(cx,ax::Menu::createWithArray(items),args.rval(),NativeOwnership::Factory);
}
template<bool Columns> bool alignMenuItems(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::Menu* menu{};
    if(!toNative(cx,args.thisv(),&menu)||!menu)return error(cx,"Menu alignment requires a live Menu");
    ax::ValueVector groups;size_t total=0;
    for(unsigned i=0;i<argc;++i)
    {
        double number;
        if(!JS::ToNumber(cx,args[i],&number))return false;
        if(!std::isfinite(number)||number<1||std::trunc(number)!=number||number>std::numeric_limits<int>::max())
            return error(cx,"Menu alignment group sizes must be positive integers");
        total+=static_cast<size_t>(number);
        groups.emplace_back(static_cast<int>(number));
    }
    if(total!=menu->getChildrenCount())return error(cx,"Menu alignment groups must cover the menu items exactly");
    if constexpr(Columns)menu->alignItemsInColumnsWithArray(groups);
    else menu->alignItemsInRowsWithArray(groups);
    args.rval().setUndefined();return true;
}
template<class T> bool menuCreate(JSContext* cx)
{
    JS::RootedObject type(cx,getClassObject<T>(cx));
    return type && function(cx,type,"create",menuFactory<T>);
}
struct CoreState
{
    uint64_t nextTimer{1};
    std::unordered_map<uint64_t, std::shared_ptr<Callback>> timers;
    std::unordered_map<ax::Object*, ax::RefPtr<ax::Object>> poolObjects;
    struct ScheduledNode
    {
        ax::WeakPtr<ax::Node> node;
        std::unordered_map<std::string, uint64_t> keys;
        bool update = false;
    };
    std::unordered_map<ax::Node*, ScheduledNode> nodes;
    void forget(ax::Node* node, const std::string& key, uint64_t serial = 0)
    {
        auto entry = nodes.find(node);
        if (entry == nodes.end())
            return;
        auto found = entry->second.keys.find(key);
        if (found != entry->second.keys.end() && (!serial || found->second == serial))
            entry->second.keys.erase(found);
        if (entry->second.keys.empty() && !entry->second.update)
            nodes.erase(entry);
    }
    void prune()
    {
        for (auto it = nodes.begin(); it != nodes.end();)
        {
            auto* node = it->second.node.get();
            if (!node)
            {
                it = nodes.erase(it);
                continue;
            }
            std::erase_if(it->second.keys, [node](const auto& key) { return !node->isScheduled(key.first); });
            if (it->second.keys.empty() && !it->second.update)
                it = nodes.erase(it);
            else
                ++it;
        }
    }
    ScheduledNode& track(ax::Node* node)
    {
        auto& entry = nodes[node];
        if (!entry.node.get())
        {
            entry.keys.clear();
            entry.update = false;
            entry.node   = ax::WeakPtr<ax::Node>(node);
        }
        return entry;
    }
};
std::unordered_map<JSContext*, std::shared_ptr<CoreState>> states;

bool requireScript(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (!argc)
        return error(cx, "require expects a resource path");
    for (unsigned i = 0; i < argc; ++i)
    {
        std::string path;
        if (!argument(cx, args, i, path))
            return false;
        if (!engine(cx)->runScript(path))
        {
            JS_ReportError(cx, "Could not execute script: %s", path.c_str());
            return false;
        }
    }
    args.rval().setUndefined();
    return true;
}
bool print(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string output;
    for (unsigned i = 0; i < argc; ++i)
    {
        JS::RootedString string(cx, JS::ToString(cx, args[i]));
        if (!string)
            return false;
        JS::RootedValue value(cx, JS::StringValue(string));
        std::string part;
        if (!fromJsValue(cx, value, part))
            return false;
        if (i)
            output += ' ';
        output += part;
    }
    AXLOGI("{}", output);
    args.rval().setUndefined();
    return true;
}
bool objectValid(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    args.rval().setBoolean(argc && detail::isNativeValid(cx, args[0]));
    return true;
}
template <bool Retain>
bool poolOwnership(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::Object* object{};
    if (!argument(cx, args, 0, object) || !object)
        return error(cx, "Pool requires a native Object");
    auto& objects = states.at(cx)->poolObjects;
    if constexpr (Retain)
        objects.try_emplace(object, object);
    else if (auto found = objects.find(object); found != objects.end())
    {
        bool deferred = false;
        if (argc > 1 && !argument(cx, args, 1, deferred))
            return false;
        if (deferred)
        {
            object->retain();
            object->autorelease();
        }
        objects.erase(found);
    }
    args.rval().setUndefined();
    return true;
}
bool timer(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    float interval{};
    bool repeat{};
    if (argc != 3 || !argument(cx, args, 1, interval) || !argument(cx, args, 2, repeat))
        return false;
    if (interval < 0)
        return error(cx, "Timer interval must be nonnegative");
    auto state = states.at(cx);
    auto cb    = Callback::create(cx, args[0]);
    if (!cb)
        return false;
    auto id = state->nextTimer++;
    state->timers.emplace(id, cb);
    ax::Director::getInstance()->getScheduler()->schedule([state, id, repeat, cb](float) {
        if (!repeat)
            state->timers.erase(id);
        call(cb);
    }, state.get(), interval, repeat ? UINT_MAX : 0, 0, false, "js.timer." + std::to_string(id));
    args.rval().set(nativeNumberValue(static_cast<double>(id)));
    return true;
}
bool cancelTimer(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    uint64_t id{};
    if (!argument(cx, args, 0, id))
        return false;
    auto state = states.at(cx);
    ax::Director::getInstance()->getScheduler()->unschedule("js.timer." + std::to_string(id), state.get());
    state->timers.erase(id);
    args.rval().setUndefined();
    return true;
}
bool platform(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject result(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
    if (!result)
        return false;
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    const char* os = "Android";
    bool mobile    = true;
#elif AX_TARGET_PLATFORM == AX_PLATFORM_IOS
    const char* os = "iOS";
    bool mobile    = true;
#elif AX_TARGET_PLATFORM == AX_PLATFORM_MAC
    const char* os = "OS X";
    bool mobile    = false;
#elif AX_TARGET_PLATFORM == AX_PLATFORM_WIN32
    const char* os = "Windows";
    bool mobile    = false;
#else
    const char* os = "Linux";
    bool mobile    = false;
#endif
    if (!property(cx, result, "os", os) || !property(cx, result, "mobile", mobile) ||
        !property(cx, result, "id", AX_TARGET_PLATFORM))
        return false;
    std::string language = "en";
    std::string languageCode;
    if (auto* application = ax::Application::getInstanceIfExists())
    {
        const char* codes[] = {"en", "zh", "zh", "fr", "it", "de", "es", "du", "ru", "ko", "ja",
                               "hu", "pt", "ar", "no", "pl", "tr", "uk", "ro", "bg", "be"};
        const auto type     = static_cast<unsigned>(application->getCurrentLanguage());
        if (type < std::size(codes))
            language = codes[type];
        if (const auto* code = application->getCurrentLanguageCode())
            languageCode = code;
    }
    else if (const char* locale = std::getenv("LANG"))
    {
        languageCode   = locale;
        auto separator = languageCode.find_first_of("_-. ");
        auto code      = languageCode.substr(0, separator);
        if (code.size() == 2)
            language = code == "nl" ? "du" : code;
    }
    if (languageCode.empty())
        languageCode = language;
    if (!property(cx, result, "language", language) || !property(cx, result, "languageCode", languageCode))
        return false;
    args.rval().setObject(*result);
    return true;
}
bool cleanScript(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string path;
    if (!argument(cx, args, 0, path))
        return false;
    engine(cx)->cleanScript(path);
    args.rval().setUndefined();
    return true;
}
bool openUrl(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string url;
    if (!argument(cx, args, 0, url))
        return false;
    auto* application = ax::Application::getInstanceIfExists();
    if (!application)
        return error(cx, "openURL requires a native application");
    args.rval().setBoolean(application->openURL(url));
    return true;
}
bool gc(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    engine(cx)->garbageCollect();
    args.rval().setUndefined();
    return true;
}
bool restart(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto vm   = runtimeState(cx);
    ax::Director::getInstance()->getScheduler()->schedule([vm](float) {
        if (vm->alive.load())
        {
            auto* e = vm->engine;
            if (e->restart() && e->runScript("script/jsb_boot.js"))
                e->runScript("main.js");
        }
    }, vm->engine, 0, 0, 0, false, "js.restart");
    args.rval().setUndefined();
    return true;
}
bool enableDebugger(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args           = JS::CallArgsFromVp(argc, vp);
    uint16_t port       = 5086;
    bool wait           = false;
    std::string address = "127.0.0.1";
    if ((argc > 0 && !argument(cx, args, 0, port)) || (argc > 1 && !argument(cx, args, 1, wait)) ||
        (argc > 2 && !argument(cx, args, 2, address)))
        return false;
    args.rval().setBoolean(engine(cx)->enableDebugger(port, wait, address));
    return true;
}
bool now(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    args.rval().set(nativeNumberValue(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count()));
    return true;
}

template <int Op>
bool nodeLifecycle(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args  = JS::CallArgsFromVp(argc, vp);
    auto* node = self<ax::Node>(cx, args);
    if (!node)
        return false;
    static constexpr const char* methods[] = {
        "onEnter", "onExit", "onEnterTransitionDidFinish", "onExitTransitionDidStart", "cleanup", "update"};
    float delta = 0;
    if constexpr (Op == 5)
        if (!argument(cx, args, 0, delta))
            return false;
    if (!engine(cx)->invokeNodeBase(node, methods[Op], delta))
        return false;
    args.rval().setUndefined();
    return true;
}
bool nodeRunAction(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args  = JS::CallArgsFromVp(argc, vp);
    auto* node = self<ax::Node>(cx, args);
    ax::Action* action{};
    if (!node || !argument(cx, args, 0, action) || !action)
        return error(cx, "runAction requires an Action");
    return wrapNative(cx, node->runAction(action), args.rval());
}
template <bool Once>
bool schedule(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args  = JS::CallArgsFromVp(argc, vp);
    auto* node = self<ax::Node>(cx, args);
    if (!node || !argc)
        return false;
    JS::RootedObject receiver(cx, &args.thisv().toObject());
    auto cb = callback(cx, args, 0, receiver);
    if (!cb)
        return false;
    float interval = 0, delay = 0;
    unsigned repeat = UINT_MAX;
    // The v3 schedule wrapper uses ToNumber, including false as zero repeats.
    double number = 0;
    if (argc > 1)
    {
        if (!JS::ToNumber(cx, args[1], &number))
            return false;
        interval = static_cast<float>(number);
    }
    if constexpr (!Once)
    {
        if (argc > 2)
        {
            if (!JS::ToNumber(cx, args[2], &number))
                return false;
            if (!std::isfinite(number) || number < 0 || number > UINT_MAX)
                return error(cx, "Schedule repeat count must be between zero and UINT_MAX");
            repeat = static_cast<unsigned>(number);
        }
        if (argc > 3)
        {
            if (!JS::ToNumber(cx, args[3], &number))
                return false;
            delay = static_cast<float>(number);
        }
    }
    if (!std::isfinite(interval) || !std::isfinite(delay) || interval < 0 || delay < 0)
        return error(cx, "Schedule intervals must be nonnegative");
    JS::RootedObject fn(cx, &args[0].toObject());
    JS::RootedValue id(cx);
    if (!JS_GetProperty(cx, fn, "__axmolScheduleId", &id))
        return false;
    if (!id.isNumber())
    {
        id.set(nativeNumberValue(static_cast<double>(states.at(cx)->nextTimer++)));
        if (!JS_DefineProperty(cx, fn, "__axmolScheduleId", id, 0))
            return false;
    }
    std::string key              = "js.node." + std::to_string(static_cast<uint64_t>(id.toNumber()));
    auto state                   = states.at(cx);
    auto serial                  = state->nextTimer++;
    state->track(node).keys[key] = serial;
    auto native                  = [cb, state, node, key, serial, remaining = repeat](float dt) mutable {
        if (Once || remaining == 0)
            state->forget(node, key, serial);
        else if (remaining != UINT_MAX)
            --remaining;
        call(cb, dt);
    };
    if constexpr (Once)
        node->scheduleOnce(native, interval, key);
    else
        node->schedule(native, interval, repeat, delay, key);
    args.rval().setUndefined();
    return true;
}
bool unschedule(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args  = JS::CallArgsFromVp(argc, vp);
    auto* node = self<ax::Node>(cx, args);
    if (!node || !argc)
        return false;
    if (args[0].isString())
    {
        std::string key;
        if (!argument(cx, args, 0, key))
            return false;
        node->unschedule(key);
        states.at(cx)->forget(node, key);
    }
    else if (args[0].isObject())
    {
        JS::RootedObject fn(cx, &args[0].toObject());
        JS::RootedValue id(cx);
        if (!JS_GetProperty(cx, fn, "__axmolScheduleId", &id))
            return false;
        if (id.isNumber())
        {
            auto key = "js.node." + std::to_string(static_cast<uint64_t>(id.toNumber()));
            node->unschedule(key);
            states.at(cx)->forget(node, key);
        }
    }
    else
        return error(cx, "unschedule expects a callback or key");
    args.rval().setUndefined();
    return true;
}
template <int Op>
bool scheduleUpdate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args  = JS::CallArgsFromVp(argc, vp);
    auto* node = self<ax::Node>(cx, args);
    if (!node)
        return false;
    if constexpr (Op == 0)
    {
        int priority = 0;
        if (argc && !argument(cx, args, 0, priority))
            return false;
        node->scheduleUpdateWithPriority(priority);
        states.at(cx)->track(node).update = true;
    }
    if constexpr (Op == 1)
    {
        node->unscheduleUpdate();
        auto state = states.at(cx);
        auto entry = state->nodes.find(node);
        if (entry != state->nodes.end())
        {
            entry->second.update = false;
            if (entry->second.keys.empty())
                state->nodes.erase(entry);
        }
    }
    if constexpr (Op == 2)
    {
        node->unscheduleAllCallbacks();
        states.at(cx)->nodes.erase(node);
    }
    args.rval().setUndefined();
    return true;
}
template <int Axis, bool Write>
bool nodeDimension(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args  = JS::CallArgsFromVp(argc, vp);
    auto* node = self<ax::Node>(cx, args);
    if (!node)
        return false;
    auto value = node->getContentSize();
    if constexpr (Write)
    {
        float n;
        if (!argument(cx, args, 0, n))
            return false;
        if constexpr (Axis == 0)
            value.x = n;
        else
            value.y = n;
        node->setContentSize(value);
        args.rval().setUndefined();
    }
    else
        args.rval().setDouble(JS::CanonicalizeNaN(Axis == 0 ? value.x : value.y));
    return true;
}
bool runScene(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args      = JS::CallArgsFromVp(argc, vp);
    auto* director = self<ax::Director>(cx, args);
    ax::Scene* scene{};
    if (!director || !argument(cx, args, 0, scene) || !scene)
        return error(cx, "runScene requires a Scene");
    if (director->getRunningScene())
        director->replaceScene(scene);
    else
        director->runWithScene(scene);
    args.rval().setUndefined();
    return true;
}
template <bool Parallel>
bool compositeAction(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::Vector<ax::FiniteTimeAction*> actions;
    if (argc == 1 && args[0].isObject())
    {
        JS::RootedObject array(cx, &args[0].toObject());
        if (JS_IsArrayObject(cx, array))
        {
            uint32_t count = 0;
            if (!JS_GetArrayLength(cx, array, &count))
                return false;
            for (uint32_t i = 0; i < count; ++i)
            {
                JS::RootedValue v(cx);
                ax::FiniteTimeAction* a{};
                if (!JS_GetElement(cx, array, i, &v) || !toNative(cx, v, &a) || !a)
                    return false;
                actions.pushBack(a);
            }
        }
        else
        {
            ax::FiniteTimeAction* a{};
            if (!argument(cx, args, 0, a) || !a)
                return false;
            actions.pushBack(a);
        }
    }
    else
        for (unsigned i = 0; i < argc; ++i)
        {
            ax::FiniteTimeAction* a{};
            if (args[i].isNullOrUndefined())
                continue;
            if (!argument(cx, args, i, a) || !a)
                return false;
            actions.pushBack(a);
        }
    if (actions.empty())
        return error(cx, "An action sequence cannot be empty");
    if constexpr (Parallel)
        return wrapNative(cx, createLegacySpawn(actions), args.rval(), NativeOwnership::Factory);
    else
        return wrapNative(cx, createLegacySequence(actions), args.rval(), NativeOwnership::Factory);
}
template <int Kind, bool Initialize = false>
bool curveAction(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    float duration;
    std::vector<ax::Vec2> points;
    if (!argument(cx, args, 0, duration) || !argument(cx, args, 1, points) || duration < 0)
        return error(cx, "Curve requires a nonnegative duration and points");
    ax::ActionInterval* action{};
    if constexpr (Kind < 2)
    {
        // v3 reads the first three points; its original custom-action test supplies four.
        if (points.size() < 3)
            return error(cx, "Bezier requires at least three control/end points");
        ax::BezierConfig config{points[2], points[0], points[1]};
        if constexpr (Initialize)
        {
            using Curve = std::conditional_t<Kind == 0, ax::BezierTo, ax::BezierBy>;
            auto* curve = self<Curve>(cx, args);
            if (!curve)
                return false;
            args.rval().setBoolean(curve->initWithDuration(duration, config));
            return true;
        }
        if constexpr (Kind == 0)
            action = ax::BezierTo::create(duration, config);
        else
            action = ax::BezierBy::create(duration, config);
    }
    else
    {
        if (points.size() < 2)
            return error(cx, "Spline requires at least two points");
        auto* array = ax::PointArray::create(points.size());
        for (const auto& p : points)
            array->addControlPoint(p);
        if constexpr (Initialize && Kind < 4)
        {
            using Curve = std::conditional_t<Kind == 2, ax::CatmullRomTo, ax::CatmullRomBy>;
            auto* curve = self<Curve>(cx, args);
            if (!curve)
                return false;
            args.rval().setBoolean(curve->initWithDuration(duration, array));
            return true;
        }
        if constexpr (Kind == 2)
            action = ax::CatmullRomTo::create(duration, array);
        if constexpr (Kind == 3)
            action = ax::CatmullRomBy::create(duration, array);
        if constexpr (Kind >= 4)
        {
            float tension;
            if (!argument(cx, args, 2, tension))
                return false;
            if constexpr (Initialize)
            {
                using Curve = std::conditional_t<Kind == 4, ax::CardinalSplineTo, ax::CardinalSplineBy>;
                auto* curve = self<Curve>(cx, args);
                if (!curve)
                    return false;
                args.rval().setBoolean(curve->initWithDuration(duration, array, tension));
                return true;
            }
            if constexpr (Kind == 4)
                action = ax::CardinalSplineTo::create(duration, array, tension);
            else
                action = ax::CardinalSplineBy::create(duration, array, tension);
        }
    }
    return wrapNative(cx, action, args.rval(), NativeOwnership::Factory);
}
template <class T>
bool actionFactory(JSContext* cx, JSNative fn, unsigned arity)
{
    JS::RootedObject klass(cx, getClassObject<T>(cx));
    return klass && function(cx, klass, "create", fn, arity);
}
bool nodeUserObject(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args  = JS::CallArgsFromVp(argc, vp);
    auto* node = self<ax::Node>(cx, args);
    if (!node)
        return false;
    return wrapNative(cx, node->getUserObject(), args.rval());
}
bool nodeChildren(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args  = JS::CallArgsFromVp(argc, vp);
    auto* node = self<ax::Node>(cx, args);
    if (!node)
        return false;
    JS::RootedObject array(cx, JS_NewArrayObject(cx, 0));
    if (!array)
        return false;
    uint32_t index = 0;
    for (auto* child : node->getChildren())
    {
        JS::RootedValue value(cx);
        if (!wrapNative(cx, child, &value) || !JS_SetElement(cx, array, index++, value))
            return false;
    }
    args.rval().setObject(*array);
    return true;
}
bool callFunc(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (!argc)
        return error(cx, "callFunc requires a function");
    JS::RootedObject receiver(cx, argc > 1 && args[1].isObject() ? &args[1].toObject() : engine(cx)->getGlobalObject());
    auto cb = callback(cx, args, 0, receiver);
    if (!cb)
        return false;
    return wrapNative(cx, static_cast<ax::CallFunc*>(ax::CallFuncN::create([cb](ax::Node* node) {
                          // The callback can reinitialize this same action. Keep the
                          // active roots alive if that replaces the executing lambda.
                          auto active = cb;
                          call(active, node);
                      })),
                      args.rval(), NativeOwnership::Factory);
}
bool allocateCallFunc(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc)
        return error(cx, "Native CallFunc allocation expects no arguments");
    JS::RootedObject prototype(cx, getPrototype<ax::CallFunc>(cx));
    if (!prototype)
        return error(cx, "Native CallFunc prototype is unavailable");
    JS::RootedObject wrapper(cx, JS_NewObject(cx, const_cast<JSClass*>(JS_GetClass(prototype)), prototype, JS::NullPtr()));
    if (!wrapper)
        return false;
    auto* native = new ax::CallFuncN;
    if (!bindNative(cx, wrapper, native, NativeOwnership::Constructor))
    {
        delete native;
        return false;
    }
    args.rval().setObject(*wrapper);
    return true;
}
bool initializeCallFunc(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    // The legacy shim prepares one closure holding receiver and optional data.
    // Only a real CallFuncN may supply the current running action's Node target.
    auto* native = self<ax::CallFuncN>(cx, args);
    if (!native)
        return false;
    if (argc != 1)
        return error(cx, "Native CallFunc initialization requires one prepared callback");
    JS::RootedObject receiver(cx, engine(cx)->getGlobalObject());
    auto cb = callback(cx, args, 0, receiver);
    if (!cb)
        return error(cx, "CallFunc initialization requires a function");
    const bool initialized = native->initWithFunction([cb](ax::Node* node) {
        auto active = cb;
        call(active, node);
    });
    args.rval().setBoolean(initialized);
    return true;
}
bool registerCallFunc(JSContext* cx)
{
    JS::RootedObject klass(cx, getClassObject<ax::CallFunc>(cx));
    JS::RootedObject prototype(cx, getPrototype<ax::CallFunc>(cx));
    // Keep the public cc.CallFunc prototype. Native clone/reverse returns a
    // CallFuncN and must retain its real dynamic type and checked base casts.
    if (!klass || !prototype ||
        (!detail::getClassInfo(cx, typeid(ax::CallFuncN)) &&
         !registerType<ax::CallFuncN, ax::CallFunc>(cx, prototype, const_cast<JSClass*>(JS_GetClass(prototype)))))
        return false;
    return function(cx, klass, "__allocate", allocateCallFunc, 0) &&
           method<ax::CallFunc>(cx, "initWithFunction", initializeCallFunc, 1);
}
template <int Operation>
bool view(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* v   = ax::Director::getInstance()->getRenderView();
    if (!v)
        return error(cx, "No render view is available");
    if constexpr (Operation == 0)
        return dimensions(cx, v->getWindowSize(), args.rval());
    if constexpr (Operation == 1)
        return dimensions(cx, v->getDesignResolutionSize(), args.rval());
    if constexpr (Operation == 7)
        return toJsValue(cx,v->getResolutionPolicy(),args.rval());
    if constexpr (Operation == 6)
    {
        const auto window=v->getWindowSize();
        const auto render=v->getRenderSize();
        if(window.width<=0||render.width<=0)return error(cx,"Render view dimensions are not initialized");
        args.rval().setDouble(JS::CanonicalizeNaN(static_cast<double>(render.width)/window.width));
        return true;
    }
    if constexpr (Operation == 2)
    {
        float w, h;
        int policy;
        if (!argument(cx, args, 0, w) || !argument(cx, args, 1, h) || !argument(cx, args, 2, policy))
            return false;
        if (w <= 0 || h <= 0 || policy < 0 || policy > 4)
            return error(cx, "Invalid design resolution");
        v->setDesignResolutionSize(w, h, static_cast<ax::ResolutionPolicy>(policy));
    }
    if constexpr (Operation == 3)
    {
        float w, h;
        if (!argument(cx, args, 0, w) || !argument(cx, args, 1, h))
            return false;
        v->setWindowSize(w, h);
    }
    if constexpr (Operation == 4)
        return dimensions(cx, ax::Director::getInstance()->getVisibleSize(), args.rval());
    if constexpr (Operation == 5)
        return toJsValue(cx, ax::Director::getInstance()->getVisibleOrigin(), args.rval());
    args.rval().setUndefined();
    return true;
}
// Browser-only viewport/fullscreen switches have no operation in a native app.
bool browserViewOption(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    args.rval().setUndefined();
    return true;
}
template <int Operation>
bool storage(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string key;
    if (!argument(cx, args, 0, key))
        return false;
    // Encode every byte so user keys cannot collide with presence metadata.
    std::string encoded;
    encoded.reserve(key.size() * 2);
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned char c : key)
    {
        encoded.push_back(digits[c >> 4]);
        encoded.push_back(digits[c & 15]);
    }
    key         = "axmol.js.storage." + encoded;
    auto* store = ax::UserDefault::getInstance();
    if constexpr (Operation == 0)
    {
        if (!store->getBoolForKey((key + ".present").c_str(), false))
        {
            args.rval().setNull();
            return true;
        }
        return toJsValue(cx, store->getStringForKey(key.c_str()), args.rval());
    }
    if constexpr (Operation == 1)
    {
        std::string value;
        if (!argument(cx, args, 1, value))
            return false;
        store->setStringForKey(key.c_str(), value);
        store->setBoolForKey((key + ".present").c_str(), true);
    }
    if constexpr (Operation == 2)
    {
        store->deleteValueForKey(key.c_str());
        store->deleteValueForKey((key + ".present").c_str());
    }
    store->flush();
    args.rval().setUndefined();
    return true;
}
}  // namespace

bool registerCore(JSContext* cx, JS::HandleObject global)
{
    JS::RootedObject toggle(cx,getClassObject<ax::MenuItemToggle>(cx)), menu(cx,getClassObject<ax::Menu>(cx));
    JS::RootedObject menuPrototype(cx,getPrototype<ax::Menu>(cx));
    if(!menuCreate<ax::MenuItem>(cx)||!menuCreate<ax::MenuItemImage>(cx)||!menuCreate<ax::MenuItemSprite>(cx)||!menuCreate<ax::MenuItemLabel>(cx)||
       !menuCreate<ax::MenuItemFont>(cx)||!menuCreate<ax::MenuItemAtlasFont>(cx)||!toggle||
       !function(cx,toggle,"createWithCallback",toggleFactory,2) || !menu ||
       !function(cx,menu,"createWithArray",menuArrayFactory,1) || !menuPrototype ||
       !function(cx,menuPrototype,"alignItemsInColumns",alignMenuItems<true>,1) ||
       !function(cx,menuPrototype,"alignItemsInRows",alignMenuItems<false>,1)) return false;
    setStaticLifetime<ax::Director>(cx);
    setStaticLifetime<ax::FileUtils>(cx);
    setStaticLifetime<ax::UserDefault>(cx);
    setStaticLifetime<ax::SpriteFrameCache>(cx);
    setStaticLifetime<ax::AnimationCache>(cx);
    engine(cx)->setNodeLifecycleNative("onEnter", nodeLifecycle<0>);
    engine(cx)->setNodeLifecycleNative("onExit", nodeLifecycle<1>);
    engine(cx)->setNodeLifecycleNative("onEnterTransitionDidFinish", nodeLifecycle<2>);
    engine(cx)->setNodeLifecycleNative("onExitTransitionDidStart", nodeLifecycle<3>);
    engine(cx)->setNodeLifecycleNative("cleanup", nodeLifecycle<4>);
    engine(cx)->setNodeLifecycleNative("update", nodeLifecycle<5>);
    auto state = std::make_shared<CoreState>();
    states[cx] = state;
    engine(cx)->addUpdateCallback([state](float) { state->prune(); });
    engine(cx)->addCleanupCallback([cx, state] {
        ax::Director::getInstance()->getScheduler()->unscheduleAllForTarget(state.get());
        for (auto& [pointer, entry] : state->nodes)
            if (auto* node = entry.node.get())
            {
                for (const auto& [key, serial] : entry.keys)
                    node->unschedule(key);
                if (entry.update)
                    node->unscheduleUpdate();
            }
        state->nodes.clear();
        state->timers.clear();
        state->poolObjects.clear();
        states.erase(cx);
    });
    JS::RootedObject cc(cx, namespaceObject(cx, global, "cc")), jsb(cx, namespaceObject(cx, global, "jsb"));
    if (!cc || !jsb)
        return false;
    if (!function(cx, global, "require", requireScript, 1) ||
        !function(cx, global, "executeScript", requireScript, 1) || !function(cx, global, "log", print) ||
        !function(cx, global, "print", print) || !function(cx, jsb, "isObjectValid", objectValid, 1))
        return false;
    if (!function(cx, jsb, "_poolRetain", poolOwnership<true>, 1) ||
        !function(cx, jsb, "_poolRelease", poolOwnership<false>, 1))
        return false;
    if (!function(cx, jsb, "_schedule", timer, 3) || !function(cx, jsb, "_cancelTimer", cancelTimer, 1) ||
        !function(cx, jsb, "_platform", platform) || !function(cx, jsb, "_cleanScript", cleanScript, 1) ||
        !function(cx, jsb, "_openURL", openUrl, 1) ||
        !function(cx, jsb, "_associateObjectWithNative", associateObjectWithNative, 2) ||
        !function(cx, jsb, "garbageCollect", gc) ||
        !function(cx, jsb, "restart", restart) || !function(cx, jsb, "enableDebugger", enableDebugger) ||
        !function(cx, jsb, "_storageGet", storage<0>, 1) || !function(cx, jsb, "_storageSet", storage<1>, 2) ||
        !function(cx, jsb, "_storageDelete", storage<2>, 1))
        return false;
    JS::RootedObject performance(cx, namespaceObject(cx, global, "performance"));
    if (!performance || !function(cx, performance, "now", now))
        return false;
    if (!method<ax::Node>(cx, "onEnter", nodeLifecycle<0>) || !method<ax::Node>(cx, "onExit", nodeLifecycle<1>) ||
        !method<ax::Node>(cx, "onEnterTransitionDidFinish", nodeLifecycle<2>) ||
        !method<ax::Node>(cx, "onExitTransitionDidStart", nodeLifecycle<3>) ||
        !method<ax::Node>(cx, "cleanup", nodeLifecycle<4>) || !method<ax::Node>(cx, "update", nodeLifecycle<5>, 1) ||
        !method<ax::Node>(cx, "schedule", schedule<false>, 1) ||
        !method<ax::Node>(cx, "scheduleOnce", schedule<true>, 1) ||
        !method<ax::Node>(cx, "scheduleUpdate", scheduleUpdate<0>) ||
        !method<ax::Node>(cx, "scheduleUpdateWithPriority", scheduleUpdate<0>, 1) ||
        !method<ax::Node>(cx, "unscheduleUpdate", scheduleUpdate<1>) ||
        !method<ax::Node>(cx, "unscheduleAllCallbacks", scheduleUpdate<2>) ||
        !method<ax::Node>(cx, "unschedule", unschedule, 1) || !method<ax::Node>(cx, "runAction", nodeRunAction, 1) ||
        !method<ax::Node>(cx, "getWidth", nodeDimension<0, false>) ||
        !method<ax::Node>(cx, "getHeight", nodeDimension<1, false>) ||
        !method<ax::Node>(cx, "setWidth", nodeDimension<0, true>, 1) ||
        !method<ax::Node>(cx, "setHeight", nodeDimension<1, true>, 1) ||
        !method<ax::Director>(cx, "runScene", runScene, 1))
        return false;
    if (!method<ax::Node>(cx, "getChildren", nodeChildren) || !method<ax::Node>(cx, "getUserObject", nodeUserObject) ||
        !actionFactory<ax::BezierTo>(cx, curveAction<0>, 2) || !actionFactory<ax::BezierBy>(cx, curveAction<1>, 2) ||
        !actionFactory<ax::CatmullRomTo>(cx, curveAction<2>, 2) ||
        !actionFactory<ax::CatmullRomBy>(cx, curveAction<3>, 2) ||
        !actionFactory<ax::CardinalSplineTo>(cx, curveAction<4>, 3) ||
        !actionFactory<ax::CardinalSplineBy>(cx, curveAction<5>, 3))
        return false;
    if (!method<ax::BezierTo>(cx, "initWithDuration", curveAction<0, true>, 2) ||
        !method<ax::BezierBy>(cx, "initWithDuration", curveAction<1, true>, 2) ||
        !method<ax::CatmullRomTo>(cx, "initWithDuration", curveAction<2, true>, 2) ||
        !method<ax::CatmullRomBy>(cx, "initWithDuration", curveAction<3, true>, 2) ||
        !method<ax::CardinalSplineTo>(cx, "initWithDuration", curveAction<4, true>, 3) ||
        !method<ax::CardinalSplineBy>(cx, "initWithDuration", curveAction<5, true>, 3))
        return false;
    if (!method<ax::Animation>(cx, "initWithSpriteFrames", legacyNativeMethod<&ax::Animation::initWithSpriteFrames>,
                               3) ||
        !method<ax::Animation>(cx, "initWithAnimationFrames",
                               legacyNativeMethod<&ax::Animation::initWithAnimationFrames>, 3) ||
        !method<ax::AnimationFrame>(cx, "initWithSpriteFrame",
                                    legacyNativeMethod<&ax::AnimationFrame::initWithSpriteFrame>, 3) ||
        !method<ax::LayerMultiplex>(cx, "initWithArray", legacyNativeMethod<&ax::LayerMultiplex::initWithArray>, 1) ||
        !method<ax::ParticleSystem>(cx, "initWithDictionary",
                                    legacyNativeMethod<static_cast<bool (ax::ParticleSystem::*)(const ax::ValueMap&)>(
                                        &ax::ParticleSystem::initWithDictionary)>,
                                    1))
        return false;
    if (!function(cx, cc, "sequence", compositeAction<false>) || !function(cx, cc, "spawn", compositeAction<true>) ||
        !function(cx, cc, "callFunc", callFunc, 1) || !registerCallFunc(cx))
        return false;
    JS::RootedObject viewObject(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
    if (!viewObject)
        return false;
    if (!function(cx, viewObject, "getFrameSize", view<0>) ||
        !function(cx, viewObject, "getDesignResolutionSize", view<1>) ||
        !function(cx, viewObject, "setDesignResolutionSize", view<2>, 3) ||
        !function(cx, viewObject, "setFrameSize", view<3>, 2) || !function(cx, viewObject, "getVisibleSize", view<4>) ||
        !function(cx, viewObject, "getVisibleOrigin", view<5>) ||
        !function(cx, viewObject, "getDevicePixelRatio", view<6>) ||
        !function(cx, viewObject, "getResolutionPolicy", view<7>))
        return false;
    // The original native Cocos bootstrap also leaves enableRetina as a no-op;
    // native display density is managed by the platform view.
    for (auto name : {"adjustViewPort", "resizeWithBrowserSize", "enableAutoFullScreen", "enableRetina"})
        if (!function(cx, viewObject, name, browserViewOption, 1))
            return false;
    JS::RootedValue viewValue(cx, JS::ObjectValue(*viewObject));
    return property(cx, cc, "view", viewValue);
}
}  // namespace axjs
