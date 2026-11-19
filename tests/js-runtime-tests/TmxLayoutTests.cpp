/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "axmol/2d/FastTMXLayer.h"
#include "axmol/2d/Sprite.h"
#include "axmol/2d/TMXXMLParser.h"
#include "axmol/base/AutoreleasePool.h"
#include "axmol/base/Director.h"
#include "axmol/platform/FileUtils.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace
{
int assertions{};
void check(bool value, const std::string& message)
{
    ++assertions;
    if (!value)
        throw std::runtime_error(message);
}
void equal(const ax::Vec2& actual, const ax::Vec2& expected, const std::string& message)
{
    check(std::abs(actual.x - expected.x) < .001f && std::abs(actual.y - expected.y) < .001f,
          message + ": actual " + std::to_string(actual.x) + "," + std::to_string(actual.y) +
              "; expected " + std::to_string(expected.x) + "," + std::to_string(expected.y));
}

#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
// Native CLI has no JVM/AAssetManager. This is the same POSIX filesystem fixture
// as FileTests/AssetsTests; the unchanged TMX file is pushed to /data/local/tmp.
class NativeFiles final : public ax::FileUtils
{
public:
    NativeFiles()
    {
        _defaultResRootPath = "/data/local/tmp/";
        _writablePath = _defaultResRootPath;
        check(init(), "POSIX TMX fixture filesystem");
    }
    std::string getWritablePath() const override { return _writablePath; }
    std::string getNativeWritableAbsolutePath() const override { return _writablePath; }
    bool isFileExistInternal(std::string_view path) const override
    {
        struct stat result{};
        return stat(std::string(path).c_str(), &result) == 0 && S_ISREG(result.st_mode);
    }
};
#endif

struct Layout
{
    int orientation = ax::TMXOrientationHex;
    int axis = ax::TMXStaggerAxis_X;
    int index = ax::TMXStaggerIndex_Odd;
    int side = 88;
    int columns = 5;
    int rows = 5;
    ax::Vec2 size{175, 150};
    ax::Vec2 imageSize;
    ax::Vec2 layerOffset;
    ax::Vec2 imageOffset;
    float scale = 1;
};

// Independent grid specification follows the pinned v3 CCTMXLayer.cpp position
// formulas (771-804), including its origin convention for staggerindex="even".
// Staggered is the zero-side-length hex layout. This does not read production
// matrices or compute the expected position from a value returned by the layer.
ax::Vec2 position(const Layout& p, const ax::Vec2& cell)
{
    const float w = p.size.width / p.scale, h = p.size.height / p.scale;
    if (p.orientation == ax::TMXOrientationOrtho)
        return {cell.x * w, (p.rows - cell.y - 1) * h};
    if (p.orientation == ax::TMXOrientationIso)
        return {w / 2 * (p.columns + cell.x - cell.y - 1), h / 2 * (p.rows * 2 - cell.x - cell.y - 2)};
    const float side = p.orientation == ax::TMXOrientationHex ? p.side / p.scale : 0.f;
    const float sign = p.index == ax::TMXStaggerIndex_Odd ? 1.f : -1.f;
    if (p.axis == ax::TMXStaggerAxis_X)
        return {cell.x * (w + side) / 2,
                (p.rows - cell.y - 1) * h - (static_cast<int>(cell.x) & 1) * sign * h / 2};
    return {cell.x * w + (static_cast<int>(cell.y) & 1) * sign * w / 2,
            (p.rows - cell.y - 1) * (h + side) / 2};
}

class CpuLayer final : public ax::FastTMXLayer
{
public:
    bool fromMap(ax::TMXMapInfo* map)
    {
        auto* info = map->getLayers().front();
        if (!initWithTilesets({}, info, map))
            return false;
        info->_ownTiles = false;
        return true;
    }
    void metadataBatch(ax::TMXTilesetInfo* tileset)
    {
        // Real native metadata and index generation; there is no texture, buffer
        // or graphics driver in this fixture, and GPU execution is not claimed.
        ax::TilesetBatch batch;
        batch.tilesetInfo = tileset;
        tileset->retain();
        const int count = static_cast<int>(_layerSize.width * _layerSize.height);
        batch.indices.resize(count * 6);
        batch.indicesVertexZOffsets[0] = 0;
        _batches.push_back(std::move(batch));
        _tileToQuadIndex.resize(count);
        _tilesetBatchIndex.resize(count);
        for (int i = 0; i < count; ++i)
        {
            _tileToQuadIndex[i] = _tiles[i] ? i : -1;
            _tilesetBatchIndex[i] = _tiles[i] ? 0 : -1;
        }
        recomputeMaxTileSize();
    }
    std::vector<int> visible(const ax::Rect& rectangle)
    {
        updateTiles(rectangle);
        auto& batch = _batches.front();
        auto it = batch.indicesVertexZNumber.find(0);
        const int count = it == batch.indicesVertexZNumber.end() ? 0 : it->second;
        std::vector<int> result;
        for (int i = 0; i < count; ++i)
            result.push_back(static_cast<int>(batch.indices[6 * i] / 4));
        return result;
    }
    void spriteLayout(ax::Sprite* sprite, const ax::Vec2& cell, uint32_t flags)
    {
        setupTileSprite(sprite, cell, 1 | flags);
    }
};

class CpuSprite final : public ax::Sprite
{
public:
    explicit CpuSprite(const ax::Vec2& size)
    {
        // Sprite::initWithTexture creates GPU resources. Set its layout dimensions
        // for this native setupTileSprite/Node-transform check without that call.
        _contentSize = _originalContentSize = size;
    }
};

ax::TMXMapInfo* fixtureMap(const Layout& p)
{
    const char* orientation = p.orientation == ax::TMXOrientationHex ? "hexagonal" :
                              p.orientation == ax::TMXOrientationStaggered ? "staggered" :
                              p.orientation == ax::TMXOrientationIso ? "isometric" : "orthogonal";
    std::ostringstream xml;
    const auto imageSize = p.imageSize.isZero() ? p.size : p.imageSize;
    xml << "<map version='1.0' orientation='" << orientation << "' width='" << p.columns << "' height='" << p.rows
        << "' tilewidth='" << p.size.width << "' tileheight='" << p.size.height << "' hexsidelength='" << p.side
        << "' staggeraxis='" << (p.axis == ax::TMXStaggerAxis_X ? "x" : "y") << "' staggerindex='"
        << (p.index == ax::TMXStaggerIndex_Odd ? "odd" : "even") << "'>"
        << "<tileset firstgid='1' name='native-metadata' tilewidth='" << imageSize.width << "' tileheight='" << imageSize.height
        << "'><tileoffset x='" << p.imageOffset.x << "' y='" << p.imageOffset.y << "'/>"
        << "<tile id='0'><animation><frame tileid='0' duration='100'/><frame tileid='1' duration='100'/></animation></tile></tileset>"
        << "<layer name='grid' width='" << p.columns << "' height='" << p.rows << "' x='" << p.layerOffset.x
        << "' y='" << p.layerOffset.y << "'><data encoding='csv'>";
    for (int i = 0; i < p.columns * p.rows; ++i)
        xml << (i ? ",1" : "1");
    xml << "</data></layer></map>";
    auto* map = ax::TMXMapInfo::createWithXML(xml.str(), "");
    check(map && map->getLayers().size() == 1 && map->getTilesets().size() == 1, "actual native XML parser fixture");
    return map;
}

bool intersects(const ax::Rect& a, const ax::Rect& b)
{
    return a.getMinX() < b.getMaxX() && b.getMinX() < a.getMaxX() &&
           a.getMinY() < b.getMaxY() && b.getMinY() < a.getMaxY();
}

void layoutAndCulling(const Layout& p)
{
    ax::AutoreleasePool pool("TMX parity fixture");
    const float previousScale = ax::Director::getInstance()->getContentScaleFactor();
    ax::Director::getInstance()->setContentScaleFactor(p.scale);
    auto* map = fixtureMap(p);
    auto* layer = new CpuLayer();
    check(layer->fromMap(map), "native empty-texture layer initialization");
    auto* tileset = map->getTilesets().front();
    tileset->_imageSize = {512, 512};
    layer->metadataBatch(tileset);
    const auto size = (p.imageSize.isZero() ? p.size : p.imageSize) / p.scale;
    const auto imageOffset = p.imageOffset / p.scale;
    for (int y = 0; y < p.rows; ++y)
        for (int x = 0; x < p.columns; ++x)
        {
            const ax::Vec2 cell(x, y);
            const auto expected = position(p, cell + p.layerOffset);
            equal(layer->getPositionAt(cell) + layer->getPosition(), expected, "grid coordinate with layer offset");
            auto* sprite = new CpuSprite(size);
            for (uint32_t flag : {0U, static_cast<uint32_t>(ax::kTMXTileHorizontalFlag),
                                  static_cast<uint32_t>(ax::kTMXTileVerticalFlag),
                                  static_cast<uint32_t>(ax::kTMXTileDiagonalFlag | ax::kTMXTileHorizontalFlag),
                                  static_cast<uint32_t>(ax::kTMXTileDiagonalFlag | ax::kTMXTileVerticalFlag),
                                  static_cast<uint32_t>(ax::kTMXTileDiagonalFlag | ax::kTMXTileHorizontalFlag | ax::kTMXTileVerticalFlag), 0U})
            {
                layer->spriteLayout(sprite, cell, flag);
                const ax::Vec2 extent = flag & ax::kTMXTileDiagonalFlag ? ax::Vec2(size.height, size.width) : size;
                const auto bounds = sprite->getBoundingBox();
                equal(bounds.origin + layer->getPosition(), expected + ax::Vec2(imageOffset.x, -imageOffset.y),
                      "converted sprite matches tile origin/offset, including odd non-square dimensions");
                equal(bounds.size, extent, "converted sprite matches quad extent");
            }
            sprite->release();
        }

    // A viewport may see a tile whose origin is outside it (large tiles, shifts,
    // offsets, map edges). Check real index assembly against independent bounds.
    const bool staggered = p.orientation == ax::TMXOrientationHex || p.orientation == ax::TMXOrientationStaggered;
    if (staggered)
    {
        for (int y = 0; y < p.rows; ++y)
            for (int x = 0; x < p.columns; ++x)
            {
                const auto point = position(p, ax::Vec2(x, y) + p.layerOffset) - layer->getPosition() +
                                   ax::Vec2(imageOffset.x, -imageOffset.y);
                const ax::Rect view(point.x + size.width - 1.25f, point.y + size.height - 1.25f, .5f, .5f);
                auto ids = layer->visible(view);
                const std::set<int> unique(ids.begin(), ids.end());
                check(unique.size() == ids.size(), "visible index buffer has no duplicate tiles");
                for (int cy = 0; cy < p.rows; ++cy)
                    for (int cx = 0; cx < p.columns; ++cx)
                    {
                        const auto origin = position(p, ax::Vec2(cx, cy) + p.layerOffset) - layer->getPosition() +
                                            ax::Vec2(imageOffset.x, -imageOffset.y);
                        if (intersects(ax::Rect(origin, size), view))
                            check(unique.contains(cx + cy * p.columns), "culling retains every visible shifted/offset tile");
                    }
            }
        auto ids = layer->visible(ax::Rect(-10000, -10000, 20000, 20000));
        check(ids.size() == static_cast<size_t>(p.columns * p.rows), "full viewport contains all tiles");
        for (size_t i = 1; i < ids.size(); ++i)
        {
            const int previous = ids[i - 1], current = ids[i];
            check(previous / p.columns <= current / p.columns, "rows draw back-to-front");
            if (previous / p.columns == current / p.columns && p.axis == ax::TMXStaggerAxis_X)
                check(position(p, ax::Vec2(previous % p.columns, previous / p.columns) + p.layerOffset).y >=
                          position(p, ax::Vec2(current % p.columns, current / p.columns) + p.layerOffset).y,
                      "column parity draws back-to-front within a row");
        }
        layer->setupTiles();
        const auto& animated = *layer->getAnimTileCoord();
        check(animated.contains(1) && animated.at(1).size() == static_cast<size_t>(p.columns * p.rows),
              "odd/even-width animation registry covers all native tile coordinates");
        std::set<std::pair<int, int>> cells;
        for (const auto& frame : animated.at(1))
            cells.emplace(static_cast<int>(frame._tilePos.x), static_cast<int>(frame._tilePos.y));
        check(cells.size() == static_cast<size_t>(p.columns * p.rows), "animation coordinates are unique and complete");
    }
    layer->release();
    ax::Director::getInstance()->setContentScaleFactor(previousScale);
}

void originalMap(const std::string& file)
{
    ax::AutoreleasePool pool("unchanged hexa-test resource");
    auto* map = ax::TMXMapInfo::create(file);
    check(map && !map->getLayers().empty(), "unchanged original hexa-test.tmx parsed");
    check(map->getHexSideLength() == 88 && map->getStaggerAxis() == ax::TMXStaggerAxis_X &&
              map->getStaggerIndex() == ax::TMXStaggerIndex_Odd, "original Tiled layout parameters");
    auto* layer = new CpuLayer();
    check(layer->fromMap(map), "original resource native layer initialized");
    Layout p;
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x)
            equal(layer->getPositionAt(ax::Vec2(x, y)), position(p, ax::Vec2(x, y)), "original 25 tile positions");
    layer->release();
}
}

