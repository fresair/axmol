/****************************************************************************
Copyright (c) 2008-2010 Ricardo Quesada
Copyright (c) 2009      Valentin Milea
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2011      Zynga Inc.
Copyright (c) 2013-2016 Chukong Technologies Inc.
Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
Copyright (c) 2019-present Simdsoft Limited.
Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

https://axmol.dev/

SPDX-License-Identifier: MIT
****************************************************************************/

#include "axmol/2d/ActionManager.h"
#include "axmol/scene/Node.h"
#include "axmol/2d/Action.h"
#include "axmol/base/Scheduler.h"
#include "axmol/base/Macros.h"
#include "axmol/base/WeakPtr.h"
#include <iterator>
#include <utility>
#include <vector>

namespace ax
{
//
// singleton stuff
//

ActionManager::ActionManager() : _currentTarget(nullptr), _currentTargetSalvaged(false) {}

ActionManager::~ActionManager()
{
    AXLOGV("deallocing ActionManager: {}", fmt::ptr(this));
    _destroying = true;
    removeAllActions();
}

// private
void ActionManager::reserveActionCapacity(ActionHandle& element)
{
    // 4 actions per Node by default
    if (!element.actions.capacity())
    {
        element.actions.reserve(4);
    }
    else if (element.actions.size() == element.actions.capacity())
    {
        element.actions.reserve(element.actions.size() * 2);
    }
}
void ActionManager::removeActionAtIndex(ssize_t index,
                                        ActionHandle& element,
                                        std::unordered_map<Node*, ActionHandle>::iterator actionIt)
{
    Action* action = static_cast<Action*>(element.actions[index]);
    // Vector::erase releases before erasing its slot. Keep the object alive
    // until the slot/index bookkeeping is complete, before any destructor
    // can reenter this manager.
    action->retain();

    if (action == element.currentAction && (!element.currentActionSalvaged))
    {
        element.currentAction->retain();
        element.currentActionSalvaged = true;
    }

    element.actionRegistrations.erase(element.actionRegistrations.begin() + index);
    element.actions.erase(index);

    // update actionIndex in case we are in tick. looping over the actions
    if (element.actionIndex >= index)
    {
        element.actionIndex--;
    }

    if (element.actions.empty())
    {
        if (_currentTarget == &element)
        {
            _currentTargetSalvaged = true;
        }
        else
        {
            eraseTargetActionHandle(actionIt);
        }
    }
    action->release();
}
// pause / resume

void ActionManager::pauseTarget(Node* target)
{
    auto it = _targets.find(target);
    if (it != _targets.end())
    {
        it->second.paused = true;
    }
}

void ActionManager::resumeTarget(Node* target)
{
    auto it = _targets.find(target);
    if (it != _targets.end())
    {
        it->second.paused = false;
    }
}

Vector<Node*> ActionManager::pauseAllRunningActions()
{
    Vector<Node*> idsWithActions;

    for (auto& [target, element] : _targets)
    {
        element.paused = true;
        idsWithActions.pushBack(const_cast<Node*>(target));
    }

    return idsWithActions;
}

void ActionManager::resumeTargets(const Vector<Node*>& targetsToResume)
{
    for (const auto& node : targetsToResume)
    {
        this->resumeTarget(node);
    }
}

// run

void ActionManager::addAction(Action* action, Node* target, bool paused)
{
    // An action/target destructor may try to register work while this manager
    // is draining its final snapshot. No new retain may escape destruction.
    if (_destroying)
        return;
    AXASSERT(action != nullptr, "action can't be nullptr!");
    AXASSERT(target != nullptr, "target can't be nullptr!");
    if (action == nullptr || target == nullptr)
        return;

    auto actionIt = _targets.find(target);
    if (actionIt == _targets.end())
    {
        actionIt                = _targets.emplace(target, ActionHandle{}).first;
        actionIt->second.paused = paused;
        auto* element           = &actionIt->second;
        element->target         = target;
        element->registration   = ++_nextTargetRegistration;
        element->previous       = _lastRegisteredTarget;
        if (_lastRegisteredTarget)
            _lastRegisteredTarget->next = element;
        else
            _firstRegisteredTarget = element;
        _lastRegisteredTarget = element;
        target->retain();
    }

    auto& actionHandle = actionIt->second;
    reserveActionCapacity(actionHandle);

    AXASSERT(!actionHandle.actions.contains(action), "action already be added!");
    actionHandle.actions.pushBack(action);
    actionHandle.actionRegistrations.push_back(++_nextActionRegistration);

    action->startWithTarget(target);
}

// remove

void ActionManager::removeAllActions()
{
    // Release/destructor callbacks may remove the successor, rehash, or
    // re-register the same living target. Visit every entry present at entry
    // once, without treating a new registration as that removed entry.
    std::vector<std::pair<Node*, uint64_t>> registrations;
    registrations.reserve(_targets.size());
    for (const auto& [target, element] : _targets)
        registrations.emplace_back(target, element.registration);
    for (const auto& [target, registration] : registrations)
    {
        auto actionIt = _targets.find(target);
        if (actionIt != _targets.end() && actionIt->second.registration == registration)
            removeTargetActionHandle(actionIt);
    }
}

void ActionManager::removeAllActionsFromTarget(Node* target)
{
    // explicit null handling
    if (target == nullptr)
    {
        return;
    }

    auto actionIt = _targets.find(target);
    if (actionIt != _targets.end())
        removeTargetActionHandle(actionIt);
}

void ActionManager::removeTargetActionHandle(std::unordered_map<Node*, ActionHandle>::iterator& actionIt)
{
    auto& element = actionIt->second;
    if (element.actions.contains(element.currentAction) && !element.currentActionSalvaged)
    {
        element.currentAction->retain();
        element.currentActionSalvaged = true;
    }

    if (_currentTarget == &element)
    {
        _currentTargetSalvaged = true;
        // Publish an empty live vector before releasing actions. Destructors
        // may reenter the manager and append actions to the current target.
        element.actionRegistrations.clear();
        auto removedActions = std::move(element.actions);
        ++actionIt;
    }
    else
    {
        eraseTargetActionHandle(actionIt);
    }
}

void ActionManager::eraseTargetActionHandle(std::unordered_map<Node*, ActionHandle>::iterator& actionIt)
{
    auto* target  = actionIt->first;
    auto& element = actionIt->second;
    if (element.previous)
        element.previous->next = element.next;
    else
        _firstRegisteredTarget = element.next;
    if (element.next)
        element.next->previous = element.previous;
    else
        _lastRegisteredTarget = element.previous;
    if (_nextOrderedTarget == &element)
        _nextOrderedTarget = element.next;

    // Remove both indexes before releasing anything. In particular a target
    // destructor may remove/re-add another target or force a map rehash.
    auto next = std::next(actionIt);
    auto* nextTarget = next == _targets.end() ? nullptr : next->first;
    auto removed = _targets.extract(actionIt);
    removed.mapped().actions.clear();
    target->release();
    actionIt = nextTarget ? _targets.find(nextTarget) : _targets.end();
}

void ActionManager::removeAction(Action* action)
{
    // explicit null handling
    if (action == nullptr)
    {
        return;
    }

    Object* target = action->getOriginalTarget();
    auto actionIt  = _targets.find(static_cast<Node*>(target));
    if (actionIt != _targets.end())
    {
        auto i = actionIt->second.actions.getIndex(action);
        if (i != AX_INVALID_INDEX)
        {
            removeActionAtIndex(i, actionIt->second, actionIt);
        }
    }
}

void ActionManager::removeActionByTag(int tag, Node* target)
{
    AXASSERT(tag != Action::INVALID_TAG, "Invalid tag value!");
    AXASSERT(target != nullptr, "target can't be nullptr!");
    if (target == nullptr)
    {
        return;
    }

    auto actionIt = _targets.find(target);
    if (actionIt != _targets.end())
    {
        auto& element = actionIt->second;
        auto limit    = element.actions.size();
        for (int i = 0; i < limit; ++i)
        {
            Action* action = static_cast<Action*>(element.actions[i]);

            if (action->getTag() == (int)tag && action->getOriginalTarget() == target)
            {
                removeActionAtIndex(i, element, actionIt);
                break;
            }
        }
    }
}

void ActionManager::removeAllActionsByTag(int tag, Node* target)
{
    AXASSERT(tag != Action::INVALID_TAG, "Invalid tag value!");
    AXASSERT(target != nullptr, "target can't be nullptr!");
    if (target)
        removeActionsMatching(target, static_cast<uint32_t>(tag), true);
}

void ActionManager::removeActionsByFlags(unsigned int flags, Node* target)
{
    if (flags == 0)
        return;
    AXASSERT(target != nullptr, "target can't be nullptr!");
    if (target)
        removeActionsMatching(target, flags, false);
}

void ActionManager::removeActionsMatching(Node* target, uint32_t selector, bool byTag)
{
    struct Candidate
    {
        WeakPtr<Action> action;
        uint64_t registration;
    };
    uint64_t registration;
    std::vector<Candidate> candidates;
    {
        auto entry = _targets.find(target);
        if (entry == _targets.end())
            return;
        registration = entry->second.registration;
        const auto& element = entry->second;
        for (size_t index = 0; index < element.actions.size(); ++index)
        {
            auto* action = element.actions[index];
            const bool matches = byTag ? static_cast<uint32_t>(action->getTag()) == selector
                                       : (action->getFlags() & selector) != 0;
            if (matches && action->getOriginalTarget() == target)
                candidates.push_back({WeakPtr<Action>(action), element.actionRegistrations[index]});
        }
    }
    // No strong snapshot retain: each removal must run its real destructor
    // now, and its callbacks may erase/re-add targets or append new actions.
    for (const auto& candidate : candidates)
    {
        auto it = _targets.find(target);
        if (it == _targets.end() || it->second.registration != registration)
            break;
        auto* action = candidate.action.get();
        if (!action)
            continue;
        const auto index = it->second.actions.getIndex(action);
        const bool matches = byTag ? static_cast<uint32_t>(action->getTag()) == selector
                                   : (action->getFlags() & selector) != 0;
        if (index != AX_INVALID_INDEX && it->second.actionRegistrations[index] == candidate.registration &&
            matches && action->getOriginalTarget() == target)
            removeActionAtIndex(index, it->second, it);
        // Never reuse it/element/limit after the final release above.
    }
}

// get

// FIXME: Passing "const O *" instead of "const O&" because HASH_FIND_IT requires the address of a pointer
// and, it is not possible to get the address of a reference
Action* ActionManager::getActionByTag(int tag, const Node* target) const
{
    AXASSERT(tag != Action::INVALID_TAG, "Invalid tag value!");

    auto it = _targets.find(const_cast<Node*>(target));
    if (it != _targets.end())
    {
        auto& actions = it->second.actions;
        if (!actions.empty())
        {
            for (auto action : actions)
            {
                if (action->getTag() == tag)
                    return action;
            }
        }
    }

    return nullptr;
}

// FIXME: Passing "const O *" instead of "const O&" because HASH_FIND_IT requires the address of a pointer
// and, it is not possible to get the address of a reference
ssize_t ActionManager::getNumberOfRunningActionsInTarget(const Node* target) const
{
    auto it = _targets.find(const_cast<Node*>(target));
    return (it != _targets.end()) ? it->second.actions.size() : 0;
}

// FIXME: Passing "const O *" instead of "const O&" because HASH_FIND_IT requires the address of a pointer
// and, it is not possible to get the address of a reference
size_t ActionManager::getNumberOfRunningActionsInTargetByTag(const Node* target, int tag)
{
    AXASSERT(tag != Action::INVALID_TAG, "Invalid tag value!");

    auto it = _targets.find(const_cast<Node*>(target));
    if (it == _targets.end())
        return 0;

    auto& actions = it->second.actions;

    if (actions.empty())
        return 0;

    int count  = 0;
    auto limit = actions.size();
    for (int i = 0; i < limit; ++i)
    {
        auto action = actions[i];
        if (action->getTag() == tag)
            ++count;
    }

    return count;
}

ssize_t ActionManager::getNumberOfRunningActions() const
{
    ssize_t count = 0;
    for (auto& [_, element] : _targets)
        count += element.actions.size();
    return count;
}

// main loop
void ActionManager::updateTarget(ActionHandle& element, float dt)
{
    _currentTarget = &element;
    _currentTargetSalvaged = false;
    if (element.paused)
        return;

    // Preserve v3 actionIndex and salvage semantics. Each virtual call may
    // remove actions/targets, append actions, or stop a VM. Keep the action
    // alive until all bookkeeping is published, including isDone/stop paths.
    for (element.actionIndex = 0; element.actionIndex < element.actions.size(); ++element.actionIndex)
    {
        auto* action = static_cast<Action*>(element.actions[element.actionIndex]);
        element.currentAction = action;
        if (!action)
            continue;
        element.currentActionSalvaged = false;
        action->retain();
        action->step(dt);
        if (!element.currentActionSalvaged)
        {
            const bool done = action->isDone();
            if (!element.currentActionSalvaged && done)
            {
                action->stop();
                if (!element.currentActionSalvaged)
                {
                    // Normal completion must not acquire a salvage retain.
                    element.currentAction = nullptr;
                    removeAction(action);
                }
            }
        }
        const bool salvaged = element.currentActionSalvaged;
        element.currentAction = nullptr;
        if (salvaged)
            action->release();
        // A destructor can now reenter, seeing no dangling currentAction.
        action->release();
    }
}

void ActionManager::update(float dt)
{
    AX_PROFILER_ZONE_SCOPED;
    // A VM may stop (and release its lease) during step(). Finish the whole
    // frame using its entry policy, then observe the new policy next frame.
    const bool ordered = _legacyTargetOrderLeases != 0;
    if (ordered)
    {
        auto* element = _firstRegisteredTarget;
        while (element)
        {
            _nextOrderedTarget = nullptr;
            updateTarget(*element, dt);
            // Read after callbacks: removed successors are already unlinked,
            // and targets appended by the last step run in this same frame.
            _nextOrderedTarget = element->next;
            if ((_currentTargetSalvaged && element->actions.empty()) || element->target->getReferenceCount() == 1)
            {
                auto it = _targets.find(element->target);
                eraseTargetActionHandle(it);
            }
            element = _nextOrderedTarget;
        }
        _nextOrderedTarget = nullptr;
    }
    else
    {
        // Preserve the modern default traversal policy.
        for (auto actionIt = _targets.begin(); actionIt != _targets.end();)
        {
            updateTarget(actionIt->second, dt);
            if ((_currentTargetSalvaged && _currentTarget->actions.empty()) || actionIt->first->getReferenceCount() == 1)
                eraseTargetActionHandle(actionIt);
            else
                ++actionIt;
        }
    }
    _currentTarget = nullptr;
}

}  // namespace ax
