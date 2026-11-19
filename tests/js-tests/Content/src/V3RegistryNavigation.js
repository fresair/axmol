// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Observe original native registries and activate their original Next callback.
// This explicit manual profile preserves the project's original auto profile.
(function (global) {
    var specifications = {
        "BillBoard Test": ["arrayOfBillBoardTest", "nextBillBoardTest", "BillBoardTestIdx"],
        "Effects Advanced Test": ["arrayOfEffectsAdvancedTest", "nextEffectAdvanceAction", "sceneIndex"],
        "Event Test": ["arrayOfEventsTest", "nextEventsTest", "sceneIdx"],
        "Camera3D Test": ["arrayOfCamera3DTest", "nextCamera3DTest", "Camera3DTestIdx"],
        "Light Test": ["arrayOfLightTest", "nextLightTest", "LightTestIdx"],
        "MaterialSystem Test": ["arrayOfMaterialSystemTest", "nextMaterialSystemTest", "MaterialSystemTestIdx"],
        "MotionStreak Test": ["arrayOfMotionStreakTest", "nextMotionAction", "sceneIdx"],
        "Node Test": ["arrayOfNodeTest", "nextNodeTest", "nodeTestSceneIdx"],
        "Particle3D Test": ["arrayOfParticle3DTest", "nextParticle3DTest", "Particle3DTestIdx"],
        "Physics3D Test": ["arrayOfPhysics3DTest", "nextPhysics3DTest", "Physics3DTestIdx"],
        "NavMesh Test": ["arrayOfNavMeshTest", "nextNavMeshTest", "NavMeshTestIdx"],
        "Sprite3D Test": ["arrayOfSprite3DTest", "nextSprite3DTest", "Sprite3DTestIdx"],
        "SpritePolygon Test": ["arrayOfSpritePolygonTest", "nextSpritePolygonTest", "SpritePolygonTestIdx"],
        "Terrain Test": ["arrayOfTerrainTest", "nextTerrainTest", "TerrainTestIdx"],
        "TextureCache Test": ["arrayOfTexCacheTest", "nextTexCacheTest", "texCacheTestSceneIdx"],
        "Scale9Sprite Test": ["arrayOfS9SpriteTest", "nextS9SpriteTest", "sceneIdx"],
        "Spine Test": ["SpineTestScene.testLayers", "SpineTestScene.nextSpineTestLayer", "spineSceneIdx"]
    };
    function locate(path) {
        var parts = path.split("."), owner = global;
        for (var i = 0; i < parts.length - 1; ++i) owner = owner && owner[parts[i]];
        return {owner: owner, key: parts[parts.length - 1]};
    }
    function read(path) { var slot = locate(path); return slot.owner && slot.owner[slot.key]; }
    function nameOf(type) {
        var names = Object.getOwnPropertyNames(global);
        for (var i = 0; i < names.length; ++i) {
            var value;
            try { value = global[names[i]]; } catch (error) { continue; }
            if (value === type) return names[i];
        }
        return "OriginalRegistryType";
    }
    function find(root, predicate, depth) {
        if (!root || depth < 0) return null;
        if (predicate(root)) return root;
        var children = root.getChildren ? root.getChildren() : [];
        for (var i = 0; i < children.length; ++i) {
            var value = find(children[i], predicate, depth - 1);
            if (value) return value;
        }
        return null;
    }
    function recordPolygonState(layer, hooks, index, phase) {
        try {
            hooks.record({kind: "polygon-layout-environment", number: index, phase: phase,
                visible_rect: {width: cc.visibleRect.width, height: cc.visibleRect.height,
                    center: cc.visibleRect.center, left: cc.visibleRect.left, bottom: cc.visibleRect.bottom},
                global_win_size: cc.winSize, director_win_size: cc.director.getWinSize(),
                native_visible_size: cc.director.getVisibleSize(), native_visible_origin: cc.director.getVisibleOrigin(),
                view: v3ViewState()});
            hooks.record({kind: "polygon-layer-state", number: index, phase: phase,
                epsilon_label_created: !!layer._epsilonLabel,
                epsilon_label_valid: !!layer._epsilonLabel && jsb.isObjectValid(layer._epsilonLabel),
                epsilon_label_text: layer._epsilonLabel && jsb.isObjectValid(layer._epsilonLabel) ? layer._epsilonLabel.getString() : null,
                performance_label_created: !!layer.perfLabel,
                performance_label_text: layer.perfLabel && jsb.isObjectValid(layer.perfLabel) ? layer.perfLabel.getString() : null,
                elapsed_time: layer.elapsedTime, previous_dt: layer.prevDt, ended: layer.ended,
                sprite_count: layer.spriteCount, triangle_count: layer.triCount,
                vertex_count: layer.vertCount, pixel_count: layer.pixelCount,
                children_count: layer.getChildren().length});
            var children = layer.getChildren();
            children.forEach(function (child, childIndex) {
                if (childIndex >= 8 && childIndex < children.length - 2) return;
                var value = {kind: "polygon-child-state", number: index, phase: phase, child_index: childIndex,
                    native_sprite: child instanceof cc.Sprite, native_slider: child instanceof ccui.Slider,
                    native_ttf: child instanceof cc.LabelTTF, native_node_running: child.isRunning(),
                    child_count: child.getChildren().length, native_name: child.getName(), position: child.getPosition()};
                if (value.native_slider) value.percent = child.getPercent();
                if (value.native_ttf) value.text = child.getString();
                if (value.native_sprite) {
                    var polygon = child.getPolygonInfo();
                    value.polygon_area = polygon.getArea();
                    value.polygon_vertices = polygon.getVertCount();
                }
                hooks.record(value);
            });
        } catch (error) {
            hooks.record({kind: "polygon-diagnostic-error", number: index, phase: phase,
                error: String(error) + (error.stack ? "\n" + error.stack : "")});
        }
    }
    function registry(category, specification, hooks) {
        this.category = category;
        this.specification = specification;
        this.hooks = hooks;
        this.entries = read(specification[0]);
        if (!Array.isArray(this.entries) || !this.entries.length)
            throw Error("Original registry is unavailable: " + specification[0]);
        this.next = locate(specification[1]);
        if (!this.next.owner || typeof this.next.owner[this.next.key] !== "function")
            throw Error("Original next factory is unavailable: " + specification[1]);
        this.originalNext = this.next.owner[this.next.key];
        this.previousAuto = autoTestEnabled;
        autoTestEnabled = false; // The original manual execution mode.
        this.attempted = {}, this.count = 0, this.index = null;
        this.layer = null, this.lastLayer = null, this.startedAt = 0;
        this.scene = null, this.sceneFrames = 0, this.advanceQueued = false;
        this.recovery = false, this.done = false;
        var self = this;
        this.next.owner[this.next.key] = function () {
            var index = (Number(global[specification[2]]) + 1) % self.entries.length;
            self.index = index;
            self.startedAt = performance.now();
            self.layer = null;
            self.observedState = false;
            self.advanceQueued = false;
            self.recovery = false;
            if (!self.attempted[index]) { self.attempted[index] = true; self.count++; }
            else hooks.record({kind: "registry-repeat", number: index,
                detail: "An original callback revisited this entry; it is not a new leaf test."});
            var name = nameOf(self.entries[index]);
            hooks.begin({number: index, type: name, title: name, path: specification[0],
                source: category.linksrc || (category.title === "Particle3D Test" ? "src/Particle3DTest/Particle3DTest.js" : "")});
            hooks.record({kind: "registry-factory", number: index, type: name,
                registry: specification[0], factory: specification[1],
                automation: "original manual mode"});
            try {
                var layer = self.originalNext.apply(this, arguments);
                self.layer = self.lastLayer = layer;
                if (hooks.attach) hooks.attach(layer);
                if (layer && layer.getTitle) hooks.rename(String(layer.getTitle()));
                else if (layer && layer.title) hooks.rename(String(layer.title()));
                else if (layer && layer._title) hooks.rename(String(layer._title));
                if (category.title === "SpritePolygon Test")
                    recordPolygonState(layer, hooks, index, "after unchanged original factory returned");
                return layer;
            } catch (error) { self.recovery = true; hooks.fail(error); throw error; }
        };
        hooks.record({kind: "registry", registry: specification[0],
            original_runtime_entries: this.entries.length,
            entries: this.entries.map(nameOf), automation: "original manual mode",
            detail: "Rendering and original callbacks are recorded; visual/input behavior remains separately unverified."});
    }
    registry.prototype.path = function () { return this.specification[0]; };
    registry.prototype.restore = function () {
        this.next.owner[this.next.key] = this.originalNext;
        autoTestEnabled = this.previousAuto;
    };
    registry.prototype.onException = function () {
        this.recovery = true;
        // Repeated errors from an original update callback can keep the
        // monitor in its error-recovery branch. Queue the actual Scheduler
        // navigation here instead of requiring another monitor tick first.
        this.queueAdvance();
    };
    registry.prototype.onMainMenu = function () { return false; };
    registry.prototype.queueAdvance = function () {
        var gateSelf = this;
        if (this.hooks.ready && !this.hooks.ready(this, function () { gateSelf.queueAdvance(); })) return;
        if (this.advanceQueued) return;
        this.advanceQueued = true;
        var self = this, scene = cc.director.getRunningScene();
        if (!scene) throw Error("Original registry navigation has no running Scene.");
        scene.scheduleOnce(function () {
            if (self.done) return;
            if (self.hooks.ready && !self.hooks.ready(self, function () {
                self.advanceQueued = false;
                self.queueAdvance();
            })) return;
            if (self.count >= self.entries.length) {
                self.done = true;
                self.hooks.record({kind: "registry-end", registry: self.specification[0],
                    reached_entries: self.count, original_runtime_entries: self.entries.length,
                    detail: "All original factories reached; rendering alone is not functional acceptance."});
                self.hooks.menu();
                return;
            }
            try {
                var layer = self.layer || self.lastLayer;
                if (layer && jsb.isObjectValid(layer) && layer.onNextCallback) {
                    var item = find(layer, function (node) {
                        return node instanceof cc.MenuItemImage && node.getTag() === BASE_TEST_MENUITEM_NEXT_TAG;
                    }, 3);
                    self.hooks.record({kind: "navigation", number: self.index,
                        method: item ? "original native Next MenuItem.activate" : "original onNextCallback"});
                    if (self.closedScope) self.beginEntry((self.index + 1) % self.entries.length);
                    if (item) item.activate();
                    else layer.onNextCallback(layer);
                } else if (layer && jsb.isObjectValid(layer) &&
                           typeof layer.nextCallback === "function") {
                    self.hooks.record({kind: "navigation", number: self.index,
                        method: "original layer nextCallback"});
                    layer.nextCallback(layer);
                } else {
                    // A failed constructor has no live Next item. Re-enter via
                    // the original category factory and its indexed entry point.
                    var entry = self.category.testScene();
                    var requested = (self.index + 1) % self.entries.length;
                    self.hooks.record({kind: "navigation", number: self.index,
                        method: "original category testScene/runThisTest", requested_entry: requested});
                    entry.runThisTest(requested);
                }
            } catch (error) {
                self.advanceQueued = false;
                self.recovery = true;
                self.hooks.fail(error);
            }
        }, 0);
    };
    registry.prototype.tick = function () {
        if (this.done || this.advanceQueued) return true;
        var scene = cc.director.getRunningScene();
        if (scene !== this.scene) { this.scene = scene; this.sceneFrames = 0; }
        else this.sceneFrames++;
        if (this.recovery) { this.queueAdvance(); return true; }
        if (!this.layer || !scene || !scene.isRunning() || !this.layer.isRunning() || this.sceneFrames < 3) return true;
        var dwell = Math.max(2.5, Number(this.layer.testDuration) || 0);
        if (this.category.title === "Spine Test" && this.index === 0) dwell = Math.max(dwell, 5.5);
        if (performance.now() - this.startedAt < dwell * 1000) return true;
        if (this.category.title === "SpritePolygon Test" && !this.observedState) {
            this.observedState = true;
            recordPolygonState(this.layer, this.hooks, this.index, "after real rendered dwell");
        }
        if (this.hooks.active() && !this.hooks.complete("rendered",
            "Original registry entry ran native frames; visual and actual-input checks remain.")) return true;
        this.queueAdvance();
        return true;
    };
    // These two original files keep their registry and index in an IIFE.
    // Their public Scene and native Next callbacks are the original entry
    // points. Record the source registry's fixed order, then observe the
    // actual Layer passed to native Scene.addChild on each original call.
    var closedSpecifications = {
        "Particle3D Test": {scene: "Particle3DTestScene", source: "src/Particle3DTest/Particle3DTest.js",
            registry: "arrayOfParticle3DTest", entries: ["Particle3DExplosionSystemDemo",
                "Particle3DLineStreakDemo", "Particle3DBlackHoleDemo", "Particle3DHypnoDemo",
                "Particle3DAdvancedLodSystemDemo", "Particle3DTimeShiftDemo", "Particle3DUVAnimDemo",
                "Particle3DFirePlaceDemo", "Particle3DElectricBeamSystemDemo", "Particle3DFlareShieldDemo",
                "Particle3DLightningBoltDemo", "Particle3DCanOfWormsDemo", "Particle3DRibbonTrailDemo",
                "Particle3DWeaponTrailDemo", "Particle3DWithSprite3DDemo"]},
        "Terrain Test": {scene: "TerrainTestScene", source: "src/TerrainTest/TerrainTest.js",
            registry: "arrayOfTerrainTest", entries: ["TerrainSimple", "TerrainWalkThru"]},
        "Loader Test": {scene: "LoaderTestScene", source: "src/LoaderTest/LoaderTest.js",
            registry: "LoaderTestScene.runThisTest", entries: ["LoaderTestLayer"], scope: "original single Scene factory"},
        "Path Tests": {scene: "PathTestScene", source: "src/PathTest/PathTest.js",
            registry: "PathTestScene.runThisTest", entries: ["PathTestLayer"], scope: "original single Scene factory"},
        "Reflection Test": {scene: "ReflectionTestScene", source: "src/ReflectionTest/ReflectionTest.js",
            registry: "ReflectionTestScene.runThisTest", entries: ["ReflectionTestLayer"], scope: "original single Scene factory"},
        "Vibrate Test": {scene: "VibrateTestScene", source: "src/VibrateTest/VibrateTest.js",
            registry: "VibrateTestScene.runThisTest", entries: ["VibrateTest"], scope: "original single Scene factory"},
        "CurrentLanguage Test": {scene: "CurrentLanguageTestScene", source: "src/CurrentLanguageTest/CurrentLanguageTest.js",
            registry: "CurrentLanguageTestScene.runThisTest", entries: ["CurrentLanguageTest"], scope: "original single Scene factory"},
        "CocosDenshion Test": {scene: "CocosDenshionTestScene", source: "src/CocosDenshionTest/CocosDenshionTest.js",
            registry: "CocosDenshionTestScene.runThisTest", entries: ["CocosDenshionTest"], scope: "original single Scene factory"}
    };
    function closedRegistry(category, specification, hooks) {
        this.category = category;
        this.hooks = hooks;
        this.specification = [specification.registry];
        this.entries = specification.entries;
        this.bindingScope = specification.scope || "original source IIFE";
        this.sceneType = global[specification.scene];
        this.previousAuto = autoTestEnabled;
        autoTestEnabled = false;
        this.closedScope = true;
        this.attempted = {}; this.count = 0; this.index = null;
        this.layer = null; this.lastLayer = null; this.startedAt = 0;
        this.scene = null; this.sceneFrames = 0; this.advanceQueued = false;
        this.recovery = false; this.done = false;
        var self = this, prototype = global[specification.scene].prototype;
        this.prototype = prototype;
        this.originalRun = prototype.runThisTest;
        this.addDescriptor = Object.getOwnPropertyDescriptor(prototype, "addChild");
        this.originalAdd = prototype.addChild;
        prototype.runThisTest = function (number) {
            self.beginEntry(number || number === 0 ? Number(number) : 0);
            return self.originalRun.apply(this, arguments);
        };
        Object.defineProperty(prototype, "addChild", {configurable: true, writable: true,
            value: function (child) {
                var result = self.originalAdd.apply(this, arguments);
                if (self.isOriginalLayer(child)) self.attachLayer(child, this, "original Scene.addChild");
                return result;
            }});
        hooks.record({kind: "registry", registry: specification.registry,
            binding_scope: this.bindingScope, source: specification.source,
            source_registry_entries: this.entries.length, entries: this.entries,
            automation: "original manual mode",
            detail: "Source order is fixed by the unchanged imported file; native Scene/Next callbacks execute each factory. Visual/input acceptance remains separate."});
    }
    closedRegistry.prototype = Object.create(registry.prototype);
    closedRegistry.prototype.isOriginalLayer = function (child) {
        if (!child || !jsb.isObjectValid(child)) return false;
        var type = global[this.entries[this.index]];
        // Some original test layers derive from LayerGradient or an original
        // native facade whose JS prototype is not cc.Layer. Match the original
        // registered type when public; retain the Layer check for IIFE types.
        return typeof type === "function" ? child instanceof type : child instanceof cc.Layer;
    };
    closedRegistry.prototype.attachLayer = function (child, scene, method) {
        if (this.layer === child) return;
        if (child.getParent() !== scene) throw Error("Original registry layer has a different native parent.");
        this.layer = this.lastLayer = child;
        if (this.hooks.attach) this.hooks.attach(child);
        if (child.getTitle) this.hooks.rename(String(child.getTitle()));
        else if (child._title) this.hooks.rename(String(child._title));
        this.hooks.record({kind: "registry-native-layer", number: this.index,
            type: this.entries[this.index], method: method,
            native_valid: jsb.isObjectValid(child), native_parent_matches: child.getParent() === scene,
            native_running: child.isRunning(), original_scene_type_matches: scene instanceof this.sceneType});
    };
    closedRegistry.prototype.tick = function () {
        if (!this.layer && !this.recovery && !this.done) {
            var scene = cc.director.getRunningScene();
            if (scene && scene instanceof this.sceneType && jsb.isObjectValid(scene) && scene.isRunning()) {
                var self = this;
                var layers = scene.getChildren().filter(function (child) { return self.isOriginalLayer(child); });
                if (layers.length === 1)
                    this.attachLayer(layers[0], scene, "actual running original Scene.getChildren");
                else if (layers.length > 1) {
                    this.hooks.fail(Error("Original registry scene has multiple matching layers."));
                    this.recovery = true;
                }
            }
            if (!this.layer && !this.recovery && performance.now() - this.startedAt >= 5000) {
                this.hooks.record({kind: "registry-layer-missing", number: this.index,
                    type: this.entries[this.index], elapsed_ms: performance.now() - this.startedAt,
                    running_scene_present: !!scene,
                    original_scene_type_matches: !!scene && scene instanceof this.sceneType});
                this.hooks.fail(Error("Original registry layer was not found in the running native scene."));
                this.recovery = true;
            }
        }
        return registry.prototype.tick.call(this);
    };
    closedRegistry.prototype.beginEntry = function (index) {
        this.index = index;
        this.startedAt = performance.now();
        this.layer = null; this.advanceQueued = false; this.recovery = false;
        if (!this.attempted[index]) { this.attempted[index] = true; this.count++; }
        else this.hooks.record({kind: "registry-repeat", number: index});
        var specification = closedSpecifications[this.category.title];
        this.hooks.begin({number: index, type: this.entries[index], title: this.entries[index],
            source: specification.source, path: specification.registry});
        this.hooks.record({kind: "registry-factory", number: index, type: this.entries[index],
            registry: specification.registry, factory: "original Scene.runThisTest/native Next callback",
            automation: "original manual mode", binding_scope: this.bindingScope});
    };
    closedRegistry.prototype.restore = function () {
        this.prototype.runThisTest = this.originalRun;
        if (this.addDescriptor) Object.defineProperty(this.prototype, "addChild", this.addDescriptor);
        else delete this.prototype.addChild;
        autoTestEnabled = this.previousAuto;
    };
    global.V3RegistryNavigation = function (category, hooks) {
        if (closedSpecifications[category.title])
            return new closedRegistry(category, closedSpecifications[category.title], hooks);
        var specification = specifications[category.title];
        return specification ? new registry(category, specification, hooks) : null;
    };
})(this);
