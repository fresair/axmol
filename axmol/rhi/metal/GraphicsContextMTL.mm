/****************************************************************************
 Copyright (c) 2018-2019 Xiamen Yaji Software Co., Ltd.
 Copyright (c) 2019-present Simdsoft Limited.
 Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

 https://axmol.dev/

 SPDX-License-Identifier: MIT
 ****************************************************************************/

#include "axmol/rhi/metal/GraphicsContextMTL.h"
#include "axmol/rhi/metal/BufferMTL.h"
#include "axmol/rhi/metal/GraphicsDeviceMTL.h"
#include "axmol/rhi/metal/GraphicsPipelineMTL.h"
#include "axmol/rhi/metal/ComputePipelineMTL.h"
#include "axmol/rhi/metal/TextureMTL.h"
#include "axmol/rhi/metal/UtilsMTL.h"
#include "axmol/rhi/metal/BufferManager.h"
#include "axmol/rhi/metal/DepthStencilStateMTL.h"
#include "axmol/rhi/metal/RenderTargetMTL.h"
#include "axmol/rhi/metal/ProgramMTL.h"
#include "axmol/rhi/SamplerRegistry.h"
#include "axmol/platform/Application.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void*);

#if AX_TARGET_PLATFORM == AX_PLATFORM_MAC
#    import <AppKit/AppKit.h>
#else
#    import <UIKit/UIKit.h>
#endif

