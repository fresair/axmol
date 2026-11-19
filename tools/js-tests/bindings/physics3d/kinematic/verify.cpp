// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Shipping SDK/SM33/Jolt regression. It never creates a RenderView.
#include "runtime/JsRuntime.h"
#include "manual/Bindings.h"
#include "axmol/base/AutoreleasePool.h"
#include "axmol/base/Director.h"
#include "axmol/base/EventDispatcher.h"
#include "axmol/base/Scheduler.h"
#include "axmol/base/UserDefault.h"
#include "axmol/platform/FileUtils.h"
#include "axmol/physics/physics-3d.h"
#if defined(AX_ENABLE_PHYSICS_2D)
#include "axmol/physics/2d/PhysicsWorld2D.h"
#endif
#include "StandaloneFiles.h"
#include "ContactSnapshot.h"
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <filesystem>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <memory>
#include <string>
#include <vector>

namespace
{
// Obtain a pointer to the real base-class frame entry without changing Scene,
// casting its object, or introducing a replacement fixedUpdate implementation.
struct SceneFrameAccess : ax::Scene { using ax::Scene::tick; };
int observedScenesCreated{}, observedScenesAlive{};

// A real Scene subtype calls the actual base fixedUpdate then reads Jolt.
// It neither replaces the solver nor casts an ordinary Scene to a subtype.
class StepObservedScene final : public ax::Scene
{
public:
    StepObservedScene() : nativeSceneSerial(++observedScenesCreated) { ++observedScenesAlive; }
    ~StepObservedScene() override
    {
        --observedScenesAlive;
        std::cout << "KINEMATIC_STATIC_RECORD {\"kind\":\"candidate-scene-subclass-destroying\",\"nativeSceneSerial\":"
                  << nativeSceneSerial << ",\"observedScenesAliveBeforeBaseDestruction\":" << observedScenesAlive
                  << ",\"scope\":\"Scene base destructor still follows this record\"}\n";
    }
    const int nativeSceneSerial;
    ax::WeakPtr<ax::Rigidbody3D> observed;
    ax::WeakPtr<ax::Node> transferOwner;
    ax::WeakPtr<ax::Scene> transferScene;
    std::string lastFrame;
    int activeStep() const { return static_cast<int>(_steps.size()); }
    bool jsSetterCallsSucceeded() const { return _jsSetterCallsSucceeded; }
    struct Mutation { int step; std::string kind; float value; };
    std::vector<Mutation> mutations;
    void beginObservedFrame(float dt, int generation)
    {
        _dt = dt; _generation = generation; ++_frame; _steps.clear();
        _jsAutoStepCalls.clear(); _jsSetterCallsSucceeded = true;
        _accumulatorBefore = _fixedAccumulator; _before = nativeBodyJson();
        _fixedDeltaBefore = _fixedDeltaTime; _maxStepsBefore = _maxFixedStepsPerFrame;
        _appliedDelta = (std::min)(dt * _timeScale, _maxDeltaTime);
        _fixedEnabledBefore = _fixedUpdateEnabled;
    }
    void finishObservedFrame()
    {
        std::ostringstream out; out << std::setprecision(17);
        out << "{\"kind\":\"native-multi-step-frame\",\"vmGeneration\":" << _generation
            << ",\"nativeSceneSerial\":" << nativeSceneSerial
            << ",\"frame\":" << _frame << ",\"dt\":" << _dt
            << ",\"fixedDelta\":" << _fixedDeltaTime << ",\"maxFixedSteps\":" << _maxFixedStepsPerFrame
            << ",\"timeScale\":" << _timeScale << ",\"accumulatorBefore\":" << _accumulatorBefore
            << ",\"fixedDeltaBefore\":" << _fixedDeltaBefore << ",\"maxStepsBefore\":" << _maxStepsBefore
            << ",\"appliedDelta\":" << _appliedDelta
            << ",\"fixedEnabledBefore\":" << (_fixedEnabledBefore?"true":"false")
            << ",\"accumulatorAfter\":" << _fixedAccumulator << ",\"before\":" << _before << ",\"steps\":[";
        for (size_t i=0;i<_steps.size();++i) { if(i)out<<',';out<<_steps[i]; }
        out << "],\"jsAutoStepCalls\":[";
        for (size_t i = 0; i < _jsAutoStepCalls.size(); ++i)
        {
            if (i) out << ',';
            out << _jsAutoStepCalls[i];
        }
        out << "],\"jsSetterCallsSucceeded\":" << (_jsSetterCallsSucceeded ? "true" : "false")
            << ",\"after\":" << nativeBodyJson() << '}';
        lastFrame = out.str(); std::cout << "KINEMATIC_STATIC_RECORD " << lastFrame << '\n';
        mutations.clear();
    }
protected:
    void fixedUpdate(float dt) override
    {
        auto* body = observed.get();
        for (const auto& mutation : mutations)
        {
            if (mutation.step != static_cast<int>(_steps.size())) continue;
            const auto& kind = mutation.kind;
            if (kind == "maxSteps") setMaxFixedStepsPerFrame(static_cast<int>(mutation.value));
            else if (kind == "fixedDelta") setFixedDeltaTime(mutation.value);
            else if (kind == "sceneTimeScale") setTimeScale(mutation.value);
            else if (kind == "autoStep") getPhysicsWorld3D()->setAutoStep(mutation.value != 0);
            else if (kind == "jsAutoStep")
                _jsSetterCallsSucceeded = callShippingAutoStep(mutation.value != 0) && _jsSetterCallsSucceeded;
            else if (body)
            {
                if (kind == "ownerDirect" && transferOwner.get()) body->setOwner(transferOwner.get());
                else if (kind == "ownerTransfer" && transferOwner.get() && body->getOwner())
                {
                    body->retain();
                    static_cast<ax::Component*>(body)->onExit();
                    auto* previous = body->getOwner();
                    if (previous->removeComponent(body)) transferOwner.get()->addComponent(body);
                    body->release();
                }
                else if (kind == "crossWorld" && transferScene.get() && body->getOwner())
                {
                    auto* owner = body->getOwner(); owner->retain();
                    if (auto* parent = owner->getParent()) parent->removeChild(owner,false);
                    transferScene.get()->addChild(owner); owner->release();
                }
                else if (kind == "sync") body->setSyncFlag(static_cast<ax::PhysicsActor::SyncFlag>(static_cast<int>(mutation.value)));
                else if (kind == "velocity") body->setLinearVelocity({mutation.value,0,0});
                else if (kind == "kinematic")
                {
                    if (mutation.value != 0) body->setKinematic();
                    else if (body->getMass() == 0) body->setMotionType(ax::Rigidbody3D::MotionType::Static);
                    else body->setDynamic();
                }
                else if (kind == "enabled") body->setEnabled(mutation.value != 0);
                else if (kind == "mass") body->setMass(mutation.value);
                else if (kind == "inertia") body->setInertia({mutation.value,mutation.value,mutation.value});
                else if (kind == "transform") body->setTransformInPhysics({mutation.value,0,0},ax::Quat::identity);
                else if (kind == "nodeSync" && body->getOwner())
                {
                    body->getOwner()->setPosition3D({mutation.value,0,0}); body->syncNodeToPhysics();
                }
                else if (kind == "position" && body->getOwner())
                {
                    body->getOwner()->setPosition3D({mutation.value,0,0});
                    body->setPosition({mutation.value,0,0},false);
                }
                else if (kind == "rotation" && body->getOwner())
                {
                    ax::Quat rotation{0,0,std::sin(mutation.value/2),std::cos(mutation.value/2)};
                    body->getOwner()->setRotationQuat(rotation); body->setRotation(rotation,true);
                }
            }
        }
        const bool solverStepped = getPhysicsWorld3D()->isAutoStep();
        ax::Scene::fixedUpdate(dt);
        std::ostringstream out;out << std::setprecision(17);
        out << "{\"step\":" << _steps.size() << ",\"dt\":" << dt
            << ",\"solverStepped\":" << (solverStepped?"true":"false")
            << ",\"native\":" << nativeBodyJson() << '}';
        _steps.push_back(out.str());
    }
private:
    bool callShippingAutoStep(bool enabled)
    {
        // Enter the exported JS method in the linked SDK's actual generated
        // consumer. No direct C++ setter, replacement JS function, or retained
        // JS root participates in this control. All roots end at this call.
        auto* engine = ax::JsEngine::getInstance();
        JSContext* cx = engine->getContext();
        if (!cx || !engine->getGlobalObject()) return false;
        JSAutoCompartment compartment(cx, engine->getGlobalObject());
        auto* world = getPhysicsWorld3D();
        const bool before = world->isAutoStep();
        JS::RootedValue worldValue(cx), setter(cx), registeredSetter(cx);
        JS::RootedValue argument(cx, JS::BooleanValue(enabled)), result(cx);
        JS::RootedObject prototype(cx, axjs::getPrototype<ax::PhysicsWorld3D>(cx));
        bool resolved = axjs::wrapNative(cx, world, &worldValue) && worldValue.isObject();
        JS::RootedObject receiver(cx, resolved ? &worldValue.toObject() : nullptr);
        resolved = resolved && prototype && JS_GetProperty(cx, receiver, "setAutoStep", &setter) &&
                   JS_GetProperty(cx, prototype, "setAutoStep", &registeredSetter);
        const bool registeredMethod = resolved && setter.isObject() && registeredSetter.isObject() &&
                                      &setter.toObject() == &registeredSetter.toObject() &&
                                      JS_ObjectIsCallable(cx, &setter.toObject());
        if (!registeredMethod && !JS_IsExceptionPending(cx))
            JS_ReportError(cx, "Shipping PhysicsWorld3D.setAutoStep must resolve to its registered callable");
        const bool called = registeredMethod &&
                            JS_CallFunctionValue(cx, receiver, setter, JS::HandleValueArray(argument), &result);
        const bool after = world->isAutoStep();
        std::ostringstream out;
        out << "{\"kind\":\"shipping-js-mid-step-autoStep-call\",\"step\":" << _steps.size()
            << ",\"enabled\":" << (enabled ? "true" : "false")
            << ",\"autoStepBefore\":" << (before ? "true" : "false")
            << ",\"autoStepAfter\":" << (after ? "true" : "false")
            << ",\"registeredPrototypeSetter\":" << (registeredMethod ? "true" : "false")
            << ",\"callSucceeded\":" << (called ? "true" : "false") << '}';
        _jsAutoStepCalls.push_back(out.str());
        return called;
    }

