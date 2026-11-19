/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "LuaBindingSmoke.h"
#include "lua-bindings/runtime/LuaEngine.h"
#include "lua-bindings/runtime/axlua_runtime.h"
#include "lua-bindings/runtime/lua_module_register.h"
#include "axmol/base/AutoreleasePool.h"
#include "axmol/base/WeakPtr.h"
#include "axmol/scene/Node.h"

#include <cstdio>
#include <stdexcept>
#include <vector>

namespace
{
void check(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

void script(lua_State* state, const char* source)
{
    if (luaL_dostring(state, source) != LUA_OK)
    {
        const std::string message = lua_tostring(state, -1);
        lua_pop(state, 1);
        throw std::runtime_error(message);
    }
}

axlua::Callback<int()> epochCallback(lua_State* state, int epoch)
{
    lua_pushinteger(state, epoch);
    lua_pushcclosure(state, [](lua_State* context) {
        lua_pushvalue(context, lua_upvalueindex(1));
        return 1;
    }, 1);
    axlua::Callback<int()> callback(state, -1);
    lua_pop(state, 1);
    return callback;
}
}

int main()
{
    ax::AutoreleasePool pool("Lua real engine restart regression");
    auto* node = new ax::Node();
    ax::WeakPtr<ax::Node> guard(node);
    std::vector<axlua::Callback<int()>> previousCallbacks;
    try
    {
        check(node->init(), "persistent native Node initialization");
        for (int epoch = 1; epoch <= 2; ++epoch)
        {
            std::printf("Lua restart regression: real VM generation %d\n", epoch);
            auto* engine = ax::LuaEngine::getInstance();
            ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
            auto* state = engine->getLuaStack()->getLuaState();
            check(state != nullptr, "new real Lua VM");
            for (const auto& callback : previousCallbacks)
                check(callback() == 0, "old callback stays invalid after a replacement VM exists");
            lua_module_register(state);

            // Run the existing Release smoke and its assertions without edits.
            check(runLuaBindingSmoke(state) == 0, "unchanged Lua binding smoke");
            check(!guard.expired(), "C++-owned Node survives real VM recreation");
            axlua::push_object(state, node);
            lua_setglobal(state, "restartNative");
            axlua::push_object(state, node);
            lua_setglobal(state, "restartNativeAgain");
            script(state, R"lua(
                assert(rawequal(restartNative, restartNativeAgain))
                assert(restartNative:getReferenceCount() >= 1)
                assert(restartNative.restartPeerMarker == nil)
                restartNative.restartPeerMarker = 'current VM only'
                restartCalls = 0
            )lua");
            // The retained Node still has the previous VM's callback. It must
            // neither enter that dead VM nor enter this new VM accidentally.
            if (epoch > 1)
            {
                node->onEnter();
                node->onExit();
                script(state, "assert(restartCalls == 0)");
            }
            script(state, R"lua(
                restartNative:setOnEnterCallback(function()
                    restartCalls = restartCalls + 1
                end)
            )lua");
            node->onEnter();
            node->onExit();
            script(state, "assert(restartCalls == 1)");

            auto callback = epochCallback(state, epoch);
            check(callback() == epoch, "new VM callback is callable");
            previousCallbacks.push_back(callback);
            check(finishLuaBindingSmoke(state) == 0, "unchanged smoke closes the real main Lua VM");
            check(!guard.expired(), "C++ ownership survives main VM close");
            for (const auto& previous : previousCallbacks)
                check(previous() == 0, "all retained callbacks return defaults after close");
            node->onEnter();
            node->onExit();
        }
        previousCallbacks.clear();
        node->setOnEnterCallback(nullptr);
        node->release();
        std::puts("Lua real two-VM restart PASS: unchanged Release smoke, wrapper identity, VM-local peer fields, retained native Node and expired callbacks");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Lua restart regression failed: %s\n", error.what());
        ax::ScriptEngineManager::getInstance()->removeScriptEngine();
        if (!guard.expired())
            node->release();
        return 1;
    }
}
