/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#pragma once
#include <jsapi.h>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace axjs
{
bool registerLegacyPhysics(JSContext*, JS::HandleObject);

// Native test diagnostics only. These functions are not registered in the JS API.
namespace tests
{
struct ChipmunkLifecycleWatch;
using ChipmunkLifecycleWatchPtr = std::shared_ptr<ChipmunkLifecycleWatch>;
inline constexpr uint32_t ChipmunkWatchMaxHandlers = 256;
inline constexpr uint32_t ChipmunkWatchMaxCallbacks = 1024;
enum class ChipmunkSpaceFreeReason : uint8_t { None, ExplicitFree, StateDestruction };
struct ChipmunkCallbackSnapshot
{
    bool captured{}, alive{};
    bool functionRootCaptured{}, receiverRootCaptured{};
    uint32_t functionRootReleaseCount{}, receiverRootReleaseCount{};
    bool ownerBoundCaptured{};
    uint32_t ownerBoundReleaseCount{};
};
struct ChipmunkHandlerSnapshot
{
    uint64_t typeA{}, typeB{};
    bool handlerAlive{}, stillRegistered{};
    // Original legacy handler order: begin, preSolve, postSolve, separate.
    std::array<ChipmunkCallbackSnapshot, 4> callbacks;
};
struct ChipmunkLifecycleSnapshot
{
    uint64_t generation{};
    bool runtimeAlive{}, liveStateInspected{};
    bool spaceStateAlive{}, spacePeerAlive{}, nativeSpaceAlive{};
    uint32_t explicitFreeCalls{}, nativeSpaceFreeCalls{};
    uint32_t spacePeerDestructions{}, spaceStateDestructions{};
    ChipmunkSpaceFreeReason firstNativeFreeReason{};
    uint32_t currentBodyCount{}, currentShapeCount{}, currentConstraintCount{}, currentHandlerCount{};
    std::vector<ChipmunkHandlerSnapshot> handlers;
};
/** Captures weak references and scalar facts, never a JS root or native lease.
 * Requires the live RuntimeState owner thread and an actual cp.Space peer.
 * Rejects more than 256 handlers or 1024 callbacks without partial observation. */
bool captureChipmunkLifecycleWatch(JSContext*, JS::HandleValue, ChipmunkLifecycleWatchPtr&);
/** With a live VM, inspects native state only on its owner thread. After VM
 * stop/expiration, returns sticky facts and captured identities without using
 * its context or locking native peers. Returns false for a null watch or a
 * live VM queried from a different thread. No function pumps or cleans up. */
bool snapshotChipmunkLifecycleWatch(const ChipmunkLifecycleWatchPtr&, ChipmunkLifecycleSnapshot&);
}
}