namespace ax::rhi::mtl
{

namespace
{

// match with axmol 3.1 shader
static constexpr int VS_UBO_BINDING_INDEX = 0;
static constexpr int FS_UBO_BINDING_INDEX = 1;

#define byte(n) ((n) * 8)
#define bit(n)  (n)
static uint8_t getBitsPerElementMTL(MTLPixelFormat pixleFormat)
{
    switch (pixleFormat)
    {
    case MTLPixelFormatDepth32Float_Stencil8:
        return byte(8);
    case MTLPixelFormatBGRA8Unorm:
    case MTLPixelFormatRGBA8Unorm:
    case MTLPixelFormatDepth32Float:
        return byte(4);
#if (AX_TARGET_PLATFORM == AX_PLATFORM_MAC)
    case MTLPixelFormatDepth24Unorm_Stencil8:
        return byte(4);
#else
    case MTLPixelFormatABGR4Unorm:
    case MTLPixelFormatBGR5A1Unorm:
    case MTLPixelFormatB5G6R5Unorm:
    case MTLPixelFormatA1BGR5Unorm:
        return byte(2);
#endif
    case MTLPixelFormatA8Unorm:
    case MTLPixelFormatR8Unorm:
        return byte(1);
    default:
        assert(false);
        break;
    }
    return 0;
}

static MTLWinding toMTLWinding(Winding winding)
{
    if (Winding::CLOCK_WISE == winding)
        return MTLWindingClockwise;
    else
        return MTLWindingCounterClockwise;
}

static MTLPrimitiveType toMTLPrimitive(PrimitiveType primitiveType)
{
    MTLPrimitiveType ret = MTLPrimitiveTypeTriangle;
    switch (primitiveType)
    {
    case PrimitiveType::POINT:
        ret = MTLPrimitiveTypePoint;
        break;
    case PrimitiveType::LINE:
        ret = MTLPrimitiveTypeLine;
        break;
    case PrimitiveType::LINE_STRIP:
        ret = MTLPrimitiveTypeLineStrip;
        break;
    case PrimitiveType::TRIANGLE:
        ret = MTLPrimitiveTypeTriangle;
        break;
    case PrimitiveType::TRIANGLE_STRIP:
        ret = MTLPrimitiveTypeTriangleStrip;
        break;
    default:
        break;
    }
    return ret;
}

static MTLIndexType toMTLIndexType(IndexFormat indexFormat)
{
    if (IndexFormat::U_SHORT == indexFormat)
        return MTLIndexTypeUInt16;
    else
        return MTLIndexTypeUInt32;
}

static MTLCullMode toMTLCullMode(CullMode mode)
{
    switch (mode)
    {
    case CullMode::NONE:
        return MTLCullModeNone;
    case CullMode::FRONT:
        return MTLCullModeFront;
    case CullMode::BACK:
        return MTLCullModeBack;
    }
}

}  // namespace

GraphicsContextImpl::GraphicsContextImpl(GraphicsDeviceImpl* driver, SurfaceHandle surface)
{
    _frameBoundarySemaphore = dispatch_semaphore_create(MAX_FRAMES_IN_FLIGHT);
    auto mtlDevice          = driver->getMTLDevice();
    _mtlCmdQueue            = driver->getMTLCmdQueue();
    auto& contextAttrs      = Application::getContextAttrs();
    auto screenPF           = UtilsMTL::toMTLPixelFormat(UtilsMTL::getDefaultColorAttachmentPixelFormat());
#if AX_TARGET_PLATFORM == AX_PLATFORM_MAC
    CGSize fbSize;
    NSView* contentView = (__bridge NSView*)surface.ptr;
    @autoreleasepool
    {
        const NSRect contentRect = [contentView frame];
        const NSRect fbRect      = [contentView convertRectToBacking:contentRect];

        fbSize.width  = (int)fbRect.size.width;
        fbSize.height = (int)fbRect.size.height;
    }  // autoreleasepool
    [contentView setWantsLayer:YES];
    _mtlLayer = [CAMetalLayer layer];
    [_mtlLayer setDevice:mtlDevice];
    [_mtlLayer setPixelFormat:screenPF];
    [_mtlLayer setFramebufferOnly:YES];
    [_mtlLayer setDrawableSize:fbSize];
    _mtlLayer.displaySyncEnabled = contextAttrs.vsync;
    [contentView setLayer:_mtlLayer];
#else
    UIView* view              = (__bridge UIView*)surface.ptr;
    _mtlLayer                 = (CAMetalLayer*)[view layer];
    _mtlLayer.device          = mtlDevice;
    _mtlLayer.pixelFormat     = screenPF;
    _mtlLayer.framebufferOnly = YES;

    const auto backingScaleFactor = [view contentScaleFactor];
    auto bounds                   = [view bounds];
    CGSize fbSize = CGSizeMake(bounds.size.width * backingScaleFactor, bounds.size.height * backingScaleFactor);
    _mtlLayer.drawableSize = fbSize;
#endif

    _screenRT = new RenderTargetImpl(this);
    _screenRT->rebuildSwapchainAttachments();
}

GraphicsContextImpl::~GraphicsContextImpl()
{
    // Wait for all frames to finish by submitting and waiting on a dummy command buffer.
    flush();
    id<MTLCommandBuffer> oneOffBuffer = [_mtlCmdQueue commandBuffer];
    [oneOffBuffer commit];
    [oneOffBuffer waitUntilCompleted];

    AX_SAFE_RELEASE_NULL(_screenRT);
    AX_SAFE_RELEASE_NULL(_graphicsPipeline);

    if (_autoreleasePool)
    {
        objc_autoreleasePoolPop(_autoreleasePool);
        _autoreleasePool = nullptr;
    }

    dispatch_semaphore_signal(_frameBoundarySemaphore);
}

bool GraphicsContextImpl::updateSurface(SurfaceHandle /*surface*/, uint32_t width, uint32_t height)
{
    invalidateLegacyReadback();
    _defaultColorTexture = nil;
    _defaultColorWritten = false;
    [_mtlLayer setDrawableSize:CGSizeMake(width, height)];
    _screenRT->rebuildSwapchainAttachments();
    return true;
}

void GraphicsContextImpl::setDepthStencilState(DepthStencilState* depthStencilState)
{
    _depthStencilState = static_cast<DepthStencilStateImpl*>(depthStencilState);
}

void GraphicsContextImpl::setGraphicsPipeline(GraphicsPipeline* graphicsPipeline)
{
    Object::assign(_graphicsPipeline, static_cast<GraphicsPipelineImpl*>(graphicsPipeline));
}

id<CAMetalDrawable> GraphicsContextImpl::acquireDrawable()
{
    if (_currentDrawable)
        return _currentDrawable;
    return (_currentDrawable = [_mtlLayer nextDrawable]);
}

void GraphicsContextImpl::releaseDrawable()
{
    _currentDrawable = nil;
    restoreFrameBufferOnly();
}

bool GraphicsContextImpl::beginFrame()
{
    _autoreleasePool = objc_autoreleasePoolPush();
    dispatch_semaphore_wait(_frameBoundarySemaphore, DISPATCH_TIME_FOREVER);

    _currentCmdBuffer = [_mtlCmdQueue commandBuffer];
    ++_readbackFrame;
    _defaultColorTexture = nil;
    _defaultColorWritten = false;
    validateLegacyReadbackSurface();
    // [_currentCmdBuffer enqueue];
    // commit will enqueue automatically

    BufferManager::beginFrame();
    return true;
}

void GraphicsContextImpl::beginExplicitRenderPass(RenderTarget* renderTarget, const RenderPassDesc& renderPassDesc)
{
    // An explicit clear is a new pass, including when its target, clear color,
    // and descriptor equal the preceding pass. Never consume it as a resume.
    endEncoding();
    _renderPassInterrupted = false;
    beginRenderPass(renderTarget, renderPassDesc);
}

void GraphicsContextImpl::beginRenderPass(RenderTarget* renderTarget, const RenderPassDesc& renderPassDesc)
{
    if (_mtlRenderEncoder != nil && _currentRenderPassDesc == renderPassDesc && _currentRT == renderTarget &&
        !renderTarget->isDirty())
    {
        return;
    }

    const bool resumeInterruptedPass = _renderPassInterrupted && _currentRT == renderTarget;
    _renderPassInterrupted           = false;
    _currentRT                       = renderTarget;
    _currentRenderPassDesc           = renderPassDesc;

    if (_mtlRenderEncoder != nil)
    {
        [_mtlRenderEncoder endEncoding];
        _mtlRenderEncoder = nil;
    }

    MTLRenderPassDescriptor* mtlDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    auto rtMTL                       = static_cast<RenderTargetImpl*>(_currentRT);
    auto nativeRenderPassDesc        = renderPassDesc;
    if (resumeInterruptedPass)
    {
        nativeRenderPassDesc.flags.clear        = TargetBufferFlags::NONE;
        nativeRenderPassDesc.flags.discardStart = TargetBufferFlags::NONE;
    }
    rtMTL->applyRenderPassAttachments(nativeRenderPassDesc, mtlDesc);

    if (_legacyReadbackUsers && renderTarget->isDefaultRenderTarget())
    {
        // COLOR0 must survive the end of this pass for synchronous reads and
        // the independent history copy. Resolve first when an MSAA attachment
        // is in use; never preserve the multisample texture as screen pixels.
        auto color = mtlDesc.colorAttachments[0];
        color.storeAction = color.resolveTexture ? MTLStoreActionStoreAndMultisampleResolve : MTLStoreActionStore;
    }

    _renderTargetWidth  = (unsigned int)mtlDesc.colorAttachments[0].texture.width;
    _renderTargetHeight = (unsigned int)mtlDesc.colorAttachments[0].texture.height;
    _mtlRenderEncoder   = [_currentCmdBuffer renderCommandEncoderWithDescriptor:mtlDesc];
    if (_mtlRenderEncoder && renderTarget->isDefaultRenderTarget())
    {
        auto color = mtlDesc.colorAttachments[0];
        _defaultColorTexture = color.resolveTexture ?: color.texture;
        _defaultColorWritten = _defaultColorTexture != nil;
        _defaultColorNeedsResolve = color.resolveTexture != nil;
    }
    //    [_mtlRenderEncoder setFrontFacingWinding:MTLWindingCounterClockwise];
}

void GraphicsContextImpl::updateDepthStencilState(const DepthStencilDesc& desc)
{
    _depthStencilState->update(desc);
}

void GraphicsContextImpl::updatePipelineState(const RenderTarget* rt,
                                              const PipelineDesc& desc,
                                              PrimitiveType primitiveType)
{
    _primitiveType = toMTLPrimitive(primitiveType);
    GraphicsContext::updatePipelineState(rt, desc, primitiveType);
    _graphicsPipeline->update(rt, desc);
    [_mtlRenderEncoder setRenderPipelineState:_graphicsPipeline->getMTLRenderPipelineState()];
}

void GraphicsContextImpl::setViewport(int x, int y, unsigned int w, unsigned int h)
{
    MTLViewport viewport;
    viewport.originX = x;
    viewport.originY = (int)(_renderTargetHeight - y - h);
    viewport.width   = w;
    viewport.height  = h;
    viewport.znear   = 0;
    viewport.zfar    = 1;
    [_mtlRenderEncoder setViewport:viewport];
}

void GraphicsContextImpl::setCullMode(CullMode mode)
{
    [_mtlRenderEncoder setCullMode:toMTLCullMode(mode)];
}

void GraphicsContextImpl::setWinding(Winding winding)
{
    [_mtlRenderEncoder setFrontFacingWinding:toMTLWinding(winding)];
}

void GraphicsContextImpl::setVertexBuffer(Buffer* buffer)
{
    // Vertex buffer is bound in index DEFAULT_ATTRIBS_BINDING_INDEX.
    [_mtlRenderEncoder setVertexBuffer:static_cast<BufferImpl*>(buffer)->getMTLBuffer()
                                offset:0
                               atIndex:GraphicsDeviceImpl::DEFAULT_ATTRIBS_BINDING_INDEX];
}

void GraphicsContextImpl::setInstanceBuffer(Buffer* buffer)
{
    // Vertex instancing transform buffer is bound in index VBO_INSTANCING_BINDING_INDEX.
    // TODO: sync device binding macros to AXSLCC
    if (buffer)
    {
        [_mtlRenderEncoder setVertexBuffer:static_cast<BufferImpl*>(buffer)->getMTLBuffer()
                                    offset:0
                                   atIndex:GraphicsDeviceImpl::VBO_INSTANCING_BINDING_INDEX];
    }
    else
    {
        [_mtlRenderEncoder setVertexBuffer:nil offset:0 atIndex:GraphicsDeviceImpl::VBO_INSTANCING_BINDING_INDEX];
    }
}

void GraphicsContextImpl::setIndexBuffer(Buffer* buffer)
{
    assert(buffer != nullptr);
    if (!buffer)
        return;

    _mtlIndexBuffer = static_cast<BufferImpl*>(buffer)->getMTLBuffer();
}

void GraphicsContextImpl::drawArrays(size_t start, size_t count, bool wireframe /* unused */)
{
    prepareDrawing();
    [_mtlRenderEncoder drawPrimitives:_primitiveType vertexStart:start vertexCount:count];
}

void GraphicsContextImpl::drawArraysInstanced(size_t start,
                                              size_t count,
                                              int instanceCount,
                                              bool wireframe /* unused */)
{
    prepareDrawing();
    [_mtlRenderEncoder drawPrimitives:_primitiveType vertexStart:start vertexCount:count instanceCount:instanceCount];
}

void GraphicsContextImpl::drawElements(IndexFormat indexType, size_t count, size_t offset, bool /* wireframe */)
{
    prepareDrawing();
    [_mtlRenderEncoder drawIndexedPrimitives:_primitiveType
                                  indexCount:count
                                   indexType:toMTLIndexType(indexType)
                                 indexBuffer:_mtlIndexBuffer
                           indexBufferOffset:offset];
}

void GraphicsContextImpl::drawElementsInstanced(IndexFormat indexType,
                                                size_t count,
                                                size_t offset,
                                                int instanceCount,
                                                bool /* wireframe */)
{
    prepareDrawing();
    [_mtlRenderEncoder drawIndexedPrimitives:_primitiveType
                                  indexCount:count
                                   indexType:toMTLIndexType(indexType)
                                 indexBuffer:_mtlIndexBuffer
                           indexBufferOffset:offset
                               instanceCount:instanceCount];
}

void GraphicsContextImpl::endRenderPass()
{
    afterDraw();
}

bool GraphicsContextImpl::dispatch(const ComputeDispatchDesc& desc)
{
    if (!desc.programState || !desc.pipeline)
        return false;

    auto* pipelineProgram = desc.pipeline->getProgram();
    if (!pipelineProgram || pipelineProgram != desc.programState->getProgram())
    {
        AXLOGE("ComputePipeline and ProgramState program mismatch");
        return false;
    }

    auto program = static_cast<ProgramImpl*>(pipelineProgram);
    if (!program || !program->getCSModule())
        return false;

    auto* computePipeline                     = static_cast<ComputePipelineImpl*>(desc.pipeline);
    id<MTLComputePipelineState> pipelineState = computePipeline->getMTLComputePipelineState();
    if (pipelineState == nil)
        return false;

    // Compute runs outside render passes. Preserve the current attachments and
    // let the next normal beginRenderPass() resume them with load actions.
    if (_mtlRenderEncoder)
    {
        auto rtMTL = static_cast<RenderTargetImpl*>(_currentRT);
        for (size_t i = 0; i < rtMTL->getNativeColorFormats().size(); ++i)
            [_mtlRenderEncoder setColorStoreAction:interruptedColorStoreAction(rtMTL, i) atIndex:i];
        if (rtMTL->getDepthStencilAttachment())
        {
            [_mtlRenderEncoder setDepthStoreAction:MTLStoreActionStore];
            [_mtlRenderEncoder setStencilStoreAction:MTLStoreActionStore];
        }
        [_mtlRenderEncoder endEncoding];
        _mtlRenderEncoder      = nil;
        _renderPassInterrupted = true;
    }

    id<MTLComputeCommandEncoder> computeEncoder = [_currentCmdBuffer computeCommandEncoder];
    [computeEncoder setComputePipelineState:pipelineState];

    _programState = desc.programState;

    auto& callbackUniforms = desc.programState->getCallbackUniforms();
    for (auto& cb : callbackUniforms)
        cb.second(_programState, cb.first);

    auto& cpuBuffer = desc.programState->getUniformBuffer();
    if (!cpuBuffer.empty())
    {
        for (auto& uboInfo : desc.programState->getActiveUniformBlockInfos())
        {
            [computeEncoder setBytes:cpuBuffer.data() + uboInfo.cpuOffset
                              length:uboInfo.sizeBytes
                             atIndex:uboInfo.binding];
        }
    }

    // Textures (separate MTL namespace, index = unified slot).
    for (const auto& [bindingIndex, bindingSet] : desc.programState->getTextureBindingSets())
    {
        for (size_t k = 0; k < bindingSet.texs.size(); ++k)
        {
            auto textureImpl = static_cast<TextureImpl*>(bindingSet.texs[k]);
            [computeEncoder setTexture:textureImpl->internalHandle() atIndex:bindingIndex + k];
        }
    }

    // Samplers (separate MTL namespace).
    auto samplerRegistry = SamplerRegistry::getInstance();
    for (const auto& samplerInfo : program->getActiveSamplerInfos())
    {
        if (!samplerInfo.samplerId || samplerInfo.count == 0)
            continue;

        auto samplerId = samplerInfo.samplerId;
        if (samplerInfo.presetIndex < 0)
        {
            if (auto overrideId = _programState->getSamplerOverride(samplerInfo.binding))
                samplerId = overrideId;
        }

        auto sampler      = samplerRegistry->getSampler(samplerId);
        auto samplerState = (__bridge id<MTLSamplerState>)sampler.ptr;
        if (samplerState == nil)
            continue;

        for (uint16_t i = 0; i < samplerInfo.count; ++i)
            [computeEncoder setSamplerState:samplerState atIndex:samplerInfo.binding + i];
    }

    // Storage buffers ([[buffer(slot)]]).
    for (const auto& [binding, bindingSet] : desc.programState->getStorageBufferBindingSets())
    {
        if (!bindingSet.buffer)
            continue;
        auto bufferImpl         = static_cast<BufferImpl*>(bindingSet.buffer);
        id<MTLBuffer> mtlBuffer = bufferImpl->getMTLBuffer();
        if (mtlBuffer == nil)
            continue;
        [computeEncoder setBuffer:mtlBuffer offset:0 atIndex:binding];
    }

    const auto& localSize       = desc.programState->getProgram()->getComputeLocalSize();
    MTLSize threadgroupsPerGrid = MTLSizeMake(desc.groupCountX, desc.groupCountY, desc.groupCountZ);
    MTLSize threadsPerThreadgroup =
        MTLSizeMake(std::max(localSize[0], 1), std::max(localSize[1], 1), std::max(localSize[2], 1));
    [computeEncoder dispatchThreadgroups:threadgroupsPerGrid threadsPerThreadgroup:threadsPerThreadgroup];
    [computeEncoder endEncoding];

    _programState = nullptr;
    return true;
}

void GraphicsContextImpl::readPixels(RenderTarget* rt, std::function<void(const PixelBufferDesc&)> callback)
{
    auto rtMTL = static_cast<RenderTargetImpl*>(rt);

    // we only read form color attachment 0
    // if it's nullptr, will regard as screen to perform capture
    auto texture = rtMTL->_color[0].texture;
    AX_SAFE_RETAIN(texture);
    _captureCallbacks.emplace_back(texture, std::move(callback));
}

void GraphicsContextImpl::acquireLegacyDefaultFramebufferReadback()
{
    if (_legacyReadbackUsers++ == 0)
    {
        _legacyPreviousFramebufferOnly = _restoreFrameBufferOnlyAfterDrawable || _mtlLayer.framebufferOnly;
        _restoreFrameBufferOnlyAfterDrawable = false;
        _mtlLayer.framebufferOnly = NO;
        invalidateLegacyReadback();
        traceLegacyReadback("enable", "none", _currentDrawable.texture);
    }
}

void GraphicsContextImpl::releaseLegacyDefaultFramebufferReadback()
{
    if (_legacyReadbackUsers && --_legacyReadbackUsers == 0)
    {
        invalidateLegacyReadback();
        _mtlLayer.framebufferOnly = _legacyPreviousFramebufferOnly;
        traceLegacyReadback("disable", "none", nil);
    }
}

void GraphicsContextImpl::invalidateLegacyReadback()
{
    _legacyColorBacking = nil;
    _legacyColorValid = false;
    _legacyCopyFailed.reset();
    ++_legacyColorGeneration;
}

void GraphicsContextImpl::validateLegacyReadbackSurface()
{
    if (_legacyColorBacking &&
        (_legacyColorBacking.width != _mtlLayer.drawableSize.width ||
         _legacyColorBacking.height != _mtlLayer.drawableSize.height ||
         _legacyColorBacking.pixelFormat != _mtlLayer.pixelFormat))
        invalidateLegacyReadback();
}

id<MTLTexture> GraphicsContextImpl::defaultReadbackSource()
{
    validateLegacyReadbackSurface();
    if (_defaultColorWritten)
    {
        if (_defaultColorTexture && ![_defaultColorTexture isFramebufferOnly] &&
            _defaultColorTexture.width == _mtlLayer.drawableSize.width &&
            _defaultColorTexture.height == _mtlLayer.drawableSize.height &&
            _defaultColorTexture.pixelFormat == _mtlLayer.pixelFormat)
        {
            traceLegacyReadback("read", "current", _defaultColorTexture);
            return _defaultColorTexture;
        }
        // A late opt-in must not silently substitute the previous frame after
        // this frame has rendered to a framebuffer-only drawable.
        traceLegacyReadback("read-unavailable", "current", _defaultColorTexture);
        return nil;
    }
    if (_legacyReadbackUsers && _legacyColorValid && _legacyColorBacking &&
        _legacyCopyFailed && !_legacyCopyFailed->load())
    {
        traceLegacyReadback("read", "backing", _legacyColorBacking);
        return _legacyColorBacking;
    }
    traceLegacyReadback("read-unavailable", "none", nil);
    return nil;
}

bool GraphicsContextImpl::readPixelsSync(RenderTarget* rt, PixelBufferDesc& result)
{
    if (!_legacyReadbackUsers || !rt || !rt->isDefaultRenderTarget())
        return GraphicsContext::readPixelsSync(rt, result);

    result._data.clear();
    result._width = result._height = 0;

    // Do not ask RenderTarget::getColorAttachment here: that acquires a fresh,
    // unrendered drawable when Scheduler runs before this frame's first pass.
    auto source = defaultReadbackSource();
    if (!source)
        return false;
    auto copyFailed = _legacyCopyFailed;
    const bool backing = source == _legacyColorBacking;
    auto submitted = _currentCmdBuffer;
    submitCurrentFrameCommands(true);
    if (submitted && submitted.status == MTLCommandBufferStatusError)
        return false;
    if (backing && copyFailed && copyFailed->load())
        return false;
    readPixels(source, 0, 0, source.width, source.height, result);
    return static_cast<bool>(result);
}

void GraphicsContextImpl::preserveLegacyDefaultColor()
{
    if (!_legacyReadbackUsers || !_defaultColorWritten || !_currentCmdBuffer)
        return;
    auto source = _defaultColorTexture;
    if (!source || [source isFramebufferOnly] || source.sampleCount != 1 ||
        source.width != _mtlLayer.drawableSize.width || source.height != _mtlLayer.drawableSize.height ||
        source.pixelFormat != _mtlLayer.pixelFormat)
    {
        invalidateLegacyReadback();
        traceLegacyReadback("copy-unavailable", "current", source);
        return;
    }
    validateLegacyReadbackSurface();
    if (!_legacyColorBacking || _legacyColorBacking.width != source.width ||
        _legacyColorBacking.height != source.height || _legacyColorBacking.pixelFormat != source.pixelFormat)
    {
        invalidateLegacyReadback();
        auto desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:source.pixelFormat
                                                                    width:source.width height:source.height mipmapped:NO];
        desc.storageMode = MTLStorageModePrivate;
        desc.usage = MTLTextureUsageShaderRead;
        _legacyColorBacking = [source.device newTextureWithDescriptor:desc];
    }
    if (!_legacyColorBacking)
        return;
    auto encoder = [_currentCmdBuffer blitCommandEncoder];
    if (!encoder)
    {
        invalidateLegacyReadback();
        return;
    }
    [encoder copyFromTexture:source sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(source.width, source.height, 1) toTexture:_legacyColorBacking
           destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
    [encoder endEncoding];
    // The completion owns only this small token, never this context or a
    // presented drawable. All resource lifetime is owned by the command buffer.
    auto failed = std::make_shared<std::atomic<bool>>(false);
    _legacyCopyFailed = failed;
    [_currentCmdBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
        failed->store(buffer.status == MTLCommandBufferStatusError);
    }];
    _legacyColorFrame = _readbackFrame;
    _legacyColorValid = true;
    traceLegacyReadback("copy", "current-to-backing", source);
}

