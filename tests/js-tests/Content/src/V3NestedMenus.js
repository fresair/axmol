// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Traversal of original native menus. All entries and scenes are created by
// the unchanged project's native menu callbacks and scene-manager factories.
(function (global) {
    function find(root, predicate, depth) {
        if (!root || depth < 0) return null;
        if (predicate(root)) return root;
        var children = root.getChildren ? root.getChildren() : [];
        for (var i = 0; i < children.length; ++i) {
            var result = find(children[i], predicate, depth - 1);
            if (result) return result;
        }
        return null;
    }
    function label(item) {
        var node = item.getLabel ? item.getLabel() : null;
        return node && node.getString ? String(node.getString()) : "Original menu entry";
    }
    function listMenu(scene, kind) {
        if (kind === "ui") return scene && scene._menu ? scene._menu : null;
        var layerType = kind === "native" ? global.NativeMainLayer : global.ExtensionsMainLayer;
        var layer = find(scene, function (node) { return typeof layerType === "function" && node instanceof layerType; }, 2);
        return find(layer, function (node) {
            if (!(node instanceof cc.Menu)) return false;
            var children = node.getChildren();
            return children.length && children[0].getLocalZOrder() >= 1000;
        }, 1);
    }
    function navigation(category, hooks) {
        this.kind = category.title === "UI Test" ? "ui" : category.title === "Native Test" ? "native" : "extensions";
        this.hooks = hooks;
        this.entries = null;
        this.index = 0;
        this.entry = null;
        this.menuPending = true;
        this.done = false;
        this.caseStarted = 0;
        this.scene = null;
        this.sceneFrames = 0;
        this.uiSeen = {};
        this.uiWrapped = false;
        this.controlVisited = 0;
        this.recovery = false;
        this.awaitingMenu = false;
        this.manualStarted = false;
        this.savedAuto = null;
        this.installHooks();
    }
    navigation.prototype.path = function () {
        return this.entry ? this.entry.title : "";
    };
    navigation.prototype.installHooks = function () {
        var self = this, hooks = this.hooks;
        if (this.kind === "ui" && global.UISceneManager) {
            var manager = global.UISceneManager;
            var current = manager.currentUIScene;
            manager.currentUIScene = function () {
                var id = this._currentUISceneId;
                if (self.uiSeen[id]) {
                    self.uiWrapped = true;
                    return current.apply(this, arguments);
                }
                self.uiSeen[id] = true;
                hooks.begin({number: id, type: "OriginalUIFactory", title: self.path() + " / " + id,
                    path: self.path(), source: "src/GUITest/UISceneManager.js"});
                self.caseStarted = performance.now();
                try { return current.apply(this, arguments); }
                catch (error) { self.recovery = true; hooks.fail(error); throw error; }
            };
            var setTitle = global.UIMainLayer.prototype.setSceneTitle;
            global.UIMainLayer.prototype.setSceneTitle = function (title) {
                hooks.rename(String(title));
                return setTitle.apply(this, arguments);
            };
            this.restoreHooks = function () {
                manager.currentUIScene = current;
                global.UIMainLayer.prototype.setSceneTitle = setTitle;
            };
        } else if (this.kind === "extensions" && global.ControlSceneManager) {
            var prototype = global.ControlSceneManager.prototype;
            var original = prototype.currentControlScene;
            prototype.currentControlScene = function () {
                if (self.entry && self.entry.title === "CCControlButtonTest") {
                    var id = this.getCurrentControlSceneId();
                    var item = global.controTestItemNames[id];
                    hooks.begin({number: id, type: item.itemTitle, title: item.itemTitle,
                        path: self.path(), source: "src/ExtensionsTest/ControlExtensionTest/CCControlSceneManager.js"});
                    self.caseStarted = performance.now();
                    self.controlVisited++;
                }
                try { return original.apply(this, arguments); }
                catch (error) { self.recovery = true; hooks.fail(error); throw error; }
            };
            this.restoreHooks = function () { prototype.currentControlScene = original; };
        }
    };
    navigation.prototype.restoreManualMode = function () {
        if (this.savedAuto !== null) {
            autoTestEnabled = this.savedAuto;
            this.savedAuto = null;
        }
    };
    navigation.prototype.restore = function () {
        this.restoreManualMode();
        if (this.restoreHooks) this.restoreHooks();
    };
    navigation.prototype.onMainMenu = function () {
        this.restoreManualMode();
        if (this.done) return false;
        this.menuPending = true;
        this.awaitingMenu = false;
        this.entry = null;
        this.hooks.launch();
        return true;
    };
    navigation.prototype.finishEntry = function (detail) {
        this.restoreManualMode();
        if (this.hooks.active()) this.hooks.complete("needsInteraction", detail || "Original interactive case has no comparison result.");
        this.hooks.record({kind: "subcategory-end", path: this.path()});
        this.awaitingMenu = true;
        this.hooks.menu();
    };
    navigation.prototype.onException = function () {
        this.recovery = true;
        if (this.kind !== "ui" && (!this.entry || this.entry.title !== "CCControlButtonTest"))
            this.finishEntry("Original subcategory failed before automated completion.");
    };
    navigation.prototype.tick = function () {
        if (this.awaitingMenu) return true;
        var scene = cc.director.getRunningScene();
        if (scene !== this.scene) { this.scene = scene; this.sceneFrames = 0; }
        else this.sceneFrames++;
        if (this.menuPending) {
            var menu = listMenu(scene, this.kind);
            if (!menu || !scene.isRunning() || this.sceneFrames < 2) return true;
            var items = menu.getChildren();
            if (!this.entries) {
                this.entries = [];
                for (var i = 0; i < items.length; ++i) this.entries.push({title: label(items[i]), index: i});
                this.hooks.record({kind: "nested-menu", menu: this.kind, native_entry_count: this.entries.length,
                    entries: this.entries.map(function (item) { return item.title; })});
                var requested = this.hooks.entryFilter();
                if (requested) {
                    this.entries = this.entries.filter(function (item) {
                        return item.title === requested || String(item.index) === requested;
                    });
                    this.hooks.record({kind: "requested-entry-scope", requested: requested,
                        selected_entries: this.entries.map(function (item) { return item.title; }),
                        detail: "Unselected original entries remain unverified by this focused run."});
                    if (!this.entries.length) {
                        this.hooks.record({kind: "entry-filter-error", status: "failed",
                            error: "Requested original menu entry is absent: " + requested});
                    }
                }
            }
            if (this.index >= this.entries.length) {
                this.done = true;
                if (this.restore) this.restore();
                this.hooks.menu();
                return true;
            }
            this.entry = this.entries[this.index++];
            this.menuPending = false;
            this.caseStarted = performance.now();
            this.uiSeen = {};
            this.uiWrapped = false;
            this.controlVisited = 0;
            this.recovery = false;
            this.manualStarted = false;
            this.hooks.record({kind: "subcategory-start", path: this.path(), menu: this.kind});
            // AssetsManagerTestLayer is an upstream manual-only BaseTestLayer
            // that does not implement the automation hooks. Its Scene1..4
            // layers can also be constructed asynchronously after this entry
            // starts, so keep original manual mode active for the full entry.
            if (this.kind === "extensions" && this.entry.title === "AssetsManagerTest") {
                this.savedAuto = autoTestEnabled;
                autoTestEnabled = false;
            }
            try { items[this.entry.index].activate(); }
            catch (error) { this.hooks.fail(error); this.onException(); }
            return true;
        }
        if (this.kind === "ui") {
            if (this.uiWrapped) { this.finishEntry(); return true; }
            var uiLayer = find(scene, function (node) { return node instanceof global.UIMainLayer; }, 2);
            var uiDwell = this.hooks.captureEnabled() ? 2500 : 500;
            if ((!this.recovery && (!uiLayer || this.sceneFrames < 3 || performance.now() - this.caseStarted < uiDwell))) return true;
            if (!uiLayer) {
                // A failed factory keeps the previous running scene alive.
                // The manager's next function still advances the original
                // private registry, including the failed entry.
                try { cc.director.runScene(global.UISceneManager.nextUIScene()); }
                catch (error) { this.hooks.fail(error); }
                this.recovery = false;
                return true;
            }
            if (!this.recovery && this.hooks.active() &&
                !this.hooks.complete("rendered", "Original UI scene ran native frames; visual and input checks remain.")) return true;
            this.recovery = false;
            try { uiLayer.nextCallback(null, ccui.Widget.TOUCH_ENDED); }
            catch (error) { this.hooks.fail(error); this.recovery = true; }
            return true;
        }
        if (this.entry && this.entry.title === "CCControlButtonTest") {
            var control = find(scene, function (node) { return node instanceof global.ControlScene; }, 2);
            var controlDwell = this.hooks.captureEnabled() ? 2500 : 500;
            if (!this.recovery && (!control || this.sceneFrames < 3 || performance.now() - this.caseStarted < controlDwell)) return true;
            if (!this.recovery && this.hooks.active() &&
                !this.hooks.complete("rendered", "Original native control scene ran frames; actual control interaction remains.")) return true;
            if (this.controlVisited >= global.controTestItemNames.length) { this.finishEntry(); return true; }
            this.recovery = false;
            try {
                if (control) control.nextCallback(null);
                else cc.director.runScene(global.ControlSceneManager.getInstance().nextControlScene());
            } catch (error) { this.hooks.fail(error); this.recovery = true; }
            return true;
        }
        if (this.kind === "extensions" && this.entry && this.entry.title === "AssetsManagerTest") {
            // The shared BaseTestLayer constructor observer starts a case
            // even in manual mode. No original automation callback will end
            // it: wait for the real layer and capture its rendered state here.
            var assetsLayer = find(scene, function (node) {
                return typeof global.AssetsManagerTestLayer === "function" &&
                    node instanceof global.AssetsManagerTestLayer;
            }, 2);
            var assetsDwell = this.hooks.captureEnabled() ? 2500 : 500;
            if (!assetsLayer || !assetsLayer.isRunning() || this.sceneFrames < 3 ||
                performance.now() - this.caseStarted < assetsDwell) return true;
            if (this.hooks.active() && !this.hooks.complete("rendered",
                "Original manual AssetsManager scene ran native frames; download, restart and interaction checks remain."))
                return true;
            this.finishEntry();
            return true;
        }
        // FileUtils and AudioEngine retain their original BaseTestLayer
        // automation. Other native/extension entries are constructed and
        // rendered individually, with unresolved interaction kept explicit.
        if (this.hooks.active() || this.hooks.baseFlow()) return true;
        var nativeDwell = this.hooks.captureEnabled() ? 2500 : 500;
        if (this.sceneFrames < 3 || performance.now() - this.caseStarted < nativeDwell) return true;
        if (!this.manualStarted) {
            this.hooks.begin({number: 0, type: "OriginalNativeMenuFactory", title: this.path(), path: this.path()});
            this.manualStarted = true;
        }
        if (this.hooks.active() &&
            !this.hooks.complete("rendered", "Original native entry ran frames; interaction and visual checks remain.")) return true;
        this.finishEntry();
        return true;
    };
    global.V3NestedNavigation = function (category, hooks) {
        if (category.title !== "UI Test" && category.title !== "Native Test" && category.title !== "Extensions Test") return null;
        return new navigation(category, hooks);
    };
})(this);