    std::string nativeBodyJson() const
    {
        auto* body=observed.get();
        if(!body || !body->isAttached())return "{\"attached\":false}";
        auto* owner=body->getOwner();
        const auto node=owner?owner->getPosition3D():ax::Vec3::zero;
        const auto nodeRotation=owner?owner->getRotationQuat():ax::Quat::identity;
        JPH::BodyLockRead lock(body->getWorld()->internalHandle().GetBodyLockInterface(),body->internalHandle());
        if(!lock.Succeeded())return "{\"attached\":true,\"lockSucceeded\":false}";
        const auto& native=lock.GetBody();const auto position=native.GetPosition();
        const auto velocity=native.IsStatic()?JPH::Vec3::sZero():native.GetLinearVelocity();
        const auto rotation=native.GetRotation();
        const auto angularVelocity=native.IsStatic()?JPH::Vec3::sZero():native.GetAngularVelocity();
        auto* motion=native.GetMotionPropertiesUnchecked();
        const auto inverseMass=motion?motion->GetInverseMassUnchecked():0.0f;
        std::ostringstream out;out << std::setprecision(17);
        out << "{\"attached\":true,\"lockSucceeded\":true,\"bodyId\":" << native.GetID().GetIndexAndSequenceNumber()
            << ",\"ownerNativeAddress\":\"" << static_cast<const void*>(owner) << "\""
            << ",\"worldNativeAddress\":\"" << static_cast<const void*>(body->getWorld()) << "\""
            << ",\"worldMatchesObservedScene\":" << (body->getWorld()==_physicsWorld3D?"true":"false")
            << ",\"motionType\":" << int(native.GetMotionType()) << ",\"inverseMass\":" << inverseMass
            << ",\"zeroStepSyncEnabled\":" << (body->isKinematicZeroStepSyncEnabled()?"true":"false")
            << ",\"active\":" << (native.IsActive()?"true":"false")
            << ",\"rigidbodySize\":" << sizeof(ax::Rigidbody3D) << ",\"worldSize\":" << sizeof(ax::PhysicsWorld3D)
            << ",\"hasMotionProperties\":" << (motion?"true":"false")
            << ",\"position\":{\"x\":" << position.GetX()<<",\"y\":"<<position.GetY()<<",\"z\":"<<position.GetZ()<<'}'
            << ",\"velocity\":{\"x\":"<<velocity.GetX()<<",\"y\":"<<velocity.GetY()<<",\"z\":"<<velocity.GetZ()<<'}'
            << ",\"rotation\":{\"x\":"<<rotation.GetX()<<",\"y\":"<<rotation.GetY()<<",\"z\":"<<rotation.GetZ()<<",\"w\":"<<rotation.GetW()<<'}'
            << ",\"angularVelocity\":{\"x\":"<<angularVelocity.GetX()<<",\"y\":"<<angularVelocity.GetY()<<",\"z\":"<<angularVelocity.GetZ()<<'}'
            << ",\"nodeRotation\":{\"x\":"<<nodeRotation.x<<",\"y\":"<<nodeRotation.y<<",\"z\":"<<nodeRotation.z<<",\"w\":"<<nodeRotation.w<<'}'
            << ",\"node\":{\"x\":"<<node.x<<",\"y\":"<<node.y<<",\"z\":"<<node.z<<"}}";
        return out.str();
    }
    std::vector<std::string> _steps;
    std::vector<std::string> _jsAutoStepCalls;
    std::string _before;
    double _accumulatorBefore{};
    int _generation{},_frame{},_maxStepsBefore{};
    float _dt{},_fixedDeltaBefore{},_appliedDelta{};
    bool _fixedEnabledBefore{};
    bool _jsSetterCallsSucceeded{true};
};

bool stepMutation(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::Scene* scene{};
    StepObservedScene::Mutation mutation;
    if(argc!=4||!axjs::toNative(cx,args[0],&scene)||!axjs::fromJsValue(cx,args[1],mutation.step)||
       !axjs::fromJsValue(cx,args[2],mutation.kind)||!axjs::fromJsValue(cx,args[3],mutation.value))return false;
    auto* observed=dynamic_cast<StepObservedScene*>(scene);if(!observed)return false;
    observed->mutations.push_back(std::move(mutation));args.rval().setUndefined();return true;
}

bool configureTransfer(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::Scene* scene{};ax::Scene* destination{};ax::Node* owner{};
    if(argc!=3||!axjs::toNative(cx,args[0],&scene))return false;
    if(!args[1].isNull()&&!axjs::toNative(cx,args[1],&owner))return false;
    if(!args[2].isNull()&&!axjs::toNative(cx,args[2],&destination))return false;
    auto* observed=dynamic_cast<StepObservedScene*>(scene);if(!observed)return false;
    observed->transferOwner=owner;observed->transferScene=destination;args.rval().setUndefined();return true;
}

bool nativeOwnerControl(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::Rigidbody3D* body{};ax::Node* owner{};
    if(argc!=2||!axjs::toNative(cx,args[0],&body)||!body||!axjs::toNative(cx,args[1],&owner)||!owner)return false;
    body->setOwner(owner);args.rval().setUndefined();return true;
}

bool observedScene(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    if(argc!=0)return false;
    auto* scene=new StepObservedScene;
    if(!scene->initWithPhysics()){delete scene;return false;}
    std::cout << "KINEMATIC_STATIC_RECORD {\"kind\":\"canonical-observed-scene-created\",\"nativeSceneSerial\":"
              << scene->nativeSceneSerial << ",\"observedScenesCreated\":" << observedScenesCreated
              << ",\"observedScenesAlive\":" << observedScenesAlive;
#if defined(AX_ENABLE_PHYSICS_2D)
    auto* world2D = scene->getPhysicsWorld2D();
    const auto id2D = world2D ? world2D->internalHandle() : b2_nullWorldId;
    const bool world2DValid = world2D && b2World_IsValid(id2D);
    std::cout << ",\"box2DEnabled\":true,\"box2DWorldIndex1\":" << id2D.index1
              << ",\"box2DWorldGeneration\":" << id2D.generation
              << ",\"box2DWorldValid\":" << (world2DValid ? "true" : "false") << "}\n";
    if (!world2DValid)
    {
        delete scene;
        JS_ReportError(cx, "Canonical test Scene received an invalid real Box2D world");
        return false;
    }
#else
    std::cout << ",\"box2DEnabled\":false}\n";
#endif
    scene->autorelease();return axjs::wrapNative(cx,static_cast<ax::Scene*>(scene),args.rval());
}

bool drainAutoreleases(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc != 0) return false;
    const int before = observedScenesAlive;
    // End-of-scenario equivalent of the Director's normal frame pool clear.
    // A deliberately retained native scene remains alive across this operation.
    ax::PoolManager::getInstance()->getCurrentPool()->clear();
    std::cout << "KINEMATIC_STATIC_RECORD {\"kind\":\"candidate-scenario-autorelease-drain\",\"sceneInstancesBefore\":"
              << before << ",\"sceneInstancesAfter\":" << observedScenesAlive
              << ",\"observedScenesCreated\":" << observedScenesCreated << "}\n";
    args.rval().setUndefined(); return true;
}

