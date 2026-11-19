// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
(function(){
    function assert(value,message){if(!value)throw Error('Scheduler regression: '+message);}
    var trace=[], paused=0, normal=0, cancels=0;
    var First=cc.Node.extend({update:function(dt){assert(Math.abs(dt-0.1)<0.001,'delta');trace.push('first');}});
    var Last=cc.Node.extend({update:function(){trace.push('last');}});
    var first=new First(),last=new Last(),timerNode=new cc.Node();
    first.retain();last.retain();timerNode.retain();
    first.onEnter();last.onEnter();timerNode.onEnter();
    last.scheduleUpdateWithPriority(10);first.scheduleUpdateWithPriority(-10);
    var calls=0;
    var timer=function(){calls++; if(calls===2){timerNode.unschedule(timer);cancels++;}};
    timerNode.schedule(timer,0);
    timerNode.schedule(function(){normal++;},0,cc.REPEAT_FOREVER,0,'normal');
    var finiteCalls=0,selfReschedules=0,falseRepeatCalls=0;
    timerNode.schedule(function(){finiteCalls++;},0,1,0);
    var stableOnce=function(){selfReschedules++;if(selfReschedules===1)timerNode.scheduleOnce(stableOnce,0);};
    timerNode.scheduleOnce(stableOnce,0);
    timerNode.schedule(function(){falseRepeatCalls++;},0,false,0);
    var frames=0;
    var control=setInterval(function(){
        frames++;
        assert(trace.length>=2&&trace.length%2===0,'update count '+trace);
        for(var i=0;i<trace.length;i+=2)assert(trace[i]==='first'&&trace[i+1]==='last','priority order '+trace);
        trace=[];
        if(frames===2){timerNode.pause();paused=normal;}
        if(frames===3){assert(normal===paused,'paused node timer');timerNode.resume();}
        if(frames===5){
            assert(calls===2&&cancels===1,'self cancellation');assert(normal>paused,'resume timer');
            assert(finiteCalls===2,'finite repeat runs exactly repeat+1 times');
            assert(selfReschedules===2,'scheduleOnce can reschedule the same callback from its final invocation');
            assert(falseRepeatCalls===1,'legacy false repeat converts to zero and runs once');
            first.unscheduleUpdate();last.unscheduleUpdate();timerNode.unscheduleAllCallbacks();
            first.onExit();last.onExit();timerNode.onExit();
            first.release();last.release();timerNode.release();clearInterval(control);
            schedulerSmokePassed=true;
        }
    },0);
    schedulerSmokePassed=false;
})();
function schedulerSmokeResult(){return schedulerSmokePassed?0:1;}
