// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Uses customMoveBy/createCustomAction from the unchanged original ActionsTest.
(function () {
    function assert(value,message) { if(!value)throw Error('Native action subclass: '+message); }
    function node() {
        var value=new cc.Node();value.retain();value.onEnter();
        value.setColor(cc.color(255,255,255));
        return value;
    }
    var source=new customMoveBy(0.2,cc.p(5,0));
    var clone=source.clone(),reverse=source.reverse();
    assert(Object.getPrototypeOf(clone)===Object.getPrototypeOf(source),'clone lost JS subclass prototype');
    assert(Object.getPrototypeOf(reverse)===Object.getPrototypeOf(source),'reverse lost JS subclass prototype');
    var first=node(),copied=node(),reversed=node(),native=node(),repeated=node(),spawned=node(),instant=node(),
        eased=node(),fluent=node(),fast=node();
    first.runAction(source);copied.runAction(clone);reversed.runAction(reverse);
    native.runAction(cc.moveBy(0.2,cc.p(5,0)));
    repeated.runAction(cc.repeat(cc.sequence(new customMoveBy(0.2,cc.p(2,0)),cc.delayTime(0.1),
        new customMoveBy(0.2,cc.p(2,0))),2));
    spawned.runAction(cc.spawn(new customMoveBy(0.2,cc.p(4,0)),new (createCustomAction(cc.RotateBy))(0.2,90)));
    instant.runAction(new (createCustomAction(cc.Hide))());
    var Counted=customMoveBy.extend({
        ctor:function(duration,position) {this._super(duration,position);this.calls=0;},
        update:function(time) {this.calls++;this._super(time);}
    });
    var counted=new Counted(0.2,cc.p(7,0)),same=counted;
    assert(counted.easing(cc.easeIn(2))===same,'easing changed the public JS object');
    counted.calls=10;
    eased.runAction(counted);
    var repeatAction=new Counted(0.2,cc.p(3,0)),repeatSame=repeatAction;
    assert(repeatAction.repeat(2)===repeatSame,'repeat changed the public JS object');
    repeatAction.calls=20;
    fluent.runAction(repeatAction);
    var speedAction=new customMoveBy(0.4,cc.p(6,0)),speedSame=speedAction;
    assert(speedAction.speed(2)===speedSame,'speed changed the public JS object');
    assert(speedAction.getSpeed()===2,'fluent speed did not expose native multiplier');
    fast.runAction(speedAction);
    actionSubclassSmokeVerify=function () {
        assert(Math.abs(first.getPositionX()-5)<0.001,'original _super native MoveBy effect');
        assert(Math.abs(copied.getPositionX()-5)<0.001,'clone native state');
        assert(Math.abs(reversed.getPositionX()+5)<0.001,'reverse native state');
        assert(Math.abs(native.getPositionX()-5)<0.001,'ordinary native action');
        assert(Math.abs(repeated.getPositionX()-8)<0.001,'Sequence/Repeat nested JS overrides');
        assert(Math.abs(spawned.getPositionX()-4)<0.001&&Math.abs(spawned.getRotation()-90)<0.001,'Spawn nested native effects');
        assert(!instant.isVisible(),'instant _super native Hide effect');
        assert(Math.abs(eased.getPositionX()-7)<0.001&&counted.calls>10,
            'easing lost native effect, inner JS override or shared instance state');
        assert(Math.abs(fluent.getPositionX()-6)<0.001&&repeatAction.calls>20,
            'fluent repeat lost native effect, inner JS override or shared instance state');
        assert(Math.abs(fast.getPositionX()-6)<0.001,'fluent Speed lost custom native effect');
        [first,copied,reversed,repeated,spawned,instant,eased,fluent,fast].forEach(function(value) {
            var color=value.getColor();
            assert(color.r<255&&color.g<255&&color.b<255,'original JS update did not run');
            assert(value.getNumberOfRunningActions()===0,'completed JS action remained in ActionManager');
        });
        assert(native.getColor().r===255&&native.getColor().g===255&&native.getColor().b===255,
            'ordinary native update dispatched as a JS override');
        [first,copied,reversed,native,repeated,spawned,instant,eased,fluent,fast].forEach(function(value) {
            value.stopAllActions();value.onExit();value.release();
        });
        return 0;
    };
})();