bool observeBody(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::Scene* scene{};ax::Rigidbody3D* body{};
    if(argc!=2||!axjs::toNative(cx,args[0],&scene)||!axjs::toNative(cx,args[1],&body)||!body)return false;
    auto* observed=dynamic_cast<StepObservedScene*>(scene);if(!observed)return false;
    observed->observed=body;args.rval().setUndefined();return true;
}

bool lastStepFrame(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::Scene* scene{};
    if(argc!=1||!axjs::toNative(cx,args[0],&scene))return false;
    auto* observed=dynamic_cast<StepObservedScene*>(scene);if(!observed)return false;
    return axjs::toJsValue(cx,observed->lastFrame,args.rval());
}

bool activeStep(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::Scene* scene{};
    if(argc!=1||!axjs::toNative(cx,args[0],&scene))return false;
    auto* observed=dynamic_cast<StepObservedScene*>(scene);if(!observed)return false;
    args.rval().setInt32(observed->activeStep());return true;
}

struct LiveScene
{
    ax::Scene* scene{};
    ax::Rigidbody3D* sphere{};
    ax::Rigidbody3D* support{};
    ax::ContactEventListener3D* observer{};
    int generation{}, nativeContacts{};
    uint32_t supportBodyIdBefore{}, sphereBodyIdBefore{};
    bool pendingZeroStepHistory{};
    float lastSolverBaseline{}, nextTarget{};
};
std::unique_ptr<LiveScene> heldScene;
std::array<int, 3> liveCallbackCalls{};
int reportedErrors{};