void GraphicsContextImpl::traceLegacyReadback(const char* event, const char* source, id<MTLTexture> texture) const
{
    static const bool enabled = [] { auto value = std::getenv("AXMOL_JS_METAL_READBACK_TRACE");
                                    return value && std::strcmp(value, "1") == 0; }();
    static std::atomic<unsigned int> count{0};
    if (!enabled)
        return;
    const bool read = std::strncmp(event, "read", 4) == 0;
    if (read)
        _readbackTraceFrame = _readbackFrame;
    // Preserve the small process budget for actual reads, their frame's
    // copy/submit/present, startup, and sparse background snapshots.
    const bool routine = std::strcmp(event, "copy") == 0 || std::strcmp(event, "present") == 0 ||
                         std::strcmp(event, "submit") == 0;
    if (routine && _readbackFrame > 2 && _readbackFrame != _readbackTraceFrame && _readbackFrame % 60 != 0)
        return;
    if (count.fetch_add(1) >= 32)
        return;
    AXLOGI("MetalLegacyReadback event={} source={} frame={} sourceFrame={} generation={} size={}x{} format={} "
           "layerFramebufferOnly={} textureFramebufferOnly={} command={} drawable={} texture={} backing={}",
           event, source, _readbackFrame, texture == _legacyColorBacking ? _legacyColorFrame : _readbackFrame,
           _legacyColorGeneration, texture.width, texture.height, static_cast<unsigned long>(texture.pixelFormat),
           static_cast<bool>(_mtlLayer.framebufferOnly), static_cast<bool>([texture isFramebufferOnly]),
           (__bridge void*)_currentCmdBuffer, (__bridge void*)_currentDrawable,
           (__bridge void*)texture, (__bridge void*)_legacyColorBacking);
}

