/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "chipmunk/chipmunk_private.h"
#include "manual/LegacyPhysicsBindings.h"
#include "manual/LegacyDrawPrimitives.h"
#include "runtime/JsRuntime.h"
#include "axmol/base/Logging.h"
#include "axmol/2d/DrawNode.h"
#include "axmol/2d/Sprite.h"
#include "axmol/scene/Scene.h"
#include "axmol/base/Director.h"
#include "axmol/base/EventDispatcher.h"
#if defined(AX_ENABLE_PHYSICS_3D)
#include "axmol/physics/physics-3d.h"
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CollisionGroup.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#endif
#if defined(AX_ENABLE_NAVMESH)
#include "axmol/navmesh/NavMesh.h"
#endif
#include <algorithm>
#include <array>
#include <cstdio>
#include <tuple>

namespace axjs
{
template<> struct Converter<cpVect>
{
    static bool read(JSContext* cx, JS::HandleValue value, cpVect& out)
    {
        JS::RootedValue x(cx), y(cx);
        return detail::readProperty(cx, value, "x", &x) && detail::readProperty(cx, value, "y", &y) &&
               fromJsValue(cx, x, out.x) && fromJsValue(cx, y, out.y);
    }
    static bool write(JSContext* cx, const cpVect& value, JS::MutableHandleValue out)
    {
        JS::RootedObject object(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
        JS::RootedValue x(cx, JS::DoubleValue(JS::CanonicalizeNaN(value.x))), y(cx, JS::DoubleValue(JS::CanonicalizeNaN(value.y)));
        if (!object || !JS_SetProperty(cx, object, "x", x) || !JS_SetProperty(cx, object, "y", y)) return false;
        out.setObject(*object); return true;
    }
};
template<> struct Converter<cpBB>
{
    static bool read(JSContext* cx, JS::HandleValue value, cpBB& out)
    {
        const char* keys[] = {"l", "b", "r", "t"};
        cpFloat* fields[] = {&out.l, &out.b, &out.r, &out.t};
        for (int i = 0; i < 4; ++i) { JS::RootedValue v(cx); if (!detail::readProperty(cx, value, keys[i], &v) || !fromJsValue(cx, v, *fields[i])) return false; }
        return true;
    }
    static bool write(JSContext* cx, const cpBB& value, JS::MutableHandleValue out)
    {
        JS::RootedObject object(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr())); if (!object) return false;
        const char* keys[] = {"l", "b", "r", "t"}; const cpFloat fields[] = {value.l, value.b, value.r, value.t};
        for (int i = 0; i < 4; ++i) { JS::RootedValue v(cx, JS::DoubleValue(JS::CanonicalizeNaN(fields[i]))); if (!JS_SetProperty(cx, object, keys[i], v)) return false; }
        out.setObject(*object); return true;
    }
};
template<> struct Converter<cpShapeFilter>
{
    static bool read(JSContext* cx, JS::HandleValue value, cpShapeFilter& out)
    {
        JS::RootedValue group(cx), categories(cx), mask(cx); uint32_t c{}, m{};
        if (!detail::readProperty(cx, value, "group", &group) || !detail::readProperty(cx, value, "categories", &categories) ||
            !detail::readProperty(cx, value, "mask", &mask) || !fromJsValue(cx, group, out.group) ||
            !JS::ToUint32(cx, categories, &c) || !JS::ToUint32(cx, mask, &m)) return false;
        out.categories = c; out.mask = m; return true;
    }
    static bool write(JSContext* cx, const cpShapeFilter& value, JS::MutableHandleValue out)
    {
        JS::RootedObject object(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr())); if (!object) return false;
        JS::RootedValue group(cx, JS::DoubleValue(value.group)), categories(cx, JS::DoubleValue(value.categories)), mask(cx, JS::DoubleValue(value.mask));
        if (!JS_SetProperty(cx, object, "group", group) || !JS_SetProperty(cx, object, "categories", categories) || !JS_SetProperty(cx, object, "mask", mask)) return false;
        out.setObject(*object); return true;
    }
};

template<> struct Converter<cpTransform>
{
    static bool read(JSContext* cx, JS::HandleValue value, cpTransform& out)
    {
        const char* keys[] = {"a", "b", "c", "d", "tx", "ty"};
        cpFloat* fields[] = {&out.a, &out.b, &out.c, &out.d, &out.tx, &out.ty};
        for (int i = 0; i < 6; ++i) { JS::RootedValue v(cx); if (!detail::readProperty(cx, value, keys[i], &v) || !fromJsValue(cx, v, *fields[i])) return false; }
        return true;
    }
};

namespace tests
{
// Sticky observations survive native/VM destruction without retaining either.
struct ChipmunkLifecycleFacts
{
    std::atomic<uint32_t> explicitFreeCalls{}, nativeSpaceFreeCalls{};
    std::atomic<uint32_t> spacePeerDestructions{}, spaceStateDestructions{};
    std::atomic<ChipmunkSpaceFreeReason> firstNativeFreeReason{ChipmunkSpaceFreeReason::None};
};
}