bool liveCallback(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    int generation{};
    if (argc != 1 || !axjs::fromJsValue(cx, args[0], generation) || generation < 1 || generation > 2) return false;
    ++liveCallbackCalls[generation];
    args.rval().setUndefined();
    return true;
}

bool holdAcrossVm(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto held = std::make_unique<LiveScene>();
    if ((argc != 4 && argc != 6) || heldScene || !axjs::toNative(cx, args[0], &held->scene) ||
        !axjs::toNative(cx, args[1], &held->sphere) || !axjs::toNative(cx, args[2], &held->support) ||
        !axjs::fromJsValue(cx, args[3], held->generation) || !held->scene || !held->sphere || !held->support ||
        held->generation < 1 || held->generation > 2) return false;
    if (argc == 6)
    {
        if (!axjs::fromJsValue(cx, args[4], held->lastSolverBaseline) ||
            !axjs::fromJsValue(cx, args[5], held->nextTarget)) return false;
        held->pendingZeroStepHistory = true;
    }
    held->scene->retain(); // Native ownership, intentionally outlives this VM.
    held->supportBodyIdBefore = held->support->internalHandle().GetIndexAndSequenceNumber();
    held->sphereBodyIdBefore = held->sphere->internalHandle().GetIndexAndSequenceNumber();
    held->observer = ax::ContactEventListener3D::create();
    auto* value = held.get();
    held->observer->onCollisionHit = [value](ax::ContactEvent3D* event) {
        if ((event->getActorA() == value->sphere && event->getActorB() == value->support) ||
            (event->getActorB() == value->sphere && event->getActorA() == value->support)) ++value->nativeContacts;
    };
    // Native passive listener uses only production callback's existing event
    // demand. It neither enables world events nor owns a JS callable/root.
    ax::Director::getInstance()->getEventDispatcher()->addEventListenerWithFixedPriority(held->observer, -128);
    heldScene = std::move(held);
    args.rval().setUndefined();
    return true;
}

