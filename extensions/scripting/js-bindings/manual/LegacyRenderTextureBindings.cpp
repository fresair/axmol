/****************************************************************************
Copyright (c) 2009      Jason Booth
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2013-2016 Chukong Technologies Inc.
Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

http://www.cocos2d-x.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/

// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
#include "manual/LegacyRenderTextureBindings.h"
#include "manual/ManualUtils.h"
#include "runtime/JsEngine.h"
#include "axmol/base/CustomEventListener.h"
#include "axmol/base/EventDispatcher.h"
#include "axmol/2d/Sprite.h"
#include "axmol/renderer/CallbackCommand.h"
#include "axmol/renderer/Renderer.h"
#include "axmol/renderer/RenderTexture.h"
#include "axmol/renderer/RenderTexturePass.h"
#include "axmol/rhi/GraphicsCore.h"
#include "axmol/scene/Camera.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

namespace axjs
{
namespace legacyRenderTexture
{
bool makeRenderState(const ax::SceneRenderState& original, const ax::Vec2& canvasPixels,
                     const ax::Vec2& targetPixels, bool flipY, ax::SceneRenderState& result)
{
    if (!std::isfinite(canvasPixels.width) || !std::isfinite(canvasPixels.height) ||
        !std::isfinite(targetPixels.width) || !std::isfinite(targetPixels.height) ||
        canvasPixels.width <= 0 || canvasPixels.height <= 0 || targetPixels.width <= 0 || targetPixels.height <= 0)
        return false;
    auto inverseView = original.getViewMatrix();
    if (!inverseView.inverse())
        return false;

    // CCRenderTexture::begin (v3) right-multiplies the Director projection
    // P*V by ortho(-1/widthRatio,1/widthRatio,-1/heightRatio,1/heightRatio,-1,1).
    // That matrix is diag(widthRatio,heightRatio,-1,1). Use an explicit scale:
    // modern createOrthographicOffCenter also converts NDC Z for the RHI.
    ax::Mat4 scale;
    ax::Mat4::createScale(canvasPixels.width / targetPixels.width, canvasPixels.height / targetPixels.height,
                         -1.0f, &scale);
    auto viewProjection = original.getViewProjectionMatrix() * scale;
    if (flipY)
    {
        ax::Mat4 flip;
        ax::Mat4::createScale(1.0f, -1.0f, 1.0f, &flip);
        viewProjection = flip * viewProjection;
    }
    result                     = original;
    result.view.projection     = viewProjection * inverseView;
    result.view.viewProjection = viewProjection;
    result.viewOverridden      = true;
    return true;
}
}  // namespace legacyRenderTexture
namespace
{
using namespace manual;
struct RenderTextureContext
{
    std::shared_ptr<RuntimeState> vm;
    uint64_t generation{};
    ax::RefPtr<ax::CustomEventListener> beforeDraw;
    bool closing = false;
    bool live() const
    {
        return !closing && vm && vm->alive && vm->generation == generation && vm->onOwnerThread();
    }
};
struct ActivePass
{
    std::shared_ptr<RenderTextureContext> context;
    ax::RefPtr<ax::Node> owner;
    ax::RefPtr<ax::RenderTexturePass> pass;
    ax::RefPtr<ax::RenderTexture> texture;
    ax::RefPtr<ax::Camera> camera;
    ax::SceneRenderState state;
    ax::SceneRenderState original;
};
// Renderer groups and the JSAPI are both owner-thread operations. Keeping the
// context and generation in each entry prevents a restarted VM from adopting
// an older pass even if SpiderMonkey reuses its JSContext address.
thread_local std::unordered_map<JSContext*, std::shared_ptr<RenderTextureContext>> contexts;
thread_local std::vector<ActivePass> activePasses;
ax::SceneRenderState originalForPass(const ax::SceneRenderState& original)
{
    // v3 begin() resets the Director projection before applying this target's
    // ratio. A nested autoDraw must not compound the outer target's scale.
    // Preserve unrelated caller-provided view overrides.
    if (!activePasses.empty())
    {
        const auto& outer = activePasses.back();
        if (original.viewOverridden && original.getCamera() == outer.state.getCamera() &&
            original.getRenderer() == outer.state.getRenderer() &&
            std::memcmp(original.getViewProjectionMatrix().m, outer.state.getViewProjectionMatrix().m,
                        sizeof(ax::Mat4::identity.m)) == 0)
            return outer.original;
    }
    return original;
}
ax::CallbackCommand* keepTextureUntilExecution(ax::Renderer* renderer, const ax::RefPtr<ax::RenderTexture>& texture)
{
    if (!texture)
        return nullptr;
    auto* hold = renderer->nextCallbackCommand();
    hold->init(ax::RT_PASS_END_ORDER + 1.0f);
    hold->func = [resource = texture]() mutable { resource.reset(); };
    renderer->addCommand(hold);
    return hold;
}
void finishTopPass()
{
    auto scope = std::move(activePasses.back());
    activePasses.pop_back();
    // A target can lose its scene/JS owners before the queued GPU commands
    // execute. The queue must own its resource through that command lifetime.
    keepTextureUntilExecution(scope.state.getRenderer(), scope.texture);
    if (scope.pass)
        scope.pass->end();
}
void finishPasses(const std::shared_ptr<RenderTextureContext>& context, size_t depth = 0)
{
    while (activePasses.size() > depth && activePasses.back().context == context)
        finishTopPass();
}

ax::ClearFlag clearFlags(unsigned mask)
{
    auto flags = ax::ClearFlag::NONE;
    if (mask & 0x4000)
        flags |= ax::ClearFlag::COLOR;
    if (mask & 0x0100)
        flags |= ax::ClearFlag::DEPTH;
    if (mask & 0x0400)
        flags |= ax::ClearFlag::STENCIL;
    return flags;
}
bool pixelFormat(int legacy, ax::rhi::PixelFormat& value)
{
    using F = ax::rhi::PixelFormat;
    switch (legacy)
    {
    case 0:
    case 2:
        value = F::RGBA8;
        return true;
    case 1:
        value = F::BGRA8;
        return true;
    case 3:
        value = F::RGB8;
        return true;
    case 4:
        value = F::RGB565;
        return true;
    case 8:
        value = F::RGBA4;
        return true;
    case 9:
        value = F::RGB5A1;
        return true;
    default:
        return false;
    }
}
// v3's RenderTexture was a Node with a display sprite and offscreen children.
// Keep that scene graph contract using the current texture resource and pass.
class LegacyRenderTexture final : public ax::Node
{
public:
    bool initialize(JSContext* cx, int width, int height, ax::rhi::PixelFormat format, ax::rhi::PixelFormat depth)
    {
        if (width <= 0 || height <= 0 || !Node::init())
            return false;
        _texture = ax::RenderTexture::createForCanvas(ax::Vec2(width, height), format, depth);
        if (!_texture)
            return false;
        _context = contexts.at(cx);
        _pass   = ax::RefPtr<ax::RenderTexturePass>(ax::RenderTexturePass::obtain(_texture), tlx::adopt_object);
        // The compatibility state handles both the legacy target ratio and
        // OpenGL texture orientation locally. Never mutate a scene Camera.
        _pass->setCameraOverrideEnabled(false);
        _sprite = ax::Sprite::createWithTexture(_texture);
        if (!_sprite)
            return false;
        _sprite->setBlendFunc(ax::BlendFunc::ALPHA_PREMULTIPLIED);
        _sprite->setOpacityModifyRGB(true);
        addChild(_sprite);
        // The modern pass already produces an upright texture on every RHI.
        _pass->begin();
        _pass->clearColor(ax::Color(0, 0, 0, 0));
        keepTextureUntilExecution(ax::Director::getInstance()->getRenderer(), _texture);
        _pass->end();
        return true;
    }
    bool begin(JSContext* cx)
    {
        auto context = _context.lock();
        if (!context || !context->live() || context->vm != runtimeState(cx))
            return false;
        auto* director = ax::Director::getInstance();
        auto* scene = director->getRunningScene();
        auto* camera = scene ? scene->getDefaultCamera() : nullptr;
        if (!camera)
            camera = director->getOffscreenCamera();
        return begin(ax::SceneRenderState(director->getRenderer(), camera), context);
    }
    bool end()
    {
        if (!_pass || !_pass->isActive())
            return true;
        if (activePasses.empty() || activePasses.back().owner.get() != this)
            return false;
        // Hold the texture until the queued pass callbacks have executed even
        // if end(), VM shutdown, or scene replacement releases the last Node.
        finishTopPass();
        return true;
    }
    bool active() const { return _pass && _pass->isActive(); }
    void clear(unsigned flags, const ax::ClearValue& value) { _pass->clear(clearFlags(flags), value); }
    ax::RenderTexture* texture() const { return _texture; }
    ax::Sprite* getSprite() const { return _sprite; }
    void setSprite(ax::Sprite* sprite) { _sprite = sprite; }
    bool getAutoDraw() const { return _autoDraw; }
    void setAutoDraw(bool enabled) { _autoDraw = enabled; }
    unsigned getClearFlags() const { return _clearFlags; }
    void setClearFlags(unsigned flags) { _clearFlags = flags; }
    ax::Color getClearColor() const { return _clear.color; }
    void setClearColor(ax::Color color) { _clear.color = color; }
    float getClearDepthVal() const { return _clear.depth; }
    void setClearDepthVal(float depth) { _clear.depth = depth; }
    unsigned getClearStencilVal() const { return _clear.stencil; }
    void setClearStencilVal(unsigned stencil) { _clear.stencil = stencil; }
    void visit(const ax::SceneRenderState& state, const ax::Mat4& parent, uint32_t parentFlags) override
    {
        if (!_visible || !_sprite)
            return;
        const auto flags = processParentFlags(state, parent, parentFlags);
        _sprite->visit(state, _modelViewTransform, flags);
        if (isVisitableByCamera(state.cameraFlag))
            draw(state, _modelViewTransform, flags);
    }
    void draw(const ax::SceneRenderState& state, const ax::Mat4& transform, uint32_t flags) override
    {
        if (!_autoDraw || !_pass || _pass->isActive())
            return;
        auto context = _context.lock();
        if (context && !context->live())
            context.reset();
        const auto depth = activePasses.size();
        if (!begin(state, context))
            return;
        // A JS draw callback can throw, remove this target, or stop its VM.
        // The guard closes every pass introduced by this traversal in LIFO order.
        struct Finish
        {
            std::shared_ptr<RenderTextureContext> context;
            ax::RefPtr<ax::Node> owner;
            size_t depth;
            ~Finish() { finishPasses(context, depth); }
        } finish{context, ax::RefPtr<ax::Node>(this), depth};
        const auto renderState = activePasses.back().state;
        const auto camera = activePasses.back().camera;
        _pass->clear(clearFlags(_clearFlags), _clear);
        sortAllChildren();
        for (auto* child : _children)
        {
            if (child != _sprite)
                child->visit(renderState, transform, flags);
            if (context && !context->live())
                break;
        }
    }

private:
    bool begin(const ax::SceneRenderState& original, const std::shared_ptr<RenderTextureContext>& context)
    {
        if (!_pass || _pass->isActive() ||
            (!activePasses.empty() && activePasses.back().context != context))
            return false;
        const auto base = originalForPass(original);
        ax::SceneRenderState state;
        if (!legacyRenderTexture::makeRenderState(base, ax::Director::getInstance()->getCanvasSizeInPixels(),
                                                  _texture->getPixelSize(), ax::rhi::GraphicsCore::isOpenGL(), state))
            return false;
        activePasses.push_back({context, ax::RefPtr<ax::Node>(this), _pass, _texture,
                               ax::RefPtr<ax::Camera>(const_cast<ax::Camera*>(state.getCamera())), state, base});
        _pass->begin();
        if (!_pass->isActive())
        {
            activePasses.pop_back();
            return false;
        }
        return true;
    }
    std::weak_ptr<RenderTextureContext> _context;
    ax::RefPtr<ax::RenderTexture> _texture;
    ax::RefPtr<ax::RenderTexturePass> _pass;
    ax::RefPtr<ax::Sprite> _sprite;
    ax::ClearValue _clear;
    unsigned _clearFlags = 0;
    bool _autoDraw       = false;
};
bool nodeVisit(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* node = self<ax::Node>(cx, args);
    if (!node || argc != 0)
        return error(cx, "Node.visit requires a live native Node and no arguments");
    if (activePasses.empty())
        node->visit();
    else
    {
        const auto context = activePasses.back().context;
        if (!context || !context->live() || context->vm != runtimeState(cx))
            return error(cx, "RenderTexture pass belongs to an inactive or different JavaScript VM");
        // JS draw callbacks may open/close passes and reallocate the stack.
        // Copy the value before native traversal and retain its Camera owner.
        const auto state = activePasses.back().state;
        const auto camera = activePasses.back().camera;
        node->visit(state, ax::Mat4::identity, ax::Node::FLAGS_TRANSFORM_DIRTY);
    }
    args.rval().setUndefined();
    return true;
}
bool create(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    int w{}, h{}, color = 2, depth = 0;
    if (argc < 2 || argc > 4 || !argument(cx, args, 0, w) || !argument(cx, args, 1, h) ||
        (argc > 2 && !argument(cx, args, 2, color)) || (argc > 3 && !argument(cx, args, 3, depth)))
        return error(cx, "RenderTexture requires width, height, optional pixel and depth/stencil formats");
    ax::rhi::PixelFormat format, depthFormat = ax::rhi::PixelFormat::NONE;
    if (!pixelFormat(color, format))
        return error(cx, "Unsupported RenderTexture color format");
    if (depth == 0x88f0)
        depthFormat = ax::rhi::PixelFormat::D24S8;
    // The portable RHI exposes packed depth/stencil; a 16-bit depth request
    // receives the higher precision attachment while retaining depth behavior.
    else if (depth == 0x81a5)
        depthFormat = ax::rhi::PixelFormat::D24S8;
    else if (depth != 0)
        return error(cx, "Unsupported RenderTexture depth/stencil format");
    auto node = std::make_unique<LegacyRenderTexture>();
    if (!node->initialize(cx, w, h, format, depthFormat))
        return error(cx, "RenderTexture initialization failed");
    JS::RootedObject prototype(cx, getPrototype<LegacyRenderTexture>(cx));
    JS::RootedObject wrapper(
        cx, JS_NewObject(cx, detail::getClassInfo(cx, typeid(LegacyRenderTexture)), prototype, JS::NullPtr()));
    if (!wrapper || !bindNative(cx, wrapper, node.get(), NativeOwnership::Constructor))
        return false;
    node.release();
    args.rval().setObject(*wrapper);
    return true;
}
template <auto F>
bool method(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (invoke<F>(cx, args))
        return true;
    if (!JS_IsExceptionPending(cx))
        JS_ReportError(cx, "Invalid RenderTexture arguments");
    return false;
}
bool begin(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args  = JS::CallArgsFromVp(argc, vp);
    auto* node = self<LegacyRenderTexture>(cx, args);
    if (!node)
        return false;
    if (argc != 0 || !node->begin(cx))
        return error(cx, "RenderTexture pass already active");
    args.rval().setUndefined();
    return true;
}
bool end(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* node = self<LegacyRenderTexture>(cx, args);
    if (!node || argc != 0)
        return error(cx, "RenderTexture.end requires a live target and no arguments");
    if (!node->end())
        return error(cx, "RenderTexture passes must end in the reverse order of begin");
    args.rval().setUndefined();
    return true;
}
template <bool LeaveOpen>
bool clear(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args  = JS::CallArgsFromVp(argc, vp);
    auto* node = self<LegacyRenderTexture>(cx, args);
    std::array<float, 4> rgba{};
    ax::ClearValue value;
    unsigned flags = 0x4000;
    if (!node || argc < 4 || argc > 7)
        return error(cx, "RenderTexture clear requires four color components");
    for (unsigned i = 0; i < 4; ++i)
        if (!argument(cx, args, i, rgba[i]))
            return false;
    value.color = ax::Color(rgba[0] / 255, rgba[1] / 255, rgba[2] / 255, rgba[3] / 255);
    if (argc > 4)
    {
        if (!argument(cx, args, 4, value.depth))
            return false;
        flags |= 0x0100;
    }
    if (argc > 5)
    {
        if (!argument(cx, args, 5, value.stencil))
            return false;
        flags |= 0x0400;
    }
    if (argc > 6 && !argument(cx, args, 6, flags))
        return false;
    if (!node->begin(cx))
        return error(cx, "RenderTexture pass already active");
    node->clear(flags, value);
    if constexpr (!LeaveOpen)
        node->end();
    args.rval().setUndefined();
    return true;
}
template <bool Stencil>
bool clearComponent(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args  = JS::CallArgsFromVp(argc, vp);
    auto* node = self<LegacyRenderTexture>(cx, args);
    ax::ClearValue value;
    if (!node)
        return false;
    if constexpr (Stencil)
    {
        if (!argument(cx, args, 0, value.stencil))
            return false;
    }
    else if (!argument(cx, args, 0, value.depth))
        return false;
    if (!node->begin(cx))
        return error(cx, "RenderTexture pass already active");
    node->clear(Stencil ? 0x0400 : 0x0100, value);
    node->end();
    args.rval().setUndefined();
    return true;
}
template <bool NonPMA>
bool save(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args  = JS::CallArgsFromVp(argc, vp);
    auto* node = self<LegacyRenderTexture>(cx, args);
    std::string filename;
    int format = 0;
    bool rgba  = true;
    if (!node || !argument(cx, args, 0, filename) || (argc > 1 && !argument(cx, args, 1, format)) ||
        (argc > 2 && !argument(cx, args, 2, rgba)))
        return false;
    if (format != 0 && format != 1)
        return error(cx, "RenderTexture image format must be JPEG or PNG");
    auto imageFormat = format == 0 ? ax::Image::Format::JPG : ax::Image::Format::PNG;
    bool result;
    if constexpr (NonPMA)
        result = argc == 1 ? node->texture()->saveToFileAsNonPMA(filename)
                           : node->texture()->saveToFileAsNonPMA(filename, imageFormat, rgba, nullptr);
    else
        result = argc == 1 ? node->texture()->saveToFile(filename)
                           : node->texture()->saveToFile(filename, imageFormat, rgba);
    args.rval().setBoolean(result);
    return true;
}
JSClass renderTextureClass = [] {
    JSClass result{};
    result.name        = "RenderTexture";
    result.flags       = JSCLASS_HAS_PRIVATE;
    result.addProperty = JS_PropertyStub;
    result.delProperty = JS_DeletePropertyStub;
    result.getProperty = JS_PropertyStub;
    result.setProperty = JS_StrictPropertyStub;
    result.enumerate   = JS_EnumerateStub;
    result.resolve     = JS_ResolveStub;
    result.convert     = JS_ConvertStub;
    result.finalize    = finalizeNative;
    return result;
}();
#define RT_METHOD(name) JS_FN(#name, (method<&LegacyRenderTexture::name>), 0, JSPROP_ENUMERATE)
const JSFunctionSpec methods[] = {RT_METHOD(getSprite),
                                  RT_METHOD(setSprite),
                                  RT_METHOD(getAutoDraw),
                                  JS_FN("isAutoDraw", (method<&LegacyRenderTexture::getAutoDraw>), 0, JSPROP_ENUMERATE),
                                  RT_METHOD(setAutoDraw),
                                  RT_METHOD(getClearFlags),
                                  RT_METHOD(setClearFlags),
                                  RT_METHOD(getClearColor),
                                  RT_METHOD(setClearColor),
                                  RT_METHOD(getClearDepthVal),
                                  RT_METHOD(setClearDepthVal),
                                  RT_METHOD(getClearStencilVal),
                                  RT_METHOD(setClearStencilVal),
                                  JS_FN("begin", begin, 0, 0),
                                  JS_FN("end", end, 0, 0),
                                  JS_FN("beginWithClear", clear<true>, 4, 0),
                                  JS_FN("clear", clear<false>, 4, 0),
                                  JS_FN("clearDepth", clearComponent<false>, 1, 0),
                                  JS_FN("clearStencil", clearComponent<true>, 1, 0),
                                  JS_FN("saveToFile", save<false>, 1, 0),
                                  JS_FN("saveToFileAsNonPMA", save<true>, 1, 0),
                                  JS_FS_END};
#undef RT_METHOD
const JSFunctionSpec statics[] = {JS_FN("create", create, 2, 0), JS_FS_END};
}  // namespace
bool registerLegacyRenderTexture(JSContext* cx, JS::HandleObject global)
{
    auto context = std::make_shared<RenderTextureContext>();
    context->vm = runtimeState(cx);
    context->generation = context->vm->generation;
    contexts[cx] = context;
    auto* listener = ax::CustomEventListener::create(ax::Director::EVENT_BEFORE_DRAW, [context](ax::CustomEvent*) {
        // Manual begin/end submits commands during the update phase. If script
        // exits by exception without end(), close its groups before screen
        // traversal so that screen draws never enter the abandoned target.
        finishPasses(context);
    });
    context->beforeDraw = listener;
    ax::Director::getInstance()->getEventDispatcher()->addEventListenerWithFixedPriority(
        listener, std::numeric_limits<int>::max());
    engine(cx)->addCleanupCallback([cx, context] {
        context->closing = true;
        ax::Director::getInstance()->getEventDispatcher()->removeEventListener(context->beforeDraw.get());
        context->beforeDraw.reset();
        finishPasses(context);
        if (auto it = contexts.find(cx); it != contexts.end() && it->second == context)
            contexts.erase(it);
    });
    JS::RootedObject cc(cx, namespaceObject(cx, global, "cc")), parent(cx, getPrototype<ax::Node>(cx));
    JS::RootedValue resource(cx);
    if (!cc || !JS_GetProperty(cx, cc, "RenderTexture", &resource) ||
        !JS_DefineProperty(cx, cc, "RenderTextureResource", resource, JSPROP_ENUMERATE))
        return false;
    JS::RootedObject prototype(
        cx, JS_InitClass(cx, cc, parent, &renderTextureClass, create, 2, nullptr, methods, nullptr, statics));
    return prototype && registerType<LegacyRenderTexture, ax::Node>(cx, prototype, &renderTextureClass) &&
           function(cx, parent, "visit", nodeVisit);
}
}  // namespace axjs
