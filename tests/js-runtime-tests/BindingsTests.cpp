/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "runtime/JsRuntime.h"
#include "generated/axjs_register_gen.h"
#include "manual/Bindings.h"
#include "axmol/base/Director.h"
#include "axmol/base/Scheduler.h"
#include "axmol/2d/ActionManager.h"
#include "axmol/base/KeyboardEvent.h"
#include "axmol/base/EventDispatcher.h"
#include "axmol/base/AutoreleasePool.h"
#include "axmol/base/UserDefault.h"
#include "axmol/renderer/Renderer.h"
#include "ProjectionTests.h"
#include "axmol/platform/FileUtils.h"
#if AX_ENABLE_EXT_SPINE
#include <spine/SkeletonAnimation.h>
#endif
#if AXJS_TEST_POINTER_SMOKE
#include "PointerTests.h"
#endif
#if AXJS_TEST_LEGACY_EXTENSIONS_SMOKE
#include "LegacyExtensionsTests.h"
#endif
#if AXJS_TEST_LEGACY_UI_SMOKE
#include "LegacyUITests.h"
#endif
#if AXJS_TEST_LEGACY_COMPONENT_SMOKE
#include "LegacyComponentTests.h"
#endif
#if AXJS_TEST_ISOLATED_COMPONENT_MODULE
#include "manual/LegacyComponentBindings.h"
#endif
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace
{
void check(bool condition,const char* message)
{ if(!condition) throw std::runtime_error(message); }
bool registration(JSContext* cx,JS::HandleObject global)
{
    if(!axjs::registerAll(cx,global))return false;
#if AXJS_TEST_ISOLATED_COMPONENT_MODULE
    // The imported SDK may predate this module; the isolated audit compiles
    // its real implementation and adds it only if registration did not.
    JS::RootedObject cc(cx);
    JS::RootedValue component(cx);
    if(!axjs::getOrCreateNamespace(cx,global,"cc",&cc)||!JS_GetProperty(cx,cc,"ComponentJS",&component))return false;
    if(component.isUndefined()&&!axjs::registerLegacyComponent(cx,global))return false;
#endif
    return true;
}
}
int main()
{
    auto* engine=ax::JsEngine::getInstance();
    ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
    engine->addRegisterCallback(registration);
    engine->setErrorHandler([](std::string_view message){std::cerr<<message<<'\n';});
    auto* files=ax::FileUtils::getInstance();
    char storagePattern[]="/tmp/axmol-js-bindings-storage-XXXXXX";
    const char* storageDirectory=mkdtemp(storagePattern);
    if(!storageDirectory) { std::cerr<<"Cannot create isolated JS test storage\n"; return 1; }
    const std::string storageRoot=storageDirectory;
    files->setWritablePath(storageRoot+"/");
    ax::UserDefault::setFileName("js-bindings-test.bin");
    auto cleanupStorage=[&] {
        ax::UserDefault::destroyInstance();
        std::error_code error;
        std::filesystem::remove_all(storageRoot,error);
    };
    const std::string sourceRoot=AXJS_TEST_SOURCE_ROOT;
    const bool initialDepthPolicy=ax::Director::getInstance()->getRenderer()->getDepthTestFor2D();
    files->addSearchPath(sourceRoot+"/extensions/scripting/js-bindings");
    files->addSearchPath(sourceRoot+"/tests/js-runtime-tests");
    files->addSearchPath(sourceRoot+"/tests/js-runtime-tests/fixtures/smoke");
    try
    {
        ax::AutoreleasePool pool("js real bindings");
        check(engine->start(),"generated/manual registration failed");
        check(engine->runScript("script/jsb_boot.js"),"compatibility bootstrap failed");
        check(engine->runScript("ConstructorSmoke.js"),"native ctor=false/plain class construction contracts failed");
        check(engine->runScript("AssociationSmoke.js"),"explicit native association/unique wrapper identity failed");
        check(engine->runScript("NodeCompatibilitySmoke.js"),"legacy Layer initialization/fractional child z-order failed");
        check(engine->runScript("MeshConstructorSmoke.js"),"legacy derived Sprite3D constructor failed");
        check(engine->runScript("LayerMultiplexSmoke.js"),"legacy variadic LayerMultiplex constructor failed");
        check(engine->runScript("LabelTTFConfigSmoke.js"),"legacy TTF config factory/invalid-font contract failed");
        check(engine->runScript(sourceRoot+"/tools/js-tests/runtime-audits/scheduler-audit/SchedulerTargetSmoke.js"),
              "legacy Scheduler target callbacks/native priority and time scale failed");
        check(engine->runScript("FontDefinitionSmoke.js"),"native empty Label/FontDefinition contracts failed");
        check(engine->runScript("TextureCompatibilitySmoke.js"),"native Texture2D compatibility entry points failed");
        check(engine->runScript("Vec2CoercionSmoke.js"),"legacy numeric-string point/size coercion failed");
        check(engine->runScript("StudioProcessBaseSmoke.js"),"native Studio ProcessBase inheritance/getters/scale/flags failed");
#if AX_ENABLE_3D
        check(engine->runScript("Effect3DCompatibilitySmoke.js"),"native EffectSprite3D/outline compatibility failed");
#endif
        {
            JS::RootedValue result(engine->getContext());
            check(engine->evalString("(function(){var item=cc.MenuItem.create();item.setEnabled(9);"
                "if(!item.isEnabled())return false;item.setEnabled(0);if(item.isEnabled())return false;"
                "var sprite=cc.Sprite.__allocate();sprite.setFlippedY(1);if(!sprite.isFlippedY())return false;"
                "sprite.setFlippedY(0);if(sprite.isFlippedY())return false;"
                "sprite.setOpacity(127.5);if(sprite.getOpacity()!==127)return false;"
                "sprite.setColor(cc.color(19,37,53,255));if(sprite.getOpacity()!==127)return false;"
                "var node=new cc.Node();node.setOpacity(127.5);node.setColor(cc.color(31,47,59,255));"
                "var atlas=cc.LabelAtlas.__allocate();atlas.setColor(cc.color(23,47,71));"
                "atlas.setOpacityModifyRGB(true);atlas.setOpacity(200);if(atlas.getOpacity()!==200)return false;"
                "atlas.setOpacity(32);if(atlas.getOpacity()!==32)return false;"
                "atlas.setColor(cc.color(19,31,43,255));if(atlas.getOpacity()!==32)return false;"
                "var curve=cc.bezierTo(1,[cc.p(10,4),cc.p(20,8),cc.p(30,12),cc.p(999,999)]);"
                "curve.startWithTarget(node);curve.update(1);"
                "return node.getPositionX()===30&&node.getPositionY()===12&&node.getOpacity()===127&&"
                "JSON.stringify(node.getColor())==='{\"r\":31,\"g\":47,\"b\":59}';})()",&result)&&result.isTrue(),
                "Original numeric bool/fractional byte arguments or independent opacity failed in actual bindings");
        }
#if AX_ENABLE_3D
        check(engine->runScript(sourceRoot+"/tools/js-tests/bindings/full-suite/native-helpers-smoke.js"),
            "full-suite native matrix/geometry/property/loader helper smoke failed");
#endif
        check(axjs::tests::runProjectionSmoke(*engine),"native Director projection/depth compatibility failed");
#if AXJS_TEST_LEGACY_EXTENSIONS_SMOKE
        check(axjs::tests::runLegacyExtensionsSmoke(*engine),"native legacy Control/Scroll/Table delegates failed");
#endif
#if AXJS_TEST_LEGACY_UI_SMOKE
        check(axjs::tests::runLegacyUISmoke(*engine),"native legacy Widget/init/layout/focus contracts failed");
#endif
#if AXJS_TEST_LEGACY_COMPONENT_SMOKE
        check(axjs::tests::runLegacyComponentSmoke(*engine),"native ComponentJS script completion/lifecycle/update contracts failed");
#endif
        check(engine->runScript("BindingSmoke.js"),"generated binding smoke failed");
        check(engine->runScript("LifecycleSmoke.js"),"native Node lifecycle/input compatibility failed");
        check(engine->runScript("AssetsSmoke.js"),"assets manager compatibility failed");
        auto* inputDispatcher=ax::Director::getInstance()->getEventDispatcher();
        const bool inputWasEnabled=inputDispatcher->isEnabled();
        inputDispatcher->setEnabled(true);
        check(engine->runScript("KeyboardSmoke.js"),"legacy keyboard descriptor setup failed");
        {
            ax::KeyboardEvent key(ax::KeyboardEvent::KeyCode::KEY_A,ax::InputPhase::KeyDown);
            auto* dispatcher=ax::Director::getInstance()->getEventDispatcher();
            dispatcher->dispatchEvent(&key);
            dispatcher->dispatchEvent(&key);
            check(engine->executeGlobalFunction("keyboardSmokeResult")==0,"keyboard mapping/callback scope/self removal failed");
        }
#if AXJS_TEST_POINTER_SMOKE
        check(axjs::tests::runPointerSmoke(engine),"native pointer listener compatibility failed");
#endif
        inputDispatcher->setEnabled(inputWasEnabled);
        check(engine->runScript("PoolSmoke.js"),"pool setup failed");
        ax::WeakPtr<ax::Node> restoredPoolObject, pooledAcrossVm;
        {
            JSContext* cx=engine->getContext();
            JS::RootedValue value(cx);
            ax::Node* object{};
            check(engine->evalString("poolSmokeA",&value)&&axjs::toNative(cx,value,&object),"pool A handle failed");
            restoredPoolObject=object;
            check(engine->evalString("poolSmokeB",&value)&&axjs::toNative(cx,value,&object),"pool B handle failed");
            pooledAcrossVm=object;
        }
        pool.clear();
        check(restoredPoolObject.get()&&pooledAcrossVm.get(),"pool did not retain across autorelease frame");
        check(engine->executeGlobalFunction("poolSmokeRestore")==0,"pool restore failed");
        pool.clear();
        check(restoredPoolObject.get()&&restoredPoolObject->getParent(),"restored pool object died before attachment");
        check(engine->evalString("poolSmokeParent.removeChild(poolSmokeA,true);poolSmokeParent.release();"),"pool detach cleanup failed");
        check(restoredPoolObject.expired(),"restored object retains hidden pool ownership");
        check(engine->runScript("SchedulerSmoke.js"),"scheduler setup failed");
        auto* scheduler=ax::Director::getInstance()->getScheduler();
        for(int i=0;i<7;++i) scheduler->update(0.1f);
        check(engine->executeGlobalFunction("schedulerSmokeResult")==0,"scheduler priority/pause/resume/self cancel failed");
        check(engine->runScript("Es6PolyfillSmoke.js"),"ES6 standard library polyfill setup failed");
        for(int i=0;i<5;++i) scheduler->update(0.02f);
        check(engine->executeGlobalFunction("es6PolyfillSmokeVerify")==0,"ES6 Promise/Object/Symbol/Array/String polyfill verification failed");
        const std::string originalProject=sourceRoot+"/tests/js-runtime-tests/fixtures/v3";
        check(std::filesystem::exists(originalProject+"/src/ActionsTest/ActionsTest.js"),"original action definition fixture is missing");
        {
            files->addSearchPath(originalProject);
            check(engine->runScript("src/BaseTestLayer/BaseTestLayer.js") && engine->runScript("src/tests_resources.js") &&
                engine->runScript("src/tests-main.js") && engine->runScript("src/ActionsTest/ActionsTest.js"),
                "complete original ActionTest definitions failed");
            check(engine->runScript("ActionSubclassSmoke.js"),"original action subclass setup failed");
            for(int i=0;i<16;++i) ax::Director::getInstance()->getActionManager()->update(0.1f);
            check(engine->executeGlobalFunction("actionSubclassSmokeVerify")==0,
                "original custom actions/native _super/clone/reverse/composite completion failed");
        }
#if AX_ENABLE_EXT_SPINE
        char tempPattern[]="/tmp/axmol-js-spine-tests-XXXXXX";
        const char* directory=mkdtemp(tempPattern);
        check(directory!=nullptr,"Spine test directory failed");
        const std::string skeleton=std::string(directory)+"/tiny.json";
        const std::string replacement=std::string(directory)+"/replacement.json";
        const std::string atlas=std::string(directory)+"/tiny.atlas";
        // An asset without texture attachments exercises the real Spine 4.3
        // parser, animation state, callbacks and borrowed objects without GPU.
        const char* json=R"JSON({"skeleton":{"spine":"4.3.0","name":"tiny"},"bones":[{"name":"root"},{"name":"child","parent":"root","x":5}],"slots":[{"name":"slot","bone":"root"}],"skins":[{"name":"default","attachments":{}},{"name":"alt","attachments":{}}],"animations":{"idle":{"bones":{"root":{"translate":[{"x":0,"y":0},{"time":0.25,"x":10,"y":0}]}}},"other":{}}})JSON";
        check(files->writeStringToFile(json,skeleton) && files->writeStringToFile(json,replacement) &&
            files->writeStringToFile("\n",atlas),"Spine fixture write failed");
        {
            JSContext* cx=engine->getContext();
            JS::RootedObject global(cx,engine->getGlobalObject());
            JS::RootedValue v(cx);
            check(axjs::toJsValue(cx,skeleton,&v)&&JS_SetProperty(cx,global,"spineTestJson",v),"Spine source host failed");
            check(axjs::toJsValue(cx,replacement,&v)&&JS_SetProperty(cx,global,"spineTestReplacement",v),"Spine replacement host failed");
            check(axjs::toJsValue(cx,atlas,&v)&&JS_SetProperty(cx,global,"spineTestAtlas",v),"Spine atlas host failed");
        }
        check(engine->runScript("SpineSmoke.js"),"Spine 4.3 bindings/lifetimes smoke failed");
        ax::WeakPtr<spine::SkeletonAnimation> spineSource;
        {
            JSContext* cx=engine->getContext();
            JS::RootedValue value(cx);
            spine::SkeletonAnimation* source{};
            spine::SkeletonAnimation* clone{};
            check(engine->evalString("spineSmokeSource",&value)&&axjs::toNative(cx,value,&source),"Spine source handle failed");
            spineSource=source;
            check(engine->evalString("spineSmokeClone",&value)&&axjs::toNative(cx,value,&clone),"Spine clone handle failed");
            check(clone->getSkeleton()->getSkin() && clone->getSkeleton()->getSkin()->getName()=="alt",
                "Spine clone did not preserve skin");
            check(clone->getAsset()!=source->getAsset(),"Spine clone lost original asset after source reinit");
        }
        pool.clear();
        check(spineSource.expired(),"Spine source retained by clone");
        check(engine->executeGlobalFunction("spineSmokeAfterSourceDestruction")==0,"Spine clone outlives source regression failed");
        std::filesystem::remove_all(directory);
#endif
        // Restarts cancel native JS timers/listeners before destroying roots.
        check(engine->evalString("var staleTimerFired=0;setInterval(function(){staleTimerFired++;},0);"),"restart timer setup failed");
        const auto generation=engine->getGeneration();
        check(engine->restart(),"integrated VM restart failed");
        check(ax::Director::getInstance()->getRenderer()->getDepthTestFor2D()==initialDepthPolicy,
            "VM cleanup did not restore the native Renderer depth policy");
        check(engine->getGeneration()>generation,"integrated generation unchanged");
        check(pooledAcrossVm.expired(),"VM shutdown did not release native pool ownership");
        check(engine->runScript("script/jsb_boot.js"),"bootstrap after restart failed");
        for(int i=0;i<4;++i) scheduler->update(0.1f);
        check(engine->evalString("if(typeof staleTimerFired!=='undefined')throw Error('stale timer state');"),"old VM callback crossed restart");
        pool.clear();
        engine->stop();
        ax::ScriptEngineManager::getInstance()->removeScriptEngine();
        cleanupStorage();
        std::cout<<"Generated bindings, native lifecycle, scheduler and Spine regressions passed\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<"JS bindings regression failed: "<<error.what()<<'\n';
        engine->stop(); ax::ScriptEngineManager::getInstance()->removeScriptEngine(); cleanupStorage(); return 1;
    }
}
