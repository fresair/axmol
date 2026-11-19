/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
/* Native Cocos2d-JS compatibility bootstrap. Runs as a classic global script. */
(function (global) {
    var cc = global.cc, jsb = global.jsb;
    global.window = global;
    function own(object, key) { return Object.prototype.hasOwnProperty.call(object, key); }
    function size(value) { return {width: value.width === undefined ? value.x : value.width,
        height: value.height === undefined ? value.y : value.height}; }
    function extend(properties) {
        var Base = this, parent = Base.prototype, nativeBase = Base.__nativeBase || Base;
        if (Base === cc.Class || Base.__plainClass) nativeBase = null;
        function Derived() {
            var self = nativeBase ? (typeof nativeBase.__allocate === 'function' ? nativeBase.__allocate() : new nativeBase()) : this;
            if (nativeBase) {
                // Closure-style subclasses call their generated parent on an
                // existing receiver and intentionally ignore its return value.
                if (this instanceof Derived && this.constructor !== Derived)
                    self = jsb._associateObjectWithNative(this, self);
                else Object.setPrototypeOf(self, Derived.prototype);
            }
            if (self.ctor && self.ctor.apply(self, arguments) === false && nativeBase)
                throw new Error('Native subclass initialization failed');
            return self;
        }
        Derived.prototype = Object.create(parent);
        Derived.prototype.constructor = Derived;
        Derived.__nativeBase = nativeBase;
        Derived.__plainClass = !nativeBase;
        Derived.extend = extend;
        Object.keys(properties).forEach(function (name) {
            var method = properties[name], base = parent[name];
            if (typeof method === 'function' && typeof base === 'function') {
                Derived.prototype[name] = function () {
                    var old = this._super;
                    this._super = base;
                    try { return method.apply(this, arguments); }
                    finally { if (old === undefined) delete this._super; else this._super = old; }
                };
            } else Derived.prototype[name] = method;
        });
        return Derived;
    }
    cc.Class = function () { if (this.ctor) this.ctor.apply(this, arguments); };
    cc.Class.extend = extend;
    cc.associateWithNative = function (receiver, superclassOrInstance) {
        if(!receiver || typeof receiver!=='object')throw new TypeError('Native association requires an object');
        // v3 retains this historical entry point after automatic .extend
        // association. Preserve that existing native identity when present.
        if(jsb.isObjectValid(receiver))return;
        var native=superclassOrInstance;
        if(typeof native==='function')native=typeof native.__allocate==='function'?native.__allocate():new native();
        jsb._associateObjectWithNative(receiver,native);
    };
    cc.each = function (object, fn, receiver) {
        if (!object) return;
        var keys = Array.isArray(object) ? object.map(function (_, index) { return index; }) : Object.keys(object);
        for (var i = 0; i < keys.length; ++i) if (fn.call(receiver, object[keys[i]], keys[i]) === false) break;
    };
    cc.extend = function (target) {
        for (var i = 1; i < arguments.length; ++i) {
            var source = arguments[i];
            if (source) Object.keys(source).forEach(function (key) { target[key] = source[key]; });
        }
        return target;
    };
    cc.defineGetterSetter = function (prototype, name, getter, setter) {
        Object.defineProperty(prototype, name, {get: getter, set: setter, enumerable: true, configurable: true});
    };
    cc.isFunction = function (v) { return typeof v === 'function'; };
    cc.isNumber = function (v) { return typeof v === 'number'; };
    cc.isString = function (v) { return typeof v === 'string'; };
    cc.isArray = Array.isArray;
    cc.isObject = function (v) { return v !== null && typeof v === 'object'; };
    cc.isUndefined = function (v) { return v === undefined; };
    cc.p = function (x, y) { return typeof x === 'object' ? {x: x.x, y: x.y} : {x: x || 0, y: y || 0}; };
    cc.size = function (w, h) { return typeof w === 'object' ? size(w) : {width: w || 0, height: h || 0}; };
    cc.rect = function (x, y, w, h) { return typeof x === 'object' ? cc.extend({}, x) : {x:x || 0,y:y || 0,width:w || 0,height:h || 0}; };
    cc.color = function (r, g, b, a) {
        if (typeof r === 'object') return {r:r.r,g:r.g,b:r.b,a:r.a === undefined ? 255 : r.a};
        if (typeof r === 'string') { var n = parseInt(r.replace('#', ''),16); return {r:n>>16&255,g:n>>8&255,b:n&255,a:255}; }
        return {r:r || 0,g:g || 0,b:b || 0,a:a === undefined ? 255 : a};
    };
    ['WHITE','YELLOW','BLUE','GREEN','RED','MAGENTA','BLACK','ORANGE','GRAY'].forEach(function (name,i) {
        var rgb = [0xffffff,0xffff00,0x0000ff,0x00ff00,0xff0000,0xff00ff,0,0xff7f00,0x808080][i];
        cc.color[name] = cc.color(rgb>>16&255,rgb>>8&255,rgb&255);
    });
    cc.pAdd = function(a,b){return cc.p(a.x+b.x,a.y+b.y);};
    cc.pSub = function(a,b){return cc.p(a.x-b.x,a.y-b.y);};
    cc.pMult = function(a,s){return cc.p(a.x*s,a.y*s);};
    cc.pNeg = function(a){return cc.p(-a.x,-a.y);};
    cc.pDot = function(a,b){return a.x*b.x+a.y*b.y;};
    cc.pCross = function(a,b){return a.x*b.y-a.y*b.x;};
    cc.pLengthSQ = function(a){return cc.pDot(a,a);};
    cc.pLength = function(a){return Math.sqrt(cc.pLengthSQ(a));};
    cc.pDistance = function(a,b){return cc.pLength(cc.pSub(a,b));};
    cc.pNormalize = function(a){var n=cc.pLength(a);return n ? cc.pMult(a,1/n) : cc.p();};
    cc.pLerp = function(a,b,t){return cc.pAdd(a,cc.pMult(cc.pSub(b,a),t));};
    cc.pForAngle = function(a){return cc.p(Math.cos(a),Math.sin(a));};
    cc.pToAngle = function(a){return Math.atan2(a.y,a.x);};
    cc.pSameAs = function(a,b){return a.x===b.x&&a.y===b.y;};
    cc.pointEqualToPoint = cc.pSameAs;
    cc.sizeEqualToSize = function(a,b){return a.width===b.width&&a.height===b.height;};
    cc.rectContainsPoint = function(r,p){return p.x>=r.x&&p.x<=r.x+r.width&&p.y>=r.y&&p.y<=r.y+r.height;};
    cc.rectIntersectsRect = function(a,b){return !(a.x+a.width<b.x||b.x+b.width<a.x||a.y+a.height<b.y||b.y+b.height<a.y);};
    cc.rectEqualToRect = function(a,b){return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;};
    cc.rectIntersection = function(a,b){var x=Math.max(a.x,b.x),y=Math.max(a.y,b.y);
        return cc.rect(x,y,Math.min(a.x+a.width,b.x+b.width)-x,Math.min(a.y+a.height,b.y+b.height)-y);};
    cc.rectUnion = function(a,b){var x=Math.min(a.x,b.x),y=Math.min(a.y,b.y);
        return cc.rect(x,y,Math.max(a.x+a.width,b.x+b.width)-x,Math.max(a.y+a.height,b.y+b.height)-y);};
    cc.degreesToRadians = function(v){return v*Math.PI/180;};
    cc.radiansToDegrees = function(v){return v*180/Math.PI;};
    cc.clampf = function(v,a,b){return Math.max(a,Math.min(b,v));};
    cc.REPEAT_FOREVER = 0xffffffff;
    cc.TEXT_ALIGNMENT_LEFT = 0; cc.TEXT_ALIGNMENT_CENTER = 1; cc.TEXT_ALIGNMENT_RIGHT = 2;
    cc.VERTICAL_TEXT_ALIGNMENT_TOP = 0; cc.VERTICAL_TEXT_ALIGNMENT_CENTER = 1; cc.VERTICAL_TEXT_ALIGNMENT_BOTTOM = 2;
    cc.ResolutionPolicy = {EXACT_FIT:0,NO_BORDER:1,SHOW_ALL:2,FIXED_HEIGHT:3,FIXED_WIDTH:4,UNKNOWN:5};
    cc.log = cc.log || global.log || global.print;
    cc.warn = cc.error = cc.log;
    // Only the public assert contract is selected here; logging stays unchanged.
    function initLegacyAssert(mode) {
        cc.assert = function () {};
        if (mode > 0) {
        cc.assert = function (cond, msg) {
            if (!cond && msg) {
                var args = [];
                for (var i = 1; i < arguments.length; i++)
                    args.push(arguments[i]);
                console.log("Assert: " + cc.formatStr.apply(cc, args));
            }
        };
        }
    }
    cc.assert = function () {};
    global.console = {log:cc.log,warn:cc.warn,error:cc.error,info:cc.log,debug:cc.log};
    global.setTimeout = function(fn,ms) { var args=Array.prototype.slice.call(arguments,2); return jsb._schedule(function(){fn.apply(global,args);},(ms||0)/1000,false); };
    global.setInterval = function(fn,ms) { var args=Array.prototype.slice.call(arguments,2); return jsb._schedule(function(){fn.apply(global,args);},(ms||0)/1000,true); };
    global.clearTimeout = global.clearInterval = function(id){jsb._cancelTimer(id);};

    (function installEs6Polyfills() {
        function def(target, name, val) {
            if (!target || target[name] !== undefined) return;
            Object.defineProperty(target, name, {value: val, writable: true, configurable: true, enumerable: false});
        }
        if (typeof global.Symbol !== 'function') {
            var symCounter = 0;
            var symRegistry = Object.create(null);
            var revRegistry = Object.create(null);
            var SymbolPolyfill = function Symbol(description) {
                if (this instanceof SymbolPolyfill) throw new TypeError('Symbol is not a constructor');
                var desc = description === undefined ? '' : String(description);
                var tag = '@@Symbol(' + desc + '):' + (++symCounter);
                return tag;
            };
            def(SymbolPolyfill, 'for', function (key) {
                var k = String(key);
                if (!own(symRegistry, k)) {
                    var s = SymbolPolyfill(k);
                    symRegistry[k] = s;
                    revRegistry[s] = k;
                }
                return symRegistry[k];
            });
            def(SymbolPolyfill, 'keyFor', function (sym) {
                return own(revRegistry, sym) ? revRegistry[sym] : undefined;
            });
            global.Symbol = SymbolPolyfill;
        }
        var Sym = global.Symbol;
        if (!Sym.iterator) Sym.iterator = typeof Sym === 'function' ? Sym('Symbol.iterator') : '@@iterator';
        if (!Sym.toStringTag) Sym.toStringTag = typeof Sym === 'function' ? Sym('Symbol.toStringTag') : '@@toStringTag';
        if (!Sym.hasInstance) Sym.hasInstance = typeof Sym === 'function' ? Sym('Symbol.hasInstance') : '@@hasInstance';
        if (!Sym.isConcatSpreadable) Sym.isConcatSpreadable = typeof Sym === 'function' ? Sym('Symbol.isConcatSpreadable') : '@@isConcatSpreadable';
        if (!Sym.toPrimitive) Sym.toPrimitive = typeof Sym === 'function' ? Sym('Symbol.toPrimitive') : '@@toPrimitive';

        def(Array.prototype, Sym.iterator, function () {
            var arr = this, idx = 0;
            return {
                next: function () {
                    return idx < arr.length ? {value: arr[idx++], done: false} : {value: undefined, done: true};
                }
            };
        });
        def(String.prototype, Sym.iterator, function () {
            var s = String(this), idx = 0;
            return {
                next: function () {
                    if (idx >= s.length) return {value: undefined, done: true};
                    var first = s.charCodeAt(idx);
                    if (first >= 0xD800 && first <= 0xDBFF && idx + 1 < s.length) {
                        var second = s.charCodeAt(idx + 1);
                        if (second >= 0xDC00 && second <= 0xDFFF) {
                            var pair = s.slice(idx, idx + 2);
                            idx += 2;
                            return {value: pair, done: false};
                        }
                    }
                    return {value: s.charAt(idx++), done: false};
                }
            };
        });

        def(Object, 'assign', function assign(target) {
            if (target === null || target === undefined) throw new TypeError('Cannot convert undefined or null to object');
            var to = Object(target);
            for (var i = 1; i < arguments.length; i++) {
                var nextSource = arguments[i];
                if (nextSource !== null && nextSource !== undefined) {
                    var keys = Object.keys(Object(nextSource));
                    for (var j = 0; j < keys.length; j++) {
                        var nextKey = keys[j];
                        to[nextKey] = nextSource[nextKey];
                    }
                    if (typeof Object.getOwnPropertySymbols === 'function') {
                        var syms = Object.getOwnPropertySymbols(Object(nextSource));
                        for (var k = 0; k < syms.length; k++) {
                            if (Object.prototype.propertyIsEnumerable.call(nextSource, syms[k])) {
                                to[syms[k]] = nextSource[syms[k]];
                            }
                        }
                    }
                }
            }
            return to;
        });
        def(Object, 'values', function values(obj) {
            if (obj === null || obj === undefined) throw new TypeError('Cannot convert undefined or null to object');
            var o = Object(obj), keys = Object.keys(o), out = new Array(keys.length);
            for (var i = 0; i < keys.length; i++) out[i] = o[keys[i]];
            return out;
        });
        def(Object, 'entries', function entries(obj) {
            if (obj === null || obj === undefined) throw new TypeError('Cannot convert undefined or null to object');
            var o = Object(obj), keys = Object.keys(o), out = new Array(keys.length);
            for (var i = 0; i < keys.length; i++) out[i] = [keys[i], o[keys[i]]];
            return out;
        });
        def(Object, 'fromEntries', function fromEntries(iterable) {
            if (iterable === null || iterable === undefined) throw new TypeError('Object.fromEntries requires an iterable');
            var obj = {};
            if (Array.isArray(iterable)) {
                for (var i = 0; i < iterable.length; i++) {
                    var pair = iterable[i];
                    if (!pair || typeof pair !== 'object') throw new TypeError('Iterator value is not an entry object');
                    obj[pair[0]] = pair[1];
                }
                return obj;
            }
            if (typeof iterable[Sym.iterator] === 'function') {
                var it = iterable[Sym.iterator](), step;
                while (!(step = it.next()).done) {
                    var p = step.value;
                    if (!p || typeof p !== 'object') throw new TypeError('Iterator value is not an entry object');
                    obj[p[0]] = p[1];
                }
                return obj;
            }
            if (typeof iterable.forEach === 'function') {
                iterable.forEach(function (v, k) { obj[k] = v; });
                return obj;
            }
            throw new TypeError('Object.fromEntries argument is not iterable');
        });

        def(Array.prototype, 'includes', function includes(searchElement, fromIndex) {
            var o = Object(this), len = o.length >>> 0;
            if (len === 0) return false;
            var n = fromIndex | 0;
            var k = Math.max(n >= 0 ? n : len - Math.abs(n), 0);
            while (k < len) {
                var el = o[k];
                if (searchElement === el || (searchElement !== searchElement && el !== el)) return true;
                k++;
            }
            return false;
        });
        def(Array.prototype, 'flat', function flat(depth) {
            var d = depth === undefined ? 1 : Number(depth);
            var result = [];
            (function flatten(arr, cur) {
                for (var i = 0; i < arr.length; i++) {
                    if (!(i in arr)) continue;
                    var el = arr[i];
                    if (Array.isArray(el) && cur > 0) flatten(el, cur - 1);
                    else result.push(el);
                }
            })(Object(this), d);
            return result;
        });
        def(Array.prototype, 'flatMap', function flatMap(callback, thisArg) {
            return Array.prototype. map.call(this, callback, thisArg).flat(1);
        });

        def(String.prototype, 'padStart', function padStart(targetLength, padString) {
            var s = String(this), len = targetLength >> 0;
            var pad = padString === undefined ? ' ' : String(padString);
            if (s.length >= len || pad.length === 0) return s;
            var fillLen = len - s.length;
            while (pad.length < fillLen) pad += pad;
            return pad.slice(0, fillLen) + s;
        });
        def(String.prototype, 'padEnd', function padEnd(targetLength, padString) {
            var s = String(this), len = targetLength >> 0;
            var pad = padString === undefined ? ' ' : String(padString);
            if (s.length >= len || pad.length === 0) return s;
            var fillLen = len - s.length;
            while (pad.length < fillLen) pad += pad;
            return s + pad.slice(0, fillLen);
        });
        def(String.prototype, 'trimStart', function trimStart() { return String(this).replace(/^\s+/, ''); });
        def(String.prototype, 'trimEnd', function trimEnd() { return String(this).replace(/\s+$/, ''); });
        def(String.prototype, 'trimLeft', String.prototype.trimStart);
        def(String.prototype, 'trimRight', String.prototype.trimEnd);

        if (Number.EPSILON === undefined) Object.defineProperty(Number, 'EPSILON', {value: Math.pow(2, -52)});
        if (Number.MAX_SAFE_INTEGER === undefined) Object.defineProperty(Number, 'MAX_SAFE_INTEGER', {value: 9007199254740991});
        if (Number.MIN_SAFE_INTEGER === undefined) Object.defineProperty(Number, 'MIN_SAFE_INTEGER', {value: -9007199254740991});
        def(Number, 'isSafeInteger', function isSafeInteger(v) {
            return typeof v === 'number' && Math.floor(v) === v && Math.abs(v) <= 9007199254740991;
        });

        if (typeof global.Promise !== 'function') {
            var PENDING = 0, FULFILLED = 1, REJECTED = 2;
            var microtaskQueue = [];
            var microtaskScheduled = false;

            function flushMicrotasks() {
                microtaskScheduled = false;
                while (microtaskQueue.length > 0) {
                    var job = microtaskQueue.shift();
                    job();
                }
            }
            function enqueueMicrotask(fn) {
                microtaskQueue.push(fn);
                if (!microtaskScheduled) {
                    microtaskScheduled = true;
                    global.setTimeout(flushMicrotasks, 0);
                }
            }
            function toIterableArray(iterable) {
                if (Array.isArray(iterable)) return iterable.slice();
                if (iterable && typeof iterable[Sym.iterator] === 'function') {
                    var out = [], it = iterable[Sym.iterator](), step;
                    while (!(step = it.next()).done) out.push(step.value);
                    return out;
                }
                if (iterable && typeof iterable.forEach === 'function') {
                    var arr = [];
                    iterable.forEach(function (v) { arr.push(v); });
                    return arr;
                }
                throw new TypeError('Argument is not iterable');
            }

            function PromisePolyfill(executor) {
                if (!(this instanceof PromisePolyfill)) throw new TypeError('Promise must be constructed via new');
                if (typeof executor !== 'function') throw new TypeError('Promise executor is not a function');
                this._state = PENDING;
                this._value = undefined;
                this._handlers = [];
                var self = this;
                var settled = false;
                function resolve(val) {
                    if (settled) return;
                    settled = true;
                    resolvePromise(self, val);
                }
                function reject(reason) {
                    if (settled) return;
                    settled = true;
                    settlePromise(self, REJECTED, reason);
                }
                try {
                    executor(resolve, reject);
                } catch (err) {
                    reject(err);
                }
            }

            function settlePromise(promise, state, value) {
                if (promise._state !== PENDING) return;
                promise._state = state;
                promise._value = value;
                var handlers = promise._handlers;
                promise._handlers = null;
                for (var i = 0; i < handlers.length; i++) {
                    scheduleHandler(promise, handlers[i]);
                }
            }

            function resolvePromise(promise, x) {
                if (promise === x) {
                    settlePromise(promise, REJECTED, new TypeError('Chaining cycle detected for promise'));
                    return;
                }
                if (x instanceof PromisePolyfill) {
                    if (x._state === PENDING) {
                        x.then(function (v) { resolvePromise(promise, v); }, function (r) { settlePromise(promise, REJECTED, r); });
                    } else {
                        settlePromise(promise, x._state, x._value);
                    }
                    return;
                }
                if (x !== null && (typeof x === 'object' || typeof x === 'function')) {
                    var then;
                    try {
                        then = x.then;
                    } catch (err) {
                        settlePromise(promise, REJECTED, err);
                        return;
                    }
                    if (typeof then === 'function') {
                        var called = false;
                        try {
                            then.call(
                                x,
                                function (y) { if (!called) { called = true; resolvePromise(promise, y); } },
                                function (r) { if (!called) { called = true; settlePromise(promise, REJECTED, r); } }
                            );
                        } catch (err) {
                            if (!called) settlePromise(promise, REJECTED, err);
                        }
                        return;
                    }
                }
                settlePromise(promise, FULFILLED, x);
            }

            function scheduleHandler(promise, handler) {
                enqueueMicrotask(function () {
                    var cb = promise._state === FULFILLED ? handler.onFulfilled : handler.onRejected;
                    if (typeof cb !== 'function') {
                        if (promise._state === FULFILLED) resolvePromise(handler.promise, promise._value);
                        else settlePromise(handler.promise, REJECTED, promise._value);
                        return;
                    }
                    try {
                        var res = cb(promise._value);
                        resolvePromise(handler.promise, res);
                    } catch (err) {
                        settlePromise(handler.promise, REJECTED, err);
                    }
                });
            }

            def(PromisePolyfill.prototype, 'then', function then(onFulfilled, onRejected) {
                var child = new PromisePolyfill(function () {});
                var handler = {onFulfilled: onFulfilled, onRejected: onRejected, promise: child};
                if (this._state === PENDING) this._handlers.push(handler);
                else scheduleHandler(this, handler);
                return child;
            });
            def(PromisePolyfill.prototype, 'catch', function (onRejected) {
                return this.then(null, onRejected);
            });
            def(PromisePolyfill.prototype, 'finally', function (onFinally) {
                var P = this.constructor || PromisePolyfill;
                return this.then(
                    function (value) {
                        return P.resolve(typeof onFinally === 'function' ? onFinally() : undefined).then(function () { return value; });
                    },
                    function (reason) {
                        return P.resolve(typeof onFinally === 'function' ? onFinally() : undefined).then(function () { throw reason; });
                    }
                );
            });

            def(PromisePolyfill, 'resolve', function resolve(value) {
                if (value instanceof PromisePolyfill) return value;
                return new PromisePolyfill(function (res) { res(value); });
            });
            def(PromisePolyfill, 'reject', function reject(reason) {
                return new PromisePolyfill(function (_, rej) { rej(reason); });
            });
            def(PromisePolyfill, 'all', function all(iterable) {
                return new PromisePolyfill(function (resolve, reject) {
                    var items = toIterableArray(iterable);
                    var total = items.length;
                    if (total === 0) { resolve([]); return; }
                    var results = new Array(total);
                    var remaining = total;
                    for (var i = 0; i < total; i++) {
                        (function (idx) {
                            PromisePolyfill.resolve(items[idx]).then(function (val) {
                                results[idx] = val;
                                if (--remaining === 0) resolve(results);
                            }, reject);
                        })(i);
                    }
                });
            });
            def(PromisePolyfill, 'race', function race(iterable) {
                return new PromisePolyfill(function (resolve, reject) {
                    var items = toIterableArray(iterable);
                    for (var i = 0; i < items.length; i++) {
                        PromisePolyfill.resolve(items[i]).then(resolve, reject);
                    }
                });
            });
            def(PromisePolyfill, 'allSettled', function allSettled(iterable) {
                return new PromisePolyfill(function (resolve) {
                    var items = toIterableArray(iterable);
                    var total = items.length;
                    if (total === 0) { resolve([]); return; }
                    var results = new Array(total);
                    var remaining = total;
                    for (var i = 0; i < total; i++) {
                        (function (idx) {
                            PromisePolyfill.resolve(items[idx]).then(
                                function (val) {
                                    results[idx] = {status: 'fulfilled', value: val};
                                    if (--remaining === 0) resolve(results);
                                },
                                function (reason) {
                                    results[idx] = {status: 'rejected', reason: reason};
                                    if (--remaining === 0) resolve(results);
                                }
                            );
                        })(i);
                    }
                });
            });
            def(PromisePolyfill, '_flushMicrotasks', flushMicrotasks);
            global.Promise = PromisePolyfill;
        }
        def(global, 'queueMicrotask', function queueMicrotask(cb) {
            if (typeof cb !== 'function') throw new TypeError('queueMicrotask requires a callback function');
            global.Promise.resolve().then(cb);
        });
    })();

    function installClass(namespace,name) {
        var Type = namespace[name];
        if (typeof Type !== 'function' || !Type.prototype) return;
        if (typeof Type.create === 'function') {
            var Native = Type;
            Type = function () {
                var instance;
                if (own(Native.prototype, '__legacyCtor') && typeof Native.__allocate === 'function') {
                    instance = Native.__allocate();
                    if (this instanceof Type && this.constructor !== Type)
                        instance = jsb._associateObjectWithNative(this, instance);
                    if (instance.ctor.apply(instance, arguments) === false)
                        throw new Error('Native '+name+' initialization failed');
                    return instance;
                }
                instance = Native.create.apply(Native, arguments);
                if(instance===null)
                    throw new Error('Native '+name+' factory initialization failed');
                if (instance && this instanceof Type && this.constructor !== Type)
                    return jsb._associateObjectWithNative(this, instance);
                return instance;
            };
            Object.setPrototypeOf(Type, Native);
            Type.prototype = Native.prototype;
            Type.prototype.constructor = Type;
            namespace[name] = Type;
        }
        Type.extend = extend;
        if (!own(Type.prototype,'ctor')) Type.prototype.ctor = function(){return this.init ? this.init.apply(this,arguments) : true;};
        var factory = name.charAt(0).toLowerCase()+name.substring(1);
        if (Type.create && !namespace[factory]) namespace[factory] = function(){return Type.create.apply(Type,arguments);};
    }
    [cc,global.ccui,global.ccs,global.sp,jsb].forEach(function(namespace){
        if(namespace)Object.getOwnPropertyNames(namespace).forEach(function(name){installClass(namespace,name);});
    });
    function property(Type,name,getter,setter) {
        if (!Type) return;
        var p=Type.prototype;
        if (typeof p[getter] !== 'function') return;
        cc.defineGetterSetter(p,name,p[getter],typeof p[setter] === 'function' ? p[setter] : undefined);
    }
    var nodeProps={x:['getPositionX','setPositionX'],y:['getPositionY','setPositionY'],rotation:['getRotation','setRotation'],
        scale:['getScale','setScale'],scaleX:['getScaleX','setScaleX'],scaleY:['getScaleY','setScaleY'],
        anchorX:['getAnchorPointX','setAnchorPointX'],anchorY:['getAnchorPointY','setAnchorPointY'],
        visible:['isVisible','setVisible'],opacity:['getOpacity','setOpacity'],color:['getColor','setColor'],
        tag:['getTag','setTag'],name:['getName','setName'],zIndex:['getLocalZOrder','setLocalZOrder'],
        parent:['getParent','setParent'],children:['getChildren','_noSetter']};
    Object.keys(nodeProps).forEach(function(n){property(cc.Node,n,nodeProps[n][0],nodeProps[n][1]);});
    if(cc.Node){
        cc.Node.prototype.attr=function(attrs){var self=this;Object.keys(attrs).forEach(function(k){self[k]=attrs[k];});return this;};
        property(cc.Node,'width','getWidth','setWidth');property(cc.Node,'height','getHeight','setHeight');
    }
    if(cc.Sprite) cc.Sprite.prototype.ctor=function(file,rect){return file ? (rect ? this.initWithFile(file,rect):this.initWithFile(file)):this.init();};
    if(cc.Label){
        cc.LabelTTF=function(text,font,size,dimensions,hAlignment,vAlignment){
            return cc.Label.createWithSystemFont(text||'',font||'Arial',size||16,dimensions||cc.size(0,0),
                hAlignment===undefined?0:hAlignment,vAlignment===undefined?0:vAlignment);
        };
        cc.LabelTTF.create=cc.LabelTTF;
        cc.LabelTTF.prototype=cc.Label.prototype;cc.LabelTTF.extend=extend;
        cc.LabelTTF.prototype.ctor=function(text,font,size,dimensions,hAlignment,vAlignment){
            this.setSystemFontName(font||'Arial');this.setSystemFontSize(size||16);
            if(dimensions)this.setDimensions(dimensions.width,dimensions.height);
            if(hAlignment!==undefined)this.setHorizontalAlignment(hAlignment);
            if(vAlignment!==undefined)this.setVerticalAlignment(vAlignment);
            this.setString(text||'');return true;
        };
        property(cc.Label,'string','getString','setString');
        property(cc.Label,'fontSize','getSystemFontSize','setSystemFontSize');
        property(cc.Label,'fontName','getSystemFontName','setSystemFontName');
    }
    if(cc.Director){
        cc.director=cc.Director.getInstance();
        var dp=cc.Director.prototype;
        if(!dp.setDisplayStats)dp.setDisplayStats=dp.setStatsDisplay;
        if(!dp.isDisplayStats)dp.isDisplayStats=dp.isStatsDisplay;
        if(!dp.getWinSize)dp.getWinSize=function(){return size(this.getCanvasSize());};
    }
    if(cc.SpriteFrameCache)cc.spriteFrameCache=cc.SpriteFrameCache.getInstance();
    if(cc.AnimationCache)cc.animationCache=cc.AnimationCache.getInstance();
    if(cc.TextureCache && cc.director.getTextureCache)cc.textureCache=cc.director.getTextureCache();
    if(cc.FileUtils)jsb.fileUtils=cc.FileUtils.getInstance();
    Object.defineProperty(cc,'winSize',{get:function(){return cc.director.getWinSize();},configurable:true});
    cc.sys=cc.sys||{};
    var storageKeys = JSON.parse(jsb._storageGet('__keys__') || '[]');
    global.localStorage = {
        getItem:function(key){return jsb._storageGet('item.'+String(key));},
        setItem:function(key,value){key=String(key);jsb._storageSet('item.'+key,String(value));if(storageKeys.indexOf(key)<0){storageKeys.push(key);jsb._storageSet('__keys__',JSON.stringify(storageKeys));}},
        removeItem:function(key){key=String(key);jsb._storageDelete('item.'+key);var i=storageKeys.indexOf(key);if(i>=0)storageKeys.splice(i,1);jsb._storageSet('__keys__',JSON.stringify(storageKeys));},
        clear:function(){storageKeys.slice().forEach(function(key){global.localStorage.removeItem(key);});},
        key:function(index){return storageKeys[index]===undefined?null:storageKeys[index];}
    };
    Object.defineProperty(global.localStorage,'length',{get:function(){return storageKeys.length;}});
    cc.extend(cc.sys,{isNative:true,OS_IOS:'iOS',OS_ANDROID:'Android',OS_OSX:'OS X',OS_WINDOWS:'Windows',OS_LINUX:'Linux',
        BROWSER_TYPE_BAIDU:'baidu',BROWSER_TYPE_WECHAT:'wechat',BROWSER_TYPE_MOBILE_QQ:'mqq',
        capabilities:{canvas:false,opengl:true},localStorage:global.localStorage});
    var platform=jsb._platform();cc.sys.os=platform.os;cc.sys.isMobile=platform.mobile;cc.sys.platform=platform.id;
    // Legacy code selects input using property presence ('mouse' in ...), so
    // false values still choose the wrong branch. Preserve v3's native keys.
    if (platform.mobile) {
        cc.sys.capabilities.accelerometer=true;
        cc.sys.capabilities.touches=true;
    } else {
        cc.sys.capabilities.keyboard=true;
        cc.sys.capabilities.mouse=true;
    }
    cc.sys.language=platform.language;cc.sys.languageCode=platform.languageCode;
    cc.sys.cleanScript=jsb._cleanScript;cc.sys.openURL=jsb._openURL;
    cc.sys.isObjectValid=jsb.isObjectValid;
    cc.sys.garbageCollect=function(){jsb.garbageCollect();};
    // The complete legacy async/path/loader implementation is installed below.
    cc.game={config:null,onStart:null,RENDER_TYPE_CANVAS:0,RENDER_TYPE_WEBGL:1,RENDER_TYPE_OPENGL:2,EVENT_HIDE:'game_on_hide',EVENT_SHOW:'game_on_show',
        run:function(config,onStart){
            if(typeof config==='function'){onStart=config;config=null;}
            config=config || JSON.parse(jsb.fileUtils.getStringFromFile('project.json')||'{}');
            this.config=config;
            if(config.debugMode == null)config.debugMode=0;
            initLegacyAssert(config.debugMode);
            if(config.frameRate)cc.director.setAnimationInterval(1/config.frameRate);
            cc.director.setDisplayStats(!!config.showFPS);
            (config.jsList||[]).forEach(function(path){require(path);});
            if(onStart)this.onStart=onStart;
            if(this.onStart)this.onStart();
        },restart:function(){jsb.restart();},end:function(){cc.director.end();}};
    cc.pool={_pool:[],_list:function(prototype,create){
        for(var i=0;i<this._pool.length;i++)if(this._pool[i].prototype===prototype)return this._pool[i].objects;
        if(!create)return [];var list=[];this._pool.push({prototype:prototype,objects:list});return list;
    },putInPool:function(obj){
        var list=this._list(Object.getPrototypeOf(obj),true);if(list.indexOf(obj)>=0)return;
        if(obj.retain)jsb._poolRetain(obj);
        try{if(obj.unuse)obj.unuse();list.push(obj);}
        catch(error){if(obj.release)jsb._poolRelease(obj,false);throw error;}
    },hasObject:function(Type){return this._list(Type.prototype,false).length>0;},
        getFromPool:function(Type){var obj=this._list(Type.prototype,false).pop();if(obj){
            try{if(obj.reuse)obj.reuse.apply(obj,Array.prototype.slice.call(arguments,1));}
            finally{if(obj.release)jsb._poolRelease(obj,true);}
        }return obj;},
        removeObject:function(obj){var list=this._list(Object.getPrototypeOf(obj),false),index=list.indexOf(obj);
            if(index>=0){list.splice(index,1);if(obj.release)jsb._poolRelease(obj,false);}
        },drainAllPools:function(){this._pool.forEach(function(pool){pool.objects.forEach(function(obj){if(obj.release)jsb._poolRelease(obj,false);});});this._pool=[];}
    };
    require('script/jsb_legacy_helpers.js');
    require('script/jsb_legacy.js');
    require('script/jsb_legacy_native.js');
    require('script/jsb_legacy_tmx.js');
    require('script/jsb_legacy_label.js');
    require('script/jsb_legacy_bmfont.js');
    require('script/jsb_legacy_component.js');
    require('script/jsb_legacy_constants.js');
    require('script/jsb_legacy_properties.js');
    require('script/jsb_legacy_gl.js');
    require('script/jsb_legacy_polygon.js');
    require('script/jsb_legacy_input.js');
    require('script/jsb_legacy_draw.js');
    require('script/jsb_legacy_actions.js');
    require('script/jsb_legacy_navmesh.js');
    require('script/jsb_legacy_extensions.js');
    require('script/jsb_legacy_ui.js');
    require('script/jsb_legacy_physics.js');
    require('script/jsb_legacy_3d.js');
    require('script/jsb_legacy_loader.js');
    jsb.urlRegExp=/^(?:https?|ftp):\/\//i;
    cc.plistParser={parse:function (text) { return jsb.fileUtils.getValueMapFromData(text); }};
    require('script/jsb_legacy_loader_plugins.js');
    if(global.ccs && jsb.fileUtils.isFileExist('script/studio/jsb_studio_load.js'))require('script/axmol_studio.js');
})(this);
