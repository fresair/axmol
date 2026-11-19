/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#pragma once

#include "Control.h"
#include <cmath>

NS_AX_EXT_BEGIN

namespace detail
{
// The caller supplies a pure picker-region predicate. Only the dispatcher may
// install a successful hit in the original event and establish capture/claim.
template <class Contains>
bool legacyControlTouchHitTest(Control* control, PointerEvent* event, Vec3* outHitPoint, const Contains& contains)
{
    if (!event || !control->isVisible())
        return false;

    const auto finite = [](const Vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    const auto& ray = event->getRay();
    if (!finite(ray.origin) || !finite(ray.direction) || ray.direction.z == 0.0f)
        return false;
    const float t = -ray.origin.z / ray.direction.z;
    if (!std::isfinite(t) || t < 0.0f)
        return false;
    const Vec3 point = ray.origin + ray.direction * t;
    if (!finite(point))
        return false;

    struct LocalHitEvent final : PointerEvent
    {
        void target(Node* node) { setCurrentTarget(node); }
    } local;
    local.copyDispatchStateFrom(*event);
    local.setCamera(event->getCamera());
    local.target(control);
    local.setHitResult(point, event->getCamera(), control);
    if (!contains(control->getTouchLocation(&local)))
        return false;
    if (outHitPoint)
        *outHitPoint = point;
    return true;
}
}  // namespace detail

NS_AX_EXT_END
