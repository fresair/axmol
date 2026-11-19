// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
#include "axmol/physics/PhysicsActor.h"
#if defined(AX_ENABLE_PHYSICS_3D)
#include "axmol/physics/3d/Rigidbody3D.h"
#include "axmol/physics/3d/PhysicsWorld3D.h"
#endif

namespace ax
{
void PhysicsActor::setSyncFlag(SyncFlag flag)
{
    if (_syncFlag == flag)
        return;
#if defined(AX_ENABLE_PHYSICS_3D)
    if (auto* body = dynamic_cast<Rigidbody3D*>(this))
    {
        if (auto* world = body->getWorld())
            world->invalidateKinematicFrameTarget(body);
    }
#endif
    _syncFlag = flag;
}

bool PhysicsActor::isEventEnabled(ContactEventBits events) const
{
    return bitmask::only(_eventBits, events);
}

void PhysicsActor::setEventEnabled(ContactEventBits events, bool enabled)
{
    if (enabled)
        _eventBits |= events;
    else
        _eventBits &= ~events;
}

}  // namespace ax
