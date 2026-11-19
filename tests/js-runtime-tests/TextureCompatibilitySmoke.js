/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
// CPU contract checks. Actual sampler mutation and image upload require the
// real GPU driver in tools/js-tests/runtime-audits/legacy-gl/gpu-smoke.js.
(function() {
    var checks=0;
    function check(value,description) {
        if(!value)throw new Error('Texture compatibility: '+description);
        ++checks;
    }
    check(cc.LINEAR===0x2601&&cc.LINEAR===gl.LINEAR,'original cc.LINEAR');
    check(cc.REPEAT===0x2901&&cc.REPEAT===gl.REPEAT,'original cc.REPEAT');
    check(cc.CLAMP_TO_EDGE===0x812f&&cc.CLAMP_TO_EDGE===gl.CLAMP_TO_EDGE,'original cc.CLAMP_TO_EDGE');
    check(cc.MIRRORED_REPEAT===0x8370&&cc.MIRRORED_REPEAT===gl.MIRRORED_REPEAT,'original cc.MIRRORED_REPEAT');
    var texture=cc.Texture2D.__allocate();
    check(texture instanceof cc.Texture2D&&texture.getName()===texture,'real native Texture2D identity');
    var alpha=texture.hasPremultipliedAlpha();
    check(texture.handleLoadedTexture(true)===undefined,'native synchronous load compatibility entry');
    check(texture.hasPremultipliedAlpha()===alpha,'browser completion must not change native premultiplication');
    check(texture.handleLoadedTexture(false)===undefined&&texture.getName()===texture,'native receiver remains canonical');
    var rejected=false;
    try { cc.Texture2D.prototype.handleLoadedTexture.call({}); } catch(error) { rejected=true; }
    var spoofRejected=false;
    try { cc.Texture2D.prototype.handleLoadedTexture.call({getName:function(){return this;}}); }
    catch(error) { spoofRejected=true; }
    check(rejected&&spoofRejected,'invalid native receiver rejected even with a JS getName method');
    cc.log('TEXTURE_COMPATIBILITY_CPU_PASS '+checks+' checks graphicsRequired=false');
})();
