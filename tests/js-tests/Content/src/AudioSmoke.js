// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
// A muted real playback verifies decoder + native callbacks without audible output.
var jsAudioSmokeDone=false,jsAudioSmokeError='';
function startJsAudioSmoke(){
    cc.audioEngine.preloadEffect('audio/js-smoke.wav',function(success){
        if(!success){jsAudioSmokeError='preload failed';return;}
        var id=cc.audioEngine.playEffect('audio/js-smoke.wav',false,1,0,0);
        if(id<0){jsAudioSmokeError='playEffect returned an invalid id';return;}
        cc.AudioEngine.setFinishCallback(id,function(finishedId,file){
            if(finishedId!==id){jsAudioSmokeError='finish callback id mismatch';return;}
            cc.audioEngine.stopEffect(id);
            jsAudioSmokeDone=true;cc.log('JavaScript native audio playback/callback smoke passed');
        });
    });
}
function jsAudioSmokeResult(){
    if(jsAudioSmokeError){cc.error(jsAudioSmokeError);return 31;}
    return jsAudioSmokeDone?0:32;
}