bool verifyLiveSceneAfterShutdown(const char* operation)
{
    if (!heldScene) return false;
    auto& held = *heldScene;
    auto* world = held.scene->getPhysicsWorld3D();
    const auto beforeCallbacks = liveCallbackCalls[held.generation];
    const auto beforeErrors = reportedErrors;
    const bool attachedBefore = held.sphere->isAttached() && held.support->isAttached();
    const bool gateRemoved = !held.sphere->getOwner()->getComponent("AxmolLegacyCollisionEventGate");
    const bool eventsStillDemanded = world->isGlobalEventEnabled(ax::ContactEventBits::Hit);
    const float supportPublished = held.support->getPosition().x;
    bool historyPassed = true;
    float supportNextPosition = supportPublished, supportNextVelocity = held.support->getLinearVelocity().x;
    if (held.pendingZeroStepHistory)
    {
        // Real World API entries after VM shutdown. The zero call retains
        // the original Jolt Update(0), whose pinned source has a zero-dt return.
        // This fixture observes the public native effects, not internal steps.
        world->stepSimulation(0);
        held.support->getOwner()->setPosition3D({held.nextTarget, 0, 0});
        world->stepSimulation(1.0f / 60.0f);
        supportNextPosition = held.support->getPosition().x;
        supportNextVelocity = held.support->getLinearVelocity().x;
        const float expectedVelocity = (held.nextTarget - held.lastSolverBaseline) / (1.0f / 60.0f);
        historyPassed = std::abs(supportNextPosition - held.nextTarget) < 0.00002f &&
                        std::abs(supportNextVelocity - expectedVelocity) < 0.0001f;
    }
    // Reset through BodyInterface for an independent post-VM scene probe.
    world->internalHandle().GetBodyInterface().SetPosition(
        held.sphere->internalHandle(), JPH::RVec3(0, 5, 0), JPH::EActivation::Activate);
    held.sphere->setLinearVelocity(ax::Vec3::zero);
    for (int i = 0; i < 180; ++i) world->stepSimulation(1.0f / 60.0f);
    const bool noOldJs = beforeCallbacks == liveCallbackCalls[held.generation] && beforeErrors == reportedErrors;
    bool actualZero = false, kinematic = false;
    {
        JPH::BodyLockRead lock(world->internalHandle().GetBodyLockInterface(), held.support->internalHandle());
        if (lock.Succeeded())
        {
            kinematic = lock.GetBody().GetMotionType() == JPH::EMotionType::Kinematic;
            if (const auto* motion = lock.GetBody().GetMotionPropertiesUnchecked())
                actualZero = motion->GetInverseMassUnchecked() == 0 &&
                             motion->GetLocalSpaceInverseInertiaUnchecked().GetAxisX().IsNearZero() &&
                             motion->GetLocalSpaceInverseInertiaUnchecked().GetAxisY().IsNearZero() &&
                             motion->GetLocalSpaceInverseInertiaUnchecked().GetAxisZ().IsNearZero();
        }
    }
    const auto supportBodyIdAfter = held.support->internalHandle().GetIndexAndSequenceNumber();
    const auto sphereBodyIdAfter = held.sphere->internalHandle().GetIndexAndSequenceNumber();
    const bool identitiesPreserved = supportBodyIdAfter == held.supportBodyIdBefore && sphereBodyIdAfter == held.sphereBodyIdBefore;
    ax::Director::getInstance()->getEventDispatcher()->removeEventListener(held.observer);
    held.scene->onExit();
    const bool detached = !held.sphere->isAttached() && !held.support->isAttached();
    const bool worldEmpty = world->internalHandle().GetNumBodies() == 0;
    const bool passed = beforeCallbacks > 0 && attachedBefore && gateRemoved && eventsStillDemanded &&
                        held.nativeContacts > 0 && noOldJs && kinematic && actualZero && identitiesPreserved && detached && worldEmpty && historyPassed;
    std::cout << "KINEMATIC_STATIC_RECORD {\"kind\":\"native-live-scene-after-vm-shutdown\",\"vmGeneration\":"
              << held.generation << ",\"operation\":\"" << operation << "\",\"callbacksBefore\":" << beforeCallbacks
              << ",\"callbacksAfter\":" << liveCallbackCalls[held.generation]
              << ",\"nativeFreshContactEvents\":" << held.nativeContacts
              << ",\"reportedErrorsBefore\":" << beforeErrors << ",\"reportedErrorsAfter\":" << reportedErrors
              << ",\"attachedBefore\":" << (attachedBefore ? "true" : "false")
              << ",\"productionGateRemoved\":" << (gateRemoved ? "true" : "false")
              << ",\"worldHitDemandPreserved\":" << (eventsStillDemanded ? "true" : "false")
              << ",\"supportStillRealKinematic\":" << (kinematic ? "true" : "false")
              << ",\"supportNativeBodyIdBefore\":" << held.supportBodyIdBefore
              << ",\"supportNativeBodyIdAfter\":" << supportBodyIdAfter
              << ",\"sphereNativeBodyIdBefore\":" << held.sphereBodyIdBefore
              << ",\"sphereNativeBodyIdAfter\":" << sphereBodyIdAfter
              << ",\"supportActualZeroNativeMassInertia\":" << (actualZero ? "true" : "false")
              << ",\"nativeSceneExitDetachedBodies\":" << (detached ? "true" : "false")
              << ",\"worldEmptyAfterExit\":" << (worldEmpty ? "true" : "false")
              << ",\"hadPendingZeroStepHistory\":" << (held.pendingZeroStepHistory ? "true" : "false")
              << ",\"historyBaseline\":" << held.lastSolverBaseline
              << ",\"publishedPositionBeforeShutdownProbe\":" << supportPublished
              << ",\"nextPositionAfterShutdown\":" << supportNextPosition
              << ",\"nextVelocityAfterShutdown\":" << supportNextVelocity
              << ",\"pendingHistoryPassed\":" << (historyPassed ? "true" : "false")
              << ",\"passed\":" << (passed ? "true" : "false") << "}\n";
    held.scene->cleanup();
    held.scene->release();
    heldScene.reset();
    return passed;
}

