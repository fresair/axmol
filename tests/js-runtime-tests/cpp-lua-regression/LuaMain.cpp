// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
#include "LuaBindingSmoke.h"
#include "lua-bindings/runtime/LuaEngine.h"
#include "lua-bindings/runtime/axlua_runtime.h"
#include "lua-bindings/runtime/lua_module_register.h"
#include "axmol/base/AutoreleasePool.h"
#include "axmol/base/Director.h"
#include "axmol/base/EventDispatcher.h"
#include "axmol/scene/Node.h"
#include <cstdio>
#include <string>
#include <csignal>
#include <execinfo.h>
#include <unistd.h>

namespace
{
bool script(lua_State* state,const char* code)
{
    if(luaL_dostring(state,code)==LUA_OK) return true;
    std::fprintf(stderr,"Lua native core regression failed: %s\n",lua_tostring(state,-1));
    lua_pop(state,1);return false;
}
bool nativeCore(lua_State* state)
{
    auto* parent=ax::Node::create();auto* child=ax::Node::create();parent->addChild(child);
    axlua::push_object(state,parent);lua_setglobal(state,"regressionParent");
    axlua::push_object(state,child);lua_setglobal(state,"regressionChild");
    if(!script(state,R"lua(
        lifecycleTrace={}
        local function record(name) return function() table.insert(lifecycleTrace,name) end end
        regressionParent:setOnEnterCallback(record('parent.enter'))
        regressionChild:setOnEnterCallback(record('child.enter'))
        regressionParent:setOnEnterTransitionDidFinishCallback(record('parent.enter.finish'))
        regressionChild:setOnEnterTransitionDidFinishCallback(record('child.enter.finish'))
        regressionParent:setOnExitTransitionDidStartCallback(record('parent.exit.start'))
        regressionChild:setOnExitTransitionDidStartCallback(record('child.exit.start'))
        regressionParent:setOnExitCallback(record('parent.exit'))
        regressionChild:setOnExitCallback(record('child.exit'))
        regressionParent:setOnCleanupCallback(record('parent.cleanup'))
        regressionChild:setOnCleanupCallback(record('child.cleanup'))
    )lua")) return false;
    parent->onEnter();parent->onEnterTransitionDidFinish();parent->onExitTransitionDidStart();parent->onExit();parent->cleanup();
    if(!script(state,R"lua(
        assert(not regressionParent:isRunning() and not regressionChild:isRunning())
        assert(table.concat(lifecycleTrace,',')=='parent.enter,child.enter,parent.enter.finish,child.enter.finish,parent.exit.start,child.exit.start,parent.exit,child.exit,parent.cleanup,child.cleanup')
        regressionParent:setOnEnterCallback(nil);regressionChild:setOnEnterCallback(nil)
        regressionParent:setOnEnterTransitionDidFinishCallback(nil);regressionChild:setOnEnterTransitionDidFinishCallback(nil)
        regressionParent:setOnExitTransitionDidStartCallback(nil);regressionChild:setOnExitTransitionDidStartCallback(nil)
        regressionParent:setOnExitCallback(nil);regressionChild:setOnExitCallback(nil)
        regressionParent:setOnCleanupCallback(nil);regressionChild:setOnCleanupCallback(nil)
        nativeEventCalls=0
        nativeEventName=string.rep('L',8192)..'.lua.custom'
        local dispatcher=ax.Director:getInstance():getEventDispatcher()
        local listener
        listener=ax.CustomEventListener:create(nativeEventName,function(event)
            assert(event:getEventName()==nativeEventName)
            nativeEventCalls=nativeEventCalls+1;escapedNativeEvent=event
            dispatcher:removeEventListener(listener)
        end)
        nativeEventListener=listener;nativeEventClone=listener:clone()
        dispatcher:addEventListenerWithFixedPriority(nativeEventListener,1)
        dispatcher:addEventListenerWithFixedPriority(nativeEventClone,2)
        collectgarbage('collect')
    )lua")) return false;
    auto* dispatcher=ax::Director::getInstance()->getEventDispatcher();
    const bool previouslyEnabled=dispatcher->isEnabled();dispatcher->setEnabled(true);
    const std::string eventName=std::string(8192,'L')+".lua.custom";
    dispatcher->dispatchCustomEvent(eventName);
    bool ok=script(state,"assert(nativeEventCalls==2);assert(not pcall(function()escapedNativeEvent:getEventName()end))");
    dispatcher->dispatchCustomEvent(eventName);
    ok=script(state,R"lua(
        assert(nativeEventCalls==3)
        local dispatcher=ax.Director:getInstance():getEventDispatcher()
        dispatcher:removeEventListener(nativeEventListener);dispatcher:removeEventListener(nativeEventClone)
        nativeEventListener=nil;nativeEventClone=nil;escapedNativeEvent=nil
    )lua") && ok;
    dispatcher->setEnabled(previouslyEnabled);
    return ok;
}
}
int main()
{
    std::signal(SIGSEGV,[](int signal){void* stack[48];int count=backtrace(stack,48);backtrace_symbols_fd(stack,count,STDERR_FILENO);_exit(128+signal);});
    std::fputs("Lua regression: initialize engine\n",stderr);
    ax::AutoreleasePool pool("existing Lua headless regression");
    auto* engine=ax::LuaEngine::getInstance();
    ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
    auto* state=engine->getLuaStack()->getLuaState();
    std::fputs("Lua regression: register original modules\n",stderr);
    lua_module_register(state);
    std::fputs("Lua regression: run unchanged LuaBindingSmoke\n",stderr);
    const int original=runLuaBindingSmoke(state);
    const bool core=original==0 && nativeCore(state);
    const int shutdown=finishLuaBindingSmoke(state);
    if(original!=0 || !core || shutdown!=0) return 1;
    std::puts("Existing Lua binding smoke + Node lifecycle/dynamic CustomEvent clone and VM shutdown passed");
    return 0;
}