void GraphicsContextImpl::endFrame()
{
    [_mtlRenderEncoder endEncoding];
    _mtlRenderEncoder      = nil;
    _renderPassInterrupted = false;

    preserveLegacyDefaultColor();

    auto drawable = acquireDrawable();
    traceLegacyReadback("present", "current", _defaultColorTexture);
    [_currentCmdBuffer presentDrawable:drawable];
    [_currentCmdBuffer addCompletedHandler:^(id<MTLCommandBuffer> commandBuffer) {
      // GPU work is complete
      // Signal the semaphore to start the CPU work
      dispatch_semaphore_signal(_frameBoundarySemaphore);
    }];

    flush();

    releaseDrawable();
    _defaultColorTexture = nil;
    _defaultColorWritten = false;
    if (_autoreleasePool)
    {
        objc_autoreleasePoolPop(_autoreleasePool);
        _autoreleasePool = nullptr;
    }
}

MTLStoreAction GraphicsContextImpl::interruptedColorStoreAction(const RenderTarget* target, size_t index) const
{
    // A compute interruption may be the last render encoder before readback
    // or presentation. Store the MSAA samples for a possible Load continuation
    // AND resolve COLOR0 now, so its readback texture is current even when no
    // later render encoder is created. Keep other paths' prior Store policy.
    if (_legacyReadbackUsers && target && target->isDefaultRenderTarget() && index == 0 &&
        _defaultColorNeedsResolve)
        return MTLStoreActionStoreAndMultisampleResolve;
    return MTLStoreActionStore;
}

