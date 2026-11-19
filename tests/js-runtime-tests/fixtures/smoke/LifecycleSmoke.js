// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// Executes actual native Node transitions and the native Scheduler without GPU.
(function(){
    function assert(condition,message){if(!condition)throw Error('Lifecycle regression: '+message);}
    var trace=[];
    var Child=cc.Node.extend({onEnter:function(){trace.push('child.enter');this._super();},
        cleanup:function(){trace.push('child.cleanup');this._super();}});
    var Base=cc.Node.extend({onEnter:function(){trace.push('base.before');this._super();trace.push('base.after');}});
    var Derived=Base.extend({onEnter:function(){trace.push('derived.before');this._super();trace.push('derived.after');},
        cleanup:function(){trace.push('parent.cleanup.before');this._super();trace.push('parent.cleanup.after');}});
    var parent=new Derived(),child=new Child();parent.addChild(child);parent.onEnter();
    assert(trace.join(',')==='derived.before,base.before,child.enter,base.after,derived.after','multilevel _super order: '+trace);
    assert(parent.isRunning()&&child.isRunning(),'native onEnter ran');
    trace=[];parent.cleanup();
    assert(trace.join(',')==='parent.cleanup.before,child.cleanup,parent.cleanup.after','native cleanup order: '+trace);
    parent.onExit();assert(!parent.isRunning()&&!child.isRunning(),'native onExit ran');

    var target={count:0},data={marker:17},node=new cc.Node();
    var action=cc.callFunc(function(sender,payload){assert(this===target&&sender===node&&payload===data,'callFunc receiver/data');this.count++;},target,data);
    action.startWithTarget(node);action.step(0);assert(target.count===1,'callFunc executed');
    var bezier=cc.bezierTo(1,[cc.p(1,0),cc.p(2,0),cc.p(3,0)]);
    bezier.startWithTarget(node);bezier.update(1);
    assert(node.getPositionX()===3,'Bezier control point adapter reaches endpoint');
    var spline=cc.catmullRomTo(1,[cc.p(3,0),cc.p(5,0),cc.p(8,0)]);
    spline.startWithTarget(node);spline.update(1);
    assert(node.getPositionX()===8,'Catmull-Rom array adapter reaches endpoint');

    var retainedEvent,received,dataObject={nested:{value:8}};
    var eventsEnabled=cc.eventManager.isEnabled();cc.eventManager.setEnabled(true);
    var listener=cc.eventManager.addCustomListener('js.lifecycle.custom',function(event){
        retainedEvent=event;received=event.getUserData();
        assert(event.getEventName()==='js.lifecycle.custom','custom event name');
        cc.eventManager.removeListener(listener);
    });
    cc.eventManager.dispatchCustomEvent('js.lifecycle.custom',dataObject);
    assert(received===dataObject,'custom event preserves JS object identity');
    var expired=false;try{retainedEvent.getEventName();}catch(e){expired=true;}
    assert(expired,'temporary Event is invalid after callback');
    cc.eventManager.dispatchCustomEvent('js.lifecycle.custom',{value:9});
    assert(received===dataObject,'listener self cancellation');
    var longName='js.dynamic.'+new Array(128).join('event-name-'),count=0;
    var dynamic=cc.EventListener.create({event:cc.EventListener.CUSTOM,eventName:longName,callback:function(event){
        assert(event.getEventName()===longName,'dynamic listener owns its event name');count++;
    }});
    var cloned=dynamic.clone();
    cc.eventManager.addListener(dynamic,1);cc.eventManager.addListener(cloned,2);
    cc.eventManager.dispatchCustomEvent(longName);
    assert(count===2,'dynamic listener and clone dispatch');
    cc.eventManager.removeListener(dynamic);cc.eventManager.removeListener(cloned);
    cc.eventManager.setEnabled(eventsEnabled);

    localStorage.setItem('__keys__','user-data');localStorage.setItem('a.present','another');
    assert(localStorage.getItem('__keys__')==='user-data','reserved-looking key works');
    localStorage.setItem('a','');assert(localStorage.getItem('a')==='','empty storage value');
    localStorage.removeItem('a');assert(localStorage.getItem('a')===null,'removed storage returns null');
    assert(localStorage.getItem('a.present')==='another','storage keys cannot collide with metadata');
    localStorage.removeItem('__keys__');localStorage.removeItem('a.present');

    var loaded=0,completed=0;
    cc.loader.load(['project.json','audio/js-smoke.wav'],function(value,total,count){
        assert(total===2&&count===loaded++,'legacy native loader progress is zero based');
    },function(error,resources){
        assert(!error&&resources[0].jsList&&resources[1]===null,'native JSON/audio loading');completed++;
    });
    assert(loaded===2&&completed===1,'loader completes once');
    var missing=0;
    cc.loader.load('missing-js-smoke-resource.txt',function(error,resources){
        assert(!error&&resources[0]==='','legacy native text loader returns an empty string for a missing file');missing++;
    });
    assert(missing===1,'missing resource completes once');
    var sentinel={},caught=false;
    try{cc.loader.load('project.json',function(){throw sentinel;},function(){completed++;});}
    catch(error){caught=error===sentinel;}
    assert(caught&&completed===1,'progress exception propagates without completion');
    cc.loader.release('project.json');cc.loader.release('audio/js-smoke.wav');
    cc.log('JavaScript lifecycle and compatibility regressions passed');
})();
