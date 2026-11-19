/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "ProjectionTests.h"
#include "runtime/JsRuntime.h"
#include "axmol/base/Director.h"
#include "axmol/base/EventDispatcher.h"
#include "axmol/scene/Scene.h"
#include "axmol/scene/Camera.h"
#include "axmol/renderer/Renderer.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace axjs::tests
{
namespace
{
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
// Expose the existing protected transition functions as member pointers. The
// test invokes them on the real Director singleton; it creates no substitute
// Director, RenderView, Scene, Camera or graphics device.
class DirectorAccess : public ax::Director
{
public:
    static void canvas(ax::Director* director,const ax::Vec2& size)
    { (director->*(&DirectorAccess::setCanvasSize))(size); }
    static void scene(ax::Director* director,ax::Scene* next)
    {
        director->*(&DirectorAccess::_nextScene)=next;
        (director->*(&DirectorAccess::setNextScene))();
    }
};
}
bool runProjectionSmoke(ax::JsEngine& engine)
{
    auto* director=ax::Director::getInstance();auto* dispatcher=director->getEventDispatcher();
    const auto canvas=director->getCanvasSize();auto* originalScene=director->getRunningScene();
    const bool enabled=dispatcher->isEnabled();
    if(originalScene)originalScene->retain();
    bool restored=false;
    auto restore=[&] {
        if(restored)return;restored=true;
        DirectorAccess::scene(director,originalScene);
        DirectorAccess::canvas(director,canvas);
        dispatcher->setEnabled(enabled);
        if(originalScene)originalScene->release();
    };
    try
    {
        dispatcher->setEnabled(true);DirectorAccess::canvas(director,{800,600});
        auto* first=ax::Scene::create();check(first,"Cannot create a real native Scene");
        DirectorAccess::scene(director,first);
        check(engine.evalString(R"JS(
var projectionSmokeEvents=0;
var projectionSmokeListener=cc.eventManager.addCustomListener(cc.Director.EVENT_PROJECTION_CHANGED,function(event){
 if(event.getEventName()!=='director_projection_changed')throw Error('projection event name');
 projectionSmokeEvents++;
});
if(cc.director.getProjection()!==cc.Director.PROJECTION_DEFAULT)throw Error('projection default');
cc.director.setProjection(cc.Director.PROJECTION_2D);
if(cc.director.getProjection()!==0||projectionSmokeEvents!==1)throw Error('projection 2D setter/event');
cc.director.setDepthTest(1);
)JS"),"Native projection setup or event dispatch failed");
        auto* camera=first->getDefaultCamera();
        check(camera&&camera->getCameraMode()==ax::CameraMode::Ortho,"2D projection did not change the real Camera mode");
        check(std::abs(camera->getProjectionMatrix().m[15]-1)<0.0001f&&
            std::abs(camera->getProjectionMatrix().m[0]-2.0f/800)<0.0001f,
            "2D projection did not change the real Camera matrix");
        check(director->getRenderer()->getDepthTestFor2D(),"Depth setter did not change the real Renderer policy");
        check(engine.evalString("cc.director.setProjection(cc.Director.PROJECTION_3D);if(cc.director.getProjection()!==1||projectionSmokeEvents!==2)throw Error('projection 3D setter/event');"),
            "3D projection setter/event failed");
        check(camera->getCameraMode()==ax::CameraMode::Classic&&std::abs(camera->getProjectionMatrix().m[15])<0.0001f&&
            std::abs(camera->getPositionZ()-director->getZEye())<0.0001f,
            "3D projection did not restore the real calibrated Camera matrix and eye");
        check(engine.evalString("cc.director.setProjection(0);cc.director.setDepthTest(0);"),"Projection state before scene transition failed");
        check(!director->getRenderer()->getDepthTestFor2D(),"False depth setter did not clear Renderer policy");
        auto* second=ax::Scene::create();check(second,"Cannot create the second real native Scene");
        DirectorAccess::scene(director,second);
        check(second->getDefaultCamera()->getCameraMode()==ax::CameraMode::Ortho&&
            std::abs(second->getDefaultCamera()->getProjectionMatrix().m[15]-1)<0.0001f,
            "Configured projection was not reapplied by the real native scene-transition event");
        check(engine.evalString("if(projectionSmokeEvents!==3)throw Error('unexpected duplicate projection event');cc.eventManager.removeListener(projectionSmokeListener);cc.director.setDepthTest(true);"),
            "Projection listener cleanup or persistent depth setup failed");
        restore();
        std::cout<<"Real Director projection events, Camera matrices, scene transition and Renderer depth policy passed\n";
        return true;
    }
    catch(const std::exception& error)
    {restore();std::cerr<<"Projection regression failed: "<<error.what()<<'\n';return false;}
}
}
