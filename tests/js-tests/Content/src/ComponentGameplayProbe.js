/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
// Observer for the unchanged native v3 ComponentTest game. Run after graphics
// initialization and the original project.json scripts have loaded. All input
// comes from the native EventDispatcher (for example an actual ADB screen tap).
// No gameplay callback, score, enemy position, random source or timer is edited.
var AxmolComponentGameplayProbeResult = null;
(function (global) {
    var state = null;
    function check(value, message) { if (!value) throw Error('Component gameplay: ' + message); }
    function valid(value) { return !!value && jsb.isObjectValid(value); }
    function elapsed() { return (Date.now() - state.started) / 1000; }
    function log(kind, value) {
        var record = {};
        Object.keys(value).forEach(function (key) { record[key] = value[key]; });
        record.kind = kind;
        record.frame = state.frames;
        record.seconds = elapsed();
        if (state.complete) {
            // Full private records keep native identity and avoid log-ring loss.
            v3Record(JSON.stringify({kind: 'original-component-observation', event: kind,
                token: state.token, mode: state.mode, run_nonce: state.nonce,
                process_id: state.pid, phase: state.completePhase,
                native_frame: cc.director.getTotalFrames(), facts: record}));
        }
        // One entity/event per line avoids the native logger's 1024-byte limit.
        var consoleRecord = record;
        if (state.complete && JSON.stringify(record).length > 850) {
            consoleRecord = {kind: kind, frame: record.frame, seconds: record.seconds,
                mode: state.mode, full_facts_saved_in_private_jsonl: true};
            if (value.file) consoleRecord.file = value.file;
        }
        cc.log('COMPONENT_GAMEPLAY_RECORD ' + JSON.stringify(consoleRecord));
    }
    function entity(node, kind) {
        for (var index = 0; index < state.entities.length; index++) {
            if (state.entities[index].node === node) return state.entities[index];
        }
        var value = {node: node, kind: kind, id: kind + '-' + (state.entities.length + 1),
            previous: null, lastLogged: -1};
        state.entities.push(value);
        log('native_entity_added', {id: value.id, type: kind});
        return value;
    }
    function sampleNode(value, layer) {
        check(valid(value.node), value.id + ' is not a live native Node');
        var point = value.node.getPosition(), size = value.node.getContentSize();
        var component = value.node.getComponent('');
        check(component instanceof cc.ComponentJS && component.getOwner() === value.node &&
            component.getScriptObject() === component, value.id + ' lost its real ComponentJS owner/identity');
        var sample = {id: value.id, type: value.kind, x: point.x, y: point.y,
            width: size.width, height: size.height, running: value.node.isRunning(),
            native_parent_is_game_layer: value.node.getParent() === layer,
            native_component_identity: true, actions: value.node.getNumberOfRunningActions(),
            seconds: elapsed()};
        check(sample.running && sample.native_parent_is_game_layer, value.id + ' is not in the active native Scene');
        if (value.previous && value.previous.x !== sample.x) {
            state.nativeMovement = true;
            sample.velocity_x = (sample.x - value.previous.x) / Math.max(0.001, sample.seconds - value.previous.seconds);
        }
        if (sample.seconds - value.lastLogged >= 0.3) {
            log('native_position', sample);
            value.lastLogged = sample.seconds;
            if (value.kind === 'enemy' && state.mode === 'win') aim(value, sample);
        }
        value.previous = sample;
        if (state.complete) {
            var round = state.rounds[state.rounds.length - 1];
            rememberPeer(value.node, value.kind, round.number);
            rememberPeer(component, value.kind + '-ComponentJS', round.number);
            if (sample.velocity_x !== undefined && sample.velocity_x !== 0 && value.kind === 'enemy') round.naturalMovement = true;
        }
        return value.id;
    }
    function viewMapping() {
        var frame = cc.view.getFrameSize(), design = cc.view.getDesignResolutionSize();
        var policy = cc.view.getResolutionPolicy(), sx = frame.width / design.width, sy = frame.height / design.height;
        if (policy === cc.ResolutionPolicy.SHOW_ALL) sx = sy = Math.min(sx, sy);
        else if (policy === cc.ResolutionPolicy.NO_BORDER) sx = sy = Math.max(sx, sy);
        else if (policy === cc.ResolutionPolicy.FIXED_HEIGHT) sx = sy;
        else if (policy === cc.ResolutionPolicy.FIXED_WIDTH) sy = sx;
        return {frame_size: frame, design: design, policy: policy, scale_x: sx, scale_y: sy,
            viewport_x: (frame.width - design.width * sx) / 2,
            viewport_y: (frame.height - design.height * sy) / 2};
    }
    function aim(value, sample) {
        if (state.complete && state.rounds.length > 1) return;
        if (!value.previous || sample.velocity_x === undefined) return;
        var visible = cc.director.getVisibleSize(), origin = cc.director.getVisibleOrigin();
        var px = origin.x + 20, py = origin.y + visible.height / 2;
        var vx = sample.velocity_x, dx = sample.x - px, dy = sample.y - py;
        var a = vx * vx - 960 * 960, b = 2 * dx * vx, c = dx * dx + dy * dy;
        var discriminant = b * b - 4 * a * c;
        if (a >= 0 || discriminant < 0 || dx <= 0) return;
        var time = (-b - Math.sqrt(discriminant)) / (2 * a);
        var x = sample.x + vx * (time + state.inputLead), realX = origin.x + visible.width + 10;
        if (x <= px) return;
        // Preserve the original player.js realY formula, including its realX
        // rather than offRealX multiplier. Only the suggested real tap changes.
        var y = py + (sample.y - py) * (realX - px) / realX;
        var mapping = state.mapping;
        log('native_input_target', {enemy: value.id, canvas_x: x, canvas_y: y,
            screen_x: Math.round(mapping.viewport_x + x * mapping.scale_x),
            screen_y: Math.round(mapping.frame_size.height - mapping.viewport_y - y * mapping.scale_y),
            coordinate_scope: 'RenderView window coordinates; verify ADB surface dimensions match the recorded native frame'});
    }
    function audioSummary() {
        var result = {background_started: false, background_advanced: false,
            projectile_started: false, restart_background_started: false, native_stop_all_calls: state.audioStops};
        state.audio.forEach(function (track) {
            if (track.file === 'background-music-aac.mp3' && track.id >= 0) {
                result.background_started = true;
                result.background_advanced = result.background_advanced || track.advanced;
                if (track.round >= 2) result.restart_background_started = true;
            }
            if (track.file === 'pew-pew-lei.wav' && track.id >= 0) result.projectile_started = true;
        });
        return result;
    }
    function observeAudio() {
        state.audio.forEach(function (track) {
            if (track.id < 0 || track.closed) return;
            var status = jsb.AudioEngine.getState(track.id), time = jsb.AudioEngine.getCurrentTime(track.id);
            if (status === jsb.AudioEngine.AudioState.PLAYING && time > 0 && !track.advanced) {
                track.advanced = true;
                log('native_audio_advanced', {id: track.id, file: track.file, round: track.round, state: status, native_time: time});
            }
            if (status === jsb.AudioEngine.AudioState.ERROR) track.closed = true;
        });
    }
    function installAudioObserver() {
        var audio = jsb.AudioEngine;
        state.savedPlay = audio.play2d;
        state.savedStop = audio.stopAll;
        state.playObserver = function () {
            var id = state.savedPlay.apply(this, arguments);
            var track = {id: id, file: arguments[0], round: state.gameOver && state.rounds.length === 1 ? 2 : Math.max(1, state.rounds.length),
                advanced: false, closed: false};
            state.audio.push(track);
            log('native_audio_play', {id: id, file: track.file, round: track.round, loop: !!arguments[1]});
            return id;
        };
        state.stopObserver = function () {
            var result = state.savedStop.apply(this, arguments);
            state.audioStops++;
            log('native_audio_stop_all', {calls: state.audioStops});
            return result;
        };
        audio.play2d = state.playObserver;
        audio.stopAll = state.stopObserver;
    }
    function cleanup() {
        state.listeners.forEach(function (listener) { cc.eventManager.removeListener(listener); });
        state.listeners = [];
        state.rounds.forEach(function (round) {
            // Restore only our own instance replacement, preserving later
            // application changes and the original prototype lookup behavior.
            if (round.component.onExit !== round.exitObserver) return;
            if (round.hadOwnExit) round.component.onExit = round.originalExit;
            else delete round.component.onExit;
        });
        if (jsb.AudioEngine.play2d === state.playObserver) jsb.AudioEngine.play2d = state.savedPlay;
        if (jsb.AudioEngine.stopAll === state.stopObserver) jsb.AudioEngine.stopAll = state.savedStop;
    }
    function finish(status, message) {
        if (AxmolComponentGameplayProbeResult) return;
        var first = state.rounds[0];
        AxmolComponentGameplayProbeResult = {status: status, mode: state.mode, error: message || null,
            frames: state.frames, seconds: elapsed(), outcome: state.outcome, restarted: state.rounds.length >= 2,
            score: first ? first.maxScore : null, enemy_spawns: state.enemySpawns,
            native_position_changed: state.nativeMovement, native_input_events: state.inputEvents,
            native_projectiles: state.projectiles, score_removed_enemies: state.scoreRemovals,
            custom_restart_events: state.customEvents, game_over_seconds: state.gameOverSeconds,
            prior_native_scene_destroyed: first ? !valid(first.scene) : false,
            audio: audioSummary(),
            scope: 'Unchanged original game, real native Scene/Component identity/positions/actions, original score/collision flow, native input and AudioEngine API state, original GameOver delay/custom-event/restart. Audio API state does not prove acoustic output; screenshots remain visual evidence.'};
        if (state.complete) {
            AxmolComponentGameplayProbeResult.complete = true;
            AxmolComponentGameplayProbeResult.passed = status === 'PASS';
            AxmolComponentGameplayProbeResult.completed_original_main_menu = state.menuActivations === 1 && state.controllerFrames >= 6;
            AxmolComponentGameplayProbeResult.original_menu_activations = state.menuActivations;
            AxmolComponentGameplayProbeResult.round_two_natural_enemy = !!state.roundTwoReady;
            AxmolComponentGameplayProbeResult.all_departed_native_peers_invalid = !!state.peersChecked;
            AxmolComponentGameplayProbeResult.destroyed_native_peer_count = state.peers.length;
            AxmolComponentGameplayProbeResult.capture_files = state.captureFiles.slice();
            AxmolComponentGameplayProbeResult.status = status === 'PASS' ? 'FUNCTIONAL_PASS_IMAGES_PENDING' : status;
            log('complete_result', AxmolComponentGameplayProbeResult);
        }
        cleanup();
        // Emit the bounded summary separately from the longer explanatory scope.
        var summary = {};
        Object.keys(AxmolComponentGameplayProbeResult).forEach(function (key) {
            if (key !== 'scope') summary[key] = AxmolComponentGameplayProbeResult[key];
        });
        cc.log('COMPONENT_GAMEPLAY_RESULT ' + JSON.stringify(state.complete ? {
            status: AxmolComponentGameplayProbeResult.status, mode: state.mode,
            complete: true, error: message || null, native_peers: state.peers.length
        } : summary));
    }
    function findLayer(scene) {
        var children = scene.getChildren();
        for (var index = 0; index < children.length; index++) {
            if (children[index] instanceof ComponentTestLayer) return children[index];
        }
        return null;
    }
    // Optional complete-category observations. The baseline outcome/restart
    // branch below stays intact; these never call gameplay callbacks.
    function rememberPeer(object, role, round) {
        if (!state.complete || !valid(object)) return null;
        for (var i = 0; i < state.peers.length; ++i) {
            if (state.peers[i].object === object) return state.peers[i];
        }
        var peer = {object: object, id: 'peer-' + (state.peers.length + 1), role: role, round: round,
            getter: object instanceof cc.Component ? 'getOwner' : 'getPosition'};
        state.peers.push(peer);
        log('native_peer_observed', {id: peer.id, role: role, round: round, getter: peer.getter,
            actual_native_valid: true, actual_native_reference_count: object.getReferenceCount()});
        return peer;
    }
    function collectPeers(scene, round, label) {
        rememberPeer(scene, label + '-Scene', round);
        function visit(node, role) {
            rememberPeer(node, role, round);
            ['sceneJSComponent', ''].forEach(function (name) {
                var component = node.getComponent(name);
                if (component instanceof cc.Component) {
                    check(component.getOwner() === node, 'observed native component retains exact owner');
                    rememberPeer(component, role + '-ComponentJS', round);
                }
            });
            node.getChildren().forEach(function (child, index) { visit(child, role + '-child-' + index); });
        }
        scene.getChildren().forEach(function (child, index) { visit(child, label + '-child-' + index); });
    }
    function captureComplete(name, facts) {
        var scene = cc.director.getRunningScene();
        check(scene && scene === state.lastAfterDrawScene && scene.isRunning(), 'same native Scene through actual after_draw capture');
        var file = ('original-component-' + state.token + '-' + state.mode + '-' + name + '.png').toLowerCase();
        check(!state.captured[name] && v3Capture(file) === true, 'unique actual after_draw framebuffer capture accepted');
        state.captured[name] = true; state.captureFiles.push('v3-captures/' + file);
        log('complete_capture', {file: 'v3-captures/' + file, actual_phase: 'director_after_draw',
            same_native_scene: true, original_native_scene_running: scene.isRunning(),
            native_frame: cc.director.getTotalFrames(), facts: facts});
    }
    function secondBackground() {
        return state.audio.filter(function (track) {
            return track.round === 2 && track.file === 'background-music-aac.mp3' && track.id >= 0 && track.advanced;
        });
    }
    function inspectCompleteRound(round) {
        collectPeers(round.scene, round.number, 'game-' + round.number);
        if (round.number === 1 && !state.captured['first-game'] && round.enemies.length && round.naturalMovement) {
            captureComplete('first-game', {round: 1, native_layer_identity: round.layer.getParent() === round.scene,
                actual_native_score: round.maxScore, original_enemy_count: round.enemies.length,
                original_enemy_native_action_and_movement: true, native_background: audioSummary()});
        }
        if (round.number !== 2 || state.completePhase !== 'second-game' || !state.gameplayAccepted) return;
        if (!round.enemies.length || !round.naturalMovement || !secondBackground().length) return;
        check(round.maxScore === 0, 'second original game has its own untouched zero score before MainMenu');
        var enemies = round.component.getScriptObject().enemies;
        check(enemies.length >= 1 && enemies.every(function (node) {
            return valid(node) && node.getParent() === round.layer && node.isRunning() && node.getNumberOfRunningActions() > 0;
        }), 'second game naturally creates live native moving enemies');
        check(round.layer.getChildren().some(function (node) {
            var component = node.getComponent('');
            return component instanceof cc.ComponentJS && node.playerComponent === component && component.getOwner() === node;
        }), 'second game has the real original player and ComponentJS');
        state.roundTwoReady = true;
        var tracks = secondBackground().map(function (track) {
            var status = jsb.AudioEngine.getState(track.id), time = jsb.AudioEngine.getCurrentTime(track.id);
            check(status === jsb.AudioEngine.AudioState.PLAYING && time > 0, 'restarted native background audio truly advances');
            return {id: track.id, round: track.round, file: track.file, native_state: status, native_time: time};
        });
        log('second_game_natural_ready', {round: 2, original_score: round.maxScore,
            actual_native_enemy_count: enemies.length, actual_native_movement: true,
            original_player_component_identity: true, native_background_tracks: tracks,
            original_gameplay_callbacks_or_random_modified: false});
        captureComplete('second-game-natural', {round: 2, native_layer_identity: round.layer.getParent() === round.scene,
            native_enemy_count: enemies.length, actual_native_score: round.maxScore, native_background_tracks: tracks});
        state.completePhase = 'ready-original-main-menu';
    }
    function inspectDepartedPeers() {
        state.peers.forEach(function (peer) {
            check(!valid(peer.object), 'departed original native ' + peer.role + ' must be destroyed');
            var error = null;
            try { peer.object[peer.getter](); } catch (caught) { error = caught; }
            check(error && String(error).length, 'expired native peer getter must raise a controlled exception');
            log('departed_native_peer', {id: peer.id, role: peer.role, round: peer.round,
                actual_native_valid: false, getter: peer.getter, getter_raised_controlled_error: true,
                actual_exception: String(error)});
        });
        state.peersChecked = true;
    }
    function inspectController(scene) {
        var controllers = scene.getChildren().filter(function (node) { return node instanceof TestController; });
        if (controllers.length !== 1) return;
        check(!(scene instanceof cc.TransitionScene) && scene.isRunning() && cc.eventManager.isEnabled(),
            'original MainMenu returns through its transition and restores real EventDispatcher');
        var controller = controllers[0];
        check(controller.getParent() === scene && controller.isRunning() && controller._itemMenu instanceof cc.Menu,
            'canonical live original TestController and native main category Menu');
        ++state.controllerFrames;
        if (state.controllerFrames < 6) return;
        check(state.menuActivations === 1 && state.sawTransition && state.sawDisabledDispatcher,
            'one original MainMenu callback used its real transition and temporary dispatcher disable');
        check(state.audioStops > state.menuAudioStopsBefore, 'original second game onExit truly calls native AudioEngine.stopAll');
        var tracks = state.audio.map(function (track) {
            var status = jsb.AudioEngine.getState(track.id), time = jsb.AudioEngine.getCurrentTime(track.id);
            check(status !== jsb.AudioEngine.AudioState.PLAYING && status !== jsb.AudioEngine.AudioState.PAUSED,
                'all observed native audio IDs are stopped after original MainMenu exit');
            return {id: track.id, round: track.round, file: track.file, native_state: status, native_time: time};
        });
        inspectDepartedPeers();
        check(state.captured['first-game'] && state.captured['game-over'] && state.captured['second-game-natural'],
            'original game, Win/Lose and restarted game actual captures were not missed');
        log('original_main_menu_returned', {actual_native_controller_identity: true,
            controller_after_draw_frames: state.controllerFrames, dispatcher_enabled: true,
            original_native_menu_activations: state.menuActivations, native_audio_stop_all_calls: state.audioStops,
            stopped_native_audio_tracks: tracks, destroyed_native_peers: state.peers.length,
            all_departed_native_peers_invalid: true});
        captureComplete('original-main-menu-returned', {original_controller: true,
            native_category_menu_children: controller._itemMenu.getChildren().length,
            dispatcher_enabled: true, native_audio_stopped: true, destroyed_native_peers: state.peers.length,
            actual_native_text: controller._itemMenu.getChildren().map(function (item) { return item.getLabel().getString(); })});
        state.completePhase = 'complete'; finish('PASS');
    }
    function tickComplete() {
        if (state.completePhase === 'ready-original-main-menu') {
            var round = state.rounds[1], scene = cc.director.getRunningScene();
            check(scene === round.scene && round.layer.getParent() === scene && scene.isRunning(),
                'second original Scene remains identical until engine-phase MainMenu activation');
            var menu = scene._mainMenu, items = menu && menu.getChildren();
            check(menu instanceof cc.Menu && items.length === 1 && items[0] instanceof cc.MenuItem &&
                items[0].isEnabled() && items[0].getLabel().getString() === 'Main Menu',
                'exact original inherited enabled MainMenu item');
            state.menuAudioStopsBefore = state.audioStops; ++state.menuActivations;
            state.completePhase = 'await-original-controller';
            log('original_main_menu_activated', {via: 'native MenuItem.activate', original_callback: 'TestScene.onMainMenuCallback',
                original_native_label: items[0].getLabel().getString(), native_menu_activations: state.menuActivations,
                invocation_phase: 'engine-update', original_transition_seconds: 0.5});
            // Called from Tick, never from the actual after_draw observer.
            items[0].activate();
        }
        if (state.completePhase === 'await-original-controller') {
            var active = cc.director.getRunningScene();
            if (active instanceof cc.TransitionScene) {
                if (!state.sawTransition) log('original_main_menu_transition', {actual_native_transition: true,
                    actual_native_dispatcher_enabled: cc.eventManager.isEnabled()});
                state.sawTransition = true;
                if (!cc.eventManager.isEnabled()) state.sawDisabledDispatcher = true;
            }
        }
    }
    function inspectRound(round, active) {
        var script = round.component.getScriptObject();
        check(script === round.component, 'Scene ComponentJS canonical identity');
        if (active) check(script.getOwner() === round.layer, 'running Scene ComponentJS native owner');
        check(Array.isArray(script.enemies), 'original Scene component onEnter did not initialize enemies');
        var score = script.numEnemyDestroyed;
        check(typeof score === 'number' && score >= round.maxScore, 'original native-driven score moved backwards');
        var enemies = script.enemies.slice(), ids = [];
        enemies.forEach(function (node) {
            var entry = entity(node, 'enemy');
            if (!entry.spawned) { entry.spawned = true; state.enemySpawns++; }
            ids.push(entry.id);
            if (active) sampleNode(entry, round.layer);
        });
        if (score > round.maxScore) {
            var removed = round.enemies.filter(function (id) { return ids.indexOf(id) < 0; });
            state.scoreRemovals += removed.length;
            log('original_score_collision', {from: round.maxScore, to: score, removed_enemies: removed,
                previously_observed_native_projectiles: state.projectiles});
        }
        round.maxScore = score;
        round.enemies = ids;
        if (active) round.layer.getChildren().forEach(function (node) {
            if (!(node instanceof cc.Sprite)) return;
            var component = node.getComponent('');
            if (!(component instanceof cc.ComponentJS)) return;
            if (node.playerComponent === component) sampleNode(entity(node, 'player'), round.layer);
            else if (component.sceneJS === script) {
                var projectile = entity(node, 'projectile');
                if (!projectile.spawned) { projectile.spawned = true; state.projectiles++; }
                sampleNode(projectile, round.layer);
            }
        });
    }
    function installExitObserver(round) {
        round.hadOwnExit = Object.prototype.hasOwnProperty.call(round.component, 'onExit');
        round.originalExit = round.component.onExit;
        check(typeof round.originalExit === 'function', 'original Scene Component onExit callback');
        function snapshot(phase) {
            if (AxmolComponentGameplayProbeResult) return;
            try {
                // Native Component::onExit still owns the live script receiver.
                // The fifth original projectile collision has already changed
                // its real score/enemies; after_draw may see an expired wrapper.
                inspectRound(round, false);
                log('original_component_exit', {phase: phase, native_component_identity: valid(round.component),
                    actual_original_score: round.maxScore, remaining_enemies: round.enemies.slice()});
            } catch (failure) { finish('FAIL', String(failure)); }
        }
        round.exitObserver = function () {
            snapshot('before-original-onExit');
            // Preserve the actual call, receiver, arguments and return value.
            // Observer errors never prevent the original lifecycle callback.
            try { return round.originalExit.apply(this, arguments); }
            finally { snapshot('after-original-onExit'); }
        };
        round.component.onExit = round.exitObserver;
    }
    function observe() {
        if (AxmolComponentGameplayProbeResult) return;
        state.frames++;
        var scene = cc.director.getRunningScene();
        check(scene && scene.isRunning(), 'no running native Scene');
        if (state.complete) {
            state.lastAfterDrawScene = scene;
            if (state.completePhase === 'await-original-controller') { observeAudio(); inspectController(scene); return; }
        }
        var layer = findLayer(scene), round = state.rounds[state.rounds.length - 1];
        if (layer) {
            if (!round || round.scene !== scene) {
                var component = layer.getComponent('sceneJSComponent');
                check(component instanceof cc.ComponentJS, 'original Scene component missing');
                round = {scene: scene, layer: layer, component: component, maxScore: 0, enemies: [],
                    firstFrame: state.frames};
                state.rounds.push(round);
                if (state.complete) {
                    round.number = state.rounds.length; round.naturalMovement = false;
                    check(round.number <= 2, 'complete observer must not hide repeated original game-over/listener behavior');
                    state.completePhase = round.number === 2 ? 'second-game' : 'first-game';
                    rememberPeer(scene, 'game-' + round.number + '-Scene', round.number);
                    rememberPeer(layer, 'game-layer', round.number);
                    rememberPeer(component, 'scene-ComponentJS', round.number);
                }
                installExitObserver(round);
                log('original_round_entered', {round: state.rounds.length, native_component: true,
                    native_scene_running: scene.isRunning(), native_layer_running: layer.isRunning()});
                if (state.rounds.length === 2) {
                    state.gameOverSeconds = elapsed() - state.gameOver;
                    log('original_restart', {native_custom_events: state.customEvents,
                        actual_game_over_seconds: state.gameOverSeconds});
                }
            }
            inspectRound(round, true);
            if (state.complete) inspectCompleteRound(round);
        } else if (round && !state.gameOver) {
            // onExit captured the final score while the native receiver lived.
            // Read again only if this rendered-frame observation remains live.
            if (valid(round.component)) inspectRound(round, false);
            scene.getChildren().forEach(function (child) {
                if (!(child instanceof GameOverLayer)) return;
                child.getChildren().forEach(function (label) {
                    if (label instanceof cc.Label) state.outcome = label.getString();
                });
            });
            check(state.outcome === 'You Lose' || state.outcome === 'You Win!', 'original GameOver Label missing');
            state.gameOver = elapsed();
            if (state.complete) { state.gameOverScene = scene; state.gameOverDraws = 0; state.completePhase = 'original-game-over'; }
            log('original_game_over', {label: state.outcome, original_score: round.maxScore,
                prior_scene_running: valid(round.scene) ? round.scene.isRunning() : false,
                prior_layer_running: valid(round.layer) ? round.layer.isRunning() : false});
        }
        if (state.complete && state.completePhase === 'original-game-over') {
            check(scene === state.gameOverScene, 'same original GameOver Scene before natural three-second restart');
            collectPeers(scene, 0, 'game-over'); ++state.gameOverDraws;
            if (state.gameOverDraws >= 2 && !state.captured['game-over']) {
                captureComplete('game-over', {actual_native_label: state.outcome,
                    actual_original_score: state.rounds[0].maxScore, native_after_draw_frames: state.gameOverDraws,
                    genuine_original_game_over_scene: true});
            }
        }
        observeAudio();
        if (state.rounds.length >= 2 && state.frames - round.firstFrame >= 5) {
            var audio = audioSummary(), first = state.rounds[0];
            check(state.customEvents >= 1 && state.gameOverSeconds >= 2.5, 'original three-second custom-event restart did not occur');
            check(state.enemySpawns > 0 && state.nativeMovement && !valid(first.scene), 'original enemies/actions/prior Scene cleanup not observed');
            check(audio.background_started && audio.background_advanced && audio.restart_background_started &&
                audio.native_stop_all_calls >= 2, 'original background audio advance/stop/restart not observed');
            if (state.mode === 'lose') check(state.outcome === 'You Lose' && first.maxScore === 0 &&
                state.inputEvents === 0 && state.projectiles === 0, 'natural no-input lose path was not preserved');
            else check(state.outcome === 'You Win!' && first.maxScore >= 5 && state.inputEvents >= 1 &&
                state.projectiles >= 1 && state.scoreRemovals >= 5 && audio.projectile_started,
                'five genuine original projectile collisions/native inputs were not observed');
            if (!state.complete) finish('PASS');
            else if (!state.gameplayAccepted) {
                state.gameplayAccepted = true;
                log('original_gameplay_baseline_accepted', {outcome: state.outcome,
                    actual_original_score: first.maxScore, native_input_events: state.inputEvents,
                    native_projectiles: state.projectiles, native_score_enemy_removals: state.scoreRemovals,
                    original_delay_seconds: state.gameOverSeconds, old_native_scene_invalid: !valid(first.scene)});
            }
        }
        if (elapsed() >= state.timeout) finish('TIMEOUT', 'Original game did not complete the expected outcome and restart');
    }
    global.axmolStartComponentGameplayProbe = function (options) {
        check(!state, 'probe already started');
        check(typeof ComponentTestScene === 'function' && typeof ComponentTestLayer === 'function',
            'the unchanged original ComponentTest scripts must be loaded');
        options = options || {};
        var mode = options.mode || 'lose';
        check(mode === 'lose' || mode === 'win', 'mode must be lose or win');
        var complete = options.complete === true;
        if (complete) check(typeof v3Record === 'function' && typeof v3Capture === 'function' && global.V3SuiteState &&
            typeof V3SuiteState.runNonce === 'string' && V3SuiteState.runNonce &&
            typeof V3SuiteState.processId === 'number' && V3SuiteState.processId > 0,
            'complete observer requires actual native run nonce/PID and private/capture API');
        state = {mode: mode, timeout: options.timeout || (complete ? 60 : 25), inputLead: options.inputLead === undefined ? 0.15 : options.inputLead,
            complete: complete, token: String(Date.now()), nonce: complete ? V3SuiteState.runNonce : null,
            pid: complete ? V3SuiteState.processId : null, completePhase: 'first-game', peers: [], captured: {}, captureFiles: [],
            menuActivations: 0, controllerFrames: 0, sawTransition: false, sawDisabledDispatcher: false,
            started: Date.now(), frames: 0, rounds: [], entities: [], listeners: [], audio: [], audioStops: 0,
            enemySpawns: 0, nativeMovement: false, inputEvents: 0, projectiles: 0, scoreRemovals: 0,
            customEvents: 0, outcome: null, gameOver: null, gameOverSeconds: null, mapping: viewMapping()};
        log('native_view_mapping', state.mapping);
        if (complete) log('complete_configuration', {complete: true, mode: mode,
            original_source_callbacks_modified: false, original_entry: 'ComponentTestScene.runThisTest',
            planned_native_main_menu_activations: 1, expected_original_game_over_delay: 3,
            expected_original_main_menu_transition_seconds: 0.5, real_input_required_for_win: true,
            acoustic_output_verified: false});
        installAudioObserver();
        state.listeners.push(cc.eventManager.addCustomListener('director_after_draw', function () {
            try { observe(); } catch (failure) { finish('FAIL', String(failure)); }
        }));
        state.listeners.push(cc.eventManager.addCustomListener('component game over', function (event) {
            state.customEvents++;
            log('native_custom_restart_event', {event_name: event.getEventName(), count: state.customEvents});
        }));
        state.listeners.push(cc.eventManager.addListener({event: cc.EventListener.TOUCH_ALL_AT_ONCE,
            onTouchesEnded: function (touches) {
                if (!state.rounds.length) return;
                state.inputEvents++;
                var point = touches[0].getLocation();
                log('native_input_ended', {count: state.inputEvents, canvas_x: point.x, canvas_y: point.y});
            }}, 2));
        // This uses the unchanged test's actual construction/entry method, in
        // the Scheduler phase. after_draw never constructs or replaces a Scene.
        new ComponentTestScene().runThisTest();
        return true;
    };
    global.axmolTickComponentGameplayProbe = function () {
        if (state && state.complete && !AxmolComponentGameplayProbeResult) {
            try { tickComplete(); } catch (failure) { finish('FAIL', String(failure) + '\n' + (failure.stack || '')); }
        }
        if (state && !AxmolComponentGameplayProbeResult && elapsed() >= state.timeout)
            finish('TIMEOUT', 'No completed rendered original game/restart within the allotted time');
        return AxmolComponentGameplayProbeResult;
    };
    global.axmolComponentGameplayCompleteContract = function () {
        return {optional_flag: 'complete', default_baseline_unchanged: true,
            stages: ['first-game', 'game-over', 'second-game-natural', 'original-main-menu-returned'],
            planned_native_main_menu_activations: 1, original_delay: 3, original_transition: 0.5,
            start: 'axmolStartComponentGameplayProbe', tick: 'axmolTickComponentGameplayProbe',
            capture_phase: 'director_after_draw', navigation_phase: 'engine-update',
            original_entry: 'ComponentTestScene.runThisTest', original_sources_modified: false};
    };
})(this);
