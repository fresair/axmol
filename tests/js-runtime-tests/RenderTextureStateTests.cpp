// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Includes this adapter to exercise its actual internal context routing.
// The test does not allocate GPU textures or execute graphics commands.
#include "manual/LegacyRenderTextureBindings.cpp"
#include "axmol/rhi/RenderTarget.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace
{
unsigned assertions = 0;
void check(bool value, const char* message)
{
    ++assertions;
    if (!value) throw std::runtime_error(message);
}
bool near(float x, float y) { return std::abs(x-y) <= 2e-4f * std::max(1.0f,std::max(std::abs(x),std::abs(y))); }
void matrix(const ax::Mat4& actual, const ax::Mat4& expected, const char* message)
{
    for (unsigned i=0;i<16;++i) check(near(actual.m[i],expected.m[i]),message);
}
ax::Vec2 ndc(const ax::SceneRenderState& state, ax::Vec3 point)
{
    ax::Vec4 clip;state.getViewProjectionMatrix().transformVector(ax::Vec4(point.x,point.y,point.z,1),&clip);
    return {clip.x/clip.w,clip.y/clip.w};
}
struct CaptureNode : ax::Node
{
    std::vector<ax::SceneRenderState> captured;
    void draw(const ax::SceneRenderState& state,const ax::Mat4&,uint32_t) override {captured.push_back(state);}
};
JSClass nodeClass=[] {
    JSClass cls{};cls.name="Node";cls.flags=JSCLASS_HAS_PRIVATE;
    cls.addProperty=JS_PropertyStub;cls.delProperty=JS_DeletePropertyStub;cls.getProperty=JS_PropertyStub;
    cls.setProperty=JS_StrictPropertyStub;cls.enumerate=JS_EnumerateStub;cls.resolve=JS_ResolveStub;
    cls.convert=JS_ConvertStub;cls.finalize=axjs::finalizeNative;return cls;
}();
bool registerMinimal(JSContext* cx,JS::HandleObject global)
{
    JS::RootedObject cc(cx,axjs::manual::namespaceObject(cx,global,"cc"));
    JS::RootedObject prototype(cx,JS_InitClass(cx,cc,JS::NullPtr(),&nodeClass,nullptr,0,nullptr,nullptr,nullptr,nullptr));
    JS::RootedValue resource(cx,JS::NullValue());
    return prototype && axjs::registerType<ax::Node>(cx,prototype,&nodeClass) &&
        JS_DefineProperty(cx,cc,"RenderTexture",resource,JSPROP_ENUMERATE) &&
        axjs::registerLegacyRenderTexture(cx,global);
}
void eval(ax::JsEngine& engine,const char* script)
{
    check(engine.evalString(script),"JS adapter assertion failed");
}
}
int main()
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    try
    {
        auto* director=ax::Director::getInstance();
        director->getEventDispatcher()->setEnabled(true);
        ax::Mat4 p=ax::Mat4::identity;
        p.m[0]=2.f/960;p.m[5]=2.f/540;p.m[12]=-1;p.m[13]=-1;
        const ax::SceneRenderState original(director->getRenderer(),nullptr,
            ax::SceneViewData::fromMatrices(ax::Mat4::identity,p,ax::Vec3::zero));
        const auto originalBytes=original.getViewProjectionMatrix();
        for (auto size : {ax::Vec2(960,540),ax::Vec2(480,270),ax::Vec2(320,270)})
        {
            ax::SceneRenderState state;
            check(axjs::legacyRenderTexture::makeRenderState(original,{960,540},size,false,state),"target matrix creation");
            for (auto sample : {ax::Vec2(.25f,.25f),ax::Vec2(.75f,.75f),ax::Vec2(.75f,.25f)})
            {
                const auto point=ndc(state,{sample.x*size.width,sample.y*size.height,0});
                check(near(point.x,sample.x*2-1)&&near(point.y,sample.y*2-1),"normalized target mapping");
            }
            check(state.viewOverridden && state.cameraFlag==original.cameraFlag,"local override/camera mask");
            matrix(original.getViewProjectionMatrix(),originalBytes,"input state unchanged");
        }
        std::puts("PASS full/half/nonuniform target coordinates against expected NDC");
        ax::RefPtr<ax::Camera> camera(new ax::Camera(),tlx::adopt_object);
        check(camera->configurePerspective(60,960.f/540,.5f,1000),"real perspective camera init");
        camera->setPosition3D({480,270,468});camera->lookAt({480,270,0});
        camera->setCameraFlag(ax::CameraFlag::USER1);
        const ax::SceneRenderState perspective(director->getRenderer(),camera.get());
        const auto beforeP=camera->getProjectionMatrix(),beforeV=camera->getViewMatrix(),beforePV=camera->getViewProjectionMatrix();
        ax::SceneRenderState adjusted;
        check(axjs::legacyRenderTexture::makeRenderState(perspective,{960,540},{480,270},false,adjusted),"perspective adjustment");
        ax::Mat4 scale;ax::Mat4::createScale(2,2,-1,&scale);
        matrix(adjusted.getViewProjectionMatrix(),beforePV*scale,"old P*V*S order");
        matrix(adjusted.getProjectionMatrix()*adjusted.getViewMatrix(),beforePV*scale,"P'=P*V*S*inverse(V)");
        matrix(adjusted.getViewMatrix(),beforeV,"view matrix retained");
        check(adjusted.getView().position==perspective.getView().position,"world camera position retained");
        check(adjusted.cameraFlag==static_cast<unsigned short>(ax::CameraFlag::USER1),"camera mask retained");
        check(std::memcmp(camera->getProjectionMatrix().m,beforeP.m,sizeof(beforeP.m))==0,"native Camera P unchanged");
        check(std::memcmp(camera->getViewMatrix().m,beforeV.m,sizeof(beforeV.m))==0,"native Camera V unchanged");
        check(std::memcmp(camera->getViewProjectionMatrix().m,beforePV.m,sizeof(beforePV.m))==0,"native Camera PV unchanged");
        std::puts("PASS translated perspective camera, exact old matrix order and native Camera immutability");
        ax::RefPtr<ax::Camera> rotatedCamera(new ax::Camera(),tlx::adopt_object);
        check(rotatedCamera->configurePerspective(47,1.6f,.3f,2000),"rotated camera perspective init");
        rotatedCamera->setPosition3D({20,37,100});rotatedCamera->lookAt({480,270,0});
        const ax::SceneRenderState rotated(director->getRenderer(),rotatedCamera.get());
        const auto rotatedP=rotatedCamera->getProjectionMatrix(),rotatedV=rotatedCamera->getViewMatrix();
        ax::SceneRenderState rotatedAdjusted;
        check(axjs::legacyRenderTexture::makeRenderState(rotated,{960,540},{480,270},false,rotatedAdjusted),"rotated adjustment");
        matrix(rotatedAdjusted.getProjectionMatrix()*rotatedAdjusted.getViewMatrix(),rotated.getViewProjectionMatrix()*scale,"noncommuting rotated PVS");
        check(std::memcmp(rotatedCamera->getProjectionMatrix().m,rotatedP.m,sizeof(rotatedP.m))==0,"rotated Camera P unchanged");
        check(std::memcmp(rotatedCamera->getViewMatrix().m,rotatedV.m,sizeof(rotatedV.m))==0,"rotated Camera V unchanged");
        std::puts("PASS noncommuting rotated camera projection order");
        ax::SceneRenderState flipped;
        check(axjs::legacyRenderTexture::makeRenderState(perspective,{960,540},{480,270},true,flipped),"OpenGL local flip");
        ax::Mat4 flip;ax::Mat4::createScale(1,-1,1,&flip);
        matrix(flipped.getViewProjectionMatrix(),flip*beforePV*scale,"clip flip precedes PVS");
        matrix(flipped.getProjectionMatrix()*flipped.getViewMatrix(),flip*beforePV*scale,"flipped P/V consistency");
        std::puts("PASS OpenGL clip orientation independently of a global Camera mutation");
        for (auto size : {ax::Vec2(0,270),ax::Vec2(-1,270),ax::Vec2(INFINITY,270),ax::Vec2(480,NAN)})
        {
            auto failed=original;
            check(!axjs::legacyRenderTexture::makeRenderState(original,{960,540},size,false,failed),"invalid target rejected");
            matrix(failed.getViewProjectionMatrix(),originalBytes,"failure leaves output unchanged");
        }
        ax::Mat4 singular=ax::Mat4::identity;singular.m[0]=0;
        const ax::SceneRenderState invalid(director->getRenderer(),nullptr,
            ax::SceneViewData::fromMatrices(singular,p,ax::Vec3::zero));
        check(!axjs::legacyRenderTexture::makeRenderState(invalid,{960,540},{480,270},false,flipped),"singular view rejected");
        std::puts("PASS invalid dimensions and singular camera view failures");
        ax::RefPtr<CaptureNode> capture(new CaptureNode(),tlx::adopt_object);
        check(capture->init(),"real Node init");
        capture->setCameraMask(0xffff);
        ax::JsEngine engine;engine.addRegisterCallback(registerMinimal);
        std::shared_ptr<axjs::RenderTextureContext> oldContext;
        for (int generation=1;generation<=2;++generation)
        {
            check(engine.start(),"real VM start");
            auto* cx=engine.getContext();auto context=axjs::contexts.at(cx);
            ax::RefPtr<ax::rhi::RenderTarget> nativeTarget(new ax::rhi::RenderTarget(false),tlx::adopt_object);
            nativeTarget->_color.resize(1);
            ax::WeakPtr<CaptureNode> orphanToken;
            ax::RefPtr<ax::RenderTexturePass> pending;
            {
            JS::RootedObject global(cx,engine.getGlobalObject());JS::RootedValue wrapped(cx);
            check(axjs::toJsValue(cx,static_cast<ax::Node*>(capture.get()),&wrapped),"borrowed Node wrapper");
            check(JS_DefineProperty(cx,global,"capture",wrapped,JSPROP_ENUMERATE),"expose capture Node");
            axjs::activePasses.push_back({context,ax::RefPtr<ax::Node>(capture.get()),{},{},camera,adjusted,perspective});
            const auto nestedBase=axjs::originalForPass(adjusted);
            matrix(nestedBase.getViewProjectionMatrix(),perspective.getViewProjectionMatrix(),"nested pass resets outer ratio");
            ax::SceneRenderState nested;
            check(axjs::legacyRenderTexture::makeRenderState(nestedBase,{960,540},{240,135},false,nested),"nested quarter target state");
            ax::Mat4 quarter;ax::Mat4::createScale(4,4,-1,&quarter);
            matrix(nested.getViewProjectionMatrix(),beforePV*quarter,"nested target scales once");
            auto unrelated=adjusted;unrelated.view.viewProjection.m[12]+=1;
            matrix(axjs::originalForPass(unrelated).getViewProjectionMatrix(),unrelated.getViewProjectionMatrix(),"unrelated caller view retained");
            eval(engine,"capture.visit();var bad=false;try{capture.visit.call({});}catch(e){bad=true;}if(!bad)throw Error('receiver was not validated');");
            check(!capture->captured.empty(),"native draw was visited by retained camera mask");
            matrix(capture->captured.back().getViewProjectionMatrix(),adjusted.getViewProjectionMatrix(),"actual JS visit receives active local state");
            axjs::activePasses.pop_back();
            eval(engine,"capture.visit();");
            matrix(capture->captured.back().getViewProjectionMatrix(),ax::Mat4::identity,"pass outside uses actual Node.visit state");
            if(oldContext)
            {
                axjs::activePasses.push_back({oldContext,ax::RefPtr<ax::Node>(capture.get()),{},{},camera,adjusted});
                eval(engine,"var stale=false;try{capture.visit();}catch(e){stale=true;}if(!stale)throw Error('old generation accepted');");
                axjs::activePasses.pop_back();
            }
            // A real RenderTexturePass queues begin/end groups without executing
            // GPU work. Its unattached native target is retained by this fixture
            // through queue cleanup; no graphics content result is asserted.
            std::vector<ax::RefPtr<ax::RenderTexturePass>> passes;
            for (unsigned i=0;i<2;++i)
            {
                ax::RefPtr<ax::RenderTexturePass> pass(ax::RenderTexturePass::obtain(),tlx::adopt_object);
                pass->setTarget(nativeTarget.get());pass->setCameraOverrideEnabled(false);pass->begin();
                check(pass->isActive(),"actual native pass active");
                axjs::activePasses.push_back({context,ax::RefPtr<ax::Node>(capture.get()),pass,{},camera,adjusted});
                passes.push_back(pass);
            }
            axjs::finishTopPass();
            check(!passes[1]->isActive()&&passes[0]->isActive(),"inner pass closes first");
            check(axjs::activePasses.size()==1,"outer scope remains active");
            // Simulate the actual update-phase JavaScript exception shape:
            // an active outer pass has no matching end before BEFORE_DRAW.
            eval(engine,"var caught=false;try{throw Error('leave pass active');}catch(e){caught=true;}if(!caught)throw Error('exception failed');");
            director->getEventDispatcher()->dispatchCustomEvent(ax::Director::EVENT_BEFORE_DRAW);
            check(!passes[0]->isActive()&&axjs::activePasses.empty(),"abandoned pass closes before screen traversal");
            director->getRenderer()->clean();
            // Keep the sole owner reference in an active scope. VM shutdown
            // must release that native Node and restore its queued group.
            ax::RefPtr<CaptureNode> orphan(new CaptureNode(),tlx::adopt_object);check(orphan->init(),"orphan node init");
            orphanToken=orphan.get();
            pending=ax::RefPtr<ax::RenderTexturePass>(ax::RenderTexturePass::obtain(),tlx::adopt_object);
            pending->setTarget(nativeTarget.get());pending->setCameraOverrideEnabled(false);pending->begin();
            axjs::activePasses.push_back({context,ax::RefPtr<ax::Node>(orphan.get()),pending,{},camera,adjusted});
            orphan.reset();check(orphanToken.get()!=nullptr,"active scope retains target Node");
            // Local Rooted values must be gone before closing the real VM.
            oldContext=context;
            }
            engine.stop();
            check(axjs::activePasses.empty(),"VM cleanup resets active stack");
            check(!pending->isActive(),"VM cleanup closes actual native pass");
            check(orphanToken.get()==nullptr,"VM cleanup releases sole target scope owner");
            director->getRenderer()->clean();
            check(!oldContext->live(),"old context inactive on VM stop");
            check(!axjs::contexts.contains(cx),"context map erased on cleanup");
            std::printf("PASS actual JS visit receiver/context routing and cleanup, VM generation %d\n",generation);
        }
        // The real native RenderTexture object has no GPU allocation in this
        // CPU fixture. Exercise the queue ownership callback itself separately.
        ax::RefPtr<ax::RenderTexture> resource(new ax::RenderTexture(),tlx::adopt_object);
        ax::WeakPtr<ax::RenderTexture> resourceToken(resource.get());
        auto* ownership=axjs::keepTextureUntilExecution(director->getRenderer(),resource);
        resource.reset();check(resourceToken.get()!=nullptr,"queue retains the native texture resource");
        ownership->execute();check(resourceToken.get()==nullptr,"queue completion releases native texture resource");
        director->getRenderer()->clean();
        std::puts("PASS native texture resource retained through queued command lifetime and released on completion");
        std::printf("RenderTexture local state CPU checks PASS (%u assertions); GPU texture allocation/pixels remain separate\n",assertions);
        return 0;
    }
    catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
