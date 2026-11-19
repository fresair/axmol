/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "generated/axjs_register_gen.h"
#include "manual/Bindings.h"
#include "manual/FileBindings.h"
#include "runtime/JsRuntime.h"
#include "axmol/base/AutoreleasePool.h"
#include "axmol/platform/FileUtils.h"
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
class NativeFiles final : public ax::FileUtils {
public:
    explicit NativeFiles(const std::string& root)
    {
        _defaultResRootPath = root;
        _writablePath = root;
        check(init(), "native file test filesystem init");
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
bool registration(JSContext* cx, JS::HandleObject global)
{
    return axjs::registerAllGenerated(cx, global) && axjs::registerCore(cx, global) && axjs::registerFiles(cx, global);
}
std::string read(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}
void eval(ax::JsEngine* engine, std::string_view source)
{
    check(engine->evalString(source), "file JavaScript assertion failed");
}
}
int main()
{
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    char pattern[] = "/data/local/tmp/axmol-js-files-XXXXXX";
#else
    char pattern[] = "/tmp/axmol-js-files-XXXXXX";
#endif
    const auto* temporary = mkdtemp(pattern);
    if (!temporary) { std::cerr << "Cannot create file test directory\n"; return 1; }
    const std::string root = std::string(temporary) + "/";
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    ax::FileUtils::setDelegate(new NativeFiles(root));
#endif
    ax::FileUtils::getInstance()->setWritablePath(root);
    auto* engine = ax::JsEngine::getInstance();
    std::vector<std::string> errors;
    engine->setErrorHandler([&](std::string_view text) { errors.emplace_back(text); });
    engine->addRegisterCallback(registration);
    ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
    try
    {
        ax::AutoreleasePool pool("JavaScript FileUtils");
        check(engine->start(), "file runtime start");
        eval(engine, R"JS(
            function expect(ok,message){if(!ok)throw Error(message);}
            var jsb={fileUtils:cc.FileUtils.getInstance()},files=jsb.fileUtils;
            var writablePath=files.getWritablePath()+'cocos/fileUtilTest/';
            expect(files.createDirectory(writablePath),'createDirectory legacy alias');
        )JS");
        std::cout << "File test: original binary TestWriteData payload\n";
        eval(engine, R"JS(
            var writeData=new Uint8Array(8);for(var i=0;i<writeData.length;i++)writeData[i]=i;
            var fullPath=writablePath+'writeDataTest.bin';
            expect(files.writeDataToFile(writeData,fullPath),'binary write');
            var readData=files.getDataFromFile(fullPath);
            expect(readData instanceof Uint8Array&&readData.length===writeData.length,'legacy Uint8Array result');
            for(var i=0;i<readData.length;i++)expect(readData[i]===writeData[i],'binary byte '+i);
        )JS");
        check(read(root + "cocos/fileUtilTest/writeDataTest.bin") == std::string("\0\1\2\3\4\5\6\7", 8), "actual original binary file bytes");
        std::cout << "File test: original string TestWriteString payload\n";
        eval(engine, R"JS(
            var writeDataStr='the string data will be write into a file';
            var fullPath=writablePath+'writeStringTest.txt';
            expect(files.writeStringToFile(writeDataStr,fullPath),'string write');
            expect(files.getStringFromFile(fullPath)===writeDataStr,'string read');
        )JS");
        std::cout << "File test: original dictionary TestWriteValueMap payload\n";
        eval(engine, R"JS(
            var valueMap={data0:{string1:'string in dictInMap key 0',string2:'string in dictInMap key 1'},
                data1:'string in array',data2:['string 0 in arrayInMap','string 1 in arrayInMap'],
                data3:true,data4:1024,data5:1024.1024,data6:1024.123};
            var fullPath=writablePath+'testWriteValueMap.plist';
            expect(files.writeValueMapToFile(valueMap,fullPath),'map write');
            var readValueMap=files.getValueMapFromFile(fullPath);
            expect(readValueMap.data0.string1===valueMap.data0.string1&&readValueMap.data0.string2===valueMap.data0.string2,'nested map');
            expect(readValueMap.data1===valueMap.data1&&readValueMap.data2.join('|')===valueMap.data2.join('|'),'map string/array');
            expect(readValueMap.data3===true&&readValueMap.data4===1024&&Math.abs(readValueMap.data5-1024.1024)<0.00001&&Math.abs(readValueMap.data6-1024.123)<0.00001,'map primitive types');
        )JS");
        check(read(root + "cocos/fileUtilTest/testWriteValueMap.plist").find("<dict>") != std::string::npos, "actual plist dictionary");
        std::cout << "File test: original array TestWriteValueVector payload\n";
        eval(engine, R"JS(
            var array=[{string1:'string in dictInArray key 0',string2:'string in dictInArray key 1'},
                'string in array',['string 0 in arrayInArray','string 1 in arrayInArray'],true,1024,1024.1024,1024.123];
            var fullPath=writablePath+'testWriteValueVector.plist';
            expect(files.writeValueVectorToFile(array,fullPath),'vector write');
            var readArray=files.getValueVectorFromFile(fullPath);
            expect(readArray.length===array.length&&readArray[0].string1===array[0].string1&&readArray[0].string2===array[0].string2,'nested vector map');
            expect(readArray[1]===array[1]&&readArray[2].join('|')===array[2].join('|')&&readArray[3]===true&&readArray[4]===1024,'vector primitive values');
            expect(Math.abs(readArray[5]-array[5])<0.00001&&Math.abs(readArray[6]-array[6])<0.00001,'vector doubles');
        )JS");
        std::cout << "File test: views, UTF8/NUL, empty/missing files and validation\n";
        eval(engine, R"JS(
            var data=new Uint8Array([9,0,255,1,42,8]),binaryPath=writablePath+'offset.bin';
            expect(files.writeDataToFile(data.subarray(1,5),binaryPath),'offset view write');
            expect(Array.prototype.join.call(files.getDataFromFile(binaryPath),',')==='0,255,1,42','offset preserved');
            expect(files.writeDataToFile(new DataView(data.buffer,1,4),binaryPath),'DataView write');
            expect(Array.prototype.join.call(files.getDataFromFile(binaryPath),',')==='0,255,1,42','DataView bytes');
            expect(files.writeDataToFile(data.buffer,binaryPath)&&files.getDataFromFile(binaryPath).length===6,'ArrayBuffer write');
            var text='文件\0内容';expect(files.writeStringToFile(text,writablePath+'utf8.txt')&&files.getStringFromFile(writablePath+'utf8.txt')===text,'UTF8/NUL');
            expect(files.writeDataToFile(new Uint8Array(0),writablePath+'empty.bin')&&files.getDataFromFile(writablePath+'empty.bin')===null,'legacy empty read null');
            expect(files.getDataFromFile(writablePath+'missing.bin')===null,'missing read null');
            var failed=false;try{files.writeDataToFile('bad',binaryPath);}catch(e){failed=true;}expect(failed,'reject nonbinary input');
            var aliasPath=writablePath+'alias.plist';expect(files.writeToFile(valueMap,aliasPath),'writeToFile legacy alias');
            expect(files.getValueMapFromData(files.getDataFromFile(aliasPath)).data0.string1===valueMap.data0.string1,'binary plist parse');
            expect(files.getValueMapFromData(files.getStringFromFile(aliasPath)).data4===1024,'string plist parse');
            failed=false;var cycle={};cycle.self=cycle;try{files.writeValueMapToFile(cycle,aliasPath);}catch(e){failed=true;}expect(failed,'cyclic map rejected');
        )JS");
        engine->garbageCollect();
        std::cout << "File test: original loadAliases lookup plist and cache invalidation\n";
        eval(engine, R"JS(
            var images=files.getWritablePath()+'Images/';expect(files.createDirectory(images),'alias image directory');
            // The original lookup-desktop.plist maps this exact filename to a PVR.
            // Disk resolution is tested without instantiating a GPU texture.
            expect(files.writeDataToFile(new Uint8Array([1,2,3]),images+'grossini_pvr_rgba8888.pvr'),'real alias target file');
            expect(files.writeDataToFile(new Uint8Array([4,5]),images+'alternative.pvr'),'alternate alias target');
            var originalPaths=files.getSearchPaths();files.addSearchPath(files.getWritablePath(),true);
            var lookup='<?xml version="1.0" encoding="UTF-8"?>\n'+
                '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n'+
                '<plist version="1.0"><dict><key>metadata</key><dict><key>version</key><integer>1</integer></dict>'+
                '<key>filenames</key><dict><key>grossini.bmp</key><string>Images/grossini_pvr_rgba8888.pvr</string></dict></dict></plist>';
            expect(files.writeStringToFile(lookup,images+'lookup-desktop.plist'),'original-format lookup plist');
            files.loadFilenameLookup('Images/lookup-desktop.plist');
            expect(files.fullPathForFilename('grossini.bmp')===images+'grossini_pvr_rgba8888.pvr','native original alias resolution');
            expect(files.getDataFromFile('grossini.bmp')[0]===1,'alias applies to native binary reads');
            files.setFilenameLookupDictionary({'grossini.bmp':'Images/alternative.pvr'});
            expect(files.fullPathForFilename('grossini.bmp')===images+'alternative.pvr'&&files.getDataFromFile('grossini.bmp')[0]===4,'replace alias invalidates cached path');
            files.setFilenameLookupDictionary({});
            expect(files.fullPathForFilename('grossini.bmp')==='','reset alias invalidates cached path');
            files.setSearchPaths(originalPaths);
        )JS");
        check(errors.empty(), errors.empty() ? "" : errors.back());
        engine->stop();
        pool.clear();
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::cout << "JavaScript FileUtils original-payload and binary/plist tests passed\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "File tests failed: " << failure.what() << '\n';
        for (const auto& error : errors)
            std::cerr << "JavaScript: " << error << '\n';
        engine->stop();
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return 1;
    }
}