bool record(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string value;
    if (argc != 1 || !axjs::fromJsValue(cx, args[0], value)) return false;
    std::cout << "KINEMATIC_STATIC_RECORD " << value << '\n';
    args.rval().setUndefined();
    return true;
}

bool snapshot(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::Rigidbody3D* body{};
    if (argc != 1 || !axjs::toNative(cx, args[0], &body) || !body) return false;
    JS::RootedObject result(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
    auto property = [&](const char* key, const auto& item) {
        JS::RootedValue value(cx);
        return axjs::toJsValue(cx, item, &value) && JS_SetProperty(cx, result, key, value);
    };
    int nativeType = -1, nativeBodyId = -1;
    float inverseMass = -1;
    ax::Vec3 localInverseInertiaDiagonal{};
    bool hasMotionProperties = false, zeroInverseInertia = false, active = false;
    if (body->isAttached())
    {
        JPH::BodyLockRead lock(body->getWorld()->internalHandle().GetBodyLockInterface(), body->internalHandle());
        if (!lock.Succeeded()) return false;
        const auto& native = lock.GetBody();
        nativeType = int(native.GetMotionType());
        nativeBodyId = int(native.GetID().GetIndexAndSequenceNumber());
        active = native.IsActive();
        if (const auto* motion = native.GetMotionPropertiesUnchecked())
        {
            hasMotionProperties = true;
            inverseMass = motion->GetInverseMassUnchecked();
            const auto matrix = motion->GetLocalSpaceInverseInertiaUnchecked();
            localInverseInertiaDiagonal = {matrix.GetAxisX().GetX(), matrix.GetAxisY().GetY(), matrix.GetAxisZ().GetZ()};
            zeroInverseInertia = matrix.GetAxisX().IsNearZero() && matrix.GetAxisY().IsNearZero() &&
                                 matrix.GetAxisZ().IsNearZero();
        }
        else inverseMass = 0;
    }
    if (!result || !property("attached", body->isAttached()) ||
        !property("zeroStepSyncEnabled",body->isKinematicZeroStepSyncEnabled()) ||
        !property("axmolMotionType", int(body->getMotionType())) || !property("nativeJoltMotionType", nativeType) ||
        !property("nativeInverseMass", inverseMass) || !property("nativeZeroInverseInertia", zeroInverseInertia) ||
        !property("nativeLocalInverseInertiaDiagonal", localInverseInertiaDiagonal) ||
        !property("nativeHasMotionProperties", hasMotionProperties) || !property("nativeActive", active) ||
        !property("publicNativeMass", body->getMass()) || !property("publicNativeInertia", body->getInertia()) ||
        !property("nativeBodyId", nativeBodyId) || !property("nativePosition", body->getPosition()) ||
        !property("nativeInverseMassFinite", std::isfinite(inverseMass)) || !property("bodySize", sizeof(ax::Rigidbody3D)) ||
        !property("nativeObjectLayer", body->isAttached() ? int(body->getWorld()->internalHandle().GetBodyInterface().GetObjectLayer(body->internalHandle())) : -1) ||
        !property("nativeVelocity", body->getLinearVelocity())) return false;
    args.rval().setObject(*result);
    return true;
}

bool frame(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::Scene* scene{};
    float dt{};
    if (argc != 2 || !axjs::toNative(cx, args[0], &scene) || !scene || !axjs::fromJsValue(cx, args[1], dt) ||
        dt < 0 || dt > 2.0f) return false;
    // Same actual Scheduler -> script frame -> Scene.tick ordering as Director.
    // A headless host explicitly drives these entries; no GPU claim follows.
    auto* scheduler = ax::Director::getInstance()->getScheduler();
    const float scriptDelta = dt * scheduler->getTimeScale();
    scheduler->update(dt);
    if (auto* engine = ax::ScriptEngineManager::getScriptEngineIfExists()) engine->updateFrame(scriptDelta);
    auto* observed=dynamic_cast<StepObservedScene*>(scene);
    if(observed)
    {
        JS::RootedObject global(cx,ax::JsEngine::getInstance()->getGlobalObject());JS::RootedValue value(cx);
        int generation{};
        if(!JS_GetProperty(cx,global,"__kinGeneration",&value)||!axjs::fromJsValue(cx,value,generation))return false;
        observed->beginObservedFrame(dt,generation);
    }
    (scene->*&SceneFrameAccess::tick)(dt);
    if (observed)
    {
        observed->finishObservedFrame();
        if (!observed->jsSetterCallsSucceeded())
        {
            if (!JS_IsExceptionPending(cx)) JS_ReportError(cx, "Shipping JS mid-step setter call failed");
            return false;
        }
    }
    args.rval().setUndefined();
    return true;
}

bool modernKinematic(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::Rigidbody3D* body{};
    if (argc != 1 || !axjs::toNative(cx, args[0], &body) || !body) return false;
    body->setKinematic(); // Modern C++ entry, without the legacy JS wrapper.
    args.rval().setUndefined();
    return true;
}

bool nativeMasslessKinematic(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    if(argc!=0)return false;
    auto* collider=ax::BoxCollider3D::create({2,2,2});
    auto* body=collider?ax::Rigidbody3D::create(collider,0):nullptr;
    if(!body)return false;
    body->setKinematic();
    return axjs::wrapNative(cx,body,args.rval());
}

bool nativeRotationControl(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::Rigidbody3D* body{};
    if (argc != 2 || !axjs::toNative(cx, args[0], &body) || !body || !args[1].isObject()) return false;
    JS::RootedObject object(cx, &args[1].toObject());
    ax::bvec3 axes{};
    JS::RootedValue x(cx), y(cx), z(cx);
    if (!JS_GetProperty(cx, object, "x", &x) || !JS_GetProperty(cx, object, "y", &y) ||
        !JS_GetProperty(cx, object, "z", &z) || !axjs::fromJsValue(cx, x, axes.x) ||
        !axjs::fromJsValue(cx, y, axes.y) || !axjs::fromJsValue(cx, z, axes.z)) return false;
    body->setRotationEnabled(axes); // Explicit C++ entry, not an exported JS API claim.
    args.rval().setUndefined();
    return true;
}

bool nativeTransformControl(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::Rigidbody3D* body{};float offset{};
    if (argc != 2 || !axjs::toNative(cx,args[0],&body) || !body || !axjs::fromJsValue(cx,args[1],offset))return false;
    body->setTransformInPhysics({offset,0,0},ax::Quat::identity);
    args.rval().setUndefined();return true;
}

bool nativeDeactivate(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::Rigidbody3D* body{};
    if(argc!=1||!axjs::toNative(cx,args[0],&body)||!body||!body->isAttached())return false;
    body->getWorld()->internalHandle().GetBodyInterface().DeactivateBody(body->internalHandle());
    args.rval().setUndefined();return true;
}

bool nativeRawPose(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); ax::Rigidbody3D* body{};
    std::string kind; float value{};
    if (argc != 3 || !axjs::toNative(cx, args[0], &body) || !body || !body->isAttached() ||
        !axjs::fromJsValue(cx, args[1], kind) || !axjs::fromJsValue(cx, args[2], value)) return false;
    auto& native = body->getWorld()->internalHandle().GetBodyInterface();
    if (kind == "position") native.SetPosition(body->internalHandle(), JPH::RVec3(value, 0, 0), JPH::EActivation::DontActivate);
    else if (kind == "rotation") native.SetRotation(body->internalHandle(), JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), value), JPH::EActivation::DontActivate);
    else return false;
    args.rval().setUndefined(); return true;
}
}