void GraphicsContextImpl::submitCurrentFrameCommands(bool waitForCompletion)
{
    const bool interrupted = _mtlRenderEncoder != nil || _renderPassInterrupted;
    if (_mtlRenderEncoder && _currentRT)
    {
        auto* target = static_cast<RenderTargetImpl*>(_currentRT);
        for (size_t i = 0; i < target->getNativeColorFormats().size(); ++i)
            [_mtlRenderEncoder setColorStoreAction:interruptedColorStoreAction(target, i) atIndex:i];
        if (target->getDepthStencilAttachment())
        {
            [_mtlRenderEncoder setDepthStoreAction:MTLStoreActionStore];
            [_mtlRenderEncoder setStencilStoreAction:MTLStoreActionStore];
        }
    }
    endEncoding();

    if (_currentCmdBuffer)
    {
        assert(_currentCmdBuffer.status != MTLCommandBufferStatusCommitted);
        traceLegacyReadback("submit", "current", _defaultColorTexture);
        [_currentCmdBuffer commit];
        if (waitForCompletion)
            [_currentCmdBuffer waitUntilCompleted];

        flushCaptureCommands();

        _currentCmdBuffer = nil;
    }

    _currentCmdBuffer      = [_mtlCmdQueue commandBuffer];
    // Keep attachment identity and resume with Load rather than repeating the
    // original clear/discard when a synchronous read splits an active pass.
    _renderPassInterrupted = interrupted;
}

