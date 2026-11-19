/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
// Cocos2d-x v3 FileUtils compatibility. Binary reads retain the original
// Uint8Array result; all IO and plist serialization use the current engine.
#include "FileBindings.h"
#include "ManualUtils.h"
#include <climits>
#include <cstring>
#include <limits>

namespace axjs {
namespace {
using namespace manual;

bool readBytes(JSContext* cx, JS::HandleValue value, ax::Data& out)
{
    if (!value.isObject())
        return error(cx, "Binary data must be an ArrayBuffer or a typed array view");
    JS::RootedObject object(cx, &value.toObject());
    uint32_t size{};
    uint8_t* data{};
    if (JS_IsArrayBufferObject(object))
    {
        size = JS_GetArrayBufferByteLength(object);
        data = JS_GetArrayBufferData(object);
    }
    else if (JS_IsArrayBufferViewObject(object))
    {
        size = JS_GetArrayBufferViewByteLength(object);
        data = static_cast<uint8_t*>(JS_GetArrayBufferViewData(object));
    }
    else
        return error(cx, "Binary data must be an ArrayBuffer or a typed array view");
    if (size && !data)
        return error(cx, "Binary buffer is detached");
    if (size && out.copy(data, size) != size)
        return error(cx, "Cannot copy binary file data");
    return true;
}

bool getData(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* files = self<ax::FileUtils>(cx, args);
    if (!files)
        return false;
    std::string path;
    if (argc != 1 || !argument(cx, args, 0, path))
        return error(cx, "getDataFromFile requires a filename");
    auto data = files->getDataFromFile(path);
    if (data.isNull())
    {
        args.rval().setNull();
        return true;
    }
    if (data.getSize() < 0 || static_cast<uint64_t>(data.getSize()) > UINT32_MAX)
        return error(cx, "File is too large for a JavaScript typed array");
    JS::RootedObject array(cx, JS_NewUint8Array(cx, static_cast<uint32_t>(data.getSize())));
    if (!array)
        return false;
    if (data.getSize())
        std::memcpy(JS_GetArrayBufferViewData(array), data.getBytes(), static_cast<size_t>(data.getSize()));
    args.rval().setObject(*array);
    return true;
}

bool writeData(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* files = self<ax::FileUtils>(cx, args);
    if (!files)
        return false;
    ax::Data data;
    std::string path;
    if (argc != 2 || !argument(cx, args, 1, path) || !readBytes(cx, args[0], data))
        return error(cx, "writeDataToFile requires binary data and a filename");
    if (!data.getSize())
    {
        // The native writeBinaryToFile assertion requires a nonempty payload.
        // Opening the same engine stream creates/truncates an empty file safely.
        args.rval().setBoolean(static_cast<bool>(files->openFileStream(path, ax::IFileStream::Mode::WRITE)));
        return true;
    }
    args.rval().setBoolean(files->writeDataToFile(data, path));
    return true;
}

bool getMap(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* files = self<ax::FileUtils>(cx, args);
    if (!files)
        return false;
    std::string path;
    if (argc != 1 || !argument(cx, args, 0, path))
        return error(cx, "getValueMapFromFile requires a filename");
    return toJsValue(cx, files->getValueMapFromFile(path), args.rval());
}

bool writeMap(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* files = self<ax::FileUtils>(cx, args);
    if (!files)
        return false;
    ax::ValueMap map;
    std::string path;
    if (argc != 2 || !argument(cx, args, 0, map) || !argument(cx, args, 1, path))
        return error(cx, "writeValueMapToFile requires a dictionary and a filename");
    args.rval().setBoolean(files->writeValueMapToFile(map, path));
    return true;
}

bool getMapFromData(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* files = self<ax::FileUtils>(cx, args);
    if (!files)
        return false;
    if (!argc || argc > 2)
        return error(cx, "getValueMapFromData requires plist bytes and an optional byte length");
    std::string text;
    ax::Data data;
    const char* bytes{};
    size_t length{};
    if (args[0].isString())
    {
        if (!fromJsValue(cx, args[0], text))
            return false;
        bytes = text.data();
        length = text.size();
    }
    else
    {
        if (!readBytes(cx, args[0], data))
            return false;
        bytes = reinterpret_cast<const char*>(data.getBytes());
        length = static_cast<size_t>(data.getSize());
    }
    if (argc == 2)
    {
        uint32_t requested{};
        if (!argument(cx, args, 1, requested) || requested > length)
            return error(cx, "Plist byte length exceeds the supplied buffer");
        length = requested;
    }
    if (length > INT_MAX)
        return error(cx, "Plist data is too large");
    if (!length)
        return toJsValue(cx, ax::ValueMap{}, args.rval());
    return toJsValue(cx, files->getValueMapFromData(bytes, static_cast<int>(length)), args.rval());
}

bool createDirectory(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* files = self<ax::FileUtils>(cx, args);
    if (!files)
        return false;
    std::string path;
    if (argc != 1 || !argument(cx, args, 0, path))
        return error(cx, "createDirectory requires a path");
    args.rval().setBoolean(files->createDirectories(path));
    return true;
}
bool loadFilenameLookup(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    return invoke<&ax::FileUtils::loadFilenameLookup>(cx, args);
}
bool setFilenameLookup(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    return invoke<&ax::FileUtils::setFilenameLookupDictionary>(cx, args);
}
bool setSearchPaths(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    auto* files = self<ax::FileUtils>(cx, args);
    if (!files)
        return false;
    std::vector<std::string> paths;
    if (argc != 1 || !argument(cx, args, 0, paths))
        return error(cx, "setSearchPaths requires an array of paths");
    // The engine's current API is a range template and therefore is not a
    // concrete method in the generated binding declarations.
    files->setSearchPaths(std::move(paths));
    args.rval().setUndefined();
    return true;
}
}  // namespace

bool registerFiles(JSContext* cx, JS::HandleObject)
{
    return method<ax::FileUtils>(cx, "getDataFromFile", getData, 1) &&
           method<ax::FileUtils>(cx, "writeDataToFile", writeData, 2) &&
           method<ax::FileUtils>(cx, "getValueMapFromFile", getMap, 1) &&
           method<ax::FileUtils>(cx, "writeValueMapToFile", writeMap, 2) &&
           method<ax::FileUtils>(cx, "writeToFile", writeMap, 2) &&
           method<ax::FileUtils>(cx, "getValueMapFromData", getMapFromData, 1) &&
           method<ax::FileUtils>(cx, "createDirectory", createDirectory, 1) &&
           method<ax::FileUtils>(cx, "loadFilenameLookup", loadFilenameLookup, 1) &&
           method<ax::FileUtils>(cx, "setFilenameLookupDictionary", setFilenameLookup, 1) &&
           method<ax::FileUtils>(cx, "setSearchPaths", setSearchPaths, 1);
}
}  // namespace axjs