int main(int argc, char** argv)
{
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    ax::FileUtils::setDelegate(new NativeFiles());
#endif
    try
    {
        const auto original = argc > 1 ? std::filesystem::path(argv[1]) :
            std::filesystem::path(__FILE__).parent_path() / "fixtures/v3/res/TileMaps/hexa-test.tmx";
        originalMap(original.string());
        std::cout << "PASS unchanged original hexa-test.tmx 25 native positions\n";
        for (int orientation : {ax::TMXOrientationHex, ax::TMXOrientationStaggered})
            for (int axis : {ax::TMXStaggerAxis_X, ax::TMXStaggerAxis_Y})
                for (int index : {ax::TMXStaggerIndex_Odd, ax::TMXStaggerIndex_Even})
                    for (int width : {1, 2, 5, 6})
                        for (int offset : {0, 1, -1})
                        {
                            Layout p;
                            p.orientation = orientation; p.axis = axis; p.index = index;
                            p.columns = width; p.rows = 3;
                            p.side = axis == ax::TMXStaggerAxis_X ? 88 : 70;
                            p.layerOffset = ax::Vec2(offset, offset);
                            p.imageOffset = offset ? ax::Vec2(-37, 21) : ax::Vec2::zero;
                            layoutAndCulling(p);
                        }
        std::cout << "PASS hex/staggered X/Y odd/even, layer/tileset offsets, non-square tile sprites, culling and animation registries\n";
        for (int axis : {ax::TMXStaggerAxis_X, ax::TMXStaggerAxis_Y})
            for (int index : {ax::TMXStaggerIndex_Odd, ax::TMXStaggerIndex_Even})
                for (bool maximumSide : {false, true})
                    for (float scale : {1.f, 2.f})
                    {
                        Layout p;
                        p.axis = axis; p.index = index; p.columns = 6; p.rows = 1;
                        p.size = {157, 93}; p.imageSize = {203, 141};
                        p.side = maximumSide ? (axis == ax::TMXStaggerAxis_X ? 157 : 93) : 0;
                        p.layerOffset = {1, -1}; p.imageOffset = {113, -79}; p.scale = scale;
                        layoutAndCulling(p);
                    }
        std::cout << "PASS side-length extremes, one-row maps, large/offset tiles and content scale 1/2\n";
        for (int orientation : {ax::TMXOrientationOrtho, ax::TMXOrientationIso})
        {
            Layout p;
            p.orientation = orientation;
            layoutAndCulling(p);
        }
        std::cout << "PASS original orthogonal/isometric positions and converted sprite geometry\n";
        std::cout << "TMX native layout CPU tests PASS (" << assertions << " assertions); GPU pixels remain separate\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "TMX native layout failure after " << assertions << " assertions: " << error.what() << '\n';
        return 1;
    }
}
