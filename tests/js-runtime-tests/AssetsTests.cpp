/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "AssetsFixture.h"
#include "runtime/JsRuntime.h"
#include "generated/axjs_register_gen.h"
#include "manual/Bindings.h"
#include "axmol/base/AutoreleasePool.h"
#include "axmol/base/Director.h"
#include "axmol/base/Scheduler.h"
#include "axmol/base/Utils.h"
#include "axmol/base/RefPtr.h"
#include "axmol/platform/FileUtils.h"
#include "assets-manager/AssetsManagerEx.h"
#include <sys/stat.h>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
using namespace std::chrono_literals;
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
const char* currentCase="startup";
void beginCase(const char* name){currentCase=name;std::cout<<"Assets test: "<<name<<std::endl;}
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
// A native test process has no Java VM or APK assets. Absolute temporary paths
// use the engine's ordinary POSIX file streams, directory and rename methods.
class NativeFiles final : public ax::FileUtils {
    std::string root;
public:
    explicit NativeFiles(std::string directory):root(std::move(directory)){_defaultResRootPath=root;_writablePath=root;check(init(),"assets native filesystem init failed");}
    std::string getWritablePath() const override{return _writablePath;}
    std::string getNativeWritableAbsolutePath() const override{return _writablePath;}
    bool isFileExistInternal(std::string_view path) const override{struct stat data{};const std::string name(path);return stat(name.c_str(),&data)==0&&S_ISREG(data.st_mode);}
};
#endif
bool digest(JSContext* cx,unsigned argc,JS::Value* vp){auto args=JS::CallArgsFromVp(argc,vp);std::string path;if(!argc||!axjs::fromJsValue(cx,args[0],path))return false;return axjs::toJsValue(cx,ax::utils::computeFileDigest(path,"md5"),args.rval());}
bool registration(JSContext* cx,JS::HandleObject global){return axjs::registerAllGenerated(cx,global)&&axjs::registerCore(cx,global)&&axjs::registerInput(cx,global)&&axjs::registerExtensions(cx,global)&&JS_DefineFunction(cx,global,"assetDigest",digest,1,0);}
void eval(ax::JsEngine* engine,std::string_view source){check(engine->evalString(source),"assets JavaScript assertion failed");}
void set(ax::JsEngine* engine,const char* name,const std::string& text){auto* cx=engine->getContext();JS::RootedObject global(cx,engine->getGlobalObject());JS::RootedValue value(cx);check(axjs::toJsValue(cx,text,&value)&&JS_SetProperty(cx,global,name,value),"assets fixture JS string failed");}
void pump(){ax::Director::getInstance()->getScheduler()->update(0.01F);if(auto* script=ax::ScriptEngineManager::getScriptEngineIfExists())script->updateFrame(0.01F);std::this_thread::sleep_for(3ms);}
void waitFor(ax::JsEngine* engine,const std::vector<std::string>& errors,std::string_view expression="done"){const auto deadline=std::chrono::steady_clock::now()+8s;for(;;){pump();check(errors.empty(),errors.empty()?"":errors.back());JS::RootedValue result(engine->getContext());check(engine->evalString(expression,&result),"assets result evaluation failed");if(JS::ToBoolean(result))return;check(std::chrono::steady_clock::now()<deadline,std::string("assets test timed out: ")+currentCase);}}
std::string manifest(std::string_view base,std::string_view version,const std::string& textHash,const std::string& binaryHash,size_t textSize,bool local=false){return "{\"packageUrl\":\""+std::string(base)+"/\",\"remoteManifestUrl\":\""+std::string(base)+"/project.manifest\",\"remoteVersionUrl\":\""+std::string(base)+"/version.manifest\",\"version\":\""+std::string(version)+"\",\"engineVersion\":\"3.0\",\"assets\":{\"assets/hello.txt\":{\"md5\":\""+(local?"old":textHash)+"\",\"size\":"+std::to_string(local?3:textSize)+",\"compressed\":false}"+(local?"":",\"assets/data.bin\":{\"md5\":\""+binaryHash+"\",\"size\":4,\"compressed\":false}")+"},\"searchPaths\":[\"assets\"]}";}
std::string read(const std::string& path){std::ifstream stream(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(stream),{});}
}
int main(){
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    char pattern[]="/data/local/tmp/axmol-js-assets-XXXXXX";
#else
    char pattern[]="/tmp/axmol-js-assets-XXXXXX";
#endif
    const char* temporary=mkdtemp(pattern);if(!temporary){std::cerr<<"Cannot create assets test storage\n";return 1;}const std::string root=std::string(temporary)+"/";
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
    ax::FileUtils::setDelegate(new NativeFiles(root));
#endif
    auto* files=ax::FileUtils::getInstance();files->setWritablePath(root);
    auto* engine=ax::JsEngine::getInstance();std::vector<std::string> errors;engine->setErrorHandler([&](std::string_view value){errors.emplace_back(value);});engine->addRegisterCallback(registration);ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
    try{axjs::tests::AssetsFixture fixture;ax::AutoreleasePool pool("JS assets end-to-end");
        const char prefix[]="脚本热更新\0内容";const std::string text=std::string(prefix,sizeof(prefix)-1)+"\n"+std::string(8192,'x');const std::string binary("\0\xff\1\x2a",4);
        const auto textHash=ax::utils::computeDigest(text,"md5"),binaryHash=ax::utils::computeDigest(binary,"md5");
        for(const auto* name:{"happy","reject","compare","broken","slow"}){const auto base=fixture.url()+"/"+name;const auto directory=root+name+"/";check(files->createDirectories(directory+"assets/"),"assets local directory failed");check(files->writeStringToFile("old",directory+"assets/hello.txt"),"assets old file failed");check(files->writeStringToFile(manifest(base,"1.0.0",textHash,binaryHash,text.size(),true),directory+"local.manifest"),"assets local manifest failed");
            fixture.add("/"+std::string(name)+"/version.manifest",manifest(base,"2.0.0",textHash,binaryHash,text.size()),200,std::string_view(name)=="slow"?200:0);
            fixture.add("/"+std::string(name)+"/project.manifest",std::string_view(name)=="broken"?"{invalid":manifest(base,"2.0.0",textHash,binaryHash,text.size()));
            fixture.add("/"+std::string(name)+"/assets/hello.txt",text);fixture.add("/"+std::string(name)+"/assets/data.bin",binary);
        }
        check(engine->start(),"assets binding runtime start failed");set(engine,"root",root);set(engine,"expectedText",text);set(engine,"textHash",textHash);set(engine,"binaryHash",binaryHash);
        eval(engine,R"JS(
            function expect(ok,message){if(!ok)throw Error(message);}var files=cc.FileUtils.getInstance(),dispatcher=cc.Director.getInstance().getEventDispatcher(),codes=jsb.EventAssetsManagerEx.EventCode;dispatcher.setEnabled(true);
            function compare(a,b){compareCalls++;expect(a==='1.0.0'&&b==='2.0.0','version callback arguments');return a===b?0:a>b?1:-1;}
            function verify(path,asset){verifyCalls++;expect(files.isFileExist(path)&&asset.compressed===false,'verify file/metadata');expect(assetDigest(path)===asset.md5,'actual downloaded file digest');expect(asset.md5===(asset.path==='assets/hello.txt'?textHash:binaryHash),'manifest checksum delivered');if(asset.path==='assets/hello.txt')expect(files.getStringFromFile(path)===expectedText,'verify UTF8/NUL body');return true;}
            function listen(manager,fn){var listener=jsb.EventListenerAssetsManagerEx.create(manager,fn);dispatcher.addEventListenerWithFixedPriority(listener,1);return listener;}
            var done=false,compareCalls=0,verifyCalls=0;
        )JS");
        beginCase("HTTP manifests, downloads, verification, progress and search paths");
        eval(engine,R"JS(
            var manager=new jsb.AssetsManagerEx(root+'happy/local.manifest',root+'happy/cache/'),events=[],updated=[],savedEvent,progress=false;
            expect(manager.getLocalManifest().isLoaded()&&manager.getLocalManifest().getVersion()==='1.0.0','local manifest loaded');manager.setVersionCompareHandle(compare);manager.setVerifyCallback(verify);
            var listener=listen(manager,function(e){savedEvent=e;var code=e.getEventCode();events.push(code);expect(e.getAssetsManagerEx()===manager,'event manager identity');if(code===codes.UPDATE_PROGRESSION){expect(e.getPercent()>=0&&e.getPercent()<=100,'progress percent');progress=true;}if(code===codes.ASSET_UPDATED)updated.push(e.getAssetId());if(code===codes.UPDATE_FINISHED)done=true;if(code===codes.UPDATE_FAILED||code===codes.ERROR_UPDATING)throw Error('happy update failed: '+e.getMessage());});manager.update();
        )JS");engine->garbageCollect();waitFor(engine,errors);
        eval(engine,R"JS(
            expect(compareCalls>0&&verifyCalls===2&&updated.length===2&&progress,'download callbacks reached JS');expect(events.indexOf(codes.NEW_VERSION_FOUND)>=0&&events[events.length-1]===codes.UPDATE_FINISHED,'finished event sequence');expect(manager.getLocalManifest().getVersion()==='2.0.0','cached manifest swapped');expect(files.fullPathForFilename('hello.txt')===root+'happy/cache/assets/hello.txt','new search path precedence');expect(files.getStringFromFile('hello.txt')===expectedText,'updated content found through search paths');var expired=false;try{savedEvent.getEventCode();}catch(e){expired=true;}expect(expired,'borrowed update event expires');dispatcher.removeEventListener(listener);
        )JS");check(read(root+"happy/cache/assets/hello.txt")==text&&read(root+"happy/cache/assets/data.bin")==binary,"downloaded assets differ from served bytes");check(files->isFileExist(root+"happy/cache/project.manifest"),"cached manifest not persisted");
        beginCase("verify false, failure event, and retry");
        eval(engine,R"JS(
            done=false;verifyCalls=0;var rejected=new jsb.AssetsManagerEx(root+'reject/local.manifest',root+'reject/cache/'),failureEvents=[],accept=false;rejected.setVerifyCallback(function(path,asset){verify(path,asset);return accept;});
            var rejectListener=listen(rejected,function(e){failureEvents.push(e.getEventCode());if(e.getEventCode()===codes.UPDATE_FAILED||e.getEventCode()===codes.UPDATE_FINISHED)done=true;});rejected.update();
        )JS");waitFor(engine,errors);eval(engine,R"JS(
            expect(verifyCalls>=1&&failureEvents.indexOf(codes.ERROR_UPDATING)>=0&&failureEvents.indexOf(codes.UPDATE_FAILED)>=0&&failureEvents.indexOf(codes.UPDATE_FINISHED)<0,'verify false fails update');expect(rejected.getLocalManifest().getVersion()==='1.0.0','failed update preserves local manifest');done=false;accept=true;rejected.downloadFailedAssets();
        )JS");waitFor(engine,errors);eval(engine,"expect(failureEvents[failureEvents.length-1]===codes.UPDATE_FINISHED&&rejected.getLocalManifest().getVersion()==='2.0.0','retry completed update');dispatcher.removeEventListener(rejectListener);");check(read(root+"reject/cache/assets/data.bin")==binary,"retry binary resource missing");
        beginCase("custom version comparator prevents download");
        eval(engine,R"JS(
            done=false;var compared=new jsb.AssetsManagerEx(root+'compare/local.manifest',root+'compare/cache/'),comparisons=0;compared.setVersionCompareHandle(function(a,b){comparisons++;expect(a==='1.0.0'&&b==='2.0.0','custom compare args');return 1;});var compareListener=listen(compared,function(e){if(e.getEventCode()===codes.ALREADY_UP_TO_DATE)done=true;if(e.getEventCode()===codes.UPDATE_FINISHED)throw Error('comparator ignored');});compared.update();
        )JS");waitFor(engine,errors);eval(engine,"expect(comparisons>0&&compared.getLocalManifest().getVersion()==='1.0.0','version compare return value honored');dispatcher.removeEventListener(compareListener);");check(fixture.count("/compare/project.manifest")==0&&fixture.count("/compare/assets/hello.txt")==0,"version comparator did not prevent downloads");
        beginCase("malformed remote manifest error");
        eval(engine,R"JS(
            done=false;var broken=new jsb.AssetsManagerEx(root+'broken/local.manifest',root+'broken/cache/');var brokenListener=listen(broken,function(e){if(e.getEventCode()===codes.ERROR_PARSE_MANIFEST)done=true;if(e.getEventCode()===codes.UPDATE_FINISHED)throw Error('malformed manifest loaded');});broken.update();
        )JS");waitFor(engine,errors);eval(engine,"expect(broken.getLocalManifest().getVersion()==='1.0.0','parse failure preserves local manifest');dispatcher.removeEventListener(brokenListener);");
        beginCase("active download across VM restart");
        eval(engine,R"JS(
            var pending=new jsb.AssetsManagerEx(root+'slow/local.manifest',root+'slow/cache/');pending.setVersionCompareHandle(function(){throw Error('old compare entered restarted VM');});pending.setVerifyCallback(function(){throw Error('old verify entered restarted VM');});var pendingListener=listen(pending,function(){throw Error('old event entered restarted VM');});pending.update();
        )JS");
        ax::RefPtr<ax::ext::AssetsManagerEx> keepPending;
        {JS::RootedValue pendingValue(engine->getContext());ax::ext::AssetsManagerEx* pendingNative{};check(engine->evalString("pending",&pendingValue)&&axjs::toNative(engine->getContext(),pendingValue,&pendingNative),"pending manager unwrap failed");keepPending.reset(pendingNative);}
        check(engine->restart(),"assets VM restart failed");const auto until=std::chrono::steady_clock::now()+5s;
        while(keepPending->getState()!=ax::ext::AssetsManagerEx::State::FAIL_TO_UPDATE){pump();check(errors.empty(),errors.empty()?"":errors.back());check(std::chrono::steady_clock::now()<until,"old verification callback did not reject its expired VM");}
        engine->garbageCollect();engine->stop();keepPending.reset();pool.clear();std::error_code ignored;std::filesystem::remove_all(root,ignored);std::cout<<"JavaScript AssetsManager HTTP update tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Assets tests failed: "<<error.what()<<'\n';engine->stop();std::error_code ignored;std::filesystem::remove_all(root,ignored);return 1;}
}