void GraphicsContextImpl::endEncoding()
{
    if (_mtlRenderEncoder)
    {
        [_mtlRenderEncoder endEncoding];
    }
    _mtlRenderEncoder = nil;
}

void GraphicsContextImpl::flush()
{
    if (_currentCmdBuffer)
    {
        assert(_currentCmdBuffer.status != MTLCommandBufferStatusCommitted);
        traceLegacyReadback("submit", "current", _defaultColorTexture);
        [_currentCmdBuffer commit];

        flushCaptureCommands();

        _currentCmdBuffer = nil;
    }
}

void GraphicsContextImpl::flushCaptureCommands()
{
    if (!_captureCallbacks.empty())
    {
        // !!!important, if have capture request, must wait pending commandBuffer finish at this frame,
        // because readPixels require sync operation to get screen pixels properly without data race issue,
        // otherwise, will lead dead-lock
        // !!!Notes, MTL is mutli-threading, all GPU handler is dispatch at GPU threads
        [_currentCmdBuffer waitUntilCompleted];

        PixelBufferDesc screenPixelData;
        id<MTLTexture> drawableTexture = nil;
        for (auto& cb : _captureCallbacks)
        {
            if (cb.first == nil)
            {  // screen capture
                if (!screenPixelData)
                {
                    drawableTexture = _legacyReadbackUsers ? defaultReadbackSource() : _currentDrawable.texture;
                    readPixels(drawableTexture, 0, 0, [drawableTexture width], [drawableTexture height],
                               screenPixelData);
                }
                cb.second(screenPixelData);
            }
            else
            {
                PixelBufferDesc pixelData;
                auto texture = cb.first;
                assert(texture != nullptr);
                traceLegacyReadback("read", "offscreen", static_cast<TextureImpl*>(texture)->internalHandle());
                readPixels(static_cast<TextureImpl*>(texture)->internalHandle(), 0, 0, texture->getWidth(),
                           texture->getHeight(), pixelData);
                AX_SAFE_RELEASE(texture);
                cb.second(pixelData);
            }
        }
        _captureCallbacks.clear();
    }
}

void GraphicsContextImpl::afterDraw()
{
    if (_mtlIndexBuffer)
    {
        _mtlIndexBuffer = nullptr;
    }

    _programState = nullptr;
    _vertexLayout = nullptr;
}

void GraphicsContextImpl::prepareDrawing() const
{
    setUniformBuffer();
    setTexturesAndSamplers();

    auto mtlDepthStencilState = _depthStencilState->getMTLDepthStencilState();
    if (mtlDepthStencilState)
    {
        [_mtlRenderEncoder setDepthStencilState:mtlDepthStencilState];
        [_mtlRenderEncoder setStencilFrontReferenceValue:_stencilReferenceValue
                                      backReferenceValue:_stencilReferenceValue];
    }
}

