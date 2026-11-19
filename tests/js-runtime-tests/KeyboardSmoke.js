// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
(function(){
    keyboardSmokeCalls=0;
    keyboardSmokeEvent=null;
    keyboardSmokeDescriptor={event:cc.EventListener.KEYBOARD,onKeyPressed:function(code,event){
        if(this!==keyboardSmokeDescriptor)throw Error('Keyboard descriptor receiver');
        if(code!==cc.KEY.a)throw Error('Keyboard legacy KEY_A mapping: '+code);
        keyboardSmokeCalls++;keyboardSmokeEvent=event;
        if(event.getKeyCode()===undefined)throw Error('Keyboard native event getter');
        cc.eventManager.removeListener(keyboardSmokeListener);
    }};
    keyboardSmokeListener=cc.eventManager.addListener(keyboardSmokeDescriptor,1);
})();
function keyboardSmokeResult(){
    if(keyboardSmokeCalls!==1)return 1;
    try{keyboardSmokeEvent.getKeyCode();}catch(error){return 0;}
    return 2;
}
