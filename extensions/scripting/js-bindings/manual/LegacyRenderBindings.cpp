// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#include "manual/LegacyRenderBindings.h"
#include "manual/LegacyDrawPrimitives.h"
#include "manual/LegacyDrawNode3D.h"
#include "manual/ManualUtils.h"
#include "axmol/scene/Camera.h"
#include "axmol/scene/Scene.h"
#include "axmol/2d/NodeGrid.h"
#include "axmol/renderer/Renderer.h"
#include "axmol/base/CustomEventListener.h"
#include "axmol/base/CustomEvent.h"
#include "axmol/base/EventDispatcher.h"
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>
#include <vector>

namespace axjs
{
namespace
{
using namespace manual;

struct LegacyProjectionState
{
    int projection{1};
    bool configured{false};
    bool previousDepthTest{false};
    ax::WeakPtr<ax::CustomEventListener> sceneListener;
    std::vector<ax::WeakPtr<ax::NodeGrid>> grids;
};
std::unordered_map<JSContext*, std::shared_ptr<LegacyProjectionState>> projectionStates;
ax::NodeGrid::LegacyProjectionMode gridProjectionMode(const LegacyProjectionState& state)
{
    return state.projection == 0 ? ax::NodeGrid::LegacyProjectionMode::Ortho
                                 : ax::NodeGrid::LegacyProjectionMode::Perspective;
}
void applyProjection(LegacyProjectionState& state)
{
    std::erase_if(state.grids, [](const auto& grid) { return !grid.get(); });
    for (const auto& grid : state.grids)
        grid.get()->setLegacyProjectionMode(gridProjectionMode(state));
    if (!state.configured)
        return;
    auto* director = ax::Director::getInstance();
    auto* scene = director->getRunningScene();
    auto size = director->getCanvasSize();
    if (!scene || size.width <= 0 || size.height <= 0)
        return;
    auto* camera = scene->getDefaultCamera();
    if (!camera)
        return;
    if (state.projection == 0)
        camera->configureOrthographicView(size, -1024.0f, 1024.0f);
    else
        camera->configureClassicView(size);
}
void registerLegacyGrid(JSContext* cx, ax::NodeGrid* grid)
{
    auto& state = *projectionStates.at(cx);
    std::erase_if(state.grids, [](const auto& entry) { return !entry.get(); });
    if (std::none_of(state.grids.begin(), state.grids.end(), [grid](const auto& entry) { return entry.get() == grid; }))
        state.grids.emplace_back(grid);
    // The v3 default projection is 3D even before setProjection is called.
    grid->setLegacyProjectionMode(gridProjectionMode(state));
}
bool nodeGridInit(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* grid = self<ax::NodeGrid>(cx, args);
    ax::Rect rect;
    if (!grid || argc > 1 || (argc == 1 && !argument(cx, args, 0, rect)))
        return error(cx, "NodeGrid initialization requires no arguments or a grid rectangle");
    const bool initialized = grid->init();
    if (initialized)
    {
        if (argc == 1)
            grid->setGridRect(rect);
        registerLegacyGrid(cx, grid);
    }
    args.rval().setBoolean(initialized);
    return true;
}
bool nodeGridCreate(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::Rect rect;
    if (argc > 1 || (argc == 1 && !argument(cx, args, 0, rect)))
        return error(cx, "NodeGrid.create requires no arguments or a grid rectangle");
    auto* grid = argc == 1 ? ax::NodeGrid::create(rect) : ax::NodeGrid::create();
    if (grid)
        registerLegacyGrid(cx, grid);
    return wrapNative(cx, grid, args.rval(), NativeOwnership::Factory);
}
template <bool Set>
bool directorProjection(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* director = self<ax::Director>(cx, args);
    if (!director)
        return false;
    auto state = projectionStates.at(cx);
    if constexpr (Set)
    {
        int projection;
        if (argc != 1 || !argument(cx, args, 0, projection))
            return error(cx, "setProjection requires a projection mode");
        if (projection != 0 && projection != 1)
            return error(cx, "Custom projection requires a native projection delegate");
        state->projection = projection;
        state->configured = true;
        applyProjection(*state);
        ax::CustomEvent event("director_projection_changed");
        director->getEventDispatcher()->dispatchEvent(&event);
        args.rval().setUndefined();
    }
    else
        args.rval().setInt32(state->projection);
    return !JS_IsExceptionPending(cx);
}
bool directorDepthTest(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* director = self<ax::Director>(cx, args);
    if (!director || argc != 1)
        return error(cx, "setDepthTest requires an enabled flag");
    director->getRenderer()->setDepthTestFor2D(JS::ToBoolean(args[0]));
    args.rval().setUndefined();
    return true;
}
bool registerDirectorRendering(JSContext* cx)
{
    auto state = std::make_shared<LegacyProjectionState>();
    auto* director = ax::Director::getInstance();
    auto* dispatcher = director->getEventDispatcher();
    state->previousDepthTest = director->getRenderer()->getDepthTestFor2D();
    state->sceneListener = dispatcher->addCustomEventListener(ax::Director::EVENT_AFTER_SET_NEXT_SCENE,
        [state](ax::CustomEvent*) { applyProjection(*state); });
    projectionStates[cx] = state;
    ax::WeakPtr<ax::EventDispatcher> weakDispatcher(dispatcher);
    engine(cx)->addCleanupCallback([cx, state, weakDispatcher] {
        for (const auto& grid : state->grids)
            if (auto* live = grid.get())
                live->setLegacyProjectionMode(ax::NodeGrid::LegacyProjectionMode::Disabled);
        state->grids.clear();
        if (auto* dispatcher = weakDispatcher.get())
        {
            if (auto* listener = state->sceneListener.get())
                dispatcher->removeEventListener(listener);
            ax::Director::getInstance()->getRenderer()->setDepthTestFor2D(state->previousDepthTest);
        }
        projectionStates.erase(cx);
    });
    JS::RootedObject type(cx, getClassObject<ax::Director>(cx));
    JS::RootedObject gridType(cx, getClassObject<ax::NodeGrid>(cx));
    return type && gridType && function(cx, gridType, "create", nodeGridCreate, 0) &&
        manual::method<ax::NodeGrid>(cx, "init", nodeGridInit, 0) &&
        manual::method<ax::NodeGrid>(cx, "ctor", nodeGridInit, 0) &&
        property(cx, type, "PROJECTION_2D", 0) && property(cx, type, "PROJECTION_3D", 1) &&
        property(cx, type, "PROJECTION_DEFAULT", 1) && property(cx, type, "PROJECTION_CUSTOM", 2) &&
        property(cx, type, "EVENT_PROJECTION_CHANGED", "director_projection_changed") &&
        property(cx, type, "EVENT_BEFORE_UPDATE", ax::Director::EVENT_BEFORE_UPDATE) &&
        property(cx, type, "EVENT_AFTER_UPDATE", ax::Director::EVENT_AFTER_UPDATE) &&
        property(cx, type, "EVENT_BEFORE_DRAW", ax::Director::EVENT_BEFORE_DRAW) &&
        property(cx, type, "EVENT_AFTER_VISIT", ax::Director::EVENT_AFTER_VISIT) &&
        property(cx, type, "EVENT_AFTER_DRAW", ax::Director::EVENT_AFTER_DRAW) &&
        property(cx, type, "EVENT_BEFORE_SET_NEXT_SCENE", ax::Director::EVENT_BEFORE_SET_NEXT_SCENE) &&
        property(cx, type, "EVENT_AFTER_SET_NEXT_SCENE", ax::Director::EVENT_AFTER_SET_NEXT_SCENE) &&
        manual::method<ax::Director>(cx, "setProjection", directorProjection<true>, 1) &&
        manual::method<ax::Director>(cx, "getProjection", directorProjection<false>) &&
        manual::method<ax::Director>(cx, "setDepthTest", directorDepthTest, 1);
}

template <int Operation>
bool matrixMath(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if constexpr (Operation == 0)
    {
        ax::Vec3 translation;
        ax::Mat4 result;
        if (!argument(cx, args, 0, translation))
            return false;
        ax::Mat4::createTranslation(translation, &result);
        return toJsValue(cx, result, args.rval());
    }
    else if constexpr (Operation == 1)
    {
        ax::Quaternion rotation;
        ax::Mat4 result;
        if (!argument(cx, args, 0, rotation))
            return false;
        ax::Mat4::createRotation(rotation, &result);
        return toJsValue(cx, result, args.rval());
    }
    else if constexpr (Operation == 6)
    {
        ax::Quaternion a, b;
        if (!argument(cx, args, 0, a) || !argument(cx, args, 1, b))
            return false;
        return toJsValue(cx, a * b, args.rval());
    }
    else
    {
        ax::Mat4 matrix;
        if (!argument(cx, args, 0, matrix))
            return false;
        if constexpr (Operation == 2)
        {
            ax::Mat4 right;
            if (!argument(cx, args, 1, right))
                return false;
            return toJsValue(cx, matrix * right, args.rval());
        }
        else if constexpr (Operation == 3)
            return toJsValue(cx, matrix.getInversed(), args.rval());
        else
        {
            ax::Vec3 point, result;
            if (!argument(cx, args, 1, point))
                return false;
            if constexpr (Operation == 4)
                result = matrix * point;
            else
                matrix.transformPoint(point, &result);
            return toJsValue(cx, result, args.rval());
        }
    }
}
#if AX_ENABLE_3D
template <int Operation>
bool spatialMath(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if constexpr (Operation == 2)
    {
        ax::Ray ray;
        ax::OBB bounds;
        if (!argument(cx, args, 0, ray) || !argument(cx, args, 1, bounds))
            return false;
        args.rval().setBoolean(ray.intersects(bounds));
        return true;
    }
    else
    {
        ax::OBB bounds;
        if (!argument(cx, args, 0, bounds))
            return false;
        if constexpr (Operation == 0)
        {
            std::array<ax::Vec3, 8> corners;
            bounds.getCorners(corners.data());
            return toJsValue(cx, corners, args.rval());
        }
        else
        {
            ax::OBB other;
            if (!argument(cx, args, 1, other))
                return false;
            args.rval().setBoolean(bounds.intersects(other));
            return true;
        }
    }
}
#endif
// cocos2d-x v3's public Camera.unproject(Size, Vec3) uses width/height
// (not Vec2's x/y aliases) and JS ToNumber. Keep these readers local to that
// overload; its Size converter deliberately permits zero/NaN dimensions.
bool legacyUnprojectSize(JSContext* cx, JS::HandleValue value, ax::Vec2& size)
{
    if (!value.isObject())
        return error(cx, "Camera.unproject requires a Size object");
    JS::RootedObject object(cx, &value.toObject());
    JS::RootedValue width(cx), height(cx);
    double w, h;
    if (!JS_GetProperty(cx, object, "width", &width) || !JS_GetProperty(cx, object, "height", &height) ||
        !JS::ToNumber(cx, width, &w) || !JS::ToNumber(cx, height, &h))
        return false;
    size.set(static_cast<float>(w), static_cast<float>(h));
    return true;
}
bool legacyUnprojectPoint(JSContext* cx, JS::HandleValue value, ax::Vec3& point)
{
    if (!value.isObject())
        return error(cx, "Camera.unproject requires a Vec3 object");
    JS::RootedObject object(cx, &value.toObject());
    JS::RootedValue x(cx), y(cx), z(cx);
    double px, py, pz;
    if (!JS_GetProperty(cx, object, "x", &x) || !JS_GetProperty(cx, object, "y", &y) ||
        !JS_GetProperty(cx, object, "z", &z) || !JS::ToNumber(cx, x, &px) || !JS::ToNumber(cx, y, &py) ||
        !JS::ToNumber(cx, z, &pz))
        return false;
    if (std::isnan(px) || std::isnan(py) || std::isnan(pz))
        return error(cx, "Camera.unproject requires numeric Vec3 components");
    point.set(static_cast<float>(px), static_cast<float>(py), static_cast<float>(pz));
    return true;
}
template <bool Inverse, bool BottomLeft>
bool cameraProjection(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args    = JS::CallArgsFromVp(argc, vp);
    auto* camera = self<ax::Camera>(cx, args);
    ax::Vec3 point;
    if (!camera)
        return false;
    auto viewport = ax::Director::getInstance()->getCanvasSize();
    bool explicitSize = false;
    if constexpr (Inverse && !BottomLeft)
    {
        // Only unproject had this public JS overload in v3. Extra arguments
        // are ignored, as in its manual binding; unprojectGL stays unchanged.
        if (argc >= 2)
        {
            if (!legacyUnprojectSize(cx, args[0], viewport) || !legacyUnprojectPoint(cx, args[1], point))
                return false;
            explicitSize = true;
        }
    }
    if (!explicitSize && !argument(cx, args, 0, point))
        return false;
    if (!explicitSize && (viewport.width <= 0 || viewport.height <= 0))
        return error(cx, "Camera projection requires a nonempty canvas");
    if constexpr (Inverse)
    {
        const float y = explicitSize ? (viewport.height - point.y) / viewport.height
                                    : (BottomLeft ? point.y / viewport.height : 1 - point.y / viewport.height);
        ax::Vec4 clip(point.x / viewport.width, y, point.z, 1);
        clip.x = clip.x * 2 - 1;
        clip.y = clip.y * 2 - 1;
        clip.z = clip.z * 2 - 1;
        camera->getViewProjectionMatrix().getInversed().transformVector(clip, &clip);
        if (clip.w != 0)
        {
            clip.x /= clip.w;
            clip.y /= clip.w;
            clip.z /= clip.w;
        }
        return toJsValue(cx, ax::Vec3(clip.x, clip.y, clip.z), args.rval());
    }
    else
    {
        ax::Vec4 clip;
        camera->getViewProjectionMatrix().transformVector(ax::Vec4(point.x, point.y, point.z, 1), &clip);
        if (clip.w == 0)
            return error(cx, "Cannot project a point at infinity");
        const float x = (clip.x / clip.w + 1) * 0.5f * viewport.width;
        const float y = (clip.y / clip.w + 1) * 0.5f * viewport.height;
        return toJsValue(cx, ax::Vec2(x, BottomLeft ? y : viewport.height - y), args.rval());
    }
}
/*
 * Legacy segment, polygon miter extrusion and triangulation adapted from
 * cocos2d-x cocos/2d/CCDrawNode.cpp, fixed v3 commit
 * e4b6a5ef8fcdc99a7ebea606312b67fe4c534b9f.
 * Copyright (c) 2012 Scott Lembcke and Howling Moon Software
 * Copyright (c) 2012 cocos2d-x.org
 * Copyright (c) 2013-2016 Chukong Technologies Inc.
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
using LegacyDrawVertex = ax::V2F_T2F_C4F;

ax::Color legacyDrawColor(const ax::Color& value)
{
    // v3 submits Color4B(Color4F): truncation, not modern Color32 rounding.
    const auto channel = [](float value) {
        return static_cast<uint8_t>(std::clamp(value * 255.0f, 0.0f, 255.0f));
    };
    return ax::Color(ax::Color32(channel(value.r), channel(value.g), channel(value.b), channel(value.a)));
}

void legacyDrawTriangle(std::vector<LegacyDrawVertex>& vertices, const ax::Vec2& a,
                        const ax::Vec2& b, const ax::Vec2& c, const ax::Color& color)
{
    vertices.push_back({a, ax::Vec2::zero, color});
    vertices.push_back({b, ax::Vec2::zero, color});
    vertices.push_back({c, ax::Vec2::zero, color});
}

bool segmentVertices(ax::DrawNode& node, const ax::Vec2& a, const ax::Vec2& b,
                     float radius, ax::Color color)
{
    const auto n = (b - a).getPerp().getNormalized();
    const auto t = n.getPerp();
    const auto nw = n * radius;
    const auto tw = t * radius;
    const auto v0 = b - (nw + tw), v1 = b + (nw - tw), v2 = b - nw, v3 = b + nw;
    const auto v4 = a - nw, v5 = a + nw, v6 = a - (nw - tw), v7 = a + (nw + tw);
    color = legacyDrawColor(color);
    const std::array<LegacyDrawVertex, 18> vertices{{
        {v0, -(n + t), color}, {v1, n - t, color}, {v2, -n, color},
        {v3, n, color}, {v1, n - t, color}, {v2, -n, color},
        {v3, n, color}, {v4, -n, color}, {v2, -n, color},
        {v3, n, color}, {v4, -n, color}, {v5, n, color},
        {v6, t - n, color}, {v4, -n, color}, {v5, n, color},
        {v6, t - n, color}, {v7, t + n, color}, {v5, n, color}
    }};
    return node.appendLegacyTriangleVertices(vertices);
}

bool segment(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* node = self<ax::DrawNode>(cx, args);
    ax::Vec2 a, b;
    ax::Color color;
    float radius;
    if (!node)
        return false;
    if (argc != 4 || !argument(cx, args, 0, a) || !argument(cx, args, 1, b) ||
        !argument(cx, args, 2, radius) || !argument(cx, args, 3, color))
        return error(cx, "drawSegment requires from, to, radius and color");
    if (!appendLegacySegment(*node, a, b, radius, color))
        return error(cx, "Invalid legacy segment triangle batch");
    args.rval().setUndefined();
    return true;
}

template <unsigned Operation>
bool legacyDrawPrimitive(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* node = self<ax::DrawNode>(cx, args);
    ax::Color color;
    if (!node)
        return false;
    if constexpr (Operation == 0)
    {
        ax::Vec2 center;
        float radius;
        if (argc != 3 || !argument(cx, args, 0, center) || !argument(cx, args, 1, radius) ||
            !argument(cx, args, 2, color))
            return error(cx, "drawDot requires center, radius and color");
        node->drawDot(center, radius, legacyDrawColor(color));
    }
    else
    {
        constexpr unsigned pointCount = Operation == 1 ? 3 : 4;
        std::array<ax::Vec2, pointCount> points;
        uint32_t segments;
        if (argc != pointCount + 2 || !argument(cx, args, pointCount, segments) ||
            !argument(cx, args, pointCount + 1, color))
            return error(cx, "Bezier requires its control points, segments and color");
        for (unsigned i = 0; i < pointCount; ++i)
            if (!argument(cx, args, i, points[i]))
                return false;
        if constexpr (Operation == 1)
            node->drawQuadBezier(points[0], points[1], points[2], segments, legacyDrawColor(color));
        else
            node->drawCubicBezier(points[0], points[1], points[2], points[3], segments, legacyDrawColor(color));
    }
    args.rval().setUndefined();
    return true;
}

bool legacyPointInside(const ax::Vec2& a, const ax::Vec2& b, const ax::Vec2& c, const ax::Vec2& p)
{
    return (c - b).cross(p - b) >= 0 && (a - c).cross(p - c) >= 0 &&
           (b - a).cross(p - a) >= 0;
}

bool legacyFillPolygon(std::span<const ax::Vec2> points, unsigned count, const ax::Color& fill,
                        std::vector<LegacyDrawVertex>& vertices)
{
    float area = 0;
    for (unsigned previous = count - 1, current = 0; current < count; previous = current++)
        area += points[previous].x * points[current].y - points[current].x * points[previous].y;
    if (area <= 0)
    {
        // Preserve v3's clockwise/zero-area fan rather than replacing its API
        // with the modern convex-default or a different tessellation policy.
        for (unsigned i = 0; i < count - 2; ++i)
            legacyDrawTriangle(vertices, points[0], points[i + 1], points[i + 2], fill);
        return true;
    }
    std::vector<unsigned> indices(count);
    std::iota(indices.begin(), indices.end(), 0);
    unsigned remaining = count, attempts = 2 * remaining, v = remaining - 1;
    while (remaining > 2)
    {
        if (!attempts--)
            return false;
        const auto u = v >= remaining ? 0 : v;
        v = u + 1 >= remaining ? 0 : u + 1;
        const auto w = v + 1 >= remaining ? 0 : v + 1;
        const auto& a = points[indices[u]];
        const auto& b = points[indices[v]];
        const auto& c = points[indices[w]];
        if ((b - a).cross(c - a) < 0.0000000001f)
            continue;
        bool inside = false;
        for (unsigned i = 0; i < remaining; ++i)
            if (i != u && i != v && i != w && legacyPointInside(a, b, c, points[indices[i]]))
            {
                inside = true;
                break;
            }
        if (inside)
            continue;
        legacyDrawTriangle(vertices, a, b, c, fill);
        indices.erase(indices.begin() + v);
        --remaining;
        attempts = 2 * remaining;
    }
    return true;
}

const char* polygonVertices(ax::DrawNode& node, std::span<const ax::Vec2> points,
                            ax::Color fill, float width, ax::Color border)
{
    if (points.size() < 3 || points.size() > std::numeric_limits<unsigned>::max())
        return "Invalid legacy polygon triangle batch";
    const auto count = static_cast<unsigned>(points.size());
    const bool outline = border.a > 0.0f && width > 0.0f;
    fill = legacyDrawColor(fill);
    border = legacyDrawColor(border);
    std::vector<LegacyDrawVertex> vertices;
    vertices.reserve((count - 2 + (outline ? 2 * count : 0)) * size_t{3});
    if (!legacyFillPolygon(points, count, fill, vertices))
        return "Cannot triangulate a nonsimple legacy polygon";
    if (outline)
    {
        struct Extrusion { ax::Vec2 offset, normal; };
        std::vector<Extrusion> extrude(count);
        for (unsigned i = 0; i < count; ++i)
        {
            const auto n1 = (points[i] - points[(i + count - 1) % count]).getPerp().getNormalized();
            const auto n2 = (points[(i + 1) % count] - points[i]).getPerp().getNormalized();
            const auto offset = (n1 + n2) * (1.0f / (ax::Vec2::dot(n1, n2) + 1.0f));
            if (!std::isfinite(offset.x) || !std::isfinite(offset.y))
                return "Cannot extrude a degenerate legacy polygon corner";
            extrude[i] = {offset, n2};
        }
        for (unsigned i = 0; i < count; ++i)
        {
            const auto j = (i + 1) % count;
            const auto n = extrude[i].normal;
            const auto inner0 = points[i] - extrude[i].offset * width;
            const auto inner1 = points[j] - extrude[j].offset * width;
            const auto outer0 = points[i] + extrude[i].offset * width;
            const auto outer1 = points[j] + extrude[j].offset * width;
            vertices.insert(vertices.end(), {
                {inner0, -n, border}, {inner1, -n, border}, {outer1, n, border},
                {inner0, -n, border}, {outer0, n, border}, {outer1, n, border}});
        }
    }
    if (!node.appendLegacyTriangleVertices(vertices))
        return "Invalid legacy polygon triangle batch";
    return nullptr;
}

bool polygon(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* node = self<ax::DrawNode>(cx, args);
    std::vector<ax::Vec2> points;
    ax::Color fill, border;
    float width;
    if (!node || !argument(cx, args, 0, points))
        return false;
    if (argc != 4 && argc != 5)
        return error(cx, "drawPolygon requires points, optional count, fill, width and border");
    unsigned offset = 1;
    uint32_t count = static_cast<uint32_t>(points.size());
    if (argc == 5)
    {
        if (!argument(cx, args, offset++, count) || count > points.size())
            return error(cx, "Polygon vertex count exceeds the supplied points");
    }
    if (count < 3 || !argument(cx, args, offset, fill) || !argument(cx, args, offset + 1, width) ||
        !argument(cx, args, offset + 2, border))
        return error(cx, "drawPolygon requires points, fill, width and border");
    if (const char* problem = appendLegacyPolygon(*node, std::span(points.data(), count), fill, width, border))
        return error(cx, problem);
    args.rval().setUndefined();
    return true;
}
template <auto Function>
bool method(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (invoke<Function>(cx, args))
        return true;
    if (!JS_IsExceptionPending(cx))
        JS_ReportError(cx, "Invalid DrawNode3D arguments");
    return false;
}
bool constructor(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc)
        return error(cx, "DrawNode3D expects no constructor arguments");
    return construct<LegacyDrawNode3D>(cx, args);
}
bool cube(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args  = JS::CallArgsFromVp(argc, vp);
    auto* node = self<LegacyDrawNode3D>(cx, args);
    std::vector<ax::Vec3> corners;
    ax::Color color;
    if (!node || !argument(cx, args, 0, corners) || !argument(cx, args, 1, color))
        return false;
    if (corners.size() != 8)
        return error(cx, "drawCube requires exactly eight corners");
    node->drawCube(corners.data(), color);
    args.rval().setUndefined();
    return true;
}
JSClass drawClass = [] {
    JSClass result{};
    result.name        = "DrawNode3D";
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
const JSFunctionSpec methods[] = {JS_FN("init", method<&LegacyDrawNode3D::init>, 0, JSPROP_ENUMERATE),
                                  JS_FN("drawLine", method<&LegacyDrawNode3D::drawLine>, 3, JSPROP_ENUMERATE),
                                  JS_FN("drawCube", cube, 2, JSPROP_ENUMERATE),
                                  JS_FN("clear", method<&LegacyDrawNode3D::clear>, 0, JSPROP_ENUMERATE),
                                  JS_FN("getBlendFunc", method<&LegacyDrawNode3D::getBlendFunc>, 0, JSPROP_ENUMERATE),
                                  JS_FN("setBlendFunc", method<&LegacyDrawNode3D::setBlendFunc>, 1, JSPROP_ENUMERATE),
                                  JS_FS_END};
const JSFunctionSpec statics[] = {JS_FN("create", method<&LegacyDrawNode3D::create>, 0, JSPROP_ENUMERATE), JS_FS_END};
}  // namespace
ax::Color legacyPrimitiveColor(const ax::Color& value)
{
    return legacyDrawColor(value);
}

bool appendLegacySegment(ax::DrawNode& node, const ax::Vec2& from, const ax::Vec2& to,
                         float radius, const ax::Color& color)
{
    return segmentVertices(node, from, to, radius, color);
}

const char* appendLegacyPolygon(ax::DrawNode& node, std::span<const ax::Vec2> points,
                               const ax::Color& fill, float borderWidth, const ax::Color& border)
{
    return polygonVertices(node, points, fill, borderWidth, border);
}

bool registerLegacyRendering(JSContext* cx, JS::HandleObject global)
{
    if (!registerDirectorRendering(cx))
        return false;
    JS::RootedObject cc(cx, namespaceObject(cx, global, "cc"));
    JS::RootedObject math(cx, namespaceObject(cx, cc, "math"));
    if (!math || !function(cx, math, "mat4CreateTranslation", matrixMath<0>, 1) ||
        !function(cx, math, "mat4CreateRotation", matrixMath<1>, 1) ||
        !function(cx, math, "mat4Multiply", matrixMath<2>, 2) ||
        !function(cx, math, "mat4GetInversed", matrixMath<3>, 1) ||
        !function(cx, math, "mat4MultiplyVec3", matrixMath<4>, 2) ||
        !function(cx, math, "mat4TransformPoint", matrixMath<5>, 2) ||
        !function(cx, math, "quatMultiply", matrixMath<6>, 2))
        return false;
#if AX_ENABLE_3D
    if (!function(cx, math, "obbGetCorners", spatialMath<0>, 1) ||
        !function(cx, math, "obbIntersectsObb", spatialMath<1>, 2) ||
        !function(cx, math, "rayIntersectsObb", spatialMath<2>, 2))
        return false;
#endif
    JS::RootedObject parent(cx, getPrototype<ax::Node>(cx));
    JS::RootedObject prototype(
        cx, JS_InitClass(cx, cc, parent, &drawClass, constructor, 0, nullptr, methods, nullptr, statics));
    return prototype && registerType<LegacyDrawNode3D, ax::Node>(cx, prototype, &drawClass) &&
           manual::method<ax::DrawNode>(cx, "_legacyDrawDot", legacyDrawPrimitive<0>, 3) &&
           manual::method<ax::DrawNode>(cx, "_legacyDrawQuadBezier", legacyDrawPrimitive<1>, 5) &&
           manual::method<ax::DrawNode>(cx, "_legacyDrawCubicBezier", legacyDrawPrimitive<2>, 6) &&
           manual::method<ax::DrawNode>(cx, "_legacyDrawSegment", segment, 4) &&
           manual::method<ax::DrawNode>(cx, "_legacyDrawPolygon", polygon, 4) &&
           manual::method<ax::DrawNode>(cx, "drawPolygon", polygon, 4) &&
           manual::method<ax::Camera>(cx, "project", cameraProjection<false, false>, 1) &&
           manual::method<ax::Camera>(cx, "projectGL", cameraProjection<false, true>, 1) &&
           manual::method<ax::Camera>(cx, "unproject", cameraProjection<true, false>, 1) &&
           manual::method<ax::Camera>(cx, "unprojectGL", cameraProjection<true, true>, 1);
}
}  // namespace axjs
