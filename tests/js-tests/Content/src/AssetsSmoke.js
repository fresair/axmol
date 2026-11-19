// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
(function(){
    function assert(ok,message){if(!ok)throw Error('Assets regression: '+message);}
    var storage='js-assets-smoke-cache-'+Date.now()+'/';
    var manager=new jsb.AssetsManager('missing-test.manifest',storage,function(a,b){return a===b?0:a>b?1:-1;});
    assert(manager instanceof jsb.AssetsManagerEx,'legacy AssetsManager selects manifest manager');
    assert(manager.getLocalManifest()&&!manager.getLocalManifest().isLoaded(),'missing manifest is unloaded');
    manager.setMaxConcurrentTask(2);assert(manager.getMaxConcurrentTask()===2,'concurrency setting');
    manager.setVerifyCallback(function(path,asset){return !!asset.md5;});manager.setVerifyCallback(null);
    var code=-1,saved;
    var eventsEnabled=cc.eventManager.isEnabled();cc.eventManager.setEnabled(true);
    var listener=new jsb.EventListenerAssetsManager(manager,function(event){code=event.getEventCode();saved=event;});
    cc.eventManager.addListener(listener,1);manager.checkUpdate();
    assert(code===jsb.EventAssetsManager.ERROR_NO_LOCAL_MANIFEST,'missing manifest produces error event: got '+code+', expected '+jsb.EventAssetsManager.ERROR_NO_LOCAL_MANIFEST);
    var expired=false;try{saved.getEventCode();}catch(e){expired=true;}
    assert(expired,'assets event expires after callback');
    cc.eventManager.removeListener(listener);
    cc.eventManager.setEnabled(eventsEnabled);
    if(jsb.fileUtils.isDirectoryExist(storage))jsb.fileUtils.removeDirectory(storage);
    var temporary=storage.slice(0,-1)+'_temp/';
    if(jsb.fileUtils.isDirectoryExist(temporary))jsb.fileUtils.removeDirectory(temporary);
    cc.log('JavaScript AssetsManager compatibility regression passed');
})();
