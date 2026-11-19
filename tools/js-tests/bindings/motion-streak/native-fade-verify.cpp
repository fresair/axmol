// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Native CPU regression for the actual MotionStreak::update implementation.
// No graphics device, texture, JS callback or surrogate fade is used.
#include "axmol/2d/MotionStreak.h"
#include "axmol/base/AutoreleasePool.h"
#include "axmol/rhi/GraphicsCore.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <type_traits>

namespace
{
struct Fields : ax::MotionStreak
{
    static constexpr auto count() { return &Fields::_nuPoints; }
    static constexpr auto capacity() { return &Fields::_maxPoints; }
    static constexpr auto ages() { return &Fields::_pointState; }
    static constexpr auto points() { return &Fields::_pointVertexes; }
    static constexpr auto vertices() { return &Fields::_vertices; }
    static constexpr auto fade() { return &Fields::_fadeDelta; }
    static constexpr auto segment() { return &Fields::_minSeg; }
    static constexpr auto stroke() { return &Fields::_stroke; }
};
static_assert(std::is_same_v<decltype(Fields::count()), unsigned int ax::MotionStreak::*>);
static_assert(std::is_same_v<decltype(Fields::ages()), float* ax::MotionStreak::*>);
int checks{}, failures{};
void check(bool value, const char* message)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
void snapshot(const ax::MotionStreak& native, const char* name, bool fast)
{
    const auto count = native.*Fields::count();
    const auto* ages = native.*Fields::ages();
    const auto* points = native.*Fields::points();
    const auto& vertices = native.*Fields::vertices();
    std::cout << "MOTION_NATIVE_STATE {\"stage\":\"" << name << "\",\"fastMode\":" << (fast ? "true" : "false")
              << ",\"count\":" << count << ",\"points\":[";
    for (unsigned i = 0; i < count; ++i)
    {
        if (i) std::cout << ',';
        std::cout << "{\"x\":" << points[i].x << ",\"y\":" << points[i].y << ",\"life\":" << ages[i]
                  << ",\"leftAlpha\":" << int(vertices[2 * i].color.a)
                  << ",\"rightAlpha\":" << int(vertices[2 * i + 1].color.a) << '}';
    }
    std::cout << "]}\n";
}
std::unique_ptr<ax::MotionStreak> fixture(bool fast)
{
    // The actual base object owns these buffers and its normal destructor frees
    // them. Protected member pointers retain MotionStreak::* type; this is not
    // a downcast of a non-derived object. GPU allocation in initWithFade is not
    // needed to exercise the exact production age/compaction/geometry update.
    auto native = std::make_unique<ax::MotionStreak>();
    native.get()->*Fields::capacity() = 4;
    native.get()->*Fields::count() = 4;
    native.get()->*Fields::fade() = 0.5f;
    native.get()->*Fields::segment() = 9.0f;
    native.get()->*Fields::stroke() = 8.0f;
    native.get()->*Fields::ages() = static_cast<float*>(std::malloc(4 * sizeof(float)));
    native.get()->*Fields::points() = static_cast<ax::Vec2*>(std::malloc(4 * sizeof(ax::Vec2)));
    auto* ages = native.get()->*Fields::ages();
    auto* points = native.get()->*Fields::points();
    if (!ages || !points) throw std::bad_alloc();
    const float life[] = {0.1f, 0.6f, 0.9f, 1.0f};
    const ax::Vec2 coordinates[] = {{0, 0}, {20, 0}, {40, 10}, {60, 20}};
    auto& vertices = native.get()->*Fields::vertices();
    vertices.resize(8);
    for (unsigned i = 0; i < 4; ++i)
    {
        ages[i] = life[i]; points[i] = coordinates[i];
        vertices[2 * i].position = {coordinates[i].x, coordinates[i].y - 4};
        vertices[2 * i + 1].position = {coordinates[i].x, coordinates[i].y + 4};
        vertices[2 * i].color = vertices[2 * i + 1].color = ax::Color32(255, 255, 255, 255);
    }
    native->setFastMode(fast);
    native->setPosition(60, 20);
    return native;
}
void verifyAlphas(const ax::MotionStreak& native, const float* life, unsigned count)
{
    check(native.*Fields::count() == count, "actual surviving point count");
    const auto* ages = native.*Fields::ages(); const auto& vertices = native.*Fields::vertices();
    for (unsigned i = 0; i < count; ++i)
    {
        check(std::abs(ages[i] - life[i]) < 1e-6f, "actual point life equals elapsed fade");
        // Permit the one-byte float-roundoff at an integer boundary, never an
        // opaque right edge. Both real vertices must equal each other exactly.
        const int expected = static_cast<int>(life[i] * 255.0f);
        check(std::abs(int(vertices[2 * i].color.a) - expected) <= 1, "left ribbon alpha follows actual point life");
        check(std::abs(int(vertices[2 * i + 1].color.a) - expected) <= 1, "right ribbon alpha follows actual point life");
        check(vertices[2 * i].color.a == vertices[2 * i + 1].color.a, "both ribbon edges fade equally");
        check(std::isfinite(vertices[2 * i].position.x) && std::isfinite(vertices[2 * i + 1].position.y),
              "actual generated ribbon geometry stays finite");
    }
}
void run(bool fast)
{
    auto native = fixture(fast);
    snapshot(*native, "initial", fast);
    native->setStartingPositionInitialized(false);
    native->update(0.5f);
    const float initial[] = {0.1f, 0.6f, 0.9f, 1.0f};
    check((native.get()->*Fields::ages())[0] == initial[0] && (native.get()->*Fields::count()) == 4,
          "uninitialized starting position leaves ribbon unchanged");
    native->setStartingPositionInitialized(true);
    native->update(0.5f);
    snapshot(*native, "after-first-expiration-and-compaction", fast);
    const float first[] = {0.35f, 0.65f, 0.75f}; verifyAlphas(*native, first, 3);
    check((native.get()->*Fields::points())[0] == ax::Vec2(20, 0), "compaction retains the actual next point");
    native->tintWithColor(ax::Color32(32, 96, 160, 255));
    native->update(0.2f);
    snapshot(*native, "tinted-both-edges-fade", fast);
    const float tinted[] = {0.25f, 0.55f, 0.65f}; verifyAlphas(*native, tinted, 3);
    const auto& vertices = native.get()->*Fields::vertices();
    for (unsigned i = 0; i < 6; ++i)
        check(vertices[i].color.r == 32 && vertices[i].color.g == 96 && vertices[i].color.b == 160,
              "fading preserves actual tint on both ribbon edges");
    native->setPosition(80, 40);
    native->update(0);
    snapshot(*native, "real-new-point-appended", fast);
    const float appended[] = {0.25f, 0.55f, 0.65f, 1.0f}; verifyAlphas(*native, appended, 4);
    check((native.get()->*Fields::points())[3] == ax::Vec2(80, 40), "new point uses actual native setPosition");
    native->update(2.2f);
    snapshot(*native, "natural-fade-no-drawable-ribbon", fast);
    // The native algorithm legitimately seeds a single stationary point when
    // all older points die; draw() requires more than one point. Do not demand
    // an invented zero-point state or reset the ribbon to manufacture a pass.
    const float single[] = {1.0f}; verifyAlphas(*native, single, 1);
    native->update(1.0f);
    snapshot(*native, "stationary-single-point-aged", fast);
    const float aged[] = {0.5f}; verifyAlphas(*native, aged, 1);
    native->update(1.0f);
    snapshot(*native, "stationary-reseed-remains-nondrawable", fast);
    verifyAlphas(*native, single, 1);
    check(ax::rhi::GraphicsCore::device() == nullptr, "CPU fade proof never initializes a graphics device");
}
}
int main()
{
    ax::AutoreleasePool pool;
    run(true); run(false);
    std::cout << "MOTION_NATIVE_FADE_RESULT checks=" << checks << " failures=" << failures
              << " modes=2 graphicsInitialized=" << (ax::rhi::GraphicsCore::device() ? "true" : "false") << '\n';
    return failures ? 65 : 0;
}
