// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
(function(){
    var checks=0;
    function expect(ok,message){++checks;if(!ok)throw Error('Label TTFConfig: '+message);}
    var config={outlineSize:0,fontSize:15,glyphs:0,customGlyphs:'',fontFilePath:'__missing_ttf_regression__.ttf'};
    expect(typeof cc.Label.createWithTTF==='function','real factory registered');
    expect(cc.Label.createWithTTF(config,'Particle count: 0')===null,'missing font returns native null');
    expect(cc.Label.createWithTTF('text',config.fontFilePath,15)===null,'string overload native missing-file behavior');
    var bad={outlineSize:0,fontSize:NaN,glyphs:0,customGlyphs:'',fontFilePath:config.fontFilePath}, failed=false;
    try{cc.Label.createWithTTF(bad,'text');}catch(error){failed=true;}
    expect(failed,'invalid font size rejected before entering native font loader');
    var label=cc.Label.create(), first=label.getTTFConfig(), second=label.getTTFConfig();
    expect(typeof first.fontSize==='number'&&typeof first.distanceFieldEnable==='boolean','actual empty Label native config');
    first.fontSize=991;
    expect(second.fontSize!==991&&label.getTTFConfig().fontSize!==991,'getter snapshots do not mutate native config');
    cc.log('LABEL_TTF_CONFIG_CPU_PASS '+checks+' checks (valid font GPU path pending)');
})();