namespace legacyphysics
{
struct Base { virtual ~Base() = default; };
struct Space;
struct Body;
struct Shape;
struct Constraint;
struct ShapeState;
struct ConstraintState;
struct SpaceState;
struct BodyState : std::enable_shared_from_this<BodyState>
{
    cpBody* native{}; bool owned{true}; Body* proxy{};
    std::weak_ptr<SpaceState> space;
    std::shared_ptr<Callback> velocity;
    double velocityLimit{INFINITY}, angularVelocityLimit{INFINITY};
    std::vector<ax::WeakPtr<ax::Sprite>> sprites;
    std::vector<std::weak_ptr<ShapeState>> shapes;
    std::vector<std::weak_ptr<ConstraintState>> constraints;
    ~BodyState() { if (native && owned) cpBodyFree(native); }
};
struct ShapeState : std::enable_shared_from_this<ShapeState>
{
    using PeerFactory = bool (*)(JSContext*, const std::shared_ptr<ShapeState>&, JS::MutableHandleValue);
    cpShape* native{}; Shape* proxy{};
    PeerFactory restorePeer{};
    std::shared_ptr<BodyState> body; std::weak_ptr<SpaceState> space;
    // Preserve the public input vertex order used by Chipmunk-JS getters. The
    // C solver independently computes its convex hull and winding.
    std::vector<cpVect> vertices;
    ~ShapeState() { if (native) cpShapeFree(native); }
};
struct ConstraintState : std::enable_shared_from_this<ConstraintState>
{
    using PeerFactory = bool (*)(JSContext*, const std::shared_ptr<ConstraintState>&, JS::MutableHandleValue);
    cpConstraint* native{}; Constraint* proxy{};
    PeerFactory restorePeer{};
    std::shared_ptr<BodyState> a, b; std::weak_ptr<SpaceState> space;
    ~ConstraintState() { if (native) cpConstraintFree(native); }
};
struct Handler : std::enable_shared_from_this<Handler>
{
    JSContext* context{}; SpaceState* space{};
    std::shared_ptr<Callback> begin, pre, post, separate;
};
struct PostStep { std::shared_ptr<Callback> callback; std::weak_ptr<SpaceState> space; std::string key; };
struct SpaceState : std::enable_shared_from_this<SpaceState>
{
    cpSpace* native{}; Space* proxy{};
    std::shared_ptr<BodyState> staticBody;
    std::vector<std::shared_ptr<BodyState>> bodies;
    std::vector<std::shared_ptr<ShapeState>> shapes;
    std::vector<std::shared_ptr<ConstraintState>> constraints;
    std::map<std::pair<cpCollisionType, cpCollisionType>, std::shared_ptr<Handler>> handlers;
    std::vector<std::unique_ptr<PostStep>> postSteps;
    std::vector<std::weak_ptr<tests::ChipmunkLifecycleFacts>> lifecycleWatches;
    bool callbackFailed{}, closing{}, stepping{};
    unsigned callbacksInFlight{};
    template<class F> void observeLifecycle(F&& record)
    {
        std::erase_if(lifecycleWatches,[&record](auto& weak) {
            auto facts=weak.lock(); if(!facts) return true;
            record(*facts); return false;
        });
    }
    void close(tests::ChipmunkSpaceFreeReason reason)
    {
        if (!native || closing) return;
        closing = true;
        // Native removal still cleans its contact cache, but Space destruction
        // must not enter JavaScript from a GC finalizer or resurrect this peer.
        // Ordinary removeShape keeps its live separation callback behavior.
        for (auto& task : postSteps) task->callback.reset();
        for (auto& constraint : constraints) if (cpSpaceContainsConstraint(native, constraint->native)) cpSpaceRemoveConstraint(native, constraint->native);
        for (auto& shape : shapes) if (cpSpaceContainsShape(native, shape->native)) cpSpaceRemoveShape(native, shape->native);
        for (auto& body : bodies) if (cpSpaceContainsBody(native, body->native)) cpSpaceRemoveBody(native, body->native);
        if (staticBody)
        {
            // A Space owns its built-in static body. Invalidate every peer
            // referring to it, including shapes previously removed from Space.
            for (auto& weak : staticBody->constraints) if (auto joint = weak.lock()) if (joint->a == staticBody || joint->b == staticBody) { if (joint->native) cpConstraintFree(joint->native); joint->native = nullptr; }
            for (auto& weak : staticBody->shapes) if (auto shape = weak.lock()) if (shape->body == staticBody) { if (shape->native) cpShapeFree(shape->native); shape->native = nullptr; }
            staticBody->native = nullptr;
        }
        cpSpaceFree(native); native = nullptr;
        observeLifecycle([reason](auto& facts) {
            if(facts.nativeSpaceFreeCalls.load(std::memory_order_relaxed)==0)
                facts.firstNativeFreeReason.store(reason,std::memory_order_release);
            facts.nativeSpaceFreeCalls.fetch_add(1,std::memory_order_release);
        });
        constraints.clear(); shapes.clear(); bodies.clear(); handlers.clear(); postSteps.clear();
    }
    ~SpaceState()
    {
        close(tests::ChipmunkSpaceFreeReason::StateDestruction);
        observeLifecycle([](auto& facts) { facts.spaceStateDestructions.fetch_add(1,std::memory_order_release); });
    }
};
struct Body : Base
{
    std::shared_ptr<BodyState> state;
    explicit Body(std::shared_ptr<BodyState> s) : state(std::move(s)) { state->proxy = this; }
    ~Body() override { if (state->proxy == this) state->proxy = nullptr; }
    cpBody* get() const { return state->native; }
};
struct Shape : Base
{
    std::shared_ptr<ShapeState> state;
    explicit Shape(std::shared_ptr<ShapeState> s) : state(std::move(s)) { state->proxy = this; }
    ~Shape() override { if (state->proxy == this) state->proxy = nullptr; }
    cpShape* get() const { return state->native; }
};
struct CircleShape : Shape { using Shape::Shape; };
struct SegmentShape : Shape { using Shape::Shape; };
struct PolyShape : Shape { using Shape::Shape; };
struct Constraint : Base
{
    std::shared_ptr<ConstraintState> state;
    explicit Constraint(std::shared_ptr<ConstraintState> s) : state(std::move(s)) { state->proxy = this; }
    ~Constraint() override { if (state->proxy == this) state->proxy = nullptr; }
    cpConstraint* get() const { return state->native; }
};
struct PinJoint : Constraint { using Constraint::Constraint; };
struct SlideJoint : Constraint { using Constraint::Constraint; };
struct PivotJoint : Constraint { using Constraint::Constraint; };
struct GrooveJoint : Constraint { using Constraint::Constraint; };
struct DampedSpring : Constraint { using Constraint::Constraint; };
struct DampedRotarySpring : Constraint { using Constraint::Constraint; };
struct RotaryLimitJoint : Constraint { using Constraint::Constraint; };
struct RatchetJoint : Constraint { using Constraint::Constraint; };
struct GearJoint : Constraint { using Constraint::Constraint; };
struct SimpleMotor : Constraint { using Constraint::Constraint; };
struct Space : Base
{
    std::shared_ptr<SpaceState> state;
    explicit Space(std::shared_ptr<SpaceState> s) : state(std::move(s)) { state->proxy = this; }
    ~Space() override
    {
        if (state->proxy == this) state->proxy = nullptr;
        state->observeLifecycle([](auto& facts) { facts.spacePeerDestructions.fetch_add(1,std::memory_order_release); });
    }
    cpSpace* get() const { return state->native; }
};
struct Arbiter : Base { cpArbiter* native{}; explicit Arbiter(cpArbiter* n) : native(n) {} cpArbiter* get() const { return native; } };

class PhysicsSprite final : public ax::Sprite
{
public:
    std::shared_ptr<BodyState> body;
    bool ignoreBodyRotation{};
    bool isDirty() const override { return body ? true : ax::Sprite::isDirty(); }
    const ax::Vec2& getPosition() const override
    {
        if (!body || !body->native) return ax::Sprite::getPosition();
        auto p = cpBodyGetPosition(body->native); position = {static_cast<float>(p.x), static_cast<float>(p.y)}; return position;
    }
    void getPosition(float* x, float* y) const override { const auto& p = getPosition(); if (x) *x = p.x; if (y) *y = p.y; }
    void setPosition(const ax::Vec2& p) override { setPosition(p.x, p.y); }
    void setPosition(float x, float y) override { if (body && body->native) cpBodySetPosition(body->native, cpv(x,y)); ax::Sprite::setPosition(x,y); }
    float getRotation() const override { return body && body->native && !ignoreBodyRotation ? static_cast<float>(-cpBodyGetAngle(body->native)*180.0/M_PI) : ax::Sprite::getRotation(); }
    void setRotation(float angle) override { if (body && body->native && !ignoreBodyRotation) cpBodySetAngle(body->native, -angle*M_PI/180.0); ax::Sprite::setRotation(angle); }
    const ax::Mat4& getNodeToParentTransform() const override
    {
        if (body && body->native)
        {
            auto* self = const_cast<PhysicsSprite*>(this); auto p = cpBodyGetPosition(body->native);
            self->ax::Sprite::setPosition(static_cast<float>(p.x), static_cast<float>(p.y));
            if (!ignoreBodyRotation) self->ax::Sprite::setRotation(static_cast<float>(-cpBodyGetAngle(body->native)*180.0/M_PI));
        }
        return ax::Sprite::getNodeToParentTransform();
    }
private:
    mutable ax::Vec2 position;
};
static ax::Vec2 point(cpVect p) { return {static_cast<float>(p.x), static_cast<float>(p.y)}; }
// Geometry/color policy follows fixed v3 CCPhysicsDebugNode.cpp. Keep its
// license with this adaptation; Chipmunk private fields stay in this JS TU.
/* Copyright (c) 2012 Scott Lembcke and Howling Moon Software
 * Copyright (c) 2012 cocos2d-x.org
 * Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
static ax::Color debugBodyColor(cpBody* body)
{
    if (!body || cpBodyGetType(body) == CP_BODY_TYPE_STATIC || cpBodyIsSleeping(body))
        return {0.5f, 0.5f, 0.5f, 0.5f};
    auto* owner = cpBodyGetSpace(body);
    if (owner && body->sleeping.idleTime > cpSpaceGetSleepTimeThreshold(owner))
        return {0.33f, 0.33f, 0.33f, 0.5f};
    return {1, 0, 0, 0.5f};
}
static void debugSegment(ax::DrawNode& node, cpVect from, cpVect to, float radius, const ax::Color& tint)
{
    if (!appendLegacySegment(node, point(from), point(to), radius, tint))
        AXLOGE("Invalid legacy physics segment geometry");
}
static void debugDot(ax::DrawNode& node, cpVect center, float radius, const ax::Color& tint)
{
    node.drawDot(point(center), radius, legacyPrimitiveColor(tint));
}
static void drawDebugShape(cpShape* shape, void* data)
{
    auto& node = *static_cast<ax::DrawNode*>(data);
    auto* body = cpShapeGetBody(shape);
    const auto tint = debugBodyColor(body);
    switch (shape->klass->type)
    {
    case CP_CIRCLE_SHAPE:
    {
        const auto* circle = reinterpret_cast<const cpCircleShape*>(shape);
        debugDot(node, circle->tc, static_cast<float>(std::max(circle->r, 1.0)), tint);
        debugSegment(node, circle->tc, cpvadd(circle->tc, cpvmult(cpBodyGetRotation(body), circle->r)), 1, tint);
        break;
    }
    case CP_SEGMENT_SHAPE:
    {
        const auto* segment = reinterpret_cast<const cpSegmentShape*>(shape);
        debugSegment(node, segment->ta, segment->tb, static_cast<float>(std::max(segment->r, 2.0)), tint);
        break;
    }
    case CP_POLY_SHAPE:
    {
        const auto* polygon = reinterpret_cast<const cpPolyShape*>(shape);
        std::vector<ax::Vec2> vertices; vertices.reserve(polygon->count);
        for (int i = 0; i < polygon->count; ++i) vertices.push_back(point(polygon->planes[i].v0));
        auto border = tint; border.a = tint.a + (1.0f - tint.a) * 0.5f;
        if (const char* problem = appendLegacyPolygon(node, vertices, tint, 1, border))
            AXLOGE("Legacy physics polygon geometry: {}", problem);
        break;
    }
    default: AXLOGE("Unknown native Chipmunk shape in legacy debug drawing"); break;
    }
}
static cpVect debugAnchor(cpBody* body, cpVect local)
{
    return cpvadd(cpBodyGetPosition(body), cpvrotate(local, cpBodyGetRotation(body)));
}
static void drawDebugConstraint(cpConstraint* constraint, void* data)
{
    auto& node = *static_cast<ax::DrawNode*>(data);
    auto* a = cpConstraintGetBodyA(constraint);
    auto* b = cpConstraintGetBodyB(constraint);
    const ax::Color tint{0, 1, 0, 0.5f};
    if (cpConstraintIsPinJoint(constraint) || cpConstraintIsSlideJoint(constraint))
    {
        const bool pin = cpConstraintIsPinJoint(constraint);
        const auto pa = debugAnchor(a, pin ? cpPinJointGetAnchorA(constraint) : cpSlideJointGetAnchorA(constraint));
        const auto pb = debugAnchor(b, pin ? cpPinJointGetAnchorB(constraint) : cpSlideJointGetAnchorB(constraint));
        debugDot(node, pa, 3, tint); debugDot(node, pb, 3, tint);
        debugSegment(node, pa, pb, 1, tint);
    }
    else if (cpConstraintIsPivotJoint(constraint))
    {
        debugDot(node, debugAnchor(a, cpPivotJointGetAnchorA(constraint)), 3, tint);
        debugDot(node, debugAnchor(b, cpPivotJointGetAnchorB(constraint)), 3, tint);
    }
    else if (cpConstraintIsGrooveJoint(constraint))
    {
        debugDot(node, debugAnchor(b, cpGrooveJointGetAnchorB(constraint)), 3, tint);
        debugSegment(node, debugAnchor(a, cpGrooveJointGetGrooveA(constraint)),
                     debugAnchor(a, cpGrooveJointGetGrooveB(constraint)), 1, tint);
    }
    // The fixed-v3 renderer has no geometry for its DampedSpring TODO or other
    // angular constraints. Their native solver behavior remains independent.
}
class PhysicsDebugNode final : public ax::DrawNode
{
public:
    std::shared_ptr<SpaceState> space;
    void draw(const ax::SceneRenderState& state, const ax::Mat4& transform, uint32_t flags) override
    {
        clear();
        if (space && space->native) appendLegacyPhysicsDebugSpace(*this, space->native);
        ax::DrawNode::draw(state, transform, flags);
    }
};

static bool invalid(JSContext* cx, const char* message = "Invalid Chipmunk arguments or released native object")
{ if (!JS_IsExceptionPending(cx)) JS_ReportError(cx, "%s", message); return false; }
template<class P> static P* receiver(JSContext* cx, JS::CallArgs& args)
{
    P* proxy{}; if (!toNative(cx, args.thisv(), &proxy) || !proxy || !proxy->get()) { invalid(cx); return nullptr; } return proxy;
}
// These are ordinary traced JS edges, so an unreachable graph of Chipmunk
// peers can still be collected. Each concrete JSClass needs its own slots;
// reserved slots are not inherited from a prototype's class.
enum PeerSlot : unsigned
{
    DependencyA = 0,
    DependencyB = 1,
    SpaceMembers = 0,
    SpaceStaticBody = 1,
    SpaceCallbacks = 2,
    BodyVelocity = 2,
    PeerSlotCount = 3
};
template<class P, class S> static bool peer(JSContext* cx, const std::shared_ptr<S>& state, JS::MutableHandleValue out);
template<class P> static bool initializePeerSlots(JSContext* cx, JS::HandleObject object, P* value,
                                                const JS::CallArgs* constructorArgs);
template<class P> static bool owned(JSContext* cx, std::unique_ptr<P> value, JS::MutableHandleValue out,
                                   const JS::CallArgs* constructorArgs = nullptr)
{
    JS::RootedObject prototype(cx, getPrototype<P>(cx));
    JS::RootedObject object(cx, JS_NewObject(cx, detail::getClassInfo(cx, typeid(P)), prototype, JS::NullPtr()));
    // Finish any allocation that can fail before transferring ownership to
    // bindNative. An unbound, partially initialized object has no native deleter.
    if (!object || !initializePeerSlots(cx, object, value.get(), constructorArgs) ||
        !bindNative(cx, object, value.get(), NativeOwnership::Constructor)) return false;
    value.release(); out.setObject(*object); return true;
}
template<class P, class S> static bool peer(JSContext* cx, const std::shared_ptr<S>& state, JS::MutableHandleValue out)
{
    if (!state || !state->native) { out.setNull(); return true; }
    if (state->proxy)
    {
        // restorePeer retains the concrete type used by owned<P>. Recover its
        // exact pointer before querying, including any base-subobject offset.
        if (auto* object = detail::findNativeValueObject(cx, static_cast<P*>(state->proxy), typeid(P)))
        { out.setObject(*object); return true; }
    }
    return owned(cx, std::make_unique<P>(state), out);
}
template<class P> static auto memberKey(void* state)
{
    constexpr char kind = std::is_same_v<P, Body> ? 'b' : std::is_same_v<P, Shape> ? 's' : 'c';
    std::array<char, 2 * sizeof(void*) + 4> key{};
    std::snprintf(key.data(), key.size(), "%c%p", kind, state);
    return key;
}
template<class P, class S> static bool rememberMember(JSContext* cx, JS::HandleObject members,
    const std::shared_ptr<S>& state, JS::HandleValue value)
{
    auto key = memberKey<P>(state.get());
    return JS_DefineProperty(cx, members, key.data(), value, 0);
}
template<class P> static bool initializePeerSlots(JSContext* cx, JS::HandleObject object, P* value,
                                                const JS::CallArgs* constructorArgs)
{
    if constexpr (std::is_base_of_v<Shape, P>)
    {
        JS::RootedValue body(cx);
        if (constructorArgs) body.set((*constructorArgs)[0]);
        else if (!peer<Body>(cx, value->state->body, &body)) return false;
        JS_SetReservedSlot(object, DependencyA, body);
    }
    else if constexpr (std::is_base_of_v<Constraint, P>)
    {
        JS::RootedValue a(cx), b(cx);
        if (constructorArgs) { a.set((*constructorArgs)[0]); b.set((*constructorArgs)[1]); }
        else if (!peer<Body>(cx, value->state->a, &a) || !peer<Body>(cx, value->state->b, &b)) return false;
        JS_SetReservedSlot(object, DependencyA, a);
        JS_SetReservedSlot(object, DependencyB, b);
    }
    else if constexpr (std::is_same_v<P, Space>)
    {
        // A private dictionary gives amortized constant-time adds even for the
        // original Logo's thousands of members. Its keys cannot be reached by
        // JS, and a null prototype prevents inherited property hooks.
        JS::RootedObject members(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
        JS::RootedValue child(cx);
        if (!members || !JS_SetPrototype(cx, members, JS::NullPtr()) ||
            !peer<Body>(cx, value->state->staticBody, &child)) return false;
        JS_SetReservedSlot(object, SpaceStaticBody, child);
        for (const auto& body : value->state->bodies)
            if (!peer<Body>(cx, body, &child) || !rememberMember<Body>(cx, members, body, child)) return false;
        for (const auto& shape : value->state->shapes)
            if (!shape->restorePeer || !shape->restorePeer(cx, shape, &child) ||
                !rememberMember<Shape>(cx, members, shape, child)) return false;
        for (const auto& joint : value->state->constraints)
            if (!joint->restorePeer || !joint->restorePeer(cx, joint, &child) ||
                !rememberMember<Constraint>(cx, members, joint, child)) return false;
        JS_SetReservedSlot(object, SpaceMembers, JS::ObjectValue(*members));
        JS::RootedObject callbacks(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
        if(!callbacks || !JS_SetPrototype(cx,callbacks,JS::NullPtr())) return false;
        JS_SetReservedSlot(object,SpaceCallbacks,JS::ObjectValue(*callbacks));
    }
    else if constexpr (std::is_same_v<P, PhysicsDebugNode>)
    {
        JS::RootedValue space(cx);
        if(!peer<Space>(cx,value->space,&space) || !detail::setOwnerDependency(cx,object,space)) return false;
    }
    return true;
}
static bool bodyValue(JSContext* cx, cpBody* raw, JS::MutableHandleValue out)
{
    if (!raw) { out.setNull(); return true; }
    auto* state = static_cast<BodyState*>(cpBodyGetUserData(raw));
    return state ? peer<Body>(cx, state->shared_from_this(), out) : invalid(cx, "Body is not owned by this Chipmunk VM");
}
static bool shapeValue(JSContext* cx, cpShape* raw, JS::MutableHandleValue out)
{
    if (!raw) { out.setNull(); return true; }
    auto* state = static_cast<ShapeState*>(cpShapeGetUserData(raw));
    // A native Space can outlive a shape's JS peer. Recreate its original
    // concrete peer type, so a PolyShape returned by a query/arbiter keeps its
    // polygon methods after GC instead of becoming a base Shape wrapper.
    return state && state->restorePeer ? state->restorePeer(cx, state->shared_from_this(), out)
                                      : invalid(cx, "Shape is not owned by this Chipmunk VM");
}
static bool constraintValue(JSContext* cx, cpConstraint* raw, JS::MutableHandleValue out)
{
    if (!raw) { out.setNull(); return true; }
    auto* state = static_cast<ConstraintState*>(cpConstraintGetUserData(raw));
    return state && state->restorePeer ? state->restorePeer(cx, state->shared_from_this(), out)
                                      : invalid(cx, "Constraint is not owned by this Chipmunk VM");
}
static bool spaceValue(JSContext* cx, cpSpace* raw, JS::MutableHandleValue out)
{
    if (!raw) { out.setNull(); return true; }
    auto* state = static_cast<SpaceState*>(cpSpaceGetUserData(raw));
    return state ? peer<Space>(cx, state->shared_from_this(), out) : invalid(cx, "Space is not owned by this Chipmunk VM");
}
template<class T> static bool result(JSContext* cx, const T& value, JS::MutableHandleValue out) { return toJsValue(cx, value, out); }
static bool result(JSContext* cx, cpBody* value, JS::MutableHandleValue out) { return bodyValue(cx, value, out); }
static bool result(JSContext* cx, cpShape* value, JS::MutableHandleValue out) { return shapeValue(cx, value, out); }
static bool result(JSContext* cx, cpConstraint* value, JS::MutableHandleValue out) { return constraintValue(cx, value, out); }
static bool result(JSContext* cx, cpSpace* value, JS::MutableHandleValue out) { return spaceValue(cx, value, out); }

template<class> struct Function;
template<class R, class... A> struct Function<R(*)(A...)> { using Return = R; using Arguments = std::tuple<A...>; static constexpr size_t count = sizeof...(A); };
template<class T> struct Argument : axjs::Argument<T> {};
template<> struct Argument<cpBool>
{
    cpBool value{}; bool read(JSContext* cx, JS::HandleValue v) { if (v.isBoolean()) { value = v.toBoolean(); return true; } return fromJsValue(cx, v, value); }
    cpBool get() const { return value; }
};
template<class Tuple, size_t Offset, size_t... I> static auto argumentTuple(std::index_sequence<I...>)
{ return std::tuple<Argument<std::tuple_element_t<I + Offset, Tuple>>...>{}; }
template<size_t Offset, class Tuple, size_t... I> static bool read(JSContext* cx, JS::CallArgs& args, Tuple& values, std::index_sequence<I...>)
{ return (std::get<I>(values).read(cx, args[I + Offset]) && ...); }
template<auto F, bool Boolean = false> static bool freeFunction(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); using Traits = Function<decltype(F)>;
    auto values = argumentTuple<typename Traits::Arguments, 0>(std::make_index_sequence<Traits::count>{});
    if (argc != Traits::count || !read<0>(cx, args, values, std::make_index_sequence<Traits::count>{})) return invalid(cx);
    if constexpr (std::is_void_v<typename Traits::Return>) { std::apply([](auto&... a) { F(a.get()...); }, values); args.rval().setUndefined(); return true; }
    else { auto value = std::apply([](auto&... a) { return F(a.get()...); }, values); if constexpr (Boolean) { args.rval().setBoolean(value); return true; } else return result(cx, value, args.rval()); }
}
template<class P, auto F, bool Boolean = false> static bool method(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); auto* self = receiver<P>(cx, args); if (!self) return false;
    using Traits = Function<decltype(F)>; constexpr size_t count = Traits::count - 1;
    auto values = argumentTuple<typename Traits::Arguments, 1>(std::make_index_sequence<count>{});
    if (argc != count || !read<0>(cx, args, values, std::make_index_sequence<count>{})) return invalid(cx);
    if constexpr (std::is_void_v<typename Traits::Return>) { std::apply([self](auto&... a) { F(self->get(), a.get()...); }, values); args.rval().setUndefined(); return true; }
    else { auto value = std::apply([self](auto&... a) { return F(self->get(), a.get()...); }, values); if constexpr (Boolean) { args.rval().setBoolean(value); return true; } else return result(cx, value, args.rval()); }
}

static bool baseHandle(JSContext*, unsigned argc, JS::Value* vp) { auto args = JS::CallArgsFromVp(argc, vp); args.rval().set(args.thisv()); return true; }
static bool bodyConstructor(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); double mass{}, moment{};
    if (argc != 2 || !fromJsValue(cx, args[0], mass) || !fromJsValue(cx, args[1], moment) || mass <= 0 || moment <= 0) return invalid(cx);
    auto state = std::make_shared<BodyState>();
    state->native = std::isinf(mass) ? cpBodyNewStatic() : cpBodyNew(mass, moment);
    cpBodySetUserData(state->native, state.get());
    return owned(cx, std::make_unique<Body>(state), args.rval());
}
static bool spaceConstructor(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); if (argc != 0) return invalid(cx);
    auto state = std::make_shared<SpaceState>(); state->native = cpSpaceNew();
    // Enable the real v7 wildcard defaults before the first collision. Later
    // handler removal must not replace the default functions of cached
    // arbiters whose wildcard handler pointers were never initialized.
    cpSpaceAddDefaultCollisionHandler(state->native);
    cpSpaceSetUserData(state->native, state.get());
    state->staticBody = std::make_shared<BodyState>(); state->staticBody->owned = false;
    state->staticBody->native = cpSpaceGetStaticBody(state->native); state->staticBody->space = state;
    cpBodySetUserData(state->staticBody->native, state->staticBody.get());
    return owned(cx, std::make_unique<Space>(state), args.rval());
}
static bool vertices(JSContext* cx, JS::HandleValue value, std::vector<cpVect>& out)
{
    if (!value.isObject()) return false;
    JS::RootedObject array(cx, &value.toObject()); uint32_t size{}; if (!JS_GetArrayLength(cx, array, &size)) return false;
    JS::RootedValue first(cx); if (size < 3 || !JS_GetElement(cx, array, 0, &first)) return false;
    if (first.isNumber())
    {
        if (size % 2 || size < 6) return false;
        out.resize(size / 2);
        for (uint32_t i = 0; i < size / 2; ++i) { JS::RootedValue x(cx), y(cx); if (!JS_GetElement(cx, array, i * 2, &x) || !JS_GetElement(cx, array, i * 2 + 1, &y) || !fromJsValue(cx, x, out[i].x) || !fromJsValue(cx, y, out[i].y)) return false; }
    }
    else { out.resize(size); for (uint32_t i = 0; i < size; ++i) { JS::RootedValue v(cx); if (!JS_GetElement(cx, array, i, &v) || !fromJsValue(cx, v, out[i])) return false; } }
    return true;
}
template<class P, int Kind> static bool shapeConstructor(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); Body* body{}; if (argc < 3 || !toNative(cx, args[0], &body) || !body || !body->get()) return invalid(cx);
    auto state = std::make_shared<ShapeState>(); state->body = body->state; state->restorePeer = peer<P, ShapeState>;
    if constexpr (Kind == 0) { cpFloat radius{}; cpVect offset{}; if (argc != 3 || !fromJsValue(cx, args[1], radius) || !fromJsValue(cx, args[2], offset) || radius < 0) return invalid(cx); state->native = cpCircleShapeNew(body->get(), radius, offset); }
    if constexpr (Kind == 1) { cpVect a{}, b{}; cpFloat radius{}; if (argc != 4 || !fromJsValue(cx, args[1], a) || !fromJsValue(cx, args[2], b) || !fromJsValue(cx, args[3], radius) || radius < 0) return invalid(cx); state->native = cpSegmentShapeNew(body->get(), a, b, radius); }
    if constexpr (Kind == 2) { cpVect offset{}; cpFloat radius{}; if (argc < 3 || argc > 4 || !vertices(cx, args[1], state->vertices) || !fromJsValue(cx, args[2], offset) || (argc == 4 && !fromJsValue(cx, args[3], radius))) return invalid(cx); for (auto& v : state->vertices) v = cpvadd(v, offset); state->native = cpPolyShapeNew(body->get(), static_cast<int>(state->vertices.size()), state->vertices.data(), cpTransformIdentity, radius); }
    body->state->shapes.emplace_back(state);
    cpShapeSetUserData(state->native, state.get()); return owned(cx, std::make_unique<P>(state), args.rval(), &args);
}
static bool boxConstructor(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); Body* body{}; double width{}, height{}, radius{};
    if (argc < 3 || argc > 4 || !toNative(cx, args[0], &body) || !body || !body->get() || !fromJsValue(cx, args[1], width) || !fromJsValue(cx, args[2], height) || (argc == 4 && !fromJsValue(cx, args[3], radius)) || width <= 0 || height <= 0) return invalid(cx);
    auto state = std::make_shared<ShapeState>(); state->body = body->state; state->restorePeer = peer<PolyShape, ShapeState>; state->native = cpBoxShapeNew(body->get(), width, height, radius);
    state->vertices = {{-width/2,-height/2}, {-width/2,height/2}, {width/2,height/2}, {width/2,-height/2}};
    body->state->shapes.emplace_back(state);
    cpShapeSetUserData(state->native, state.get()); return owned(cx, std::make_unique<PolyShape>(state), args.rval(), &args);
}
template<class P, auto F> static bool jointConstructor(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); using Traits = Function<decltype(F)>; constexpr size_t count = Traits::count - 2;
    Body* a{}; Body* b{}; auto values = argumentTuple<typename Traits::Arguments, 2>(std::make_index_sequence<count>{});
    if (argc != Traits::count || !toNative(cx, args[0], &a) || !toNative(cx, args[1], &b) || !a || !b || !a->get() || !b->get() || !read<2>(cx, args, values, std::make_index_sequence<count>{})) return invalid(cx);
    auto state = std::make_shared<ConstraintState>(); state->a = a->state; state->b = b->state; state->restorePeer = peer<P, ConstraintState>;
    state->native = std::apply([a,b](auto&... value) { return F(a->get(), b->get(), value.get()...); }, values);
    a->state->constraints.emplace_back(state); b->state->constraints.emplace_back(state);
    cpConstraintSetUserData(state->native, state.get()); return owned(cx, std::make_unique<P>(state), args.rval(), &args);
}
static bool pivotConstructor(JSContext* cx, unsigned argc, JS::Value* vp)
{ return argc == 3 ? jointConstructor<PivotJoint, cpPivotJointNew>(cx, argc, vp) : jointConstructor<PivotJoint, cpPivotJointNew2>(cx, argc, vp); }

template<class P, int Operation> static bool membership(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); Space* space = receiver<Space>(cx, args); P* child{};
    if (!space || argc != 1 || !toNative(cx, args[0], &child) || !child || !child->get()) return invalid(cx);
    if (cpSpaceIsLocked(space->get())) return invalid(cx, "Space is locked; change membership in a post-step callback");
    auto existing = child->state->space.lock();
    if constexpr (Operation == 0) if (existing && existing != space->state) return invalid(cx, "Chipmunk object belongs to another Space");
    const bool contains = [&] {
        if constexpr (std::is_same_v<P, Body>) return cpSpaceContainsBody(space->get(), child->get());
        else if constexpr (std::is_same_v<P, Shape>) return cpSpaceContainsShape(space->get(), child->get());
        else return cpSpaceContainsConstraint(space->get(), child->get());
    }();
    if constexpr (Operation == 2) { args.rval().setBoolean(contains); return true; }
    // Removing from an unrelated Space is a no-op. In particular it must not
    // clear the actual owner's weak membership or its JS retention edge.
    if constexpr (Operation == 1) if (!contains) { args.rval().set(args[0]); return true; }
    if constexpr (Operation == 0 && std::is_same_v<P, Body>)
        if (!contains && cpBodyGetType(child->get()) == CP_BODY_TYPE_STATIC)
            return invalid(cx, "Static bodies are attached through their shapes");
    JS::RootedObject object(cx, &args.thisv().toObject());
    JS::RootedValue membersValue(cx, JS_GetReservedSlot(object, SpaceMembers));
    if (!membersValue.isObject()) return invalid(cx, "Chipmunk Space peer members are not initialized");
    JS::RootedObject members(cx, &membersValue.toObject());
    auto& states = [&]() -> auto& {
        if constexpr (std::is_same_v<P, Body>) return space->state->bodies;
        else if constexpr (std::is_same_v<P, Shape>) return space->state->shapes;
        else return space->state->constraints;
    }();
    if constexpr (Operation == 0)
    {
        // Preserve the exact argument, including its expandos. Both the JS
        // property and native-state vector allocate before native mutation.
        if (!rememberMember<P>(cx, members, child->state, args[0])) return false;
        if (!contains)
        {
            states.push_back(child->state);
            if constexpr (std::is_same_v<P, Body>) cpSpaceAddBody(space->get(), child->get());
            else if constexpr (std::is_same_v<P, Shape>) cpSpaceAddShape(space->get(), child->get());
            else cpSpaceAddConstraint(space->get(), child->get());
        }
        child->state->space = space->state;
    }
    else if constexpr (Operation == 1)
    {
        if constexpr (std::is_same_v<P, Body>) cpSpaceRemoveBody(space->get(), child->get());
        else if constexpr (std::is_same_v<P, Shape>) cpSpaceRemoveShape(space->get(), child->get());
        else cpSpaceRemoveConstraint(space->get(), child->get());
        std::erase(states, child->state);
        if (child->state->space.lock() == space->state) child->state->space.reset();
        // Shape removal may invoke separation/post-step callbacks. Delete only
        // this key from the live dictionary, retaining any peers added there.
        auto key = memberKey<P>(child->state.get());
        if (!JS_DeleteProperty(cx, members, key.data())) return false;
    }
    args.rval().set(args[0]); return true;
}
static bool spaceFree(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); Space* space{}; if (!toNative(cx, args.thisv(), &space) || !space) return false;
    if (space->get() && (cpSpaceIsLocked(space->get()) || space->state->stepping || space->state->callbacksInFlight))
        return invalid(cx, "Cannot release a Space during a native operation or callback");
    space->state->observeLifecycle([](auto& facts) { facts.explicitFreeCalls.fetch_add(1,std::memory_order_release); });
    space->state->close(tests::ChipmunkSpaceFreeReason::ExplicitFree);
    // close/destruction remains entirely native, including when called by GC.
    // Only this live JS entry point releases the Space's ordinary JS edges.
    JS_SetReservedSlot(&args.thisv().toObject(), SpaceMembers, JS::UndefinedValue());
    JS_SetReservedSlot(&args.thisv().toObject(), SpaceStaticBody, JS::UndefinedValue());
    JS_SetReservedSlot(&args.thisv().toObject(), SpaceCallbacks, JS::UndefinedValue());
    args.rval().setUndefined(); return true;
}
static bool setShapeBody(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); auto* shape = receiver<Shape>(cx, args); Body* body{};
    if (!shape || argc != 1 || !toNative(cx, args[0], &body) || !body || !body->get()) return invalid(cx);
    if (cpShapeGetSpace(shape->get())) return invalid(cx, "Remove a Shape from its Space before replacing its Body");
    body->state->shapes.emplace_back(shape->state);
    cpShapeSetBody(shape->get(), body->get()); shape->state->body = body->state;
    JS_SetReservedSlot(&args.thisv().toObject(), DependencyA, args[0]);
    args.rval().setUndefined(); return true;
}
static bool polyVerts(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); auto* shape = receiver<PolyShape>(cx, args); if (!shape) return false;
    JS::RootedObject array(cx, JS_NewArrayObject(cx, static_cast<unsigned>(shape->state->vertices.size()*2))); if (!array) return false;
    for (uint32_t i = 0; i < shape->state->vertices.size(); ++i) { JS::RootedValue x(cx, JS::DoubleValue(JS::CanonicalizeNaN(shape->state->vertices[i].x))), y(cx, JS::DoubleValue(JS::CanonicalizeNaN(shape->state->vertices[i].y))); if (!JS_SetElement(cx, array, 2*i, x) || !JS_SetElement(cx, array, 2*i+1, y)) return false; }
    args.rval().setObject(*array); return true;
}
static bool polyMoment(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); double mass{}, radius{}; cpVect offset{}; std::vector<cpVect> verts;
    if (argc < 3 || argc > 4 || !fromJsValue(cx, args[0], mass) || !vertices(cx, args[1], verts) || !fromJsValue(cx, args[2], offset) || (argc == 4 && !fromJsValue(cx, args[3], radius))) return invalid(cx);
    args.rval().set(nativeNumberValue(cpMomentForPoly(mass, static_cast<int>(verts.size()), verts.data(), offset, radius))); return true;
}
template<bool Area> static bool polyGeometry(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); std::vector<cpVect> verts; double radius{};
    if (argc < 1 || argc > (Area?2u:1u) || !vertices(cx, args[0], verts) || (argc == 2 && !fromJsValue(cx, args[1], radius))) return invalid(cx);
    if constexpr (Area) { args.rval().set(nativeNumberValue(cpAreaForPoly(static_cast<int>(verts.size()), verts.data(), radius))); return true; }
    else return toJsValue(cx, cpCentroidForPoly(static_cast<int>(verts.size()), verts.data()), args.rval());
}
static bool segmentMoment(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); double mass{}, radius{}; cpVect a{}, b{};
    if (argc < 3 || argc > 4 || !fromJsValue(cx, args[0], mass) || !fromJsValue(cx, args[1], a) || !fromJsValue(cx, args[2], b) || (argc == 4 && !fromJsValue(cx, args[3], radius))) return invalid(cx);
    args.rval().set(nativeNumberValue(cpMomentForSegment(mass, a, b, radius))); return true;
}

struct NativeCallbackScope
{
    SpaceState& space;
    explicit NativeCallbackScope(SpaceState& s):space(s){++space.callbacksInFlight;}
    ~NativeCallbackScope(){--space.callbacksInFlight;}
};
static bool invokeCollision(cpArbiter* raw, cpSpace*, void* data, int phase)
{
    auto* rawHandler = static_cast<Handler*>(data);
    if (!rawHandler || !rawHandler->space || rawHandler->space->closing) return true;
    auto handler = rawHandler->shared_from_this();
    auto callback = phase == 0 ? handler->begin : phase == 1 ? handler->pre : phase == 2 ? handler->post : handler->separate;
    if (!callback || !callback->isValid()) return true;
    auto state = callback->state(); if (!state || !state->onOwnerThread()) return false;
    NativeCallbackScope dispatch(*handler->space);
    JSContext* cx = state->context;
    JS::RootedObject callbackRecord(cx);
    if(!callback->pinOwnerRecord(cx,&callbackRecord)) return false;
    ScopedNative scope; Arbiter arbiter(raw); JS::AutoValueVector args(cx);
    JS::RootedValue a(cx), space(cx), value(cx);
    if (!wrapBorrowed(cx, &arbiter, &a, scope.token()) || !peer<Space>(cx, handler->space->shared_from_this(), &space) ||
        !args.append(a) || !args.append(space) || !callback->call(JS::HandleValueArray(args), &value))
    { handler->space->callbackFailed = true; return false; }
    return phase > 1 || JS::ToBoolean(value);
}
static cpBool collisionBegin(cpArbiter* a, cpSpace* s, void* d) { return invokeCollision(a, s, d, 0); }
static cpBool collisionPre(cpArbiter* a, cpSpace* s, void* d) { return invokeCollision(a, s, d, 1); }
static void collisionPost(cpArbiter* a, cpSpace* s, void* d) { invokeCollision(a, s, d, 2); }
static void collisionSeparate(cpArbiter* a, cpSpace* s, void* d) { invokeCollision(a, s, d, 3); }
static std::string handlerKey(cpCollisionType a,cpCollisionType b)
{ return "h"+std::to_string(a)+":"+std::to_string(b); }
template<bool Default> static bool setCollisionHandler(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); auto* space = receiver<Space>(cx, args); constexpr unsigned offset = Default ? 0 : 2;
    cpCollisionType a = CP_WILDCARD_COLLISION_TYPE, b = CP_WILDCARD_COLLISION_TYPE;
    if (!space || argc != offset + 4 || (!Default && (!fromJsValue(cx, args[0], a) || !fromJsValue(cx, args[1], b)))) return invalid(cx);
    auto handler = std::make_shared<Handler>(); handler->context = cx; handler->space = space->state.get();
    std::shared_ptr<Callback>* callbacks[] = {&handler->begin, &handler->pre, &handler->post, &handler->separate};
    JS::RootedObject receiver(cx, &args.thisv().toObject());
    JS::RootedObject records(cx,JS_NewArrayObject(cx,4));
    JS::RootedObject dictionary(cx,&JS_GetReservedSlot(receiver,SpaceCallbacks).toObject());
    if(!records || !JS_SetPrototype(cx,records,JS::NullPtr())) return false;
    for (unsigned i = 0; i < 4; ++i)
        if (!args[i + offset].isNullOrUndefined())
        {
            if(!args[i+offset].isObject() || !JS_ObjectIsCallable(cx,&args[i+offset].toObject()))
                return invalid(cx,"Collision callback must be callable or null");
            JS::RootedValue record(cx);
            *callbacks[i] = Callback::createOwnerBound(cx,args[i+offset],receiver,&record);
            if(!*callbacks[i] || !JS_SetElement(cx,records,i,record)) return false;
        }
    JS::RootedValue recordArray(cx,JS::ObjectValue(*records));
    auto key=handlerKey(a,b);
    if(!JS_DefineProperty(cx,dictionary,key.c_str(),recordArray,0)) return false;
    auto* native = Default ? cpSpaceAddDefaultCollisionHandler(space->get()) : cpSpaceAddCollisionHandler(space->get(), a, b);
    native->beginFunc = collisionBegin; native->preSolveFunc = collisionPre; native->postSolveFunc = collisionPost; native->separateFunc = collisionSeparate; native->userData = handler.get();
    space->state->handlers[{a,b}] = handler; args.rval().setUndefined(); return true;
}
static bool removeCollisionHandler(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); auto* space = receiver<Space>(cx, args); cpCollisionType a{}, b{};
    if (!space || argc != 2 || !fromJsValue(cx, args[0], a) || !fromJsValue(cx, args[1], b)) return invalid(cx);
    // v7 removed the removal function. Revert this pair to the genuine default
    // handler by clearing its pair-specific JS callbacks and copying defaults.
    auto* native = cpSpaceAddCollisionHandler(space->get(), a, b); auto* defaults = cpSpaceAddDefaultCollisionHandler(space->get());
    native->beginFunc = defaults->beginFunc; native->preSolveFunc = defaults->preSolveFunc;
    native->postSolveFunc = defaults->postSolveFunc; native->separateFunc = defaults->separateFunc; native->userData = defaults->userData;
    JS::RootedObject dictionary(cx,&JS_GetReservedSlot(&args.thisv().toObject(),SpaceCallbacks).toObject());
    if(!JS_DeleteProperty(cx,dictionary,handlerKey(a,b).c_str())) return false;
    space->state->handlers.erase({a,b}); args.rval().setUndefined(); return true;
}
static void postStepDispatch(cpSpace*, void*, void* data)
{
    auto* task = static_cast<PostStep*>(data); auto callback = task->callback;
    auto space=task->space.lock();
    if(callback && callback->isValid() && space)
    {
        NativeCallbackScope dispatch(*space);
        auto runtime=callback->state(); JSContext* cx=runtime->context;
        JS::RootedObject record(cx); JS::RootedValue owner(cx);
        if(callback->pinOwnerRecord(cx,&record) && peer<Space>(cx,space,&owner))
        {
            JS::RootedObject dictionary(cx,&JS_GetReservedSlot(&owner.toObject(),SpaceCallbacks).toObject());
            bool called=callback->call(JS::HandleValueArray::empty());
            bool deleted=JS_DeleteProperty(cx,dictionary,task->key.c_str());
            if(!called || !deleted) space->callbackFailed=true;
        }
        else space->callbackFailed=true;
    }
    task->callback.reset();
}
static bool addPostStep(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); auto* space = receiver<Space>(cx, args); if (!space || argc == 0) return invalid(cx);
    // Function.bind retains exact JS keys/arguments through the Space's traced
    // record, including cycles, without a permanent root or pointer cast.
    if (!args[0].isObject() || !JS_ObjectIsFunction(cx, &args[0].toObject())) return invalid(cx);
    JS::RootedObject function(cx, &args[0].toObject()); JS::RootedValue bind(cx), bound(cx); JS::AutoValueVector boundArgs(cx);
    if (!JS_GetProperty(cx, function, "bind", &bind) || !boundArgs.append(args.thisv())) return false;
    for (unsigned i = 1; i < argc; ++i) if (!boundArgs.append(args[i])) return false;
    if (!JS_CallFunctionValue(cx, function, bind, JS::HandleValueArray(boundArgs), &bound)) return false;
    if(!space->get()) return invalid(cx,"Space was released while binding the post-step callback");
    if(!bound.isObject() || !JS_ObjectIsCallable(cx,&bound.toObject()))
        return invalid(cx,"Post-step bind must return a callable value");
    auto task = std::make_unique<PostStep>(); JS::RootedObject receiver(cx, &args.thisv().toObject());
    JS::RootedValue record(cx); task->callback = Callback::createOwnerBound(cx,bound,receiver,&record);
    if (!task->callback) return false;
    task->space=space->state; task->key="p"+std::to_string(reinterpret_cast<uintptr_t>(task.get()));
    JS::RootedObject dictionary(cx,&JS_GetReservedSlot(receiver,SpaceCallbacks).toObject());
    if(!JS_DefineProperty(cx,dictionary,task->key.c_str(),record,0)) return false;
    auto* pointer = task.get(); space->state->postSteps.push_back(std::move(task));
    bool added=cpSpaceAddPostStepCallback(space->get(),postStepDispatch,pointer,pointer);
    if(!added)
    {
        bool deleted=JS_DeleteProperty(cx,dictionary,pointer->key.c_str());
        std::erase_if(space->state->postSteps,[pointer](const auto& p){return p.get()==pointer;});
        if(!deleted) return false;
    }
    args.rval().setBoolean(added); return true;
}
static bool arbiterPeers(JSContext* cx, unsigned argc, JS::Value* vp, bool bodies)
{
    auto args = JS::CallArgsFromVp(argc, vp); auto* arbiter = receiver<Arbiter>(cx, args); if (!arbiter) return false;
    JS::RootedObject array(cx, JS_NewArrayObject(cx, 2)); JS::RootedValue a(cx), b(cx); if (!array) return false;
    if (bodies) { cpBody* x{}; cpBody* y{}; cpArbiterGetBodies(arbiter->get(), &x, &y); if (!bodyValue(cx, x, &a) || !bodyValue(cx, y, &b)) return false; }
    else { cpShape* x{}; cpShape* y{}; cpArbiterGetShapes(arbiter->get(), &x, &y); if (!shapeValue(cx, x, &a) || !shapeValue(cx, y, &b)) return false; }
    if (!JS_SetElement(cx, array, 0, a) || !JS_SetElement(cx, array, 1, b)) return false;
    args.rval().setObject(*array); return true;
}
static bool arbiterBodies(JSContext* cx, unsigned argc, JS::Value* vp) { return arbiterPeers(cx, argc, vp, true); }
static bool arbiterShapes(JSContext* cx, unsigned argc, JS::Value* vp) { return arbiterPeers(cx, argc, vp, false); }
template<bool B> static bool arbiterPoint(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); auto* arbiter = receiver<Arbiter>(cx, args); int index{};
    if (!arbiter || argc > 1 || (argc == 1 && !fromJsValue(cx, args[0], index)) || index < 0 || index >= cpArbiterGetCount(arbiter->get())) return invalid(cx);
    return toJsValue(cx, B ? cpArbiterGetPointB(arbiter->get(), index) : cpArbiterGetPointA(arbiter->get(), index), args.rval());
}
struct Iteration
{
    JSContext* context{}; std::shared_ptr<Callback> callback; bool ok{true};
    void call(JS::HandleValue value) { JS::RootedValue argument(context, value), result(context); if (ok && !callback->call(JS::HandleValueArray(argument), &result)) ok = false; }
};
static void iterateBody(cpBody* body, void* data) { auto* i = static_cast<Iteration*>(data); JS::RootedValue v(i->context); if (bodyValue(i->context, body, &v)) i->call(v); else i->ok = false; }
static void iterateShape(cpShape* shape, void* data) { auto* i = static_cast<Iteration*>(data); JS::RootedValue v(i->context); if (shapeValue(i->context, shape, &v)) i->call(v); else i->ok = false; }
static void iterateConstraint(cpConstraint* joint, void* data) { auto* i = static_cast<Iteration*>(data); JS::RootedValue v(i->context); if (constraintValue(i->context, joint, &v)) i->call(v); else i->ok = false; }
static void iterateBodyShape(cpBody*, cpShape* shape, void* data) { iterateShape(shape, data); }
static void iterateBodyConstraint(cpBody*, cpConstraint* joint, void* data) { iterateConstraint(joint, data); }
static void iterateArbiter(cpBody*, cpArbiter* raw, void* data)
{
    auto* i = static_cast<Iteration*>(data); ScopedNative scope; Arbiter arbiter(raw); JS::RootedValue value(i->context);
    if (wrapBorrowed(i->context, &arbiter, &value, scope.token())) i->call(value); else i->ok = false;
}
template<class P, int Kind> static bool each(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); auto* self = receiver<P>(cx, args); if (!self || argc != 1) return invalid(cx);
    JS::RootedObject receiver(cx, &args.thisv().toObject()); auto callback = Callback::create(cx, args[0], receiver); if (!callback) return false;
    Iteration data{cx, callback};
    if constexpr (std::is_same_v<P, Space>)
    { if constexpr (Kind == 0) cpSpaceEachBody(self->get(), iterateBody, &data); if constexpr (Kind == 1) cpSpaceEachShape(self->get(), iterateShape, &data); if constexpr (Kind == 2) cpSpaceEachConstraint(self->get(), iterateConstraint, &data); }
    else
    { if constexpr (Kind == 1) cpBodyEachShape(self->get(), iterateBodyShape, &data); if constexpr (Kind == 2) cpBodyEachConstraint(self->get(), iterateBodyConstraint, &data); if constexpr (Kind == 3) cpBodyEachArbiter(self->get(), iterateArbiter, &data); }
    callback->cancel(); args.rval().setUndefined(); return data.ok;
}
static bool set(JSContext* cx, JS::HandleObject object, const char* name, double number)
{ JS::RootedValue v(cx, JS::DoubleValue(JS::CanonicalizeNaN(number))); return JS_SetProperty(cx, object, name, v); }
static bool set(JSContext* cx, JS::HandleObject object, const char* name, cpVect point)
{ JS::RootedValue v(cx); return toJsValue(cx, point, &v) && JS_SetProperty(cx, object, name, v); }
static bool pointInfo(JSContext* cx, const cpPointQueryInfo& info, JS::MutableHandleValue out)
{
    if (!info.shape) { out.setNull(); return true; }
    JS::RootedObject object(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr())); JS::RootedValue shape(cx);
    if (!object || !shapeValue(cx, const_cast<cpShape*>(info.shape), &shape) || !JS_SetProperty(cx, object, "shape", shape) ||
        !set(cx, object, "point", info.point) || !set(cx, object, "p", info.point) || !set(cx, object, "distance", info.distance) ||
        !set(cx, object, "d", info.distance) || !set(cx, object, "gradient", info.gradient)) return false;
    out.setObject(*object); return true;
}
static bool segmentInfo(JSContext* cx, const cpSegmentQueryInfo& info, JS::MutableHandleValue out)
{
    if (!info.shape) { out.setNull(); return true; }
    JS::RootedObject object(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr())); JS::RootedValue shape(cx);
    if (!object || !shapeValue(cx, const_cast<cpShape*>(info.shape), &shape) || !JS_SetProperty(cx, object, "shape", shape) ||
        !set(cx, object, "point", info.point) || !set(cx, object, "normal", info.normal) || !set(cx, object, "n", info.normal) ||
        !set(cx, object, "alpha", info.alpha) || !set(cx, object, "t", info.alpha)) return false;
    out.setObject(*object); return true;
}
template<bool Segment> static bool shapeQuery(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); auto* shape = receiver<Shape>(cx, args); cpVect a{}, b{}; double radius{};
    if (!shape || argc < 1 || !fromJsValue(cx, args[0], a)) return invalid(cx);
    cpShapeCacheBB(shape->get());
    if constexpr (Segment) { if (argc < 2 || argc > 3 || !fromJsValue(cx, args[1], b) || (argc == 3 && !fromJsValue(cx, args[2], radius))) return invalid(cx); cpSegmentQueryInfo info{}; cpShapeSegmentQuery(shape->get(), a, b, radius, &info); return segmentInfo(cx, info, args.rval()); }
    else { if (argc != 1) return invalid(cx); cpPointQueryInfo info{}; cpShapePointQuery(shape->get(), a, &info); return pointInfo(cx, info, args.rval()); }
}
template<bool Segment> static bool firstQuery(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); auto* space = receiver<Space>(cx, args); cpVect a{}, b{}; double distance{}; cpShapeFilter filter{};
    if (!space || argc != (Segment ? 4u : 3u) || !fromJsValue(cx, args[0], a)) return invalid(cx);
    if constexpr (Segment) { if (argc != 4 || !fromJsValue(cx, args[1], b) || !fromJsValue(cx, args[2], distance) || !fromJsValue(cx, args[3], filter)) return invalid(cx); cpSegmentQueryInfo info{}; cpSpaceSegmentQueryFirst(space->get(), a, b, distance, filter, &info); return segmentInfo(cx, info, args.rval()); }
    else { if (argc != 3 || !fromJsValue(cx, args[1], distance) || !fromJsValue(cx, args[2], filter)) return invalid(cx); cpPointQueryInfo info{}; cpSpacePointQueryNearest(space->get(), a, distance, filter, &info); return pointInfo(cx, info, args.rval()); }
}
static void velocityDispatch(cpBody* raw, cpVect gravity, cpFloat damping, cpFloat dt)
{
    auto* body = static_cast<BodyState*>(cpBodyGetUserData(raw)); auto callback = body->velocity;
    if (!callback || !callback->isValid()) { cpBodyUpdateVelocity(raw, gravity, damping, dt); return; }
    auto state = callback->state(); if (!state || !state->onOwnerThread()) return;
    JSContext* cx = state->context; JS::AutoValueVector args(cx); JS::RootedValue vector(cx);
    JS::RootedObject record(cx); if(!callback->pinOwnerRecord(cx,&record)) return;
    if (!toJsValue(cx, gravity, &vector) || !args.append(vector) || !args.append(JS::DoubleValue(JS::CanonicalizeNaN(damping))) || !args.append(JS::DoubleValue(JS::CanonicalizeNaN(dt))) || !callback->call(JS::HandleValueArray(args)))
        if (auto space = body->space.lock()) space->callbackFailed = true;
}
static bool velocityGet(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); if (!receiver<Body>(cx, args)) return false;
    JS::RootedObject object(cx, &args.thisv().toObject()); return JS_GetProperty(cx, object, "updateVelocity", args.rval());
}
static bool velocitySet(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); auto* body = receiver<Body>(cx, args); if (!body || argc != 1) return invalid(cx);
    JS::RootedObject object(cx, &args.thisv().toObject());
    if (args[0].isNullOrUndefined())
    {
        body->state->velocity.reset(); JS_SetReservedSlot(object,BodyVelocity,JS::UndefinedValue());
        cpBodySetVelocityUpdateFunc(body->get(), cpBodyUpdateVelocity);
    }
    else
    {
        if(!args[0].isObject() || !JS_ObjectIsCallable(cx,&args[0].toObject()))
            return invalid(cx,"Velocity callback must be callable or null");
        JS::RootedValue record(cx); auto callback=Callback::createOwnerBound(cx,args[0],object,&record);
        if(!callback) return false;
        JS_SetReservedSlot(object,BodyVelocity,record); body->state->velocity=callback;
        cpBodySetVelocityUpdateFunc(body->get(),velocityDispatch);
    }
    args.rval().setUndefined(); return true;
}

static bool spriteConstructor(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); auto native = std::make_unique<PhysicsSprite>(); bool initialized{};
    if (argc == 0) initialized = native->init();
    else if (argc <= 2)
    {
        std::string file; ax::Rect rect;
        if (!fromJsValue(cx, args[0], file) || (argc == 2 && !fromJsValue(cx, args[1], rect))) return invalid(cx);
        initialized = argc == 1 ? native->initWithFile(file) : native->initWithFile(file, rect);
    }
    else return invalid(cx);
    if (!initialized) { args.rval().setNull(); return true; }
    return owned(cx, std::move(native), args.rval());
}
static bool spriteSetBody(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); PhysicsSprite* sprite{}; Body* body{};
    if (!toNative(cx, args.thisv(), &sprite) || !sprite || argc != 1 || !toNative(cx, args[0], &body)) return invalid(cx);
    JS::RootedObject owner(cx,&args.thisv().toObject());
    if(!detail::setOwnerDependency(cx,owner,args[0])) return false;
    if (sprite->body) std::erase_if(sprite->body->sprites, [sprite](const auto& p) { return p.expired() || p.get() == sprite; });
    sprite->body = body ? body->state : nullptr;
    if (body) { body->state->sprites.emplace_back(sprite); const auto p = cpBodyGetPosition(body->get()); sprite->ax::Sprite::setPosition(static_cast<float>(p.x), static_cast<float>(p.y)); }
    args.rval().setUndefined(); return true;
}
static bool spriteGetBody(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); PhysicsSprite* sprite{}; if (!toNative(cx, args.thisv(), &sprite) || !sprite) return false;
    return peer<Body>(cx, sprite->body, args.rval());
}
static bool ignoreRotation(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); PhysicsSprite* sprite{}; if (!toNative(cx, args.thisv(), &sprite) || !sprite) return false;
    if (argc == 0) args.rval().setBoolean(sprite->ignoreBodyRotation);
    else if (argc == 1 && args[0].isBoolean()) { sprite->ignoreBodyRotation = args[0].toBoolean(); args.rval().setUndefined(); }
    else return invalid(cx);
    return true;
}
static bool debugConstructor(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); Space* space{};
    if (argc > 1 || (argc == 1 && !toNative(cx, args[0], &space))) return invalid(cx);
    auto native = std::make_unique<PhysicsDebugNode>(); if (!native->init()) return invalid(cx, "Cannot initialize the physics debug renderer");
    if (space) native->space = space->state;
    return owned(cx, std::move(native), args.rval());
}
static bool debugSetSpace(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); PhysicsDebugNode* node{}; Space* space{};
    if (argc != 1 || !toNative(cx, args.thisv(), &node) || !node || !toNative(cx, args[0], &space)) return invalid(cx);
    JS::RootedObject owner(cx,&args.thisv().toObject());
    if(!detail::setOwnerDependency(cx,owner,args[0])) return false;
    node->space = space ? space->state : nullptr; args.rval().setUndefined(); return true;
}
static bool debugGetSpace(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); PhysicsDebugNode* node{}; if (!toNative(cx, args.thisv(), &node) || !node) return false;
    return peer<Space>(cx, node->space, args.rval());
}

template<bool Angular, bool Set> static bool bodyLimit(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); auto* body = receiver<Body>(cx, args); if (!body) return false;
    auto& limit = Angular ? body->state->angularVelocityLimit : body->state->velocityLimit;
    if constexpr (Set) { if (argc != 1 || !fromJsValue(cx, args[0], limit) || limit < 0) return invalid(cx); args.rval().setUndefined(); }
    else args.rval().set(nativeNumberValue(limit));
    return true;
}
static void syncSprites(const std::shared_ptr<BodyState>& body)
{
    if (!body->native) return;
    auto position = cpBodyGetPosition(body->native); auto angle = cpBodyGetAngle(body->native);
    for (auto& sprite : body->sprites) if (auto* live = sprite.get())
    { live->ax::Sprite::setPosition(static_cast<float>(position.x), static_cast<float>(position.y)); auto* physics = dynamic_cast<PhysicsSprite*>(live); if (!physics || !physics->ignoreBodyRotation) live->ax::Sprite::setRotation(static_cast<float>(-angle * 180.0 / M_PI)); }
    std::erase_if(body->sprites, [](const auto& p) { return p.expired(); });
}
static bool spaceStep(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); auto* space = receiver<Space>(cx, args); double dt{};
    if (!space || argc != 1 || !fromJsValue(cx, args[0], dt) || !std::isfinite(dt) || dt < 0) return invalid(cx);
    if (cpSpaceIsLocked(space->get()) || space->state->stepping || space->state->callbacksInFlight)
        return invalid(cx, "Cannot recursively step a Chipmunk Space");
    struct StepGuard { SpaceState& state; StepGuard(SpaceState& s):state(s){state.stepping=true;} ~StepGuard(){state.stepping=false;} } guard(*space->state);
    cpSpaceStep(space->get(), dt);
    std::erase_if(space->state->postSteps, [](const auto& p) { return !p->callback; });
    for (auto& body : space->state->bodies)
    {
        if (std::isfinite(body->velocityLimit)) cpBodySetVelocity(body->native, cpvclamp(cpBodyGetVelocity(body->native), body->velocityLimit));
        if (std::isfinite(body->angularVelocityLimit)) cpBodySetAngularVelocity(body->native, cpfclamp(cpBodyGetAngularVelocity(body->native), -body->angularVelocityLimit, body->angularVelocityLimit));
        syncSprites(body);
    }
    if (space->state->callbackFailed) { space->state->callbackFailed = false; return invalid(cx, "Chipmunk JavaScript callback failed"); }
    if (JS_IsExceptionPending(cx)) return false;
    args.rval().setUndefined(); return true;
}
static bool contactGraph(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); if (!receiver<Space>(cx, args)) return false;
    // Chipmunk 7 always maintains the real contact graph for body iterators.
    // Its 6.x enable switch became unnecessary when the backend changed.
    if (argc == 0) args.rval().setBoolean(true); else if (argc == 1 && args[0].isBoolean()) args.rval().setUndefined(); else return invalid(cx);
    return true;
}
static bool impossibleConstructor(JSContext* cx, unsigned, JS::Value*)
{ return invalid(cx, "This Chipmunk base or callback object is not directly constructible"); }
template<class P, class Parent = Base> static bool registerClass(JSContext* cx, JS::HandleObject module,
    const char* name, JSNative constructor, unsigned arity, const JSFunctionSpec* methods)
{
    static JSClass type = [name] {
        JSClass t{}; t.name = name; t.flags = JSCLASS_HAS_PRIVATE | JSCLASS_HAS_RESERVED_SLOTS(PeerSlotCount);
        t.addProperty = JS_PropertyStub; t.delProperty = JS_DeletePropertyStub;
        t.getProperty = JS_PropertyStub; t.setProperty = JS_StrictPropertyStub;
        t.enumerate = JS_EnumerateStub; t.resolve = JS_ResolveStub;
        t.convert = JS_ConvertStub; t.finalize = axjs::finalizeNative; return t;
    }();
    JS::RootedObject parent(cx);
    if constexpr (!std::is_void_v<Parent>) parent = getPrototype<Parent>(cx);
    JS::RootedObject prototype(cx, JS_InitClass(cx, module, parent, &type, constructor, arity, nullptr, methods, nullptr, nullptr));
    return prototype && registerType<P, Parent>(cx, prototype, &type);
}

#define CP_METHOD(P, Name, F) JS_FN(Name, (method<P, F>), Function<decltype(&F)>::count - 1, JSPROP_ENUMERATE)
#define CP_BOOL_METHOD(P, Name, F) JS_FN(Name, (method<P, F, true>), Function<decltype(&F)>::count - 1, JSPROP_ENUMERATE)
#define CP_FUNCTION(Name, F) JS_FN(Name, (freeFunction<F>), Function<decltype(&F)>::count, JSPROP_ENUMERATE)

static const JSFunctionSpec baseMethods[] = { JS_FN("getHandle", baseHandle, 0, 0), JS_FS_END };
static const JSFunctionSpec bodyMethods[] = {
    CP_METHOD(Body, "getType", cpBodyGetType), CP_METHOD(Body, "setType", cpBodySetType),
    CP_METHOD(Body, "getMass", cpBodyGetMass), CP_METHOD(Body, "setMass", cpBodySetMass),
    CP_METHOD(Body, "getMoment", cpBodyGetMoment), CP_METHOD(Body, "setMoment", cpBodySetMoment),
    CP_METHOD(Body, "getPosition", cpBodyGetPosition), CP_METHOD(Body, "setPosition", cpBodySetPosition),
    CP_METHOD(Body, "getVelocity", cpBodyGetVelocity), CP_METHOD(Body, "setVelocity", cpBodySetVelocity),
    CP_METHOD(Body, "getForce", cpBodyGetForce), CP_METHOD(Body, "setForce", cpBodySetForce),
    CP_METHOD(Body, "getAngle", cpBodyGetAngle), CP_METHOD(Body, "setAngle", cpBodySetAngle),
    CP_METHOD(Body, "getAngularVelocity", cpBodyGetAngularVelocity), CP_METHOD(Body, "setAngularVelocity", cpBodySetAngularVelocity),
    CP_METHOD(Body, "getTorque", cpBodyGetTorque), CP_METHOD(Body, "setTorque", cpBodySetTorque),
    CP_METHOD(Body, "getRotation", cpBodyGetRotation), CP_METHOD(Body, "getSpace", cpBodyGetSpace),
    CP_METHOD(Body, "getCenterOfGravity", cpBodyGetCenterOfGravity), CP_METHOD(Body, "setCenterOfGravity", cpBodySetCenterOfGravity),
    CP_METHOD(Body, "localToWorld", cpBodyLocalToWorld), CP_METHOD(Body, "worldToLocal", cpBodyWorldToLocal),
    CP_METHOD(Body, "getVelocityAtWorldPoint", cpBodyGetVelocityAtWorldPoint), CP_METHOD(Body, "getVelocityAtLocalPoint", cpBodyGetVelocityAtLocalPoint),
    CP_METHOD(Body, "applyForceAtWorldPoint", cpBodyApplyForceAtWorldPoint), CP_METHOD(Body, "applyForceAtLocalPoint", cpBodyApplyForceAtLocalPoint),
    CP_METHOD(Body, "applyImpulseAtWorldPoint", cpBodyApplyImpulseAtWorldPoint), CP_METHOD(Body, "applyImpulseAtLocalPoint", cpBodyApplyImpulseAtLocalPoint),
    CP_METHOD(Body, "updateVelocity", cpBodyUpdateVelocity), CP_METHOD(Body, "updatePosition", cpBodyUpdatePosition),
    CP_METHOD(Body, "activate", cpBodyActivate), CP_METHOD(Body, "sleep", cpBodySleep), CP_BOOL_METHOD(Body, "isSleeping", cpBodyIsSleeping),
    JS_FN("getVelLimit", (bodyLimit<false, false>), 0, 0), JS_FN("setVelLimit", (bodyLimit<false, true>), 1, 0),
    JS_FN("getAngVelLimit", (bodyLimit<true, false>), 0, 0), JS_FN("setAngVelLimit", (bodyLimit<true, true>), 1, 0),
    JS_FN("eachShape", (each<Body, 1>), 1, 0), JS_FN("eachConstraint", (each<Body, 2>), 1, 0), JS_FN("eachArbiter", (each<Body, 3>), 1, 0), JS_FS_END
};
static const JSFunctionSpec shapeMethods[] = {
    CP_METHOD(Shape, "getBody", cpShapeGetBody), JS_FN("setBody", setShapeBody, 1, 0), CP_METHOD(Shape, "getSpace", cpShapeGetSpace),
    CP_METHOD(Shape, "getMass", cpShapeGetMass), CP_METHOD(Shape, "setMass", cpShapeSetMass),
    CP_METHOD(Shape, "getDensity", cpShapeGetDensity), CP_METHOD(Shape, "setDensity", cpShapeSetDensity),
    CP_METHOD(Shape, "getMoment", cpShapeGetMoment), CP_METHOD(Shape, "getArea", cpShapeGetArea), CP_METHOD(Shape, "getCenterOfGravity", cpShapeGetCenterOfGravity),
    CP_METHOD(Shape, "getBB", cpShapeGetBB), CP_METHOD(Shape, "cacheBB", cpShapeCacheBB), CP_METHOD(Shape, "update", cpShapeUpdate),
    CP_BOOL_METHOD(Shape, "getSensor", cpShapeGetSensor), CP_METHOD(Shape, "setSensor", cpShapeSetSensor),
    CP_METHOD(Shape, "getElasticity", cpShapeGetElasticity), CP_METHOD(Shape, "setElasticity", cpShapeSetElasticity),
    CP_METHOD(Shape, "getFriction", cpShapeGetFriction), CP_METHOD(Shape, "setFriction", cpShapeSetFriction),
    CP_METHOD(Shape, "getSurfaceVelocity", cpShapeGetSurfaceVelocity), CP_METHOD(Shape, "setSurfaceVelocity", cpShapeSetSurfaceVelocity),
    CP_METHOD(Shape, "getCollisionType", cpShapeGetCollisionType), CP_METHOD(Shape, "setCollisionType", cpShapeSetCollisionType),
    CP_METHOD(Shape, "getFilter", cpShapeGetFilter), CP_METHOD(Shape, "setFilter", cpShapeSetFilter),
    JS_FN("pointQuery", (shapeQuery<false>), 1, 0), JS_FN("segmentQuery", (shapeQuery<true>), 2, 0), JS_FS_END
};
static const JSFunctionSpec circleMethods[] = { CP_METHOD(CircleShape, "getRadius", cpCircleShapeGetRadius), CP_METHOD(CircleShape, "getOffset", cpCircleShapeGetOffset), JS_FS_END };
static const JSFunctionSpec segmentMethods[] = { CP_METHOD(SegmentShape, "getA", cpSegmentShapeGetA), CP_METHOD(SegmentShape, "getB", cpSegmentShapeGetB), CP_METHOD(SegmentShape, "getNormal", cpSegmentShapeGetNormal), CP_METHOD(SegmentShape, "getRadius", cpSegmentShapeGetRadius), CP_METHOD(SegmentShape, "setNeighbors", cpSegmentShapeSetNeighbors), JS_FS_END };
static const JSFunctionSpec polyMethods[] = { CP_METHOD(PolyShape, "getCount", cpPolyShapeGetCount), CP_METHOD(PolyShape, "getNumVerts", cpPolyShapeGetCount), CP_METHOD(PolyShape, "getVert", cpPolyShapeGetVert), CP_METHOD(PolyShape, "getRadius", cpPolyShapeGetRadius), JS_FN("getVerts", polyVerts, 0, 0), JS_FS_END };
static const JSFunctionSpec spaceMethods[] = {
    CP_METHOD(Space, "getIterations", cpSpaceGetIterations), CP_METHOD(Space, "setIterations", cpSpaceSetIterations),
    CP_METHOD(Space, "getGravity", cpSpaceGetGravity), CP_METHOD(Space, "setGravity", cpSpaceSetGravity),
    CP_METHOD(Space, "getDamping", cpSpaceGetDamping), CP_METHOD(Space, "setDamping", cpSpaceSetDamping),
    CP_METHOD(Space, "getIdleSpeedThreshold", cpSpaceGetIdleSpeedThreshold), CP_METHOD(Space, "setIdleSpeedThreshold", cpSpaceSetIdleSpeedThreshold),
    CP_METHOD(Space, "getSleepTimeThreshold", cpSpaceGetSleepTimeThreshold), CP_METHOD(Space, "setSleepTimeThreshold", cpSpaceSetSleepTimeThreshold),
    CP_METHOD(Space, "getCollisionSlop", cpSpaceGetCollisionSlop), CP_METHOD(Space, "setCollisionSlop", cpSpaceSetCollisionSlop),
    CP_METHOD(Space, "getCollisionBias", cpSpaceGetCollisionBias), CP_METHOD(Space, "setCollisionBias", cpSpaceSetCollisionBias),
    CP_METHOD(Space, "getCollisionPersistence", cpSpaceGetCollisionPersistence), CP_METHOD(Space, "setCollisionPersistence", cpSpaceSetCollisionPersistence),
    CP_METHOD(Space, "getStaticBody", cpSpaceGetStaticBody), CP_METHOD(Space, "getCurrentTimeStep", cpSpaceGetCurrentTimeStep),
    CP_BOOL_METHOD(Space, "isLocked", cpSpaceIsLocked), CP_METHOD(Space, "useSpatialHash", cpSpaceUseSpatialHash), CP_METHOD(Space, "reindexStatic", cpSpaceReindexStatic),
    JS_FN("addBody", (membership<Body, 0>), 1, 0), JS_FN("removeBody", (membership<Body, 1>), 1, 0), JS_FN("containsBody", (membership<Body, 2>), 1, 0),
    JS_FN("addShape", (membership<Shape, 0>), 1, 0), JS_FN("removeShape", (membership<Shape, 1>), 1, 0), JS_FN("containsShape", (membership<Shape, 2>), 1, 0),
    JS_FN("addConstraint", (membership<Constraint, 0>), 1, 0), JS_FN("removeConstraint", (membership<Constraint, 1>), 1, 0), JS_FN("containsConstraint", (membership<Constraint, 2>), 1, 0),
    JS_FN("step", spaceStep, 1, 0), JS_FN("free", spaceFree, 0, 0), JS_FN("getEnableContactGraph", contactGraph, 0, 0), JS_FN("setEnableContactGraph", contactGraph, 1, 0),
    JS_FN("addCollisionHandler", (setCollisionHandler<false>), 6, 0), JS_FN("setDefaultCollisionHandler", (setCollisionHandler<true>), 4, 0), JS_FN("removeCollisionHandler", removeCollisionHandler, 2, 0),
    JS_FN("addPostStepCallback", addPostStep, 1, 0), JS_FN("eachBody", (each<Space, 0>), 1, 0), JS_FN("eachShape", (each<Space, 1>), 1, 0), JS_FN("eachConstraint", (each<Space, 2>), 1, 0),
    JS_FN("pointQueryNearest", (firstQuery<false>), 3, 0), JS_FN("segmentQueryFirst", (firstQuery<true>), 4, 0), JS_FS_END
};
static const JSFunctionSpec constraintMethods[] = {
    CP_METHOD(Constraint, "getA", cpConstraintGetBodyA), CP_METHOD(Constraint, "getB", cpConstraintGetBodyB), CP_METHOD(Constraint, "getSpace", cpConstraintGetSpace),
    CP_METHOD(Constraint, "getMaxForce", cpConstraintGetMaxForce), CP_METHOD(Constraint, "setMaxForce", cpConstraintSetMaxForce),
    CP_METHOD(Constraint, "getErrorBias", cpConstraintGetErrorBias), CP_METHOD(Constraint, "setErrorBias", cpConstraintSetErrorBias),
    CP_METHOD(Constraint, "getMaxBias", cpConstraintGetMaxBias), CP_METHOD(Constraint, "setMaxBias", cpConstraintSetMaxBias),
    CP_BOOL_METHOD(Constraint, "getCollideBodies", cpConstraintGetCollideBodies), CP_METHOD(Constraint, "setCollideBodies", cpConstraintSetCollideBodies), CP_METHOD(Constraint, "getImpulse", cpConstraintGetImpulse), JS_FS_END
};
#define CP_JOINT_PROPERTY(P, Prefix, Name) CP_METHOD(P, "get" #Name, Prefix##Get##Name), CP_METHOD(P, "set" #Name, Prefix##Set##Name)
static const JSFunctionSpec pinMethods[] = { CP_JOINT_PROPERTY(PinJoint, cpPinJoint, AnchorA), CP_JOINT_PROPERTY(PinJoint, cpPinJoint, AnchorB), CP_JOINT_PROPERTY(PinJoint, cpPinJoint, Dist), JS_FS_END };
static const JSFunctionSpec slideMethods[] = { CP_JOINT_PROPERTY(SlideJoint, cpSlideJoint, AnchorA), CP_JOINT_PROPERTY(SlideJoint, cpSlideJoint, AnchorB), CP_JOINT_PROPERTY(SlideJoint, cpSlideJoint, Min), CP_JOINT_PROPERTY(SlideJoint, cpSlideJoint, Max), JS_FS_END };
static const JSFunctionSpec pivotMethods[] = { CP_JOINT_PROPERTY(PivotJoint, cpPivotJoint, AnchorA), CP_JOINT_PROPERTY(PivotJoint, cpPivotJoint, AnchorB), JS_FS_END };
static const JSFunctionSpec grooveMethods[] = { CP_JOINT_PROPERTY(GrooveJoint, cpGrooveJoint, GrooveA), CP_JOINT_PROPERTY(GrooveJoint, cpGrooveJoint, GrooveB), CP_JOINT_PROPERTY(GrooveJoint, cpGrooveJoint, AnchorB), JS_FS_END };
static const JSFunctionSpec springMethods[] = { CP_JOINT_PROPERTY(DampedSpring, cpDampedSpring, AnchorA), CP_JOINT_PROPERTY(DampedSpring, cpDampedSpring, AnchorB), CP_JOINT_PROPERTY(DampedSpring, cpDampedSpring, RestLength), CP_JOINT_PROPERTY(DampedSpring, cpDampedSpring, Stiffness), CP_JOINT_PROPERTY(DampedSpring, cpDampedSpring, Damping), JS_FS_END };
static const JSFunctionSpec rotarySpringMethods[] = { CP_JOINT_PROPERTY(DampedRotarySpring, cpDampedRotarySpring, RestAngle), CP_JOINT_PROPERTY(DampedRotarySpring, cpDampedRotarySpring, Stiffness), CP_JOINT_PROPERTY(DampedRotarySpring, cpDampedRotarySpring, Damping), JS_FS_END };
static const JSFunctionSpec rotaryLimitMethods[] = { CP_JOINT_PROPERTY(RotaryLimitJoint, cpRotaryLimitJoint, Min), CP_JOINT_PROPERTY(RotaryLimitJoint, cpRotaryLimitJoint, Max), JS_FS_END };
static const JSFunctionSpec ratchetMethods[] = { CP_JOINT_PROPERTY(RatchetJoint, cpRatchetJoint, Angle), CP_JOINT_PROPERTY(RatchetJoint, cpRatchetJoint, Phase), CP_JOINT_PROPERTY(RatchetJoint, cpRatchetJoint, Ratchet), JS_FS_END };
static const JSFunctionSpec gearMethods[] = { CP_JOINT_PROPERTY(GearJoint, cpGearJoint, Phase), CP_JOINT_PROPERTY(GearJoint, cpGearJoint, Ratio), JS_FS_END };
static const JSFunctionSpec motorMethods[] = { CP_JOINT_PROPERTY(SimpleMotor, cpSimpleMotor, Rate), JS_FS_END };
static const JSFunctionSpec arbiterMethods[] = {
    CP_METHOD(Arbiter, "getElasticity", cpArbiterGetRestitution), CP_METHOD(Arbiter, "setElasticity", cpArbiterSetRestitution),
    CP_METHOD(Arbiter, "getFriction", cpArbiterGetFriction), CP_METHOD(Arbiter, "setFriction", cpArbiterSetFriction),
    CP_METHOD(Arbiter, "getSurfaceVelocity", cpArbiterGetSurfaceVelocity), CP_METHOD(Arbiter, "setSurfaceVelocity", cpArbiterSetSurfaceVelocity),
    CP_METHOD(Arbiter, "getCount", cpArbiterGetCount), CP_METHOD(Arbiter, "getNormal", cpArbiterGetNormal),
    JS_FN("getPointA", (arbiterPoint<false>), 0, 0), JS_FN("getPointB", (arbiterPoint<true>), 0, 0), CP_METHOD(Arbiter, "getDepth", cpArbiterGetDepth),
    JS_FN("getBodies", arbiterBodies, 0, 0), JS_FN("getShapes", arbiterShapes, 0, 0),
    CP_BOOL_METHOD(Arbiter, "isFirstContact", cpArbiterIsFirstContact), CP_BOOL_METHOD(Arbiter, "isRemoval", cpArbiterIsRemoval),
    CP_METHOD(Arbiter, "totalImpulse", cpArbiterTotalImpulse), CP_METHOD(Arbiter, "totalKE", cpArbiterTotalKE), CP_BOOL_METHOD(Arbiter, "ignore", cpArbiterIgnore), JS_FS_END
};
static const JSFunctionSpec spriteMethods[] = { JS_FN("setBody", spriteSetBody, 1, 0), JS_FN("getBody", spriteGetBody, 0, 0), JS_FN("setIgnoreBodyRotation", ignoreRotation, 1, 0), JS_FN("isIgnoreBodyRotation", ignoreRotation, 0, 0), JS_FS_END };
static const JSFunctionSpec debugMethods[] = { JS_FN("setSpace", debugSetSpace, 1, 0), JS_FN("getSpace", debugGetSpace, 0, 0), JS_FS_END };
static const JSFunctionSpec functions[] = {
    CP_FUNCTION("vadd", cpvadd), CP_FUNCTION("vsub", cpvsub), CP_FUNCTION("vneg", cpvneg), CP_FUNCTION("vmult", cpvmult),
    CP_FUNCTION("vdot", cpvdot), CP_FUNCTION("vcross", cpvcross), CP_FUNCTION("vperp", cpvperp), CP_FUNCTION("vrperp", cpvrperp),
    CP_FUNCTION("vproject", cpvproject), CP_FUNCTION("vforangle", cpvforangle), CP_FUNCTION("vtoangle", cpvtoangle),
    CP_FUNCTION("vrotate", cpvrotate), CP_FUNCTION("vunrotate", cpvunrotate), CP_FUNCTION("vlength", cpvlength), CP_FUNCTION("vlengthsq", cpvlengthsq),
    CP_FUNCTION("vnormalize", cpvnormalize), CP_FUNCTION("vnormalize_safe", cpvnormalize), CP_FUNCTION("vlerp", cpvlerp), CP_FUNCTION("vlerpconst", cpvlerpconst),
    CP_FUNCTION("vslerp", cpvslerp), CP_FUNCTION("vslerpconst", cpvslerpconst), CP_FUNCTION("vclamp", cpvclamp), CP_FUNCTION("vdist", cpvdist), CP_FUNCTION("vdistsq", cpvdistsq),
    JS_FN("veql", (freeFunction<cpveql, true>), 2, 0), JS_FN("vnear", (freeFunction<cpvnear, true>), 3, 0),
    CP_FUNCTION("fclamp", cpfclamp), CP_FUNCTION("fclamp01", cpfclamp01), CP_FUNCTION("flerp", cpflerp), CP_FUNCTION("flerpconst", cpflerpconst),
    CP_FUNCTION("momentForBox", cpMomentForBox), CP_FUNCTION("momentForCircle", cpMomentForCircle), JS_FN("momentForSegment", segmentMoment, 3, 0), JS_FN("momentForPoly", polyMoment, 3, 0),
    CP_FUNCTION("areaForCircle", cpAreaForCircle), CP_FUNCTION("areaForSegment", cpAreaForSegment), JS_FN("areaForPoly", (polyGeometry<true>), 1, 0), JS_FN("centroidForPoly", (polyGeometry<false>), 1, 0),
    JS_FN("bodyNew", bodyConstructor, 2, 0), JS_FN("spaceNew", spaceConstructor, 0, 0), JS_FN("boxShapeNew", boxConstructor, 4, 0), JS_FN("segmentShapeNew", (shapeConstructor<SegmentShape, 1>), 4, 0), JS_FS_END
};
#undef CP_JOINT_PROPERTY
#undef CP_METHOD
#undef CP_BOOL_METHOD
#undef CP_FUNCTION
} // namespace legacyphysics

void appendLegacyPhysicsDebugSpace(ax::DrawNode& node, cpSpace* space)
{
    if (!space) return;
    cpSpaceEachShape(space, legacyphysics::drawDebugShape, &node);
    cpSpaceEachConstraint(space, legacyphysics::drawDebugConstraint, &node);
}

namespace tests
{
struct ChipmunkLifecycleWatch
{
    struct HandlerWatch
    {
        std::pair<cpCollisionType, cpCollisionType> pair;
        std::weak_ptr<legacyphysics::Handler> handler;
        std::array<std::weak_ptr<Callback>, 4> callbacks;
        std::array<CallbackRootsWatchPtr, 4> roots;
    };
    std::weak_ptr<RuntimeState> runtime;
    uint64_t generation{};
    std::weak_ptr<legacyphysics::SpaceState> space;
    std::shared_ptr<ChipmunkLifecycleFacts> facts;
    std::vector<HandlerWatch> handlers;
};

bool captureChipmunkLifecycleWatch(JSContext* cx, JS::HandleValue value, ChipmunkLifecycleWatchPtr& out)
{
    auto runtime=runtimeState(cx);
    // A caller on another thread must not report an error through the VM.
    if(runtime && !runtime->onOwnerThread()) return false;
    if(!runtime || !runtime->alive || !runtime->onOwnerThread())
    {
        if(cx) JS_ReportError(cx,"Chipmunk lifecycle watch requires a live VM owner thread");
        return false;
    }
    legacyphysics::Space* peer{};
    if(!toNative(cx,value,&peer) || !peer || !peer->state)
    {
        if(!JS_IsExceptionPending(cx)) JS_ReportError(cx,"Chipmunk lifecycle watch requires an actual cp.Space peer");
        return false;
    }
    auto& space=*peer->state;
    size_t callbackCount{};
    for(const auto& [pair,handler]:space.handlers)
        callbackCount+=static_cast<bool>(handler->begin)+static_cast<bool>(handler->pre)+
                       static_cast<bool>(handler->post)+static_cast<bool>(handler->separate);
    if(space.handlers.size()>ChipmunkWatchMaxHandlers || callbackCount>ChipmunkWatchMaxCallbacks)
    {
        JS_ReportError(cx,"Chipmunk lifecycle watch exceeds 256 handlers or 1024 callbacks");
        return false;
    }
    auto watch=std::make_shared<ChipmunkLifecycleWatch>();
    watch->runtime=runtime; watch->generation=runtime->generation;
    watch->space=peer->state; watch->facts=std::make_shared<ChipmunkLifecycleFacts>();
    watch->handlers.reserve(space.handlers.size());
    for(const auto& [pair,handler]:space.handlers)
    {
        ChipmunkLifecycleWatch::HandlerWatch entry;
        entry.pair=pair; entry.handler=handler;
        const std::shared_ptr<Callback> callbacks[]={handler->begin,handler->pre,handler->post,handler->separate};
        for(size_t i=0;i<4;++i) if(callbacks[i])
        {
            entry.callbacks[i]=callbacks[i];
            entry.roots[i]=callbacks[i]->watchRootsForTests();
            if(!entry.roots[i])
            {
                JS_ReportError(cx,"Chipmunk lifecycle callback belongs to an inactive VM generation");
                return false;
            }
        }
        watch->handlers.push_back(std::move(entry));
    }
    std::erase_if(space.lifecycleWatches,[](auto& weak) { return weak.expired(); });
    space.lifecycleWatches.emplace_back(watch->facts);
    out=std::move(watch);
    return true;
}

bool snapshotChipmunkLifecycleWatch(const ChipmunkLifecycleWatchPtr& watch, ChipmunkLifecycleSnapshot& out)
{
    if(!watch) return false;
    out={}; out.generation=watch->generation;
    const auto& facts=*watch->facts;
    out.explicitFreeCalls=facts.explicitFreeCalls.load(std::memory_order_acquire);
    out.nativeSpaceFreeCalls=facts.nativeSpaceFreeCalls.load(std::memory_order_acquire);
    out.spacePeerDestructions=facts.spacePeerDestructions.load(std::memory_order_acquire);
    out.spaceStateDestructions=facts.spaceStateDestructions.load(std::memory_order_acquire);
    out.firstNativeFreeReason=facts.firstNativeFreeReason.load(std::memory_order_acquire);
    auto runtime=watch->runtime.lock();
    out.runtimeAlive=runtime && runtime->alive && runtime->generation==watch->generation;
    if(out.runtimeAlive && !runtime->onOwnerThread()) return false;
    // Never lock old native state after stop or use a context as VM identity.
    auto space=out.runtimeAlive ? watch->space.lock() : nullptr;
    out.liveStateInspected=out.runtimeAlive;
    if(space)
    {
        out.spaceStateAlive=true;
        out.spacePeerAlive=space->proxy!=nullptr;
        out.nativeSpaceAlive=space->native!=nullptr;
        out.currentBodyCount=static_cast<uint32_t>(space->bodies.size());
        out.currentShapeCount=static_cast<uint32_t>(space->shapes.size());
        out.currentConstraintCount=static_cast<uint32_t>(space->constraints.size());
        out.currentHandlerCount=static_cast<uint32_t>(space->handlers.size());
    }
    out.handlers.reserve(watch->handlers.size());
    for(const auto& captured:watch->handlers)
    {
        ChipmunkHandlerSnapshot entry;
        entry.typeA=static_cast<uint64_t>(captured.pair.first);
        entry.typeB=static_cast<uint64_t>(captured.pair.second);
        if(out.runtimeAlive)
        {
            auto handler=captured.handler.lock();
            entry.handlerAlive=static_cast<bool>(handler);
            if(space && handler)
            {
                auto registered=space->handlers.find(captured.pair);
                entry.stillRegistered=registered!=space->handlers.end() && registered->second==handler;
            }
        }
        for(size_t i=0;i<4;++i)
        {
            auto& callback=entry.callbacks[i];
            callback.captured=static_cast<bool>(captured.roots[i]);
            callback.alive=out.runtimeAlive && !captured.callbacks[i].expired();
            CallbackRootsSnapshot roots;
            if(snapshotCallbackRoots(captured.roots[i],roots))
            {
                callback.functionRootCaptured=roots.functionRootCaptured;
                callback.receiverRootCaptured=roots.receiverRootCaptured;
                callback.functionRootReleaseCount=roots.functionRootReleaseCount;
                callback.receiverRootReleaseCount=roots.receiverRootReleaseCount;
                callback.ownerBoundCaptured=roots.ownerBoundCaptured;
                callback.ownerBoundReleaseCount=roots.ownerBoundReleaseCount;
            }
        }
        out.handlers.push_back(std::move(entry));
    }
    return true;
}
} // namespace tests

#if defined(AX_ENABLE_PHYSICS_3D)
namespace legacyphysics3d
{
using legacyphysics::invalid;
class CollisionFilter final : public JPH::GroupFilter
{
public:
    mutable std::mutex mutex;
    std::unordered_map<uint32_t, uint32_t> masks;
    std::set<std::pair<uint32_t,uint32_t>> disabled;
    bool CanCollide(const JPH::CollisionGroup& a, const JPH::CollisionGroup& b) const override
    {
        std::lock_guard lock(mutex); auto x = a.GetSubGroupID(), y = b.GetSubGroupID();
        if ((masks.contains(x) && masks.at(x) == 0) || (masks.contains(y) && masks.at(y) == 0)) return false;
        return !disabled.contains(std::minmax(x,y));
    }
};
struct BodyOptions
{
    ax::WeakPtr<ax::Rigidbody3D> body;
    ax::Vec3 linearFactor{1,1,1}; uint32_t mask{0xffffffff};
    JPH::BodyID applied; bool dirty{true};
};
struct CollisionBinding
{
    ax::WeakPtr<ax::Rigidbody3D> body;
    ax::WeakPtr<ax::ContactEventListener3D> listener;
    ax::WeakPtr<ax::Component> gate;
};
struct State
{
    std::vector<CollisionBinding> collisionBindings;
    // Jolt installs its allocator when the first physics world initializes.
    // Pure JS registration and nonphysics bootstrap must not allocate it yet.
    JPH::Ref<CollisionFilter> filter;
    std::vector<BodyOptions> bodies;
    std::vector<ax::WeakPtr<ax::ContactEventListener3D>> listeners;
    BodyOptions& options(ax::Rigidbody3D* body)
    {
        if (!filter) filter = new CollisionFilter;
        for (auto& option : bodies) if (option.body.get() == body) return option;
        BodyOptions option; option.body = body; bodies.push_back(option); return bodies.back();
    }
    void update()
    {
        if (!filter) return;
        for (auto& option : bodies)
        {
            auto* body = option.body.get(); if (!body || !body->isAttached() || (!option.dirty && option.applied == body->internalHandle())) continue;
            auto id = body->internalHandle(); auto* world = body->getWorld(); auto& system = world->internalHandle();
            { std::lock_guard lock(filter->mutex); filter->masks[id.GetIndexAndSequenceNumber()] = option.mask; }
            system.GetBodyInterface().SetCollisionGroup(id, JPH::CollisionGroup(filter, 0, id.GetIndexAndSequenceNumber()));
            if (body->isDynamic())
            {
                auto bodyMass = body->getMass();
                JPH::BodyLockWrite lock(system.GetBodyLockInterface(), id);
                if (lock.Succeeded())
                {
                    auto* motion = lock.GetBody().GetMotionProperties();
                    auto mass = lock.GetBody().GetShape()->GetMassProperties(); mass.ScaleToMass(bodyMass);
                    auto allowed = static_cast<uint32_t>(motion->GetAllowedDOFs());
                    const JPH::EAllowedDOFs translation[] = {JPH::EAllowedDOFs::TranslationX,JPH::EAllowedDOFs::TranslationY,JPH::EAllowedDOFs::TranslationZ};
                    const float factors[] = {option.linearFactor.x,option.linearFactor.y,option.linearFactor.z};
                    for (int i = 0; i < 3; ++i) if (factors[i] == 0) allowed &= ~static_cast<uint32_t>(translation[i]); else allowed |= static_cast<uint32_t>(translation[i]);
                    motion->SetMassProperties(static_cast<JPH::EAllowedDOFs>(allowed), mass);
                }
            }
            option.applied = id; option.dirty = false;
        }
        std::erase_if(bodies, [this](const auto& option) {
            if (!option.body.expired()) return false;
            std::lock_guard lock(filter->mutex); filter->masks.erase(option.applied.GetIndexAndSequenceNumber());
            return true;
        });
    }
};
static std::unordered_map<JSContext*, std::weak_ptr<State>> states;
static std::shared_ptr<State> state(JSContext* cx) { auto found = states.find(cx); return found == states.end() ? nullptr : found->second.lock(); }
static bool world(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); ax::Scene* scene{}; if (!toNative(cx, args.thisv(), &scene) || !scene) return false;
    return wrapNative(cx, scene->getPhysicsWorld3D(), args.rval());
}
static bool rayCast(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); ax::PhysicsWorld3D* self{}; ax::Vec3 a{}, b{};
    if (argc != 2 || !toNative(cx, args.thisv(), &self) || !self || !fromJsValue(cx, args[0], a) || !fromJsValue(cx, args[1], b)) return invalid(cx);
    ax::PhysicsWorld3D::HitResult hit; bool found = self->rayCast(a,b,&hit);
    JS::RootedObject object(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr())); JS::RootedValue point(cx), normal(cx), actor(cx), body(cx), collider(cx), success(cx,JS::BooleanValue(found));
    if (!object || !toJsValue(cx,hit.hitPosition,&point) || !toJsValue(cx,hit.hitNormal,&normal) || !JS_SetProperty(cx,object,"hitPosition",point) || !JS_SetProperty(cx,object,"hitNormal",normal) || !JS_SetProperty(cx,object,"hit",success)) return false;
    if (found)
    {
        if (!wrapNative(cx,hit.hitActor,&actor) || !wrapNative(cx,hit.rigidbody,&body) || !wrapNative(cx,hit.collider,&collider) || !JS_SetProperty(cx,object,"hitObj",actor) || !JS_SetProperty(cx,object,"hitActor",actor) || !JS_SetProperty(cx,object,"rigidbody",body) || !JS_SetProperty(cx,object,"collider",collider)) return false;
    }
    args.rval().setObject(*object); return true;
}
static bool bodyTransform(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); ax::Rigidbody3D* self{}; ax::Vec3 offset{}; ax::Quat rotation = ax::Quat::identity;
    if (argc < 1 || argc > 2 || !toNative(cx,args.thisv(),&self) || !self || !fromJsValue(cx,args[0],offset) || (argc == 2 && !fromJsValue(cx,args[1],rotation))) return invalid(cx);
    self->setTransformInPhysics(offset,rotation); args.rval().setUndefined(); return true;
}
static bool kinematicZeroStepSync(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::Rigidbody3D* body{};
    bool enabled{};
    if (argc != 1 || !toNative(cx,args.thisv(),&body) || !body || !fromJsValue(cx,args[0],enabled))
        return invalid(cx,"A live Rigidbody3D and zero-step sync policy are required");
    body->setKinematicZeroStepSyncEnabled(enabled);
    args.rval().setUndefined();
    return true;
}
static bool linearFactor(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp); ax::Rigidbody3D* body{}; ax::Vec3 factor{}; auto shared = state(cx);
    if (!shared || argc != 1 || !toNative(cx,args.thisv(),&body) || !body || !fromJsValue(cx,args[0],factor) ||
        (factor.x != 0 && factor.x != 1) || (factor.y != 0 && factor.y != 1) || (factor.z != 0 && factor.z != 1)) return invalid(cx,"Jolt translation factors must be 0 (locked) or 1 (free)");
    auto& option = shared->options(body); option.linearFactor = factor; option.dirty = true; shared->update(); args.rval().setUndefined(); return true;
}
static bool bodyMask(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc,vp); ax::Rigidbody3D* body{}; auto shared = state(cx); uint32_t mask{};
    if (!shared || !toNative(cx,args.thisv(),&body) || !body || argc > 1 || (argc == 1 && !JS::ToUint32(cx,args[0],&mask))) return invalid(cx);
    auto& option = shared->options(body);
    if (argc == 0) args.rval().setNumber(option.mask);
    else { option.mask = mask; option.dirty = true; shared->update(); args.rval().setUndefined(); }
    return true;
}
static bool addJoint(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc,vp); ax::PhysicsWorld3D* world{}; ax::Joint3D* joint{}; ax::Rigidbody3D* body{}; bool disable{};
    if (argc != 3 || !toNative(cx,args.thisv(),&world) || !world || !toNative(cx,args[0],&joint) || !joint || !toNative(cx,args[1],&body) || !body || !fromJsValue(cx,args[2],disable)) return invalid(cx);
    auto* owner = body->getOwner();
    auto* other = joint->getConnectedBody();
    // Old tests register constraints from a Layer constructor, before that
    // Layer belongs to a Scene. Explicit registration must create the real
    // Jolt bodies in the requested world instead of requiring onEnter first.
    auto compatible = [world](ax::Rigidbody3D* value) {
        if (!value || !value->getOwner()) return false;
        if (value->getWorld() && value->getWorld() != world) return false;
        auto* scene = value->getOwner()->getScene();
        return !scene || scene->getPhysicsWorld3D() == world;
    };
    if (!compatible(body) || (other && !compatible(other)) || (joint->getWorld() && joint->getWorld() != world))
        return invalid(cx,"A legacy joint must attach to bodies in this physics world");
    if (joint->getOwner() && joint->getOwner() != owner) return invalid(cx,"Joint already belongs to another body");
    const bool bodyWasAttached = body->isAttached();
    if (!body->attachToPhysicsWorld(world)) return invalid(cx,"Jolt could not attach the legacy body");
    if (other && !other->attachToPhysicsWorld(world))
    {
        if (!bodyWasAttached) world->removePhysicsActor(body);
        return invalid(cx,"Jolt could not attach the connected legacy body");
    }
    if (!joint->getOwner()) owner->addComponent(joint);
    if (!joint->isAttached()) joint->onEnter();
    if (!joint->isAttached()) return invalid(cx,"Jolt could not attach the legacy joint");
    if (disable && joint->getConnectedBody())
    {
        auto shared = state(cx); auto* other = joint->getConnectedBody(); shared->options(body); shared->options(other); shared->update();
        std::lock_guard lock(shared->filter->mutex); shared->filter->disabled.insert(std::minmax(body->internalHandle().GetIndexAndSequenceNumber(),other->internalHandle().GetIndexAndSequenceNumber()));
    }
    args.rval().setUndefined(); return true;
}
static bool removeJoint(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc,vp); ax::PhysicsWorld3D* world{}; ax::Joint3D* joint{};
    if (argc != 1 || !toNative(cx,args.thisv(),&world) || !world || !toNative(cx,args[0],&joint) || !joint) return invalid(cx);
    if (joint->getWorld() && joint->getWorld() != world) return invalid(cx,"Joint belongs to another world");
    auto* a = joint->getAttachedBody(); auto* b = joint->getConnectedBody(); auto shared = state(cx);
    if (a && b && shared && shared->filter) { std::lock_guard lock(shared->filter->mutex); shared->filter->disabled.erase(std::minmax(a->internalHandle().GetIndexAndSequenceNumber(),b->internalHandle().GetIndexAndSequenceNumber())); }
    if (joint->isAttached()) joint->onExit();
    if (auto* owner = joint->getOwner()) owner->removeComponent(joint);
    args.rval().setUndefined(); return true;
}
template<class Joint, int Kind> static bool limits(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc,vp); Joint* joint{};
    if (!toNative(cx,args.thisv(),&joint) || !joint) return false;
    if constexpr (Kind == 0) { float a{}, b{}; if (argc != 2 || !fromJsValue(cx,args[0],a) || !fromJsValue(cx,args[1],b)) return invalid(cx); joint->setLimits({a,b}); }
    if constexpr (Kind == 1) { float a{},b{},c{}; if (argc != 3 || !fromJsValue(cx,args[0],a) || !fromJsValue(cx,args[1],b) || !fromJsValue(cx,args[2],c)) return invalid(cx); joint->setLimits({a,b,c}); }
    if constexpr (Kind == 2 || Kind == 3) { ax::SixDofLimits value; if (argc != 2 || !fromJsValue(cx,args[0],value.lower) || !fromJsValue(cx,args[1],value.upper)) return invalid(cx); if constexpr (Kind == 2) joint->setLinearLimits(value); else joint->setAngularLimits(value); }
    args.rval().setUndefined(); return true;
}

// The legacy constructors accept both orthogonal axes of each local frame.
// Current Joint3D exposes only its primary axis, so preserve the full frame in
// the real Jolt constraint instead of rebuilding its roll from a helper axis.
static JPH::Vec3 frameAxis(const ax::Mat4& frame,int column)
{
    return JPH::Vec3(frame.m[column*4],frame.m[column*4+1],frame.m[column*4+2]);
}
static JPH::RVec3 framePoint(const ax::Mat4& frame)
{
    return JPH::RVec3(frame.m[12],frame.m[13],frame.m[14]);
}
static ax::Mat4 fixedWorldFrame(ax::Rigidbody3D* body,const ax::Mat4& local)
{
    ax::Mat4 result=local;
    auto origin=body->local2World(ax::Vec3::zero);
    for(int i=0;i<3;++i)
    {
        auto axis=body->local2World({local.m[i*4],local.m[i*4+1],local.m[i*4+2]})-origin;
        result.m[i*4]=axis.x;result.m[i*4+1]=axis.y;result.m[i*4+2]=axis.z;
    }
    auto point=body->local2World({local.m[12],local.m[13],local.m[14]});
    result.m[12]=point.x;result.m[13]=point.y;result.m[14]=point.z;
    return result;
}
template<class NativeJoint,class Settings>
class FrameJoint final : public NativeJoint
{
public:
    FrameJoint(ax::Rigidbody3D* connected,const ax::Mat4& a,const ax::Mat4& b)
        : NativeJoint(connected),frameA(a),frameB(b)
    {
        this->setAnchor({a.m[12],a.m[13],a.m[14]});this->setConnectedAnchor({b.m[12],b.m[13],b.m[14]});
        this->setAxis({a.m[0],a.m[1],a.m[2]});this->setConnectedAxis({b.m[0],b.m[1],b.m[2]});
    }
protected:
    bool attachToBody() override
    {
        if(this->isAttached())return true;
        if(!NativeJoint::attachToBody())return false;
        auto copy=this->_constraint->GetConstraintSettings();
        auto* settings=static_cast<Settings*>(copy.GetPtr());
        settings->mSpace=JPH::EConstraintSpace::LocalToBodyCOM;
        if constexpr(std::is_same_v<Settings,JPH::SliderConstraintSettings>)
        {
            settings->mPoint1=framePoint(frameA);settings->mPoint2=framePoint(frameB);
            settings->mSliderAxis1=frameAxis(frameA,0);settings->mSliderAxis2=frameAxis(frameB,0);
            settings->mNormalAxis1=frameAxis(frameA,1);settings->mNormalAxis2=frameAxis(frameB,1);
        }
        else if constexpr(std::is_same_v<Settings,JPH::SwingTwistConstraintSettings>)
        {
            settings->mPosition1=framePoint(frameA);settings->mPosition2=framePoint(frameB);
            settings->mTwistAxis1=frameAxis(frameA,0);settings->mTwistAxis2=frameAxis(frameB,0);
            // Jolt's plane axis is the frame's Z axis; its normal axis is
            // plane.cross(twist), reconstructing the old matrix's Y column.
            settings->mPlaneAxis1=frameAxis(frameA,2);settings->mPlaneAxis2=frameAxis(frameB,2);
        }
        else
        {
            settings->mPosition1=framePoint(frameA);settings->mPosition2=framePoint(frameB);
            settings->mAxisX1=frameAxis(frameA,0);settings->mAxisX2=frameAxis(frameB,0);
            settings->mAxisY1=frameAxis(frameA,1);settings->mAxisY2=frameAxis(frameB,1);
        }
        auto& system=this->_world->internalHandle();
        auto* constraint=system.GetBodyInterface().CreateConstraint(settings,this->_attachedBody->internalHandle(),
            this->_connectedBody ? this->_connectedBody->internalHandle() : JPH::BodyID{});
        if(!constraint)return false;
        system.RemoveConstraint(this->_constraint);this->_constraint=constraint;system.AddConstraint(constraint);
        system.GetBodyInterface().ActivateConstraint(constraint);
        return true;
    }
private:
    ax::Mat4 frameA,frameB;
};
template<class Joint,class Settings>
static bool frameJoint(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);ax::Rigidbody3D* body{};ax::Rigidbody3D* connected{};ax::Mat4 a,b;
    if(argc!=4 || !toNative(cx,args[0],&body) || !body || !toNative(cx,args[1],&connected) ||
        !fromJsValue(cx,args[2],a) || (connected && !fromJsValue(cx,args[3],b)))return invalid(cx);
    if(!connected)b=fixedWorldFrame(body,a);
    auto* result=new FrameJoint<Joint,Settings>(connected,a,b);result->autorelease();
    return wrapNative(cx,static_cast<Joint*>(result),args.rval(),NativeOwnership::Factory);
}

// This native component is created only by an actual legacy collision callback
// registration. It follows the owner's real component lifecycle; it neither
// changes physics state nor relies on the JS runtime pump preceding the solver.
class CollisionEventGate final : public ax::Component
{
public:
    CollisionEventGate(ax::Rigidbody3D* body,const std::shared_ptr<Callback>& callback)
        : _body(body), _callback(callback) { setName("AxmolLegacyCollisionEventGate"); }
    void onEnter() override { ax::Component::onEnter(); enableForAttachedWorld(); }
    void onExit() override { _appliedWorld.reset(); ax::Component::onExit(); }
    void onRemove() override { _callback.reset(); _appliedWorld.reset(); ax::Component::onRemove(); }
    void update(float) override { enableForAttachedWorld(); }
private:
    void enableForAttachedWorld()
    {
        auto callback=_callback.lock(); auto* body=_body.get();
        if(!callback || !callback->isValid() || !body)return;
        auto* world=body->getWorld();
        if(!world || !body->isAttached()) { _appliedWorld.reset(); return; }
        if(_appliedWorld.get()==world)return;
        world->setGlobalEventEnabled(ax::ContactEventBits::Hit,true);
        _appliedWorld=world;
    }
    ax::WeakPtr<ax::Rigidbody3D> _body;
    std::weak_ptr<Callback> _callback;
    ax::WeakPtr<ax::PhysicsWorld3D> _appliedWorld;
};
static bool collisionCallback(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc,vp); ax::Rigidbody3D* body{}; if (argc < 1 || argc > 2 || !toNative(cx,args.thisv(),&body) || !body || !body->getOwner()) return invalid(cx,"Assign the body component to a node before installing its collision callback");
    JS::RootedObject receiver(cx,argc == 2 && args[1].isObject() ? &args[1].toObject() : &args.thisv().toObject()); auto callback = Callback::create(cx,args[0],receiver); if (!callback) return false;
    auto* listener = ax::ContactEventListener3D::create(); if (!listener) return false; ax::WeakPtr<ax::Rigidbody3D> weak(body);
    listener->onCollisionHit = [callback,weak](ax::ContactEvent3D* event) {
        auto* live = weak.get(); if (!live || !callback->isValid() || (event->getActorA() != live && event->getActorB() != live)) return;
        auto runtime = callback->state(); if (!runtime || !runtime->onOwnerThread()) return; JSContext* cx = runtime->context;
        JS::RootedObject info(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr())), points(cx,JS_NewArrayObject(cx,0)); JS::RootedValue a(cx),b(cx),value(cx);
        if (!info || !points || !wrapNative(cx,event->getActorA(),&a) || !wrapNative(cx,event->getActorB(),&b) || !JS_SetProperty(cx,info,"objA",a) || !JS_SetProperty(cx,info,"objB",b)) return;
        unsigned index{};
        for (const auto& point : event->getContactInfo().points) {
            JS::RootedObject object(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr())); JS::RootedValue p(cx),q(cx),entry(cx);
            if (!object || !toJsValue(cx,point.sideA.point,&p) || !toJsValue(cx,point.sideB.point,&q) || !JS_SetProperty(cx,object,"worldPositionOnA",p) || !JS_SetProperty(cx,object,"worldPositionOnB",q)) return;
            entry.setObject(*object); if (!JS_SetElement(cx,points,index++,entry)) return;
        }
        value.setObject(*points); if (!JS_SetProperty(cx,info,"collisionPointList",value)) return; value.setObject(*info); callback->call(JS::HandleValueArray(value));
    };
    auto shared=state(cx); auto* dispatcher=ax::Director::getInstance()->getEventDispatcher();
    if(!shared)return false;
    for(auto& entry:shared->collisionBindings)
        if(entry.body.get()==body) {
            if(auto* previous=entry.listener.get())dispatcher->removeEventListener(previous);
            if(auto* gate=entry.gate.get())if(auto* owner=gate->getOwner())owner->removeComponent(gate);
        }
    std::erase_if(shared->collisionBindings,[body](const auto& entry) { return entry.body.expired() || entry.body.get()==body; });
    body->setEventEnabled(ax::ContactEventBits::Hit,true);
    dispatcher->addEventListenerWithSceneGraphPriority(listener,body->getOwner());
    auto* gate=new CollisionEventGate(body,callback);gate->autorelease();
    if(!body->getOwner()->addComponent(gate)) { dispatcher->removeEventListener(listener);return false; }
    shared->collisionBindings.push_back({ax::WeakPtr<ax::Rigidbody3D>(body),ax::WeakPtr<ax::ContactEventListener3D>(listener),ax::WeakPtr<ax::Component>(gate)});
    shared->listeners.emplace_back(listener); args.rval().setUndefined(); return true;
}
static bool registerBindings(JSContext* cx, JS::HandleObject)
{
    auto shared = std::make_shared<State>(); states[cx] = shared;
    engine(cx)->addUpdateCallback([shared](float) { shared->update(); });
    engine(cx)->addCleanupCallback([shared,cx] {
        auto* dispatcher = ax::Director::getInstance()->getEventDispatcher(); for (auto& listener : shared->listeners) if (auto* p = listener.get()) dispatcher->removeEventListener(p);
        for(auto& entry:shared->collisionBindings)if(auto* gate=entry.gate.get())if(auto* owner=gate->getOwner())owner->removeComponent(gate);
        shared->collisionBindings.clear();
        if (shared->filter) { std::lock_guard lock(shared->filter->mutex); shared->filter->disabled.clear(); shared->filter->masks.clear(); }
        shared->bodies.clear(); shared->listeners.clear(); states.erase(cx);
    });
    JS::RootedObject scene(cx,getPrototype<ax::Scene>(cx)), world(cx,getPrototype<ax::PhysicsWorld3D>(cx)), body(cx,getPrototype<ax::Rigidbody3D>(cx));
    const JSFunctionSpec worldMethods[] = { JS_FN("rayCast",rayCast,2,0), JS_FN("__addLegacyJoint",addJoint,3,0), JS_FN("removePhysics3DConstraint",removeJoint,1,0), JS_FS_END };
    const JSFunctionSpec bodyMethods[] = { JS_FN("__setKinematicZeroStepSync",kinematicZeroStepSync,1,0), JS_FN("setTransformInPhysics",bodyTransform,2,0), JS_FN("setLinearFactor",linearFactor,1,0), JS_FN("getMask",bodyMask,0,0), JS_FN("setMask",bodyMask,1,0), JS_FN("setCollisionCallback",collisionCallback,1,0), JS_FS_END };
    if (!scene || !world || !body || !JS_DefineFunction(cx,scene,"getPhysics3DWorld",legacyphysics3d::world,0,0) || !JS_DefineFunctions(cx,world,worldMethods) || !JS_DefineFunctions(cx,body,bodyMethods)) return false;
    JS::RootedObject slider(cx,getPrototype<ax::SliderJoint3D>(cx)), hinge(cx,getPrototype<ax::HingeJoint3D>(cx)), cone(cx,getPrototype<ax::ConeTwistJoint3D>(cx)), six(cx,getPrototype<ax::SixDofJoint3D>(cx));
    JS::RootedObject sliderClass(cx,getClassObject<ax::SliderJoint3D>(cx)), coneClass(cx,getClassObject<ax::ConeTwistJoint3D>(cx)), sixClass(cx,getClassObject<ax::SixDofJoint3D>(cx));
    if(!JS_DefineFunction(cx,sliderClass,"__legacyCreateFrames",frameJoint<ax::SliderJoint3D,JPH::SliderConstraintSettings>,4,0) ||
        !JS_DefineFunction(cx,coneClass,"__legacyCreateFrames",frameJoint<ax::ConeTwistJoint3D,JPH::SwingTwistConstraintSettings>,4,0) ||
        !JS_DefineFunction(cx,sixClass,"__legacyCreateFrames",frameJoint<ax::SixDofJoint3D,JPH::SixDOFConstraintSettings>,4,0))return false;
    return JS_DefineFunction(cx,slider,"__setLegacyLimits",limits<ax::SliderJoint3D,0>,2,0) && JS_DefineFunction(cx,hinge,"__setLegacyLimits",limits<ax::HingeJoint3D,0>,2,0) &&
        JS_DefineFunction(cx,cone,"__setLegacyLimits",limits<ax::ConeTwistJoint3D,1>,3,0) && JS_DefineFunction(cx,six,"__setLegacyLinearLimits",limits<ax::SixDofJoint3D,2>,2,0) && JS_DefineFunction(cx,six,"__setLegacyAngularLimits",limits<ax::SixDofJoint3D,3>,2,0);
}
} // namespace legacyphysics3d
#endif

#if defined(AX_ENABLE_NAVMESH)
namespace legacynavmesh
{
static bool setNavMesh(JSContext* cx,unsigned argc,JS::Value* vp)
{ auto args = JS::CallArgsFromVp(argc,vp); ax::Scene* scene{}; ax::NavMesh* mesh{}; if (argc != 1 || !toNative(cx,args.thisv(),&scene) || !scene || !toNative(cx,args[0],&mesh)) return false; scene->setNavMesh(mesh); args.rval().setUndefined(); return true; }
static bool getNavMesh(JSContext* cx,unsigned argc,JS::Value* vp)
{ auto args = JS::CallArgsFromVp(argc,vp); ax::Scene* scene{}; if (!toNative(cx,args.thisv(),&scene) || !scene) return false; return wrapNative(cx,scene->getNavMesh(),args.rval()); }
static bool findPath(JSContext* cx,unsigned argc,JS::Value* vp)
{ auto args = JS::CallArgsFromVp(argc,vp); ax::NavMesh* mesh{}; ax::Vec3 start{},end{}; if (argc != 2 || !toNative(cx,args.thisv(),&mesh) || !mesh || !fromJsValue(cx,args[0],start) || !fromJsValue(cx,args[1],end)) return legacyphysics::invalid(cx); std::vector<ax::Vec3> points; mesh->findPath(start,end,points); return toJsValue(cx,points,args.rval()); }
static bool registerBindings(JSContext* cx)
{ JS::RootedObject scene(cx,getPrototype<ax::Scene>(cx)),mesh(cx,getPrototype<ax::NavMesh>(cx)); return scene && mesh && JS_DefineFunction(cx,scene,"setNavMesh",setNavMesh,1,0) && JS_DefineFunction(cx,scene,"getNavMesh",getNavMesh,0,0) && JS_DefineFunction(cx,mesh,"findPath",findPath,2,0); }
}
#endif

bool registerLegacyPhysics(JSContext* cx, JS::HandleObject global)
{
    using namespace legacyphysics;
    JS::RootedObject module(cx); if (!getOrCreateNamespace(cx, global, "cp", &module)) return false;
    if (!registerClass<Base, void>(cx, module, "Base", impossibleConstructor, 0, baseMethods) ||
        !registerClass<Body>(cx, module, "Body", bodyConstructor, 2, bodyMethods) ||
        !registerClass<Shape>(cx, module, "Shape", impossibleConstructor, 0, shapeMethods) ||
        !registerClass<CircleShape, Shape>(cx, module, "CircleShape", shapeConstructor<CircleShape, 0>, 3, circleMethods) ||
        !registerClass<SegmentShape, Shape>(cx, module, "SegmentShape", shapeConstructor<SegmentShape, 1>, 4, segmentMethods) ||
        !registerClass<PolyShape, Shape>(cx, module, "PolyShape", shapeConstructor<PolyShape, 2>, 3, polyMethods) ||
        !registerClass<Space>(cx, module, "Space", spaceConstructor, 0, spaceMethods) ||
        !registerClass<Constraint>(cx, module, "Constraint", impossibleConstructor, 0, constraintMethods) ||
        !registerClass<PinJoint, Constraint>(cx, module, "PinJoint", jointConstructor<PinJoint, cpPinJointNew>, 4, pinMethods) ||
        !registerClass<SlideJoint, Constraint>(cx, module, "SlideJoint", jointConstructor<SlideJoint, cpSlideJointNew>, 6, slideMethods) ||
        !registerClass<PivotJoint, Constraint>(cx, module, "PivotJoint", pivotConstructor, 3, pivotMethods) ||
        !registerClass<GrooveJoint, Constraint>(cx, module, "GrooveJoint", jointConstructor<GrooveJoint, cpGrooveJointNew>, 5, grooveMethods) ||
        !registerClass<DampedSpring, Constraint>(cx, module, "DampedSpring", jointConstructor<DampedSpring, cpDampedSpringNew>, 7, springMethods) ||
        !registerClass<DampedRotarySpring, Constraint>(cx, module, "DampedRotarySpring", jointConstructor<DampedRotarySpring, cpDampedRotarySpringNew>, 5, rotarySpringMethods) ||
        !registerClass<RotaryLimitJoint, Constraint>(cx, module, "RotaryLimitJoint", jointConstructor<RotaryLimitJoint, cpRotaryLimitJointNew>, 4, rotaryLimitMethods) ||
        !registerClass<RatchetJoint, Constraint>(cx, module, "RatchetJoint", jointConstructor<RatchetJoint, cpRatchetJointNew>, 5, ratchetMethods) ||
        !registerClass<GearJoint, Constraint>(cx, module, "GearJoint", jointConstructor<GearJoint, cpGearJointNew>, 4, gearMethods) ||
        !registerClass<SimpleMotor, Constraint>(cx, module, "SimpleMotor", jointConstructor<SimpleMotor, cpSimpleMotorNew>, 3, motorMethods) ||
        !registerClass<Arbiter>(cx, module, "Arbiter", impossibleConstructor, 0, arbiterMethods) || !JS_DefineFunctions(cx, module, functions)) return false;
    JS::RootedObject bodyPrototype(cx, getPrototype<Body>(cx));
    const JSPropertySpec bodyProperties[] = { JS_PSGS("velocity_func", velocityGet, velocitySet, JSPROP_ENUMERATE), JS_PS_END };
    if (!JS_DefineProperties(cx, bodyPrototype, bodyProperties)) return false;
    JS::RootedObject cc(cx); if (!getOrCreateNamespace(cx, global, "cc", &cc) ||
        !registerClass<PhysicsSprite, ax::Sprite>(cx, cc, "PhysicsSprite", spriteConstructor, 0, spriteMethods) ||
        !registerClass<PhysicsDebugNode, ax::DrawNode>(cx, cc, "PhysicsDebugNode", debugConstructor, 1, debugMethods)) return false;
    JS::RootedObject sprite(cx, getClassObject<PhysicsSprite>(cx)), debug(cx, getClassObject<PhysicsDebugNode>(cx));
    if (!JS_DefineFunction(cx, sprite, "create", spriteConstructor, 0, 0) || !JS_DefineFunction(cx, debug, "create", debugConstructor, 1, 0)) return false;
    struct Constant { const char* name; double value; };
    for (auto c : {Constant{"VERSION_MAJOR", CP_VERSION_MAJOR}, Constant{"VERSION_MINOR", CP_VERSION_MINOR}, Constant{"VERSION_RELEASE", CP_VERSION_RELEASE},
                   Constant{"ALL_CATEGORIES", CP_ALL_CATEGORIES}, Constant{"NO_GROUP", CP_NO_GROUP}, Constant{"USE_DOUBLES", CP_USE_DOUBLES},
                   Constant{"BODY_TYPE_DYNAMIC", CP_BODY_TYPE_DYNAMIC}, Constant{"BODY_TYPE_KINEMATIC", CP_BODY_TYPE_KINEMATIC}, Constant{"BODY_TYPE_STATIC", CP_BODY_TYPE_STATIC}})
    { JS::RootedValue value(cx, JS::DoubleValue(c.value)); if (!JS_DefineProperty(cx, module, c.name, value, JSPROP_ENUMERATE)) return false; }
#if defined(AX_ENABLE_PHYSICS_3D)
    if (!legacyphysics3d::registerBindings(cx,global)) return false;
#endif
#if defined(AX_ENABLE_NAVMESH)
    if (!legacynavmesh::registerBindings(cx)) return false;
#endif
    return true;
}
} // namespace axjs
