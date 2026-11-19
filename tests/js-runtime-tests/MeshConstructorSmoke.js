// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
(function () {
    var count=0;
    function check(ok,message){++count;if(!ok)throw Error('Mesh constructor: '+message);}
    var direct=new jsb.Sprite3D();
    check(jsb.isObjectValid(direct)&&direct instanceof cc.MeshRenderer,'empty constructor uses real native init');
    var Derived=jsb.Sprite3D.extend({ctor:function(file){this._super(file);this.initialized=true;}});
    var derived=new Derived();
    check(derived.initialized&&derived instanceof Derived&&derived instanceof cc.MeshRenderer,
        'derived constructor preserves receiver identity');
    var message='';
    try{new Derived('__axmol_absent_model_constructor__.c3b');}catch(error){message=String(error);}
    check(message.indexOf('initialization failed')>=0&&message.indexOf('Node.init')<0,
        'derived filename reaches real model initialization and propagates failure');
    message='';
    try{new jsb.Sprite3D('__axmol_absent_model_constructor__.c3b');}catch(error){message=String(error);}
    check(message.indexOf('initialization failed')>=0&&message.indexOf('Node.init')<0,
        'direct filename follows the same native initialization contract');
    cc.log('MESH_CONSTRUCTOR_CPU_PASS '+count+' checks (valid model GPU path pending)');
})();
