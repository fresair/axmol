/****************************************************************************
 Copyright (c) 2013-2016 Chukong Technologies Inc.
 Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

 https://axmol.dev/

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
#include "axmol/2d/NodeGrid.h"
#include "axmol/2d/Grid.h"
#include "axmol/renderer/Renderer.h"
#include "axmol/scene/Camera.h"

namespace ax
{

namespace
{
SceneViewData legacyGridView(const Vec2& size, bool perspective)
{
    Mat4 view = Mat4::identity;
    Mat4 projection;
    Vec3 eye = Vec3::zero;
    if (perspective)
    {
        // Fixed v3 Director::_3D: keep real clip-w/depth and perspective-correct
        // interpolation. Camera::Classic's near=.5 is intentionally not used.
        const float zeye = size.height / 1.154700538379252f;
        eye.set(size.width * 0.5f, size.height * 0.5f, zeye);
        Mat4::createPerspective(60.0f, size.width / size.height, 10.0f, zeye + size.height * 0.5f, &projection);
        Mat4::createLookAt(eye, Vec3(eye.x, eye.y, 0.0f), Vec3::yAxis, &view);
    }
    else
        Mat4::createOrthographicOffCenter(0, size.width, 0, size.height, -1024.0f, 1024.0f, &projection);
    return SceneViewData::fromMatrices(view, projection, eye);
}
}  // namespace

NodeGrid* NodeGrid::create()
{
    NodeGrid* ret = new NodeGrid();
    if (ret->init())
    {
        ret->autorelease();
    }
    else
    {
        AX_SAFE_DELETE(ret);
    }
    return ret;
}

NodeGrid* NodeGrid::create(const ax::Rect& rect)
{
    NodeGrid* ret = NodeGrid::create();
    if (ret)
    {
        ret->setGridRect(rect);
    }
    return ret;
}

NodeGrid::NodeGrid() {}

void NodeGrid::setTarget(Node* target)
{
#if AX_ENABLE_GC_FOR_NATIVE_OBJECTS
    auto sEngine = ScriptEngineManager::getInstance()->getScriptEngine();
    if (sEngine)
    {
        if (_gridTarget)
            sEngine->releaseScriptObject(this, _gridTarget);
        if (target)
            sEngine->retainScriptObject(this, target);
    }
#endif  // AX_ENABLE_GC_FOR_NATIVE_OBJECTS
    AX_SAFE_RELEASE(_gridTarget);
    AX_SAFE_RETAIN(target);
    _gridTarget = target;
}

NodeGrid::~NodeGrid()
{
    AX_SAFE_RELEASE(_gridCamera);
    if (_nodeGrid)
        _nodeGrid->setCaptureSourceOverAlpha(false);
    AX_SAFE_RELEASE(_nodeGrid);
    AX_SAFE_RELEASE(_gridTarget);
}

Camera* NodeGrid::getGridCamera(const Camera* currentCamera)
{
    if (!_gridCamera)
    {
        _gridCamera = Camera::create(CameraMode::Ortho);
        AX_SAFE_RETAIN(_gridCamera);
    }
    else
    {
        _gridCamera->configureOrthographicView(_director->getCanvasSize(), -1024, 1024);
    }

    _gridCamera->setCameraFlag(currentCamera ? currentCamera->getCameraFlag() : CameraFlag::DEFAULT);

    return _gridCamera;
}

void NodeGrid::setLegacyProjectionMode(LegacyProjectionMode mode)
{
    _legacyProjectionMode = mode;
    if (_nodeGrid)
        _nodeGrid->setCaptureSourceOverAlpha(mode != LegacyProjectionMode::Disabled);
}

void NodeGrid::onGridBeginDraw(GridBase* grid, bool sourceOverAlpha)
{
    grid->beforeDrawWithAlphaPolicy(sourceOverAlpha);
}

void NodeGrid::onGridEndDraw(GridBase* grid, const SceneRenderState& state)
{
    grid->afterDraw(this, state);
}

void NodeGrid::visit(const SceneRenderState& state, const Mat4& parentTransform, uint32_t parentFlags)
{
    // quick return if not visible. children won't be drawn.
    if (!_visible)
    {
        return;
    }

    bool dirty = (parentFlags & FLAGS_TRANSFORM_DIRTY) || _transformUpdated;
    if (dirty)
        _modelViewTransform = this->transform(parentTransform);
    _transformUpdated = false;

    RefPtr<GridBase> grid(_nodeGrid);
    auto activeGrid    = grid && grid->isActive();
    const auto projectionMode = _legacyProjectionMode;
    const bool projectToVisitingCamera = _projectGridBlitToVisitingCamera;
    auto currentCamera = state.getCamera();
    const auto canvasSize = _director->getCanvasSize();
    const bool legacyProjection = activeGrid && !projectToVisitingCamera &&
                                  projectionMode != LegacyProjectionMode::Disabled &&
                                  canvasSize.width > 0.0f && canvasSize.height > 0.0f;
    auto gridCam = activeGrid && !legacyProjection ? getGridCamera(currentCamera) : nullptr;

    auto* renderer = state.getRenderer();
    if (activeGrid)
    {
        // The full capture/composite pair occupies this node's position in its
        // caller's queue. A fresh pooled command also supports nested visits.
        auto* group = renderer->getNextGroupCommand();
        group->init(_globalZOrder);
        renderer->addCommand(group);
        renderer->pushGroup(group->getRenderQueueID());
    }

    SceneRenderState captureState = state;
    if (legacyProjection)
    {
        captureState.view = legacyGridView(canvasSize, false);
        captureState.viewOverridden = true;
    }

    if (activeGrid)
        onGridBeginDraw(grid.get(), projectionMode != LegacyProjectionMode::Disabled);

    if (_gridTarget)
    {
        _gridTarget->visit(captureState, _modelViewTransform, dirty);
    }

    int i                = 0;
    bool visibleByCamera = isVisitableByCamera(state.cameraFlag);

    if (!_children.empty())
    {
        sortAllChildren();
        // draw children zOrder < 0
        for (auto size = _children.size(); i < size; ++i)
        {
            auto node = _children.at(i);

            if (node && node->getLocalZOrder() < 0)
                node->visit(captureState, _modelViewTransform, dirty);
            else
                break;
        }
        // self draw,currently we have nothing to draw on NodeGrid, so there is no need to add render command
        if (visibleByCamera)
            this->draw(captureState, _modelViewTransform, dirty);

        for (auto it = _children.cbegin() + i, itCend = _children.cend(); it != itCend; ++it)
        {
            (*it)->visit(captureState, _modelViewTransform, dirty);
        }
    }
    else if (visibleByCamera)
    {
        this->draw(captureState, _modelViewTransform, dirty);
    }

    // FIX ME: Why need to set _orderOfArrival to 0??
    // Please refer to https://github.com/cocos2d/cocos2d-x/pull/6920
    // setOrderOfArrival(0);

    if (activeGrid)
    {
        // Capture may use the scene/XR camera, while blit still uses gridCam's
        // canvas ortho matrix. Projecting the uploaded grid vertices/UVs keeps
        // tile edges in the same camera space as the captured texture.
        const auto& screenProjection = state.getViewProjectionMatrix();
        grid->setScreenProjectionForBlit(projectToVisitingCamera ? &screenProjection : nullptr, canvasSize);
    }

    // The explicit VR projection path keeps its canvas ortho blit. Legacy JS
    // restores the selected Director projection using value snapshots instead
    // of modifying a camera shared with queued scene traversal.
    SceneRenderState gridRenderState = gridCam ? SceneRenderState(state.getRenderer(), gridCam) : state;
    if (legacyProjection)
    {
        gridRenderState.view = legacyGridView(canvasSize, projectionMode == LegacyProjectionMode::Perspective);
        gridRenderState.viewOverridden = true;
    }
    if (activeGrid)
        onGridEndDraw(grid.get(), gridRenderState);

    if (activeGrid)
        renderer->popGroup();

    if (activeGrid)
        grid->setScreenProjectionForBlit(nullptr, Vec2::zero);
}

void NodeGrid::setGrid(GridBase* grid)
{
    if (_nodeGrid == grid)
        return;
    if (_nodeGrid)
        _nodeGrid->setCaptureSourceOverAlpha(false);
    AX_SAFE_RETAIN(grid);
    AX_SAFE_RELEASE(_nodeGrid);
    _nodeGrid = grid;
    if (_nodeGrid)
        _nodeGrid->setCaptureSourceOverAlpha(_legacyProjectionMode != LegacyProjectionMode::Disabled);
}

}  // namespace ax
