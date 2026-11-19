// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Run inside the real Android JNI application with its APK AssetManager.
// This fixture changes only FileUtils settings and restores them before return.
function runJsFileAssetsSmoke() {
    var files = cc.FileUtils.getInstance();
    var savedRoot = files.getDefaultResourceRootPath();
    var savedSearch = files.getSearchPaths().slice(0);
    var failures = [], groups = 0;
    var temporary = null;
    function expect(ok, message) { if (!ok) throw new Error(message); }
    function run(name, callback) {
        groups++;
        try { callback(); cc.log("FILE_ASSETS PASS " + name); }
        catch (error) {
            var detail = String(error) + (error.stack ? "\n" + error.stack : "");
            failures.push(name + ": " + detail);
            cc.log("FILE_ASSETS FAIL " + failures[failures.length - 1]);
        }
    }
    function textFile(name, prefix) {
        expect(files.isFileExist(name), "APK asset exists: " + name);
        var value = files.getStringFromFile(name), data = files.getDataFromFile(name);
        expect(value.indexOf(prefix) === 0 && data && data.length > 0, "original asset bytes: " + name);
        expect(files.getFileSize(name) === data.length, "relative size matches bytes: " + name);
        var path = files.fullPathForFilename(name);
        expect(path && files.getFileSize(path) === data.length, "resolved size matches bytes: " + path);
        cc.log("FILE_ASSETS resolved " + name + " -> " + path + " bytes=" + data.length);
    }
    try {
        run("default assets root and relative script", function () {
            files.setDefaultResourceRootPath("assets/"); files.setSearchPaths([]);
            var data = files.getDataFromFile("script/jsb_boot.js");
            expect(data && data.length > 0 && files.isFileExist("script/jsb_boot.js"), "default real boot asset");
            expect(files.getFileSize("script/jsb_boot.js") === data.length, "default script size/read");
        });
        run("relative original material and native Properties", function () {
            files.addSearchPath("legacy/js-tests/res", true);
            textFile("Materials/2d_effects.material", "material sample");
            var properties = cc.Properties.createNonRefCounted("Materials/2d_effects.material#sample");
            expect(properties && properties.getId() === "sample", "original relative Properties URL");
        });
        run("custom resource root", function () {
            files.setDefaultResourceRootPath("assets/legacy/js-tests/"); files.setSearchPaths([]);
            textFile("res/Materials/2d_effects.material", "material sample");
        });
        run("explicit assets/script with custom root", function () {
            expect(files.isAbsolutePath("assets/script/jsb_boot.js"), "explicit assets path remains absolute");
            expect(files.fullPathForFilename("assets/script/jsb_boot.js") === "assets/script/jsb_boot.js", "explicit path preserved");
            var data = files.getDataFromFile("assets/script/jsb_boot.js");
            expect(data && data.length > 0 && files.getFileSize("assets/script/jsb_boot.js") === data.length,
                "explicit script size/read");
        });
        run("original Particle3D nested search paths", function () {
            files.addSearchPath("res/Particle3D/scripts", true); files.addSearchPath("res/Particle3D/materials", true);
            var data = files.getDataFromFile("explosionSystem.pu");
            expect(data && data.length > 0 && files.getFileSize("explosionSystem.pu") === data.length, "original PU script bytes/size");
            data = files.getDataFromFile("pu_mediapack_01.material");
            expect(data && data.length > 0 && files.getFileSize("pu_mediapack_01.material") === data.length, "original PU material bytes/size");
        });
        run("original JS CCBI binary variant", function () {
            files.addSearchPath("res/resjs", true);
            var data = files.getDataFromFile("ccb/HelloCocosBuilder.ccbi");
            expect(data && data.length === 3286 && data[5] === 1, "complete original JS CCBI flag/bytes");
            expect(files.getFileSize("ccb/HelloCocosBuilder.ccbi") === data.length, "binary CCBI size");
        });
        run("writable absolute filesystem path", function () {
            temporary = files.getWritablePath() + "js-file-assets-regression.txt";
            expect(files.writeStringToFile("absolute native filesystem", temporary), "write absolute native file");
            expect(files.isAbsolutePath(temporary), "external file remains absolute");
            textFile(temporary, "absolute native filesystem");
            expect(files.removeFile(temporary), "remove temporary native file"); temporary = null;
        });
        run("empty/missing paths and root restoration", function () {
            expect(!files.isAbsolutePath("") && files.getFileSize("") === -1, "empty metadata");
            expect(!files.isFileExist("absent-js-file-assets.material") && files.getFileSize("absent-js-file-assets.material") === -1,
                "missing APK metadata");
            files.setDefaultResourceRootPath(savedRoot); files.setSearchPaths(savedSearch); files.purgeCachedEntries();
            expect(files.getDefaultResourceRootPath() === savedRoot, "restore original resource root");
            expect(files.getDataFromFile("assets/script/jsb_boot.js").length > 0, "restored explicit script read");
        });
    } finally {
        if (temporary) files.removeFile(temporary);
        files.setDefaultResourceRootPath(savedRoot); files.setSearchPaths(savedSearch); files.purgeCachedEntries();
    }
    cc.log("FILE_ASSETS " + (failures.length ? "FAILED" : "PASS") + " groups=" + groups + " failures=" + failures.length);
    return failures.length ? 64 : 0;
}