void GraphicsContextImpl::setTexturesAndSamplers() const
{
    const auto program = _programState->getProgram();
    const bool combinedSamplers = program->getActiveSamplerInfos().empty();

    // Phase 1: bind textures.
    for (const auto& [bindingIndex, bindingSet] : _programState->getTextureBindingSets())
    {
        auto& texs     = bindingSet.texs;
        auto arraySize = texs.size();
        for (auto k = 0; k < arraySize; ++k)
        {
            const auto slot  = bindingIndex + k;
            auto textureImpl = static_cast<TextureImpl*>(texs[k]);
            [_mtlRenderEncoder setFragmentTexture:textureImpl->internalHandle() atIndex:slot];
            // Combined GLSL samplers use the texture slot for both bindings.
            // Keep their dynamic texture parameters independent of any sampler
            // state left on the encoder by a preceding separate-sampler draw.
            if (combinedSamplers)
            {
                const auto samplerState = textureImpl->internalSampler();
                AXASSERT(samplerState != nil, "missing Metal combined texture sampler");
                [_mtlRenderEncoder setFragmentSamplerState:samplerState atIndex:slot];
            }
        }
    }

    // Phase 2: bind samplers resolved by Program creation.
    auto samplerRegistry = SamplerRegistry::getInstance();
    for (const auto& samplerInfo : program->getActiveSamplerInfos())
    {
        if (!samplerInfo.samplerId || samplerInfo.binding < 0 || samplerInfo.count == 0)
            continue;

        auto samplerId = samplerInfo.samplerId;
        if (samplerInfo.presetIndex < 0)
        {
            if (auto overrideId = _programState->getSamplerOverride(samplerInfo.binding))
                samplerId = overrideId;
        }

        const auto sampler      = samplerRegistry->getSampler(samplerId);
        const auto samplerState = (__bridge id<MTLSamplerState>)sampler.ptr;

        if (samplerState == nil)
        {
            AXASSERT(false, "failed to resolve Metal sampler");
            continue;
        }

        for (uint16_t index = 0; index < samplerInfo.count; ++index)
        {
            [_mtlRenderEncoder setFragmentSamplerState:samplerState atIndex:samplerInfo.binding + index];
        }
    }
}

void GraphicsContextImpl::setUniformBuffer() const
{
    if (_programState)
    {
        auto& callbackUniforms = _programState->getCallbackUniforms();
        for (auto& cb : callbackUniforms)
            cb.second(_programState, cb.first);

        auto& cpuBuffer = _programState->getUniformBuffer();
        if (!cpuBuffer.empty())
        {
            const auto bufferPtr = cpuBuffer.data();
            for (auto& uboInfo : _programState->getActiveUniformBlockInfos())
            {
                switch (uboInfo.stage)
                {
                case ShaderStage::VERTEX:
                    [_mtlRenderEncoder setVertexBytes:bufferPtr + uboInfo.cpuOffset
                                               length:uboInfo.sizeBytes
                                              atIndex:uboInfo.binding];
                    break;
                case ShaderStage::FRAGMENT:
                    [_mtlRenderEncoder setFragmentBytes:bufferPtr + uboInfo.cpuOffset
                                                 length:uboInfo.sizeBytes
                                                atIndex:uboInfo.binding];
                    break;
                default:;
                }
            }
        }

        // Bind storage buffers to the graphics stages (GPU render VS/PS). Binding
        // to an unused stage is harmless in Metal.
        for (const auto& [binding, bindingSet] : _programState->getStorageBufferBindingSets())
        {
            if (!bindingSet.buffer)
                continue;
            auto bufferImpl         = static_cast<BufferImpl*>(bindingSet.buffer);
            id<MTLBuffer> mtlBuffer = bufferImpl->getMTLBuffer();
            if (mtlBuffer == nil)
                continue;

            [_mtlRenderEncoder setVertexBuffer:mtlBuffer offset:0 atIndex:binding];
            [_mtlRenderEncoder setFragmentBuffer:mtlBuffer offset:0 atIndex:binding];
        }
    }
}

void GraphicsContextImpl::setScissorRect(bool enabled, float x, float y, float width, float height)
{
    MTLScissorRect scissorRect;
    if (enabled)
    {
        y                  = _renderTargetHeight - height - y;
        int minX           = std::clamp((int)x, 0, (int)_renderTargetWidth);
        int minY           = std::clamp((int)y, 0, (int)_renderTargetHeight);
        int maxX           = std::clamp((int)(x + width), 0, (int)_renderTargetWidth);
        int maxY           = std::clamp((int)(y + height), 0, (int)_renderTargetHeight);
        scissorRect.x      = minX;
        scissorRect.y      = minY;
        scissorRect.width  = maxX - minX;
        scissorRect.height = maxY - minY;
        if (scissorRect.width == 0 || scissorRect.height == 0)
        {
            scissorRect.width  = 0;
            scissorRect.height = 0;
        }
    }
    else
    {
        scissorRect.x      = 0;
        scissorRect.y      = 0;
        scissorRect.width  = _renderTargetWidth;
        scissorRect.height = _renderTargetHeight;
    }

    [_mtlRenderEncoder setScissorRect:scissorRect];
}

void GraphicsContextImpl::readPixels(id<MTLTexture> texture,
                                     size_t origX,
                                     size_t origY,
                                     size_t rectWidth,
                                     size_t rectHeight,
                                     PixelBufferDesc& pbd)
{
    if (!texture || [texture isFramebufferOnly] || !rectWidth || !rectHeight ||
        origX > texture.width || origY > texture.height || rectWidth > texture.width - origX ||
        rectHeight > texture.height - origY || !getBitsPerElementMTL(texture.pixelFormat))
        return;
    NSUInteger texWidth   = texture.width;
    NSUInteger texHeight  = texture.height;
    MTLRegion region      = MTLRegionMake2D(0, 0, texWidth, texHeight);
    MTLRegion imageRegion = MTLRegionMake2D(origX, origY, rectWidth, rectHeight);

    MTLTextureDescriptor* textureDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:[texture pixelFormat]
                                                                                           width:texWidth
                                                                                          height:texHeight
                                                                                       mipmapped:NO];
    id<MTLDevice> device              = static_cast<GraphicsDeviceImpl*>(axdrv)->getMTLDevice();
    id<MTLTexture> readPixelsTexture  = [device newTextureWithDescriptor:textureDesc];
    if (!readPixelsTexture)
        return;

    auto oneOffBuffer = [_mtlCmdQueue commandBuffer];
    // [oneOffBuffer enqueue];

    id<MTLBlitCommandEncoder> blitCommandEncoder = [oneOffBuffer blitCommandEncoder];
    if (!blitCommandEncoder)
        return;
    [blitCommandEncoder copyFromTexture:texture
                            sourceSlice:0
                            sourceLevel:0
                           sourceOrigin:region.origin
                             sourceSize:region.size
                              toTexture:readPixelsTexture
                       destinationSlice:0
                       destinationLevel:0
                      destinationOrigin:region.origin];

