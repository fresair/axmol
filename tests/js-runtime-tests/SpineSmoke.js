// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
(function(){
    function assert(value,message){if(!value)throw Error('Spine regression: '+message);}
    var node=sp.SkeletonAnimation.createWithJsonFile(spineTestJson,spineTestAtlas,1);
    assert(node,'Spine4.3 asset loaded');node.retain();
    var skeleton=node.getSkeleton(),bone=node.findBone('child'),slot=node.findSlot('slot');
    assert(bone===node.findBone('child')&&skeleton.bones[1]===bone,'borrowed bone identity');
    assert(slot===skeleton.slots[0]&&slot.bone===skeleton.bones[0],'borrowed slot identity');
    assert(bone.x===5&&bone.data.name==='child','live bone fields');bone.x=12;assert(bone.x===12,'bone mutation');
    node.setSkin('alt');skeleton.x=7;skeleton.scaleX=2;
    var clone=node.clone(0,0);clone.retain();
    var cloneSkeleton=clone.getSkeleton(),cloneBone=clone.findBone('child');
    assert(cloneSkeleton!==skeleton&&cloneBone!==bone,'clone has independent native skeleton/pose');
    assert(cloneSkeleton.x===7&&cloneSkeleton.scaleX===2&&cloneBone.x===12,'clone copies skeleton and bone pose');
    assert(cloneBone.worldX===bone.worldX,'clone copies rendered bone transform');
    cloneBone.x=24;assert(bone.x===12,'clone bone mutation is independent');
    spineSmokeSource=node;spineSmokeClone=clone;spineSmokeCloneBone=cloneBone;
    var starts=0,disposes=0,trackDispose=0;
    node.setStartListener(function(entry){starts++;assert(this===node&&entry.animation.name==='idle','start listener');});
    node.setDisposeListener(function(entry){disposes++;assert(entry.trackIndex===0,'dispose entry live in callback');});
    var entry=node.setAnimation(0,'idle',false);
    assert(starts===1&&entry===node.getCurrent(0),'start callback and track identity');
    assert(entry.animation.duration===0.25&&entry.loop===false,'track animation fields');
    entry.timeScale=2;assert(entry.timeScale===2,'live track field');
    node.setTrackDisposeListener(entry,function(live){trackDispose++;assert(live===entry,'track dispose identity');});
    node.clearTracks();
    var expired=false;try{entry.trackTime;}catch(e){expired=true;}
    assert(expired&&disposes===1&&trackDispose===1,'track disposed after both callbacks');
    var newEntry=node.setAnimation(0,'idle',true);assert(newEntry!==entry,'track pool address cannot revive wrapper');
    node.initWithJsonFile(spineTestReplacement,spineTestAtlas,1);
    expired=false;try{bone.x;}catch(e){expired=true;}
    assert(expired,'asset swap invalidates bone generation');
    expired=false;try{skeleton.x;}catch(e){expired=true;}
    assert(expired,'asset swap invalidates skeleton generation');
    assert(cloneBone.x===24&&cloneSkeleton.x===7,'source asset replacement leaves clone valid');
    node.release();
})();
function spineSmokeAfterSourceDestruction(){
    var expired=false;try{spineSmokeSource.getSkeleton();}catch(e){expired=true;}
    if(!expired||spineSmokeCloneBone.x!==24||spineSmokeClone.getSkeleton().x!==7)
        throw Error('Spine regression: clone lifetime after source destruction');
    var entry=spineSmokeClone.setAnimation(0,'idle',false);
    if(!entry||spineSmokeClone.getCurrent(0)!==entry)
        throw Error('Spine regression: cloned asset supports animation after source destruction');
    spineSmokeClone.release();return 0;
}
