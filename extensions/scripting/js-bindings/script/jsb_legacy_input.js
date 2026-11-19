/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
/* Legacy Layer input uses the same native dispatcher as EventListener.create. */
(function () {
    if(cc.EventKeyboard) {
        var NativeKeyboardEvent=cc.EventKeyboard;
        // v3's manual constructor maps cc.KEY through parseKeyCode and uses
        // JS truthiness for pressed. Axmol InputPhase::KeyDown/KeyUp are 5/6.
        function EventKeyboard(key,pressed) {
            return new NativeKeyboardEvent(parseKeyCode(key),pressed?5:6);
        }
        Object.setPrototypeOf(EventKeyboard,NativeKeyboardEvent);
        EventKeyboard.prototype=NativeKeyboardEvent.prototype;
        cc.EventKeyboard=EventKeyboard;
    }
    if(cc.Device) {
        cc.inputManager={setAccelerometerEnabled:cc.Device.setAccelerometerEnabled,
            setAccelerometerInterval:cc.Device.setAccelerometerInterval,getDPI:cc.Device.getDPI};
        if(cc.sys.isMobile)cc.sys.capabilities.accelerometer=true;
    }
    if(!cc.Layer)return;
    var p=cc.Layer.prototype;
    function listener(layer,key,enabled,description) {
        var previous=layer[key];
        if(previous){cc.eventManager.removeListener(previous);delete layer[key];}
        if(enabled){var current=cc.EventListener.create(description);cc.eventManager.addListener(current,layer);layer[key]=current;}
    }
    function invoke(layer,name,args,fallback) {
        return typeof layer[name]==='function'?layer[name].apply(layer,args):fallback;
    }
    p.isTouchEnabled=function () { return !!this.__legacyTouchListener; };
    p.getTouchMode=function () { return this.__legacyTouchMode===undefined?cc.TOUCHES_ALL_AT_ONCE:this.__legacyTouchMode; };
    p.setTouchMode=function (mode) {
        this.__legacyTouchMode=mode;if(this.isTouchEnabled())this.setTouchEnabled(true);
    };
    p.isSwallowsTouches=function () { return this.__legacySwallow!==false; };
    p.setSwallowsTouches=function (swallow) {
        this.__legacySwallow=!!swallow;if(this.isTouchEnabled())this.setTouchEnabled(true);
    };
    p.setTouchEnabled=function (enabled) {
        var layer=this,one=this.getTouchMode()===cc.TOUCHES_ONE_BY_ONE;
        var descriptor={event:one?cc.EventListener.TOUCH_ONE_BY_ONE:cc.EventListener.TOUCH_ALL_AT_ONCE,
            swallowTouches:this.isSwallowsTouches()};
        ['Began','Moved','Ended','Cancelled'].forEach(function (phase) {
            var name=(one?'onTouch':'onTouches')+phase;
            descriptor[name]=function () { return invoke(layer,name,arguments,false); };
        });
        listener(this,'__legacyTouchListener',!!enabled,descriptor);
    };
    p.isKeyboardEnabled=function () { return !!this.__legacyKeyboardListener; };
    p.setKeyboardEnabled=function (enabled) {
        var layer=this;
        listener(this,'__legacyKeyboardListener',!!enabled,{event:cc.EventListener.KEYBOARD,
            onKeyPressed:function () { return invoke(layer,'onKeyPressed',arguments); },
            onKeyReleased:function () { return invoke(layer,'onKeyReleased',arguments); }});
    };
    p.isMouseEnabled=function () { return !!this.__legacyMouseListener; };
    p.setMouseEnabled=function (enabled) {
        var layer=this,descriptor={event:cc.EventListener.MOUSE};
        ['Down','Up','Move','Scroll'].forEach(function (phase) {
            var name='onMouse'+phase;descriptor[name]=function () { return invoke(layer,name,arguments); };
        });
        listener(this,'__legacyMouseListener',!!enabled,descriptor);
    };
    p.isAccelerometerEnabled=function () { return !!this.__legacyAccelerationListener; };
    p.setAccelerometerEnabled=function (enabled) {
        var layer=this;
        cc.inputManager.setAccelerometerEnabled(!!enabled);
        listener(this,'__legacyAccelerationListener',!!enabled,{event:cc.EventListener.ACCELERATION,
            callback:function () { return invoke(layer,'onAcceleration',arguments); }});
    };
    p.setAccelerometerInterval=function (interval) { cc.inputManager.setAccelerometerInterval(interval); };
})();
