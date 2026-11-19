/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/Bindings.h"
#include "runtime/JsRuntime.h"
#include "axmol/base/AutoreleasePool.h"
#include "axmol/platform/FileUtils.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
void check(bool ok, const char* text)
{
    if (!ok)
        throw std::runtime_error(text);
}
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
class NativeFiles final : public ax::FileUtils
{
public:
    explicit NativeFiles(const std::string& root)
    {
        _defaultResRootPath = root;
        _writablePath = root;
        check(init(), "Material preflight filesystem init");
    }
    std::string getWritablePath() const override { return _writablePath; }
    std::string getNativeWritableAbsolutePath() const override { return _writablePath; }
    bool isFileExistInternal(std::string_view path) const override
    {
        struct stat data{};
        return stat(std::string(path).c_str(), &data) == 0 && S_ISREG(data.st_mode);
    }
};
#endif
void eval(ax::JsEngine* engine, std::string_view text)
{
    check(engine->evalString(text), "Material preflight JavaScript assertion failed");
}
}

int main()
{
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    char pattern[] = "/data/local/tmp/axmol-material-preflight-XXXXXX";
#else
    char pattern[] = "/tmp/axmol-material-preflight-XXXXXX";
#endif
    const char* directory = mkdtemp(pattern);
    if (!directory)
        return 1;
    const std::string root = std::string(directory) + "/";
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    ax::FileUtils::setDelegate(new NativeFiles(root));
#endif
    auto* files = ax::FileUtils::getInstance();
    files->setWritablePath(root);
    auto* engine = ax::JsEngine::getInstance();
    engine->addRegisterCallback(axjs::registerAll);
    ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
    int result = 1;
    try
    {
        ax::AutoreleasePool pool("JS Material preflight");
        check(engine->start(), "Material preflight VM initialization");
        eval(engine, R"JS(
            function expect(ok, text) { if (!ok) throw new Error(text); }
            function fails(callback, pattern) {
                var failure = null;
                try { callback(); } catch (error) { failure = String(error); }
                expect(failure !== null, 'expected native Material error');
                if (pattern) expect(pattern.test(failure), 'unexpected native error: ' + failure);
            }
            var files = cc.FileUtils.getInstance(), root = files.getWritablePath();
            function material(vertex, fragment) {
                return '${vertex} = ' + vertex + '\nmaterial sample\n{\n technique one\n {\n pass 0\n {\n shader\n {\n' +
                    'vertexShader = ${vertex}\nfragmentShader = ' + fragment + '\ndefines = UNARCHIVED_PREFLIGHT 1\n' +
                    '}\n}\n}\n technique two\n {\n}\n}\n';
            }
            function source(properties) {
                return properties.getNamespace('one', false, false).getNamespace('0', false, false)
                    .getNamespace('shader', true, false);
            }
        )JS");
        std::cout << "Material preflight test: native arguments and receiver validation\n";
        eval(engine, R"JS(
            fails(function () { cc.Material.createWithProperties(); });
            fails(function () { cc.Material.createWithProperties(null); });
            fails(function () { cc.Material.createWithProperties({}); });
            fails(function () { cc.Material.createWithFilename({}); });
            fails(function () { cc.Pass.prototype.getGLProgramState.call({}); });
        )JS");
        std::cout << "Material preflight test: missing and empty filename factory returns null\n";
        eval(engine, R"JS(
            expect(cc.Material.createWithFilename(root + 'missing.material') === null, 'missing material null');
            expect(files.writeStringToFile('', root + 'empty.material'), 'write empty material');
            expect(cc.Material.createWithFilename(root + 'empty.material') === null, 'empty material null');
        )JS");
        std::cout << "Material preflight test: missing/empty shaders fail before native GPU load\n";
        eval(engine, R"JS(
            expect(files.writeStringToFile('void main(){gl_FragColor=vec4(1.0);}', root + 'fragment.glsl'), 'fragment file');
            expect(files.writeStringToFile(material(root + 'missing.glsl', root + 'fragment.glsl'), root + 'missing-shader.material'), 'material file');
            var properties = cc.Properties.createNonRefCounted(root + 'missing-shader.material#sample');
            expect(properties, 'actual parsed properties');
            fails(function () { cc.Material.createWithProperties(properties); }, /shader file is missing/);
            expect(files.writeStringToFile('', root + 'empty.glsl'), 'empty shader');
            properties.setVariable('vertex', root + 'empty.glsl');
            fails(function () { cc.Material.createWithProperties(properties); }, /shader file is empty/);
        )JS");
        std::cout << "Material preflight test: unmatched source/defines and original Properties preservation\n";
        eval(engine, R"JS(
            expect(files.writeStringToFile('void main(){gl_Position=vec4(0.0);}', root + 'vertex.glsl'), 'vertex file');
            properties.setVariable('vertex', root + 'vertex.glsl');
            var shader = source(properties), vertex = shader.getString('vertexShader'), fragment = shader.getString('fragmentShader');
            var definitions = shader.getString('defines');
            properties.rewind();
            expect(properties.getNextNamespace().getId() === 'one', 'caller namespace cursor');
            fails(function () { cc.Material.createWithProperties(properties); }, /no compiled legacy archive/);
            expect(shader.getString('vertexShader') === vertex && shader.getString('fragmentShader') === fragment &&
                shader.getString('defines') === definitions, 'caller shader settings preserved after failure');
            expect(properties.getNextNamespace().getId() === 'two', 'caller namespace cursor preserved after failure');
            properties.setVariable('vertex', root + 'changed.glsl');
            expect(shader.getString('vertexShader') === root + 'changed.glsl', 'raw variable expression preserved after failure');
            fails(function () { cc.Material.createWithProperties(properties); }, /shader file is missing/);
        )JS");
        engine->garbageCollect();
        engine->stop();
        std::cout << "JavaScript native Material preflight tests passed (no graphics rendering claimed)\n";
        result = 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Material preflight FAIL: " << error.what() << '\n';
        engine->stop();
    }
    files->destroyInstance();
    std::filesystem::remove_all(root);
    return result;
}