int main(int argc, char** argv)
{
    std::cout << std::unitbuf << std::setprecision(17);
    if (argc != 2 && argc != 3)
    {
        std::cerr << "Usage: js-physics3d-kinematic-tests <repository-root> [fixture-name.js]\n";
        return 2;
    }
    const auto root = std::filesystem::absolute(argv[1]);
    const std::string fixtureName = argc == 3 ? argv[2] : "MultiSteps.js";
    if (fixtureName != "MultiSteps.js" && fixtureName != "DirectPosition.js" &&
        fixtureName != "DirectRotation.js" && fixtureName != "PublicActivation.js" &&
        fixtureName != "ZeroStep.js" && fixtureName != "ZeroStepMatrix.js")
    {
        std::cerr << "Unknown canonical Kinematic fixture: " << fixtureName << '\n';
        return 2;
    }
    const auto here = root / "tools/js-tests/bindings/physics3d/kinematic";
    const auto fixtureScript = here / fixtureName;
    const bool requireLiveScene = fixtureName == "MultiSteps.js" || fixtureName == "ZeroStepMatrix.js";
    const auto storage = physics3dtest::storagePath(
        std::filesystem::temp_directory_path() / "axmol-js-physics3d-kinematic" / fixtureName);
    physics3dtest::installFiles(root, storage);
    auto* files = ax::FileUtils::getInstance();
    files->addSearchPath((root / "extensions/scripting/js-bindings").string());
    files->addSearchPath(root.string());
    ax::UserDefault::setFileName("canonical-kinematic.bin");
    auto* engine = ax::JsEngine::getInstance();
    ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
    engine->addRegisterCallback(axjs::registerAll);
    engine->addRegisterCallback([](JSContext* cx, JS::HandleObject global) {
        return JS_DefineFunction(cx, global, "__kinRecord", record, 1, 0) &&
               JS_DefineFunction(cx, global, "__kinSnapshot", snapshot, 1, 0) &&
               JS_DefineFunction(cx, global, "__kinFrame", frame, 2, 0) &&
               JS_DefineFunction(cx, global, "__kinModernSetKinematic", modernKinematic, 1, 0) &&
               JS_DefineFunction(cx, global, "__kinNativeMasslessKinematic", nativeMasslessKinematic, 0, 0) &&
               JS_DefineFunction(cx, global, "__kinNativeRotationControl", nativeRotationControl, 2, 0) &&
               JS_DefineFunction(cx, global, "__kinNativeTransformControl", nativeTransformControl, 2, 0) &&
               JS_DefineFunction(cx, global, "__kinNativeDeactivate", nativeDeactivate, 1, 0) &&
               JS_DefineFunction(cx, global, "__kinNativeRawPose", nativeRawPose, 3, 0) &&
               JS_DefineFunction(cx, global, "__kinLiveCallback", liveCallback, 1, 0) &&
               JS_DefineFunction(cx, global, "__kinHoldAcrossVm", holdAcrossVm, 4, 0) &&
               JS_DefineFunction(cx, global, "__kinObservedScene", observedScene, 0, 0) &&
               JS_DefineFunction(cx, global, "__kinDrainAutoreleases", drainAutoreleases, 0, 0) &&
               JS_DefineFunction(cx, global, "__kinObserveBody", observeBody, 2, 0) &&
               JS_DefineFunction(cx, global, "__kinLastStepFrame", lastStepFrame, 1, 0) &&
               JS_DefineFunction(cx, global, "__kinStepMutation", stepMutation, 4, 0) &&
               JS_DefineFunction(cx, global, "__kinActiveStep", activeStep, 1, 0) &&
               JS_DefineFunction(cx, global, "__kinConfigureTransfer", configureTransfer, 3, 0) &&
               JS_DefineFunction(cx, global, "__kinNativeOwnerControl", nativeOwnerControl, 2, 0) &&
               physics3dtest::registerContactSnapshot(cx, global);
    });
    engine->setErrorHandler([](std::string_view message) { ++reportedErrors;std::cerr << message << '\n'; });
    ax::Director::getInstance()->getEventDispatcher()->setEnabled(true);
    ax::AutoreleasePool pool("isolated native mass-zero kinematic regression");
    bool running = engine->start(), allPassed = true;
    for (int generation = 1; running && generation <= 2; ++generation)
    {
        if (generation == 2)
        {
            running = engine->restart();
            allPassed = (heldScene ? verifyLiveSceneAfterShutdown("restart") : !requireLiveScene) && allPassed;
        }
        running = running && engine->runScript("script/jsb_boot.js");
        if (running)
        {
            JSContext* cx = engine->getContext();
            JS::RootedObject global(cx, engine->getGlobalObject());
            JS::RootedValue value(cx, JS::Int32Value(generation));
            running = JS_SetProperty(cx, global, "__kinGeneration", value);
        }
        running = running && engine->runScript(fixtureScript.string());
        bool passed = false;
        if (running)
        {
            JSContext* cx = engine->getContext();
            JS::RootedObject global(cx, engine->getGlobalObject());
            JS::RootedValue result(cx), failures(cx);
            if (JS_GetProperty(cx, global, "kinematicStaticResult", &result) && result.isObject())
            {
                JS::RootedObject object(cx, &result.toObject());
                if (JS_GetProperty(cx, object, "failures", &failures) && failures.isObject())
                {
                    uint32_t count{};
                    JS::RootedObject array(cx, &failures.toObject());
                    passed = JS_GetArrayLength(cx, array, &count) && count == 0;
                }
            }
        }
        allPassed = allPassed && running && passed;
        std::cout << "KINEMATIC_STATIC_VM generation=" << generation << " passed=" << passed << '\n';
    }
    engine->stop();
    allPassed = (heldScene ? verifyLiveSceneAfterShutdown("stop") : !requireLiveScene) && allPassed;
    ax::ScriptEngineManager::getInstance()->removeScriptEngine();
    pool.clear();
    return running && allPassed ? 0 : 1;
}
