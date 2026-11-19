/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/LegacyTMXBindings.h"
#include "runtime/JsRuntime.h"
#include "axmol/2d/FastTMXLayer.h"
#include "axmol/2d/FastTMXTiledMap.h"
#include "axmol/2d/Sprite.h"
#include "axmol/renderer/Texture2D.h"
#include <jsfriendapi.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace axjs
{
namespace
{
bool error(JSContext* cx, const char* message)
{
    if (!JS_IsExceptionPending(cx))
        JS_ReportError(cx, "%s", message);
    return false;
}

ax::FastTMXLayer* layerReceiver(JSContext* cx, JS::CallArgs& args, bool needTiles = true)
{
    ax::FastTMXLayer* layer{};
    if (!toNative(cx, args.thisv(), &layer) || !layer)
    {
        error(cx, "TMXLayer requires a live native layer");
        return nullptr;
    }
    if (needTiles && !layer->getTiles())
    {
        error(cx, "TMXLayer tile data is unavailable after releaseMap");
        return nullptr;
    }
    return layer;
}

bool coordinate(JSContext* cx, ax::FastTMXLayer* layer, JS::HandleValue value, ax::Vec2& point)
{
    if (!fromJsValue(cx, value, point))
        return error(cx, "TMXLayer requires a tile coordinate");
    const auto& size = layer->getLayerSize();
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0 || point.y < 0 ||
        point.x >= size.width || point.y >= size.height)
        return error(cx, "TMXLayer tile coordinate is outside the layer");
    return true;
}

bool tileGID(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* layer = layerReceiver(cx, args);
    ax::Vec2 point;
    if (!layer || argc < 1 || argc > 2 || !coordinate(cx, layer, args[0], point))
        return error(cx, "TMXLayer.getTileGIDAt requires a coordinate and optional flags object");
    if (argc == 2 && !args[1].isObject())
        return error(cx, "TMXLayer flags output must be an object");
    ax::TMXTileFlags flags{};
    const auto gid = layer->getTileGIDAt(point, &flags);
    if (argc == 2)
    {
        JS::RootedObject output(cx, &args[1].toObject());
        JS::RootedValue value(cx, JS::DoubleValue(static_cast<uint32_t>(flags)));
        if (!JS_SetProperty(cx, output, "flags", value))
            return false;
    }
    args.rval().setInt32(gid);
    return true;
}

bool tileFlags(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* layer = layerReceiver(cx, args);
    ax::Vec2 point;
    if (!layer || argc != 1 || !coordinate(cx, layer, args[0], point))
        return error(cx, "TMXLayer.getTileFlagsAt requires one coordinate");
    ax::TMXTileFlags flags{};
    layer->getTileGIDAt(point, &flags);
    args.rval().setNumber(static_cast<uint32_t>(flags));
    return true;
}

bool setTileGID(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* layer = layerReceiver(cx, args);
    ax::Vec2 point;
    int gid{};
    uint32_t flags{};
    if (!layer || argc < 2 || argc > 3 || !fromJsValue(cx, args[0], gid) ||
        !coordinate(cx, layer, args[1], point) || (argc == 3 && !JS::ToUint32(cx, args[2], &flags)))
        return error(cx, "TMXLayer.setTileGID requires a GID, coordinate and optional flags");
    if (gid < 0 || (gid != 0 && layer->batchIndexForGID(gid) < 0))
        return error(cx, "TMXLayer GID does not belong to a layer tileset");
    layer->setTileGID(gid, point, static_cast<ax::TMXTileFlags>(flags));
    args.rval().setUndefined();
    return true;
}

bool getTile(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* layer = layerReceiver(cx, args);
    ax::Vec2 point;
    if (!layer || argc != 1 || !coordinate(cx, layer, args[0], point))
        return error(cx, "TMXLayer.getTileAt requires one coordinate");
    return toJsValueWithOwner(cx, layer->getTileAt(point), args.rval(), args.thisv());
}

bool removeTile(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* layer = layerReceiver(cx, args);
    ax::Vec2 point;
    if (!layer || argc != 1 || !coordinate(cx, layer, args[0], point))
        return error(cx, "TMXLayer.removeTileAt requires one coordinate");
    layer->removeTileAt(point);
    args.rval().setUndefined();
    return true;
}

bool getTiles(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* layer = layerReceiver(cx, args);
    if (!layer || argc != 0)
        return error(cx, "TMXLayer.getTiles takes no arguments");
    const auto& size = layer->getLayerSize();
    const double count = double(size.width) * size.height;
    if (!std::isfinite(count) || size.width < 0 || size.height < 0 ||
        std::floor(size.width) != size.width || std::floor(size.height) != size.height ||
        count > std::numeric_limits<int>::max())
        return error(cx, "TMXLayer tile count is invalid");
    JS::RootedObject array(cx, JS_NewUint32Array(cx, static_cast<uint32_t>(count)));
    if (!array)
        return false;
    auto* values = static_cast<uint32_t*>(JS_GetArrayBufferViewData(array));
    // Fast layers transfer individual tiles to sprites. The old getTiles
    // contract includes those logical GIDs, rather than exposing the fast
    // renderer's internal zero placeholders for sprite-backed tiles.
    for (int y = 0; y < size.height; ++y)
    {
        for (int x = 0; x < size.width; ++x)
        {
            ax::TMXTileFlags flags{};
            const auto gid = layer->getTileGIDAt(ax::Vec2(x, y), &flags);
            values[x + y * static_cast<int>(size.width)] = static_cast<uint32_t>(gid) | flags;
        }
    }
    args.rval().setObject(*array);
    return true;
}

bool getTexture(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* layer = layerReceiver(cx, args, false);
    if (!layer || argc != 0)
        return error(cx, "TMXLayer.getTexture takes no arguments");
    const auto& batches = layer->getBatches();
    // The original layer owns one tileset. Fast layers may own several;
    // the primary batch matches the existing native getTileSet contract.
    auto* texture = batches.empty() ? nullptr : batches.front().texture;
    return toJsValueWithOwner(cx, texture, args.rval(), args.thisv());
}

bool releaseMap(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* layer = layerReceiver(cx, args, false);
    if (!layer || argc != 0)
        return error(cx, "TMXLayer.releaseMap takes no arguments");
    layer->releaseMap();
    args.rval().setUndefined();
    return true;
}

bool legacyMapOffsets(JSContext* cx, ax::FastTMXTiledMap* map)
{
    for (auto* layer : map->getLayers())
        if (!layer->setLegacyTilesetOffsetPolicy(true))
            return error(cx, "TMX legacy offset policy must be selected before extracting or releasing tiles");
    return true;
}

template<bool XML>
bool mapInit(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::FastTMXTiledMap* map{};
    std::string source, resourcePath;
    bool invisible = false;
    const unsigned required = XML ? 2 : 1;
    if (argc < required || argc > required + 1 || !toNative(cx, args.thisv(), &map) || !map ||
        !fromJsValue(cx, args[0], source) ||
        (XML && !fromJsValue(cx, args[1], resourcePath)) ||
        (argc > required && !fromJsValue(cx, args[required], invisible)))
        return error(cx, "TMXTiledMap initialization requires a source, XML resource path when applicable, and optional visibility flag");
    const bool initialized = XML ? map->initWithXML(source, resourcePath, invisible)
                                 : map->initWithTMXFile(source, invisible);
    if (initialized && !legacyMapOffsets(cx, map))
        return false;
    args.rval().setBoolean(initialized);
    return true;
}

template<bool XML>
bool mapFactory(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string source, resourcePath;
    bool invisible = false;
    const unsigned required = XML ? 2 : 1;
    if (argc < required || argc > required + 1 || !fromJsValue(cx, args[0], source) ||
        (XML && !fromJsValue(cx, args[1], resourcePath)) ||
        (argc > required && !fromJsValue(cx, args[required], invisible)))
        return error(cx, "TMXTiledMap factory requires a source, XML resource path when applicable, and optional visibility flag");
    auto* map = XML ? ax::FastTMXTiledMap::createWithXML(source, resourcePath, invisible)
                    : ax::FastTMXTiledMap::create(source, invisible);
    if (map && !legacyMapOffsets(cx, map))
        return false;
    return wrapNative(cx, map, args.rval(), NativeOwnership::Factory);
}

std::unique_ptr<ax::TMXLayerInfo> copyLayerInfo(JSContext* cx, ax::TMXLayerInfo* source)
{
    const auto& size = source->_layerSize;
    const double count = double(size.width) * size.height;
    if (!std::isfinite(count) || size.width < 0 || size.height < 0 ||
        std::floor(size.width) != size.width || std::floor(size.height) != size.height ||
        count > std::numeric_limits<int>::max() || (count != 0 && !source->_tiles))
    {
        error(cx, "TMX layer information has no valid tile data");
        return nullptr;
    }
    auto copy = std::make_unique<ax::TMXLayerInfo>();
    copy->_properties = source->_properties;
    copy->_name = source->_name;
    copy->_layerSize = size;
    copy->_visible = source->_visible;
    copy->_opacity = source->_opacity;
    copy->_offset = source->_offset;
    if (count != 0)
    {
        const size_t bytes = static_cast<size_t>(count) * sizeof(uint32_t);
        copy->_tiles = static_cast<uint32_t*>(std::malloc(bytes));
        if (!copy->_tiles)
        {
            JS_ReportOutOfMemory(cx);
            return nullptr;
        }
        std::memcpy(copy->_tiles, source->_tiles, bytes);
    }
    return copy;
}

bool layerInit(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* receiver = layerReceiver(cx, args, false);
    ax::TMXTilesetInfo* tileset{};
    ax::TMXLayerInfo* info{};
    ax::TMXMapInfo* map{};
    if (!receiver || argc != 3 || !toNative(cx, args[0], &tileset) || !toNative(cx, args[1], &info) ||
        !toNative(cx, args[2], &map) || !info || !map)
        return error(cx, "TMXLayer.initWithTilesetInfo requires tileset, layer and map information");
    if (receiver->getTiles())
        return error(cx, "TMXLayer is already initialized");
    // The parser or an existing native map owns the source array. A manually
    // constructed layer takes a copy, preserving independent releaseMap and
    // destructor lifetimes rather than sharing a double-freed raw allocation.
    auto copy = copyLayerInfo(cx, info);
    if (!copy)
        return false;
    const bool initialized = receiver->initWithTilesetInfo(tileset, copy.get(), map);
    if (initialized)
    {
        copy->_ownTiles = false;
        if (!receiver->setLegacyTilesetOffsetPolicy(true))
            return error(cx, "TMX legacy offset policy must be selected before extracting or releasing tiles");
    }
    args.rval().setBoolean(initialized);
    return true;
}

bool layerFactory(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    if (argc == 0)
        return constructUninitialized<ax::FastTMXLayer>(cx, args);
    ax::TMXTilesetInfo* tileset{};
    ax::TMXLayerInfo* layer{};
    ax::TMXMapInfo* map{};
    if (argc != 3 || !toNative(cx, args[0], &tileset) || !toNative(cx, args[1], &layer) ||
        !toNative(cx, args[2], &map) || !layer || !map)
        return error(cx, "TMXLayer.create requires tileset, layer and map information");
    auto copy = copyLayerInfo(cx, layer);
    if (!copy)
        return false;
    const auto tilesets = tileset ? std::span<ax::TMXTilesetInfo* const>(&tileset, 1)
                                 : std::span<ax::TMXTilesetInfo* const>{};
    auto* result = ax::FastTMXLayer::create(tilesets, copy.get(), map);
    if (result)
    {
        copy->_ownTiles = false;
        if (!result->setLegacyTilesetOffsetPolicy(true))
            return error(cx, "TMX legacy offset policy must be selected before extracting or releasing tiles");
    }
    return wrapNative(cx, result, args.rval(), NativeOwnership::Factory);
}

template<class T>
bool getProperties(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    T* object{};
    if (argc != 0 || !toNative(cx, args.thisv(), &object) || !object)
        return error(cx, "TMX getProperties requires a live receiver and no arguments");
    return toJsValue(cx, object->getProperties(), args.rval());
}

template<class T>
bool setProperties(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    T* object{};
    ax::ValueMap properties;
    if (argc != 1 || !toNative(cx, args.thisv(), &object) || !object || !fromJsValue(cx, args[0], properties))
        return error(cx, "TMX setProperties requires one properties object");
    object->setProperties(properties);
    args.rval().setUndefined();
    return true;
}

template<class T>
bool properties(JSContext* cx)
{
    JS::RootedObject prototype(cx, getPrototype<T>(cx));
    return prototype && JS_DefineFunction(cx, prototype, "getProperties", getProperties<T>, 0, 0) &&
           JS_DefineFunction(cx, prototype, "setProperties", setProperties<T>, 1, 0);
}

bool getObject(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::TMXObjectGroup* group{};
    std::string name;
    if (argc != 1 || !toNative(cx, args.thisv(), &group) || !group || !fromJsValue(cx, args[0], name))
        return error(cx, "TMXObjectGroup.getObject requires an object name");
    return toJsValue(cx, group->getObject(name), args.rval());
}
}

