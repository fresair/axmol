// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
(function(){
    function unuse(){this.setName('pooled');}
    function reuse(name){this.setName(name);}
    poolSmokeTypeA=cc.Node.extend({unuse:unuse,reuse:reuse});
    poolSmokeTypeB=cc.Node.extend({unuse:unuse,reuse:reuse});
    poolSmokeA=new poolSmokeTypeA();poolSmokeB=new poolSmokeTypeB();
    cc.pool.putInPool(poolSmokeA);cc.pool.putInPool(poolSmokeA);cc.pool.putInPool(poolSmokeB);
    if(!cc.pool.hasObject(poolSmokeTypeA)||!cc.pool.hasObject(poolSmokeTypeB))throw Error('Pool prototype buckets');
})();
function poolSmokeRestore(){
    var taken=cc.pool.getFromPool(poolSmokeTypeA,'restored');
    if(taken!==poolSmokeA||taken.getName()!=='restored')throw Error('Pool identity/reuse after frame');
    if(cc.pool.hasObject(poolSmokeTypeA)||!cc.pool.hasObject(poolSmokeTypeB))throw Error('Pool constructor-name collision');
    poolSmokeParent=new cc.Node();poolSmokeParent.retain();poolSmokeParent.addChild(taken);
    return 0;
}
