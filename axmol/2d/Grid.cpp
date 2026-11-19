/****************************************************************************
Copyright (c) 2009      On-Core
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2013-2016 Chukong Technologies Inc.
Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
Copyright (c) 2019-present Simdsoft Limited.
Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

 https://axmol.dev/

SPDX-License-Identifier: MIT
****************************************************************************/
#include "axmol/2d/Grid.h"

#include "axmol/base/Macros.h"
#include "axmol/base/Utils.h"
#include "axmol/scene/Node.h"
#include "axmol/renderer/Renderer.h"
#include "axmol/renderer/RenderTexturePass.h"
#include "axmol/renderer/Texture2D.h"
#include "axmol/renderer/Shaders.h"
#include "axmol/rhi/ProgramState.h"
#include "axmol/rhi/RenderTarget.h"
#include "axmol/rhi/GraphicsCore.h"
#include "axmol/scene/Camera.h"
#include "axmol/math/Vec4.h"

#include <cmath>

namespace ax
{

static constexpr float GRID_BLIT_ORDER = RT_PASS_END_ORDER - 1;

static bool projectGridPointToCanvas(const Mat4& projection, const Vec3& point, const Vec2& size, Vec2& out)
{
    if (size.width <= 0.0f || size.height <= 0.0f)
        return false;

    Vec4 clip(point.x, point.y, point.z, 1.0f);
    projection.transformVector(&clip);

    if (std::abs(clip.w) <= 0.000001f)
        return false;

    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    out.set((ndcX + 1.0f) * 0.5f * size.width, (ndcY + 1.0f) * 0.5f * size.height);
    return true;
}

static Vec2 projectGridTexCoordToCanvas(const Mat4& projection,
                                        const Vec3& point,
                                        const Vec2& size,
                                        bool flipped,
                                        const Vec2& fallbackUV)
{
    Vec2 projected;
    if (!projectGridPointToCanvas(projection, point, size, projected))
        return fallbackUV;

    Vec2 uv(projected.x / size.width, projected.y / size.height);
    if (flipped)
        uv.y = 1.0f - uv.y;
    return uv;
}

static Vec3 projectGridVertexToCanvas(const Mat4& projection, const Vec3& point, const Vec2& size)
{
    Vec2 projected;
    if (projectGridPointToCanvas(projection, point, size, projected))
        return Vec3(projected.x, projected.y, 0.0f);

    return point;
}
// implementation of GridBase

bool GridBase::initWithSize(const Vec2& gridSize)
{
    return initWithSize(gridSize, Rect::zero);
}

bool GridBase::initWithSize(const Vec2& gridSize, const ax::Rect& rect)
{
    Director* director = Director::getInstance();
    Vec2 s             = director->getCanvasSizeInPixels();

    Texture2D* texture = new Texture2D();

    rhi::TextureDesc desc;
    desc.width        = s.width;
    desc.height       = s.height;
    desc.textureUsage = rhi::TextureUsage::RENDER_TARGET;
    desc.pixelFormat  = rhi::PixelFormat::RGBA8;

    texture->initWithSpec(desc, Texture2D::DEFAULT_SLICE_DATA);

    const bool initialized = initWithSize(gridSize, texture, false, rect);
    _internallyGeneratedTexture = initialized;

    texture->release();

    return initialized;
}

bool GridBase::initWithSize(const Vec2& gridSize, Texture2D* texture, bool flipped)
{
    return initWithSize(gridSize, texture, flipped, Rect::zero);
}

bool GridBase::initWithSize(const Vec2& gridSize, Texture2D* texture, bool flipped, const Rect& rect)
{
    bool ret = true;

    _active    = false;
    _reuseGrid = 0;
    _gridSize  = gridSize;

    AX_SAFE_RELEASE(_renderTarget);
    AX_SAFE_RETAIN(texture);
    AX_SAFE_RELEASE(_texture);
    _texture = texture;
    _internallyGeneratedTexture = false;
    _isTextureFlipped = rhi::GraphicsCore::isOpenGL() ? flipped : !flipped;

    if (rect.equals(Rect::zero))
    {
        auto size = _texture->getContentSize();
        _gridRect.setRect(0, 0, size.width, size.height);
    }
    else
    {
        _gridRect = rect;
    }
    _step.x = _gridRect.size.width / _gridSize.width;
    _step.y = _gridRect.size.height / _gridSize.height;

    AX_SAFE_RELEASE(_programState);
    AX_SAFE_RELEASE(_vertexLayout);
    auto* program = axpm->getBuiltinProgram(rhi::ProgramType::POSITION_TEXTURE);
    _programState = new rhi::ProgramState(program);
    Object::assign(_vertexLayout, program->getVertexLayout());
    _drawCommand.setWeakPSVL(_programState, _vertexLayout);
    _mvpMatrixLocation = _programState->getUniformLocation("u_MVPMatrix");
    _textureLocation   = _programState->getUniformLocation("u_tex0");

    calculateVertexPoints();
    updateBlendState();
    return ret;
}

void GridBase::updateBlendState()
{
    if (!_texture || !_texture->hasPremultipliedAlpha())
    {
        _blendFunc = BlendFunc::ALPHA_NON_PREMULTIPLIED;
    }
    else
    {
        _blendFunc = BlendFunc::ALPHA_PREMULTIPLIED;
    }
}

GridBase::~GridBase()
{
    AXLOGV("deallocing GridBase: {}", fmt::ptr(this));

    AX_SAFE_RELEASE(_renderTarget);

    // TODO: ? why 2.0 comments this line:        setActive(false);
    AX_SAFE_RELEASE(_texture);

    AX_SAFE_RELEASE(_programState);
    AX_SAFE_RELEASE(_vertexLayout);
}

// properties
void GridBase::setActive(bool active)
{
    _active = active;
}

void GridBase::setTextureFlipped(bool flipped)
{
    if (_isTextureFlipped != flipped)
    {
        _isTextureFlipped = flipped;
        calculateVertexPoints();
    }
}

void GridBase::setGridRect(const ax::Rect& rect)
{
    _gridRect = rect;
}

void GridBase::setCaptureSourceOverAlpha(bool enabled)
{
    if (_internallyGeneratedTexture)
    {
        _texture->setPremultipliedAlpha(enabled);
        updateBlendState();
    }
}

void GridBase::beforeDraw()
{
    beforeDrawWithAlphaPolicy(false);
}

void GridBase::beforeDrawWithAlphaPolicy(bool sourceOverAlpha)
{
    // The texture and target are shared by sequential capture/blit pairs, but
    // their lifetime must not end when another view records a pass this frame.
    if (!_renderTarget)
        _renderTarget = axdrv->createRenderTarget(_texture->getRHITexture());

    // RGB source-over into transparent black accumulates PMA color. Only
    // these owned captures change representation; external texture overloads
    // retain the caller's declared PMA flag. Keep this capture's selection even
    // if legacy policy changes while child traversal is collecting commands.
    setCaptureSourceOverAlpha(sourceOverAlpha);
    // Standard source-over capture produces PMA pixels even when its external
    // target retains a straight-alpha declaration. Composite this capture once;
    // leave the external metadata and subsequent direct blits unchanged.
    _captureBlendFunc = sourceOverAlpha ? BlendFunc::ALPHA_PREMULTIPLIED : _blendFunc;

    if (!_renderTexturePass)
    {
        _renderTexturePass = RefPtr<RenderTexturePass>(RenderTexturePass::obtain(), tlx::adopt_object);
        _renderTexturePass->setCameraOverrideEnabled(false);
    }

    _renderTexturePass->setTarget(_renderTarget);
    _renderTexturePass->setGridCaptureSourceOverAlpha(sourceOverAlpha);
    _renderTexturePass->setViewport(
        Viewport(0, 0, static_cast<int>(_texture->getWidth()), static_cast<int>(_texture->getHeight())));
    _renderTexturePass->begin();

    _renderTexturePass->clear(ClearFlag::COLOR, {.color = _clearColor, .depth = 1.0f, .stencil = 0});
}

void GridBase::afterDraw(ax::Node* /*target*/, const SceneRenderState& state)
{
    if (_renderTexturePass && _renderTexturePass->isActive())
        _renderTexturePass->end();

    // Custom Grid subclasses that use the protected command keep the same
    // hooks. Built-in grids attach them to each independent pending draw.
    _drawCommand.setBeforeCallback([this]() { beforeBlit(); });
    _drawCommand.setAfterCallback([this]() { afterBlit(); });
    blit(state);
    _captureBlendFunc.reset();
}

void GridBase::enqueueBlit(const SceneRenderState& state,
                           const void* vertices,
                           size_t vertexCount,
                           const unsigned short* indices,
                           size_t indexCount)
{
    BlitCommand* draw = nullptr;
    for (const auto& entry : _blitCommands)
    {
        if (!entry->pending)
        {
            draw = entry.get();
            break;
        }
    }
    if (!draw)
    {
        draw = _blitCommands.emplace_back(std::make_unique<BlitCommand>()).get();
        draw->command.setOwnPSVL(_programState->clone(), _vertexLayout, RenderCommand::ADOPT_FLAG_PS);
    }

    auto& command = draw->command;
    if (command.getVertexCapacity() != vertexCount)
        command.createVertexBuffer(sizeof(Vec3) + sizeof(Vec2), vertexCount, CustomCommand::BufferUsage::DYNAMIC);
    if (command.getIndexCapacity() != indexCount)
        command.createIndexBuffer(CustomCommand::IndexFormat::U_SHORT, indexCount, CustomCommand::BufferUsage::DYNAMIC);
    command.updateVertexBuffer(vertices, vertexCount * (sizeof(Vec3) + sizeof(Vec2)));
    command.updateIndexBuffer(indices, indexCount * sizeof(unsigned short));
    command.init(GRID_BLIT_ORDER, _captureBlendFunc.value_or(_blendFunc));

    auto* programState = command.unsafePS();
    const auto& projection = state.getViewProjectionMatrix();
    programState->setUniform(_mvpMatrixLocation, projection.m, sizeof(projection.m));
    programState->setTexture(_textureLocation, 0, _texture->getRHITexture());
    command.setBeforeCallback([this]() { beforeBlit(); });
    command.setAfterCallback([this, draw]() {
        afterBlit();
        draw->pending = false;
    });
    draw->pending = true;

    auto* renderer = state.getRenderer();
    renderer->addCommand(&command);
    // Keep native state alive through the draw without a Grid/command ownership
    // cycle. State bracketing belongs to the actual draw callbacks above.
    renderer->addCallbackCommand([self = RefPtr<GridBase>(this)]() mutable { self.reset(); }, GRID_BLIT_ORDER);
}

void GridBase::setScreenProjectionForBlit(const Mat4* projection, const Vec2& size)
{
    _screenProjectionForBlitEnabled = projection && size.width > 0.0f && size.height > 0.0f;
    if (_screenProjectionForBlitEnabled)
    {
        _screenProjectionForBlit     = *projection;
        _screenProjectionForBlitSize = size;
    }
}

// implementation of Grid3D

Grid3D* Grid3D::create(const Vec2& gridSize)
{
    Grid3D* ret = new Grid3D();
    if (ret->initWithSize(gridSize))
    {
        ret->autorelease();
    }
    else
    {
        delete ret;
        ret = nullptr;
    }
    return ret;
}

Grid3D* Grid3D::create(const Vec2& gridSize, const Rect& rect)
{
    Grid3D* ret = new Grid3D();

    if (ret->initWithSize(gridSize, rect))
    {
        ret->autorelease();
    }
    else
    {
        delete ret;
        ret = nullptr;
    }

    return ret;
}

Grid3D* Grid3D::create(const Vec2& gridSize, Texture2D* texture, bool flipped)
{
    Grid3D* ret = new Grid3D();

    if (ret->initWithSize(gridSize, texture, flipped))
    {
        ret->autorelease();
    }
    else
    {
        delete ret;
        ret = nullptr;
    }

    return ret;
}

Grid3D* Grid3D::create(const Vec2& gridSize, Texture2D* texture, bool flipped, const Rect& rect)
{
    Grid3D* ret = new Grid3D();

    if (ret->initWithSize(gridSize, texture, flipped, rect))
    {
        ret->autorelease();
    }
    else
    {
        delete ret;
        ret = nullptr;
    }

    return ret;
}

Grid3D::Grid3D() {}

Grid3D::~Grid3D()
{
    AX_SAFE_FREE(_texCoordinates);
    AX_SAFE_FREE(_vertices);
    AX_SAFE_FREE(_indices);
    AX_SAFE_FREE(_originalVertices);
    AX_SAFE_FREE(_vertexBuffer);
}

void Grid3D::beforeBlit()
{
    auto renderer = Director::getInstance()->getRenderer();

    if (_needDepthTestForBlit)
    {
        _oldDepthTest = renderer->getDepthTest();
        renderer->setDepthTest(true);

        _oldDepthWrite = renderer->getDepthWrite();
        renderer->setDepthWrite(true);
    }
}

void Grid3D::afterBlit()
{
    auto renderer = Director::getInstance()->getRenderer();
    if (_needDepthTestForBlit)
    {
        renderer->setDepthTest(_oldDepthTest);
        renderer->setDepthWrite(_oldDepthWrite);
    }
}

void Grid3D::blit(const SceneRenderState& state)
{
    updateVertexBuffer();
    enqueueBlit(state, _vertexBuffer, static_cast<size_t>((_gridSize.width + 1) * (_gridSize.height + 1)), _indices,
                static_cast<size_t>(_gridSize.width * _gridSize.height * 6));
}

void Grid3D::calculateVertexPoints()
{
    float width       = (float)_texture->getWidth();
    float height      = (float)_texture->getHeight();
    auto contentSize  = _texture->getContentSize();
    float texelScaleX = contentSize.width > 0.0f ? width / contentSize.width : 1.0f;
    float texelScaleY = contentSize.height > 0.0f ? height / contentSize.height : 1.0f;

    AX_SAFE_FREE(_vertices);
    AX_SAFE_FREE(_originalVertices);
    AX_SAFE_FREE(_texCoordinates);
    AX_SAFE_FREE(_vertexBuffer);
    AX_SAFE_FREE(_indices);

    size_t numOfPoints = static_cast<size_t>((_gridSize.width + 1) * (_gridSize.height + 1));

    _vertices         = malloc(numOfPoints * sizeof(Vec3));
    _originalVertices = malloc(numOfPoints * sizeof(Vec3));
    _texCoordinates   = malloc(numOfPoints * sizeof(Vec2));
    _vertexBuffer     = malloc(numOfPoints * (sizeof(Vec3) + sizeof(Vec2)));
    _indices =
        (unsigned short*)malloc(static_cast<size_t>(_gridSize.width * _gridSize.height * sizeof(unsigned short) * 6));

    float* vertArray         = (float*)_vertices;
    float* texArray          = (float*)_texCoordinates;
    unsigned short* idxArray = _indices;

    for (int x = 0; x < _gridSize.width; ++x)
    {
        for (int y = 0; y < _gridSize.height; ++y)
        {
            int idx = (int)(y * _gridSize.width) + x;

            float x1 = x * _step.x + _gridRect.origin.x;
            float x2 = x1 + _step.x;
            float y1 = y * _step.y + _gridRect.origin.y;
            float y2 = y1 + _step.y;

            unsigned short a = (unsigned short)(x * (_gridSize.height + 1) + y);
            unsigned short b = (unsigned short)((x + 1) * (_gridSize.height + 1) + y);
            unsigned short c = (unsigned short)((x + 1) * (_gridSize.height + 1) + (y + 1));
            unsigned short d = (unsigned short)(x * (_gridSize.height + 1) + (y + 1));

            unsigned short tempidx[6] = {a, b, d, b, c, d};

            memcpy(&idxArray[6 * idx], tempidx, 6 * sizeof(unsigned short));

            int l1[4] = {a * 3, b * 3, c * 3, d * 3};

            Vec3 e(x1, y1, 0);
            Vec3 f(x2, y1, 0);
            Vec3 g(x2, y2, 0);
            Vec3 h(x1, y2, 0);

            Vec3 l2[4] = {e, f, g, h};

            int tex1[4]   = {a * 2, b * 2, c * 2, d * 2};
            Vec2 Tex2F[4] = {Vec2(x1, y1), Vec2(x2, y1), Vec2(x2, y2), Vec2(x1, y2)};

            for (int i = 0; i < 4; ++i)
            {
                float texX = Tex2F[i].x * texelScaleX;
                float texY = Tex2F[i].y * texelScaleY;

                vertArray[l1[i]]     = l2[i].x;
                vertArray[l1[i] + 1] = l2[i].y;
                vertArray[l1[i] + 2] = l2[i].z;

                texArray[tex1[i]] = texX / width;
                if (_isTextureFlipped)
                {
                    texArray[tex1[i] + 1] = (height - texY) / height;
                }
                else
                {
                    texArray[tex1[i] + 1] = texY / height;
                }
            }
        }
    }

    updateVertexAndTexCoordinate();

    memcpy(_originalVertices, _vertices,
           static_cast<size_t>((_gridSize.width + 1) * (_gridSize.height + 1) * sizeof(Vec3)));
}

Vec3 Grid3D::getVertex(const Vec2& pos) const
{
    AXASSERT(pos.x == (unsigned int)pos.x && pos.y == (unsigned int)pos.y, "Numbers must be integers");

    int index        = (int)(pos.x * (_gridSize.height + 1) + pos.y) * 3;
    float* vertArray = (float*)_vertices;

    Vec3 vert(vertArray[index], vertArray[index + 1], vertArray[index + 2]);

    return vert;
}

Vec3 Grid3D::getOriginalVertex(const Vec2& pos) const
{
    AXASSERT(pos.x == (unsigned int)pos.x && pos.y == (unsigned int)pos.y, "Numbers must be integers");

    int index        = (int)(pos.x * (_gridSize.height + 1) + pos.y) * 3;
    float* vertArray = (float*)_originalVertices;

    Vec3 vert(vertArray[index], vertArray[index + 1], vertArray[index + 2]);

    return vert;
}

void Grid3D::setVertex(const Vec2& pos, const Vec3& vertex)
{
    AXASSERT(pos.x == (unsigned int)pos.x && pos.y == (unsigned int)pos.y, "Numbers must be integers");
    int index            = (int)(pos.x * (_gridSize.height + 1) + pos.y) * 3;
    float* vertArray     = (float*)_vertices;
    vertArray[index]     = vertex.x;
    vertArray[index + 1] = vertex.y;
    vertArray[index + 2] = vertex.z;
}

void Grid3D::reuse()
{
    if (_reuseGrid > 0)
    {
        memcpy(_originalVertices, _vertices,
               static_cast<size_t>((_gridSize.width + 1) * (_gridSize.height + 1) * sizeof(Vec3)));
        --_reuseGrid;
    }
}

void Grid3D::updateVertexBuffer()
{
    size_t numOfPoints          = static_cast<size_t>((_gridSize.width + 1) * (_gridSize.height + 1));
    auto tempVecPointer         = (Vec3*)_vertices;
    auto tempOriginalVecPointer = (Vec3*)_originalVertices;
    auto tempTexPointer         = (Vec2*)_texCoordinates;
    for (size_t i = 0; i < numOfPoints; ++i)
    {
        auto offset = i * (sizeof(Vec3) + sizeof(Vec2));
        if (_screenProjectionForBlitEnabled)
        {
            auto vertex =
                projectGridVertexToCanvas(_screenProjectionForBlit, tempVecPointer[i], _screenProjectionForBlitSize);
            auto texCoord =
                projectGridTexCoordToCanvas(_screenProjectionForBlit, tempOriginalVecPointer[i],
                                            _screenProjectionForBlitSize, _isTextureFlipped, tempTexPointer[i]);
            memcpy((char*)_vertexBuffer + offset, &vertex, sizeof(Vec3));
            memcpy((char*)_vertexBuffer + offset + sizeof(Vec3), &texCoord, sizeof(Vec2));
        }
        else
        {
            memcpy((char*)_vertexBuffer + offset, &tempVecPointer[i], sizeof(Vec3));
            memcpy((char*)_vertexBuffer + offset + sizeof(Vec3), &tempTexPointer[i], sizeof(Vec2));
        }
    }
}

void Grid3D::updateVertexAndTexCoordinate()
{
    unsigned int numOfPoints = static_cast<unsigned int>((_gridSize.width + 1) * (_gridSize.height + 1));
    auto tempVecPointer      = (Vec3*)_vertices;
    auto tempTexPointer      = (Vec2*)_texCoordinates;
    for (unsigned int i = 0; i < numOfPoints; ++i)
    {
        auto offset = i * (sizeof(Vec3) + sizeof(Vec2));
        memcpy((char*)_vertexBuffer + offset, &tempVecPointer[i], sizeof(Vec3));
        memcpy((char*)_vertexBuffer + offset + sizeof(Vec3), &tempTexPointer[i], sizeof(Vec2));
    }
}

// implementation of TiledGrid3D

TiledGrid3D::~TiledGrid3D()
{
    AX_SAFE_FREE(_texCoordinates);
    AX_SAFE_FREE(_vertices);
    AX_SAFE_FREE(_originalVertices);
    AX_SAFE_FREE(_indices);
    AX_SAFE_FREE(_vertexBuffer);
}

TiledGrid3D* TiledGrid3D::create(const Vec2& gridSize)
{
    TiledGrid3D* ret = new TiledGrid3D();

    if (ret->initWithSize(gridSize))
    {
        ret->autorelease();
    }
    else
    {
        delete ret;
        ret = nullptr;
    }

    return ret;
}

TiledGrid3D* TiledGrid3D::create(const Vec2& gridSize, const Rect& rect)
{
    TiledGrid3D* ret = new TiledGrid3D();

    if (ret->initWithSize(gridSize, rect))
    {
        ret->autorelease();
    }
    else
    {
        delete ret;
        ret = nullptr;
    }

    return ret;
}

TiledGrid3D* TiledGrid3D::create(const Vec2& gridSize, Texture2D* texture, bool flipped, const Rect& rect)
{
    TiledGrid3D* ret = new TiledGrid3D();

    if (ret->initWithSize(gridSize, texture, flipped, rect))
    {
        ret->autorelease();
    }
    else
    {
        delete ret;
        ret = nullptr;
    }

    return ret;
}

TiledGrid3D* TiledGrid3D::create(const Vec2& gridSize, Texture2D* texture, bool flipped)
{
    TiledGrid3D* ret = new TiledGrid3D();

    if (ret->initWithSize(gridSize, texture, flipped))
    {
        ret->autorelease();
    }
    else
    {
        delete ret;
        ret = nullptr;
    }

    return ret;
}

void TiledGrid3D::blit(const SceneRenderState& state)
{
    updateVertexBuffer();
    const auto gridSize = static_cast<size_t>(_gridSize.width * _gridSize.height);
    enqueueBlit(state, _vertexBuffer, gridSize * 4, _indices, gridSize * 6);
}

void TiledGrid3D::calculateVertexPoints()
{
    float width       = (float)_texture->getWidth();
    float height      = (float)_texture->getHeight();
    auto contentSize  = _texture->getContentSize();
    float texelScaleX = contentSize.width > 0.0f ? width / contentSize.width : 1.0f;
    float texelScaleY = contentSize.height > 0.0f ? height / contentSize.height : 1.0f;

    int numQuads = (int)(_gridSize.width * _gridSize.height);
    AX_SAFE_FREE(_vertices);
    AX_SAFE_FREE(_originalVertices);
    AX_SAFE_FREE(_texCoordinates);
    AX_SAFE_FREE(_indices);
    AX_SAFE_FREE(_vertexBuffer);

    _vertices         = malloc(numQuads * 4 * sizeof(Vec3));
    _originalVertices = malloc(numQuads * 4 * sizeof(Vec3));
    _texCoordinates   = malloc(numQuads * 4 * sizeof(Vec2));
    _indices          = (unsigned short*)malloc(numQuads * 6 * sizeof(unsigned short));
    _vertexBuffer     = malloc(numQuads * 4 * (sizeof(Vec3) + sizeof(Vec2)));

    float* vertArray         = (float*)_vertices;
    float* texArray          = (float*)_texCoordinates;
    unsigned short* idxArray = _indices;

    for (int x = 0; x < _gridSize.width; ++x)
    {
        for (int y = 0; y < _gridSize.height; ++y)
        {
            float x1 = x * _step.x + _gridRect.origin.x;
            float x2 = x1 + _step.x;
            float y1 = y * _step.y + _gridRect.origin.y;
            float y2 = y1 + _step.y;

            *vertArray++ = x1;
            *vertArray++ = y1;
            *vertArray++ = 0;
            *vertArray++ = x2;
            *vertArray++ = y1;
            *vertArray++ = 0;
            *vertArray++ = x1;
            *vertArray++ = y2;
            *vertArray++ = 0;
            *vertArray++ = x2;
            *vertArray++ = y2;
            *vertArray++ = 0;

            float newY1 = y1;
            float newY2 = y2;
            float texX1 = x1 * texelScaleX;
            float texX2 = x2 * texelScaleX;
            newY1 *= texelScaleY;
            newY2 *= texelScaleY;

            if (_isTextureFlipped)
            {
                newY1 = height - newY1;
                newY2 = height - newY2;
            }

            *texArray++ = texX1 / width;
            *texArray++ = newY1 / height;
            *texArray++ = texX2 / width;
            *texArray++ = newY1 / height;
            *texArray++ = texX1 / width;
            *texArray++ = newY2 / height;
            *texArray++ = texX2 / width;
            *texArray++ = newY2 / height;
        }
    }

    for (int x = 0; x < numQuads; x++)
    {
        idxArray[x * 6 + 0] = (unsigned short)(x * 4 + 0);
        idxArray[x * 6 + 1] = (unsigned short)(x * 4 + 1);
        idxArray[x * 6 + 2] = (unsigned short)(x * 4 + 2);

        idxArray[x * 6 + 3] = (unsigned short)(x * 4 + 1);
        idxArray[x * 6 + 4] = (unsigned short)(x * 4 + 2);
        idxArray[x * 6 + 5] = (unsigned short)(x * 4 + 3);
    }

    updateVertexAndTexCoordinate();
    memcpy(_originalVertices, _vertices, numQuads * 12 * sizeof(float));
}

void TiledGrid3D::setTile(const Vec2& pos, const Quad3& coords)
{
    AXASSERT(pos.x == (unsigned int)pos.x && pos.y == (unsigned int)pos.y, "Numbers must be integers");
    int idx          = (int)(_gridSize.height * pos.x + pos.y) * 4 * 3;
    float* vertArray = (float*)_vertices;
    memcpy(&vertArray[idx], &coords, sizeof(Quad3));
}

Quad3 TiledGrid3D::getOriginalTile(const Vec2& pos) const
{
    AXASSERT(pos.x == (unsigned int)pos.x && pos.y == (unsigned int)pos.y, "Numbers must be integers");
    int idx          = (int)(_gridSize.height * pos.x + pos.y) * 4 * 3;
    float* vertArray = (float*)_originalVertices;

    Quad3 ret;
    memcpy(&ret, &vertArray[idx], sizeof(Quad3));

    return ret;
}

Quad3 TiledGrid3D::getTile(const Vec2& pos) const
{
    AXASSERT(pos.x == (unsigned int)pos.x && pos.y == (unsigned int)pos.y, "Numbers must be integers");
    int idx          = (int)(_gridSize.height * pos.x + pos.y) * 4 * 3;
    float* vertArray = (float*)_vertices;

    Quad3 ret;
    memcpy(&ret, &vertArray[idx], sizeof(Quad3));

    return ret;
}

void TiledGrid3D::reuse()
{
    if (_reuseGrid > 0)
    {
        int numQuads = (int)(_gridSize.width * _gridSize.height);

        memcpy(_originalVertices, _vertices, numQuads * 12 * sizeof(float));
        --_reuseGrid;
    }
}

void TiledGrid3D::updateVertexBuffer()
{
    size_t gradSize             = static_cast<size_t>(_gridSize.width * _gridSize.height);
    size_t numOfPoints          = gradSize * 4;
    auto tempVecPointer         = (Vec3*)_vertices;
    auto tempOriginalVecPointer = (Vec3*)_originalVertices;
    auto tempTexPointer         = (Vec2*)_texCoordinates;
    for (size_t i = 0; i < numOfPoints; ++i)
    {
        auto offset = i * (sizeof(Vec3) + sizeof(Vec2));
        if (_screenProjectionForBlitEnabled)
        {
            auto vertex =
                projectGridVertexToCanvas(_screenProjectionForBlit, tempVecPointer[i], _screenProjectionForBlitSize);
            auto texCoord =
                projectGridTexCoordToCanvas(_screenProjectionForBlit, tempOriginalVecPointer[i],
                                            _screenProjectionForBlitSize, _isTextureFlipped, tempTexPointer[i]);
            memcpy((char*)_vertexBuffer + offset, &vertex, sizeof(Vec3));
            memcpy((char*)_vertexBuffer + offset + sizeof(Vec3), &texCoord, sizeof(Vec2));
        }
        else
        {
            memcpy((char*)_vertexBuffer + offset, &tempVecPointer[i], sizeof(Vec3));
            memcpy((char*)_vertexBuffer + offset + sizeof(Vec3), &tempTexPointer[i], sizeof(Vec2));
        }
    }
}

void TiledGrid3D::updateVertexAndTexCoordinate()
{
    size_t gradSize     = static_cast<size_t>(_gridSize.width * _gridSize.height);
    auto numOfPoints    = gradSize * 4;
    auto tempVecPointer = (Vec3*)_vertices;
    auto tempTexPointer = (Vec2*)_texCoordinates;
    for (size_t i = 0; i < numOfPoints; ++i)
    {
        auto offset = i * (sizeof(Vec3) + sizeof(Vec2));
        memcpy((char*)_vertexBuffer + offset, &tempVecPointer[i], sizeof(Vec3));
        memcpy((char*)_vertexBuffer + offset + sizeof(Vec3), &tempTexPointer[i], sizeof(Vec2));
    }
}

}  // namespace ax