bool registerLegacyTMX(JSContext* cx, JS::HandleObject)
{
    JS::RootedObject layer(cx, getPrototype<ax::FastTMXLayer>(cx));
    JS::RootedObject type(cx, getClassObject<ax::FastTMXLayer>(cx));
    JS::RootedObject group(cx, getPrototype<ax::TMXObjectGroup>(cx));
    JS::RootedObject map(cx, getPrototype<ax::FastTMXTiledMap>(cx));
    JS::RootedObject mapType(cx, getClassObject<ax::FastTMXTiledMap>(cx));
    const JSFunctionSpec methods[] = {
        JS_FN("initWithTilesetInfo", layerInit, 3, 0),
        JS_FN("getTileGIDAt", tileGID, 1, 0), JS_FN("getTileFlagsAt", tileFlags, 1, 0),
        JS_FN("setTileGID", setTileGID, 2, 0), JS_FN("getTileAt", getTile, 1, 0),
        JS_FN("removeTileAt", removeTile, 1, 0), JS_FN("getTiles", getTiles, 0, 0),
        JS_FN("getTexture", getTexture, 0, 0),
        JS_FN("releaseMap", releaseMap, 0, 0), JS_FS_END};
    return layer && type && group && map && mapType && JS_DefineFunctions(cx, layer, methods) &&
           JS_DefineFunction(cx, map, "initWithTMXFile", mapInit<false>, 1, 0) &&
           JS_DefineFunction(cx, map, "initWithXML", mapInit<true>, 2, 0) &&
           JS_DefineFunction(cx, mapType, "create", mapFactory<false>, 1, 0) &&
           JS_DefineFunction(cx, mapType, "createWithXML", mapFactory<true>, 2, 0) &&
           JS_DefineFunction(cx, type, "create", layerFactory, 0, 0) &&
           JS_DefineFunction(cx, group, "getObject", getObject, 1, 0) &&
           properties<ax::FastTMXLayer>(cx) && properties<ax::FastTMXTiledMap>(cx) &&
           properties<ax::TMXLayerInfo>(cx) && properties<ax::TMXObjectGroup>(cx) &&
           properties<ax::TMXMapInfo>(cx);
}
}
