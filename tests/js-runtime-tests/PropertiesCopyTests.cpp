/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "axmol/base/Properties.h"
#include "axmol/platform/FileUtils.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
void check(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
class NativeFiles final : public ax::FileUtils
{
public:
    explicit NativeFiles(const std::string& root)
    {
        _defaultResRootPath = root;
        _writablePath = root;
        check(init(), "Properties filesystem init");
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
}

int main()
{
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    char pattern[] = "/data/local/tmp/axmol-properties-copy-XXXXXX";
#else
    char pattern[] = "/tmp/axmol-properties-copy-XXXXXX";
#endif
    const char* directory = mkdtemp(pattern);
    if (!directory)
        return 1;
    const std::string root = std::string(directory) + "/";
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    ax::FileUtils::setDelegate(new NativeFiles(root));
#endif
    auto* files = ax::FileUtils::getInstance();
    const auto path = root + "variables.properties";
    int result = 1;
    try
    {
        check(files->writeStringToFile(R"PROPERTIES(
${rootValue} = root
group outer
{
    ${outerValue} = outer
    group selected
    {
        ${localValue} = local
        fromRoot = ${rootValue}
        fromOuter = ${outerValue}
        fromLocal = ${localValue}
        group nested
        {
            fromParent = ${localValue}
            fromRoot = ${rootValue}
        }
    }
}
group base
{
    group shared
    {
        owner = ${derivedValue}
    }
}
group derived : base
{
    ${derivedValue} = derived
    group added
    {
        owner = ${derivedValue}
    }
}
)PROPERTIES", path), "write Properties fixture");
        std::cout << "Properties copy test: detached #namespace keeps inherited and local variables\n";
        std::unique_ptr<ax::Properties> selected(ax::Properties::createNonRefCounted(path + "#outer/selected"));
        check(selected && selected->getString("fromRoot") == "root", "detached inherited root variable");
        check(selected->getString("fromOuter") == "outer", "detached nearest ancestor variable");
        check(selected->getString("fromLocal") == "local", "detached local variable");
        auto* nested = selected->getNamespace("nested");
        check(nested && nested->getString("fromParent") == "local" && nested->getString("fromRoot") == "root",
              "detached descendants use copied parent variables");
        selected->setVariable("rootValue", "updated-root");
        check(selected->getString("fromRoot") == "updated-root" && nested->getString("fromRoot") == "updated-root",
              "detached raw variable expression preserved");
#ifndef AXJS_PROPERTIES_BASELINE
        std::cout << "Properties copy test: public clone is independent and preserves property expressions\n";
        std::unique_ptr<ax::Properties> copied(selected->clone());
        auto* copiedNested = copied->getNamespace("nested");
        copied->setVariable("localValue", "copied-local");
        check(selected->getString("fromLocal") == "local" && copied->getString("fromLocal") == "copied-local",
              "clone local variables do not alias original");
        check(copiedNested && copiedNested->getString("fromParent") == "copied-local", "clone children use new parent");
        selected.reset();
        check(copied->getString("fromOuter") == "outer" && copiedNested->getString("fromRoot") == "updated-root",
              "clone remains usable after source deletion");
        std::cout << "Properties copy test: inherited and added namespaces point at the derived tree\n";
        std::unique_ptr<ax::Properties> tree(ax::Properties::createNonRefCounted(path));
        auto* derived = tree->getNamespace("derived");
        check(derived, "find derived namespace");
        auto* inherited = derived->getNamespace("shared");
        auto* added = derived->getNamespace("added");
        check(inherited && inherited->getString("owner") == "derived", "inherited namespace parent repaired");
        check(added && added->getString("owner") == "derived", "added namespace parent repaired");
        std::unique_ptr<ax::Properties> copiedTree(tree->clone());
        tree.reset();
        auto* copiedDerived = copiedTree->getNamespace("derived");
        check(copiedDerived && copiedDerived->getNamespace("shared")->getString("owner") == "derived",
              "full tree clone does not retain deleted original parents");
#endif
        std::cout << "Properties deep-copy regression PASS\n";
        result = 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Properties deep-copy regression FAIL: " << error.what() << '\n';
    }
    files->destroyInstance();
    std::filesystem::remove_all(root);
    return result;
}
