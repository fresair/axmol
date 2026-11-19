// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// SPDX-License-Identifier: MIT
#include <doctest.h>
#include "axmol/scene/Node.h"
#include "axmol/base/Director.h"
#include "axmol/base/EventDispatcher.h"
#include "axmol/base/CustomEventListener.h"
#include "axmol/base/CustomEvent.h"
#include <string>
#include <vector>

TEST_CASE("Native Node lifecycle callbacks remain ordered without a script VM")
{
    std::vector<std::string> trace;
    auto* parent=ax::Node::create();
    auto* child=ax::Node::create();
    parent->addChild(child);
    parent->markScriptBindingExposed();
    child->markScriptBindingExposed();
    parent->setOnEnterCallback([&]{trace.emplace_back("parent.enter");});
    child->setOnEnterCallback([&]{trace.emplace_back("child.enter");});
    parent->setOnEnterTransitionDidFinishCallback([&]{trace.emplace_back("parent.enter.finish");});
    child->setOnEnterTransitionDidFinishCallback([&]{trace.emplace_back("child.enter.finish");});
    parent->setOnExitTransitionDidStartCallback([&]{trace.emplace_back("parent.exit.start");});
    child->setOnExitTransitionDidStartCallback([&]{trace.emplace_back("child.exit.start");});
    parent->setOnExitCallback([&]{trace.emplace_back("parent.exit");});
    child->setOnExitCallback([&]{trace.emplace_back("child.exit");});
    parent->setOnCleanupCallback([&]{trace.emplace_back("parent.cleanup");});
    child->setOnCleanupCallback([&]{trace.emplace_back("child.cleanup");});
    parent->onEnter();
    CHECK(parent->isRunning()); CHECK(child->isRunning());
    parent->onEnterTransitionDidFinish(); parent->onExitTransitionDidStart(); parent->onExit(); parent->cleanup();
    CHECK_FALSE(parent->isRunning()); CHECK_FALSE(child->isRunning());
    const std::vector<std::string> expected={"parent.enter","child.enter","parent.enter.finish","child.enter.finish",
        "parent.exit.start","child.exit.start","parent.exit","child.exit","parent.cleanup","child.cleanup"};
    CHECK(trace==expected);
    parent->setOnEnterCallback({}); child->setOnEnterCallback({});
    parent->setOnEnterTransitionDidFinishCallback({}); child->setOnEnterTransitionDidFinishCallback({});
    parent->setOnExitTransitionDidStartCallback({}); child->setOnExitTransitionDidStartCallback({});
    parent->setOnExitCallback({}); child->setOnExitCallback({});
    parent->setOnCleanupCallback({}); child->setOnCleanupCallback({});
}

TEST_CASE("Native custom listeners own dynamic names across cloning and self removal")
{
    auto* dispatcher=ax::Director::getInstance()->getEventDispatcher();
    const bool previouslyEnabled=dispatcher->isEnabled();
    dispatcher->setEnabled(true);
    const std::string name=std::string(8192,'N')+".native.custom";
    int calls=0;
    ax::CustomEventListener* listener{};
    listener=ax::CustomEventListener::create(std::string(name),[&](ax::CustomEvent* event) {
        CHECK(event->getEventName()==name);
        ++calls;
        dispatcher->removeEventListener(listener);
    });
    REQUIRE(listener!=nullptr);
    auto* cloned=listener->clone();
    REQUIRE(cloned!=nullptr);
    // Reuse similarly sized allocations after the temporary name was freed.
    std::vector<std::string> churn(32,std::string(name.size(),'Z'));
    dispatcher->addEventListenerWithFixedPriority(listener,1);
    dispatcher->addEventListenerWithFixedPriority(cloned,2);
    dispatcher->dispatchCustomEvent(name);
    CHECK(calls==2);
    dispatcher->dispatchCustomEvent(name);
    CHECK(calls==3);
    dispatcher->removeEventListener(listener); dispatcher->removeEventListener(cloned);
    dispatcher->setEnabled(previouslyEnabled);
}
