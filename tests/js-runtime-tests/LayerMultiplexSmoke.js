// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
(function () {
    var count=0;
    function check(ok,message){++count;if(!ok)throw Error('LayerMultiplex: '+message);}
    function layer(){return new cc.Layer();}
    var a=layer(),b=layer(),c=layer(),d=layer(),e=layer(),f=layer();
    var mux=new cc.LayerMultiplex(a,b,c,d,e,f);
    check(mux instanceof cc.LayerMultiplex&&mux.getChildrenCount()===1&&mux.getChildren()[0]===a,
        'original six-layer constructor attaches the first native layer');
    mux.switchTo(5);
    check(mux.getChildrenCount()===1&&mux.getChildren()[0]===f&&a.getParent()===null,
        'native switch reaches the sixth layer');
    mux.switchTo(1,false);
    check(mux.getChildren()[0]===b&&jsb.isObjectValid(a)&&a.getReferenceCount()===2,
        'inactive layer has its original autorelease reference and the native vector reference');
    var array=[layer(),layer()],other=new cc.LayerMultiplex(array);
    check(other.getChildren()[0]===array[0],'array constructor');
    other.switchTo(1);check(other.getChildren()[0]===array[1],'array native switch');
    var factoryChild=layer(),factory=cc.LayerMultiplex.createWithArray([factoryChild]);
    check(factory.getChildren()[0]===factoryChild&&factory instanceof cc.LayerMultiplex,'factory keeps native type identity');
    var Derived=cc.LayerMultiplex.extend({ctor:function(x,y){return this._super(x,y);}});
    var x=layer(),y=layer(),derived=new Derived(x,y);
    derived.switchTo(1);check(derived instanceof Derived&&derived.getChildren()[0]===y,'derived ctor and native switch');
    var empty=new cc.LayerMultiplex();
    check(empty.getChildrenCount()===0&&empty.getAnchorPoint().x===0.5,'empty native initialization');
    cc.log('LAYER_MULTIPLEX_PASS '+count+' checks');
})();
