// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
var pointerSmokeListeners = [];
var pointerSmokeCounts = null;
var pointerSmokeSaved = null;
var pointerSmokeCase = 0;
var pointerSmokeFailure = '';

function pointerSmokeDispose() {
    pointerSmokeListeners.forEach(function(listener) { cc.eventManager.removeListener(listener); });
    pointerSmokeListeners = [];
}
function pointerSmokeSetup(testCase) {
    pointerSmokeDispose();
    pointerSmokeCase = testCase;
    pointerSmokeCounts = {began:0,moved:0,ended:0,cancelled:0,lower:0};
    pointerSmokeSaved = null;
    pointerSmokeFailure = '';
    var descriptor = {event:cc.EventListener.TOUCH_ONE_BY_ONE, swallowTouches:testCase===3};
    function verify(receiver, touch, event) {
        if (receiver !== descriptor) pointerSmokeFailure = 'Pointer descriptor callback receiver';
        if (touch !== event) pointerSmokeFailure = 'Pointer event arguments lost wrapper identity';
        if (typeof touch.getID() !== 'number') pointerSmokeFailure = 'Pointer ID unavailable during callback';
        pointerSmokeSaved = touch;
    }
    descriptor.onTouchBegan = function(touch,event) {
        verify(this,touch,event);
        pointerSmokeCounts.began++;
        if (testCase === 4) cc.eventManager.removeListener(pointerSmokeListeners[0]);
        return testCase !== 0;
    };
    descriptor.onTouchMoved = function(touch,event) { verify(this,touch,event); pointerSmokeCounts.moved++; };
    descriptor.onTouchEnded = function(touch,event) { verify(this,touch,event); pointerSmokeCounts.ended++; };
    descriptor.onTouchCancelled = function(touch,event) { verify(this,touch,event); pointerSmokeCounts.cancelled++; };
    pointerSmokeListeners.push(cc.eventManager.addListener(descriptor,-20));
    if (testCase === 3) {
        pointerSmokeListeners.push(cc.eventManager.addListener({event:cc.EventListener.TOUCH_ALL_AT_ONCE,
            onTouchesBegan:function(){pointerSmokeCounts.lower++;},onTouchesMoved:function(){pointerSmokeCounts.lower++;},
            onTouchesEnded:function(){pointerSmokeCounts.lower++;},onTouchesCancelled:function(){pointerSmokeCounts.lower++;}},-10));
    }
}
function pointerSmokeResult() {
    if (pointerSmokeFailure) throw Error(pointerSmokeFailure);
    var c = pointerSmokeCounts;
    if (c.began !== 1) throw Error('Pointer began count '+c.began);
    if (pointerSmokeCase === 0 || pointerSmokeCase === 4) {
        if (c.moved || c.ended || c.cancelled) throw Error('Unclaimed or removed listener received followup');
    } else if (pointerSmokeCase === 2) {
        if (c.moved !== 1 || c.cancelled !== 1 || c.ended) throw Error('Pointer cancel claim lifetime');
    } else if (c.moved !== 1 || c.ended !== 1 || c.cancelled) throw Error('Claimed pointer phases');
    if (c.lower) throw Error('Swallowed touch leaked to all-at-once listener');
    var invalid = false;
    try { pointerSmokeSaved.getID(); } catch (error) { invalid = true; }
    if (!invalid) throw Error('Pointer wrapper survived its callback scope');
    return 0;
}

// Clone one real listener twice and dispatch the same native pointer to all
// three. Their independent claims must each survive through Up and Cancel.
function pointerCloneSetup() {
    pointerSmokeDispose();
    pointerCloneCounts = {began:0,moved:0,ended:0,cancelled:0};
    var descriptor = {event:cc.EventListener.TOUCH_ONE_BY_ONE, swallowTouches:false};
    function checkReceiver(receiver,touch,event) {
        if(receiver!==descriptor || touch!==event) throw Error('Cloned pointer receiver/event identity');
    }
    descriptor.onTouchBegan=function(touch,event){checkReceiver(this,touch,event);pointerCloneCounts.began++;return true;};
    descriptor.onTouchMoved=function(touch,event){checkReceiver(this,touch,event);pointerCloneCounts.moved++;};
    descriptor.onTouchEnded=function(touch,event){checkReceiver(this,touch,event);pointerCloneCounts.ended++;};
    descriptor.onTouchCancelled=function(touch,event){checkReceiver(this,touch,event);pointerCloneCounts.cancelled++;};
    var source=cc.EventListener.create(descriptor), first=source.clone(), second=first.clone();
    if(source===first || first===second) throw Error('Listener clone identity');
    pointerSmokeListeners=[source,first,second];
    pointerSmokeListeners.forEach(function(listener,i){cc.eventManager.addListener(listener,-30+i);});
    jsb.garbageCollect();
    return 0;
}
function pointerCloneResult(cancelled) {
    var counts=pointerCloneCounts;
    if(counts.began!==3 || counts.moved!==3 || counts.ended!==(cancelled?0:3) || counts.cancelled!==(cancelled?3:0))
        throw Error('Independent cloned pointer claims: '+JSON.stringify(counts));
    return 0;
}
function pointerCloneFreshResult() {
    var counts=pointerCloneCounts;
    if(counts.began!==2 || counts.moved!==0 || counts.ended!==2 || counts.cancelled!==0)
        throw Error('Native clone copied an active claim: '+JSON.stringify(counts));
    return 0;
}
function pointerRemovalSetup() {
    pointerSmokeDispose();
    pointerRemovalCounts={one:0,all:0,mouse:0,custom:0};
    var one=cc.EventListener.create({event:cc.EventListener.TOUCH_ONE_BY_ONE,
        onTouchBegan:function(){pointerRemovalCounts.one++;return true;}});
    var all=cc.EventListener.create({event:cc.EventListener.TOUCH_ALL_AT_ONCE,
        onTouchesBegan:function(){pointerRemovalCounts.all++;}});
    var mouse=cc.EventListener.create({event:cc.EventListener.MOUSE,
        onMouseDown:function(){pointerRemovalCounts.mouse++;}});
    var custom=cc.EventListener.create({event:cc.EventListener.CUSTOM,eventName:'pointer-type-sentinel',
        callback:function(){pointerRemovalCounts.custom++;}});
    pointerSmokeListeners=[one,one.clone(),all,all.clone(),mouse,mouse.clone(),custom,custom.clone()];
    pointerSmokeListeners.forEach(function(listener,i){cc.eventManager.addListener(listener,-40+i);});
    // Use the real old API alias once the batch is merged. The isolated staging
    // runner supplies this numeric alias before its native probe executes.
    cc.eventManager.removeListeners(cc.EventListener.TOUCH_ONE_BY_ONE);
    return 0;
}
function pointerRemovalResult(stage) {
    var c=pointerRemovalCounts;
    var expected=stage===0?[0,4,2,2]:stage===1?[0,6,2,4]:[0,6,2,4];
    if(c.one!==expected[0] || c.all!==expected[1] || c.mouse!==expected[2] || c.custom!==expected[3])
        throw Error('Legacy listener removal mixed types at '+stage+': '+JSON.stringify(c));
    return 0;
}