#if (AX_TARGET_PLATFORM == AX_PLATFORM_MAC)
    [blitCommandEncoder synchronizeResource:readPixelsTexture];
#endif
    [blitCommandEncoder endEncoding];

    [oneOffBuffer commit];
    [oneOffBuffer waitUntilCompleted];
    if (oneOffBuffer.status != MTLCommandBufferStatusCompleted)
        return;
    auto bytePerRow = rectWidth * getBitsPerElementMTL(texture.pixelFormat) / 8;
    auto texelData  = pbd._data.resize(bytePerRow * rectHeight);
    if (texelData != nullptr)
    {
        [readPixelsTexture getBytes:texelData bytesPerRow:bytePerRow fromRegion:imageRegion mipmapLevel:0];
        UtilsMTL::swizzleImage(texelData, rectWidth, rectHeight, readPixelsTexture.pixelFormat);
        pbd._width  = static_cast<int>(rectWidth);
        pbd._height = static_cast<int>(rectHeight);
    }
}

void GraphicsContextImpl::setFrameBufferOnly(bool frameBufferOnly)
{
    if (_legacyReadbackUsers)
    {
        _mtlLayer.framebufferOnly = NO;
        return;
    }
    [_mtlLayer setFramebufferOnly:frameBufferOnly];
    if (!frameBufferOnly)
        _restoreFrameBufferOnlyAfterDrawable = true;
}

void GraphicsContextImpl::restoreFrameBufferOnly()
{
    if (_legacyReadbackUsers)
        return;
    if (_restoreFrameBufferOnlyAfterDrawable)
    {
        [_mtlLayer setFramebufferOnly:YES];
        _restoreFrameBufferOnlyAfterDrawable = false;
    }
}

bool GraphicsContextImpl::copyTexture(id<MTLTexture> src, Texture* dst)
{
    if (src == nil || !dst || _currentCmdBuffer == nil || _currentCmdBuffer.status == MTLCommandBufferStatusCommitted)
        return false;

    auto* dstImpl = static_cast<TextureImpl*>(dst);
    if (dstImpl->internalHandle() == nil)
        dstImpl->updateData(nullptr, dst->getWidth(), dst->getHeight(), 0, 0);

    auto dstTexture = dstImpl->internalHandle();
    if (dstTexture == nil || src == dstTexture || src.textureType != MTLTextureType2D ||
        dstTexture.textureType != MTLTextureType2D || src.width != dstTexture.width ||
        src.height != dstTexture.height || src.pixelFormat != dstTexture.pixelFormat || src.sampleCount != 1 ||
        dstTexture.sampleCount != 1 || src.mipmapLevelCount != 1 || dstTexture.mipmapLevelCount != 1)
    {
        AXLOGW("copyTexture mismatch: dstTexture==nil={}, src==dstTexture={}, "
               "src.textureType={}, dstTexture.textureType={}, "
               "src.width={}, dstTexture.width={}, "
               "src.height={}, dstTexture.height={}, "
               "src.pixelFormat={}, dstTexture.pixelFormat={}, "
               "src.sampleCount={}, dstTexture.sampleCount={}, "
               "src.mipmapLevelCount={}, dstTexture.mipmapLevelCount={}",
               dstTexture == nil, src == dstTexture, static_cast<int>(src.textureType),
               static_cast<int>(dstTexture.textureType), src.width, dstTexture.width, src.height, dstTexture.height,
               static_cast<int>(src.pixelFormat), static_cast<int>(dstTexture.pixelFormat), src.sampleCount,
               dstTexture.sampleCount, src.mipmapLevelCount, dstTexture.mipmapLevelCount);
        return false;
    }
    // Metal permits only one active command encoder per command buffer.
    endEncoding();

    id<MTLBlitCommandEncoder> blitEncoder = [_currentCmdBuffer blitCommandEncoder];
    if (blitEncoder == nil)
        return false;

    const MTLSize size = MTLSizeMake(src.width, src.height, 1);
    [blitEncoder copyFromTexture:src
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:size
                       toTexture:dstTexture
                destinationSlice:0
                destinationLevel:0
               destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blitEncoder endEncoding];
    return true;
}

bool GraphicsContextImpl::copyTexture(Texture* src, Texture* dst)
{
    if (!validateTextureCopy(src, dst))
        return false;

    auto* srcImpl = static_cast<TextureImpl*>(src);
    return copyTexture(srcImpl->internalHandle(), dst);
}

bool GraphicsContextImpl::copyTexture(RenderTarget* src, Texture* dst)
{
    if (!src || !dst)
        return false;

    auto attachment = static_cast<RenderTargetImpl*>(src)->getColorAttachment(0);
    if (!attachment || attachment.level != 0)
        return false;

    if (!src->isDefaultRenderTarget())
    {
        return copyTexture(attachment.texture, dst);
    }

    // A drawable created while framebufferOnly is YES cannot be a blit source.
    // The caller must disable framebufferOnly before the drawable is acquired.
    if (_mtlLayer.framebufferOnly)
    {
        // AXLOGE("Metal: copyTexture from the screen render target requires setFrameBufferOnly(false) before "
        //        "acquiring the drawable");
        return false;
    }
    return copyTexture(attachment.texture, dst);
}

}  // namespace ax::rhi::mtl
