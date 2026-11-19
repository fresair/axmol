/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "runtime/JsRuntime.h"
#include "axmol/base/AutoreleasePool.h"
#include "axmol/platform/FileUtils.h"
#include <filesystem>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <initializer_list>
#include <stdexcept>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
void check(bool condition,const char* message)
{ if(!condition) throw std::runtime_error(message); }
template<class T> [[gnu::noinline]] T numericFixtureInput(T input)
{
    // Model a runtime native result, including noncanonical payload bits.
    volatile T value=input;
    return value;
}
bool nativeNanPayload(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    if(argc==1 && args[0].isTrue())
        return axjs::toJsValue(cx,numericFixtureInput(std::bit_cast<float>(uint32_t{0xfff90000})),args.rval());
    return axjs::toJsValue(cx,numericFixtureInput(std::bit_cast<double>(uint64_t{0xffff200000000000})),args.rval());
}
void checkNumericOutputs(ax::JsEngine* engine)
{
    JSContext* cx=engine->getContext();
    JS::RootedValue value(cx);
    const auto verify=[&](auto input) {
        check(axjs::toJsValue(cx,numericFixtureInput(input),&value),"Native numeric conversion failed");
        const bool numeric=value.isNumber();
        const double number=numeric ? value.toNumber() : 0;
        const auto bits=value.asRawBits();
        // Baseline must report the malformed tag without ever tracing it.
        value.setUndefined();
        if(!numeric)
            std::fprintf(stderr,"NONCANONICAL_NATIVE_NUMBER bits=%016llx\n",
                         static_cast<unsigned long long>(bits));
        check(numeric,"Native NaN was boxed as a non-number");
        if(std::isnan(input)) check(std::isnan(number),"Native NaN changed its numeric meaning");
        else check(number==static_cast<double>(input) && std::signbit(number)==std::signbit(input),
                   "Native number or signed zero changed during boxing");
    };
    // Explicit initialized payloads: no uninitialized native reads in this regression.
    for(uint32_t bits:{0xfff90000u,0xfff88000u,0xffc00000u,0x7fc00001u,0x7f800001u,0xff800001u})
        verify(std::bit_cast<float>(bits));
    for(uint64_t bits:{UINT64_C(0xffff200000000000),UINT64_C(0xffff100000000000),
                      UINT64_C(0xfffc000000000001),UINT64_C(0x7ff8000000000001),
                      UINT64_C(0xfff0000000000001),UINT64_C(0x7ff0000000000001)})
        verify(std::bit_cast<double>(bits));
    for(double number:{0.0,-0.0,1.5,-2.5,std::numeric_limits<double>::denorm_min(),
                       -2147483648.0,2147483647.0,-2147483649.0,2147483648.0,
                       9007199254740991.0,-9007199254740991.0,
                       std::numeric_limits<double>::max(),std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()}) verify(number);
    verify(std::numeric_limits<uint32_t>::max());
    verify(std::numeric_limits<int64_t>::min());
    verify(std::numeric_limits<uint64_t>::max());
    JS::RootedObject global(cx,engine->getGlobalObject());
    check(JS_DefineFunction(cx,global,"nativeNanPayload",nativeNanPayload,1,0),"NaN callback registration failed");
    check(engine->evalString("var nativeNanHolders=[]; for(var n=0;n<256;++n) {"
        "nativeNanHolders.push({available:true,value:nativeNanPayload(n%2===0)}); }"
        "nativeNanHolders.every(function(row){return typeof row.value==='number' && row.value!==row.value;});",
        &value) && value.isTrue(),"Native NaN callback/object property failed");
    JS_GC(JS_GetRuntime(cx));
    check(engine->evalString("nativeNanHolders.every(function(row){return typeof row.value==='number' && "
        "row.value!==row.value && JSON.stringify(row)==='{\"available\":true,\"value\":null}';});",
        &value) && value.isTrue(),"Native NaN object properties failed after GC");
    check(engine->evalString("nativeNanHolders=null;"),"NaN fixture cleanup failed");
    std::fprintf(stderr,"NATIVE_NUMERIC_OUTPUTS_PASS payloads=12 finite_and_infinite=14 integer_outputs=3 properties=256 gc=1\n");
}
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
// This executable has no JVM or APK AssetManager. Its absolute temporary files
// use FileUtils' real POSIX streams, size/read/write and directory operations.
class NativeFiles final : public ax::FileUtils
{
public:
    explicit NativeFiles(std::string root)
    {
        _defaultResRootPath=std::move(root);
        _writablePath=_defaultResRootPath;
        check(init(),"native runtime filesystem init failed");
    }
    std::string getWritablePath() const override { return _writablePath; }
    std::string getNativeWritableAbsolutePath() const override { return _writablePath; }
    bool isFileExistInternal(std::string_view path) const override
    {
        struct stat status{};
        return stat(std::string(path).c_str(),&status)==0&&S_ISREG(status.st_mode);
    }
};
#endif
struct TestProtocol { virtual ~TestProtocol()=default; virtual int protocolValue() const=0; };
struct Borrowed { int value{19}; };
struct OwnedValue
{
    static int destroyed;
    Borrowed child;
    ~OwnedValue() {++destroyed;}
    Borrowed* getChild() {return &child;}
};
int OwnedValue::destroyed{};
struct OffsetPrefix { virtual ~OffsetPrefix()=default; int padding[4]{}; };
struct OffsetBase { virtual ~OffsetBase()=default; int value{23}; };
struct OffsetValue final : OffsetPrefix, OffsetBase {};
class TestObject : public ax::Object, public TestProtocol
{
public:
    static int destroyed;
    int value{7};
    int initCalls{};
    Borrowed child;
    ~TestObject() override { ++destroyed; }
    bool init() { ++initCalls; return true; }
    int add(int input) const { return value+input; }
    int protocolValue() const override { return value; }
    size_t stringSize(std::string_view text) const { return text.size(); }
    bool boolIdentity(bool input) const { return input; }
    float sumSpan(std::span<const ax::Vec2> points) const
    { float result=0; for(const auto& point:points) result+=point.x+point.y; return result; }
    Borrowed* getChild() { return &child; }
    std::vector<Borrowed*> getChildren() { return {&child}; }
};
int TestObject::destroyed{};
JSClass nativeClass={"RuntimeTestObject",JSCLASS_HAS_PRIVATE,JS_PropertyStub,JS_DeletePropertyStub,
    JS_PropertyStub,JS_StrictPropertyStub,JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub,axjs::finalizeNative,
    nullptr,nullptr,nullptr,nullptr};
JSClass borrowedClass={"RuntimeBorrowed",JSCLASS_HAS_PRIVATE,JS_PropertyStub,JS_DeletePropertyStub,
    JS_PropertyStub,JS_StrictPropertyStub,JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub,axjs::finalizeNative,
    nullptr,nullptr,nullptr,nullptr};
bool constructor(JSContext* cx,unsigned argc,JS::Value* vp)
{ auto args=JS::CallArgsFromVp(argc,vp); return axjs::construct<TestObject>(cx,args); }
bool add(JSContext* cx,unsigned argc,JS::Value* vp)
{ auto args=JS::CallArgsFromVp(argc,vp); return axjs::invoke<&TestObject::add>(cx,args); }
bool stringSize(JSContext* cx,unsigned argc,JS::Value* vp)
{ auto args=JS::CallArgsFromVp(argc,vp); return axjs::invoke<&TestObject::stringSize>(cx,args); }
bool boolIdentity(JSContext* cx,unsigned argc,JS::Value* vp)
{ auto args=JS::CallArgsFromVp(argc,vp); return axjs::invoke<&TestObject::boolIdentity>(cx,args); }
bool sumSpan(JSContext* cx,unsigned argc,JS::Value* vp)
{ auto args=JS::CallArgsFromVp(argc,vp); return axjs::invoke<&TestObject::sumSpan>(cx,args); }
bool getChild(JSContext* cx,unsigned argc,JS::Value* vp)
{ auto args=JS::CallArgsFromVp(argc,vp); return axjs::invoke<&TestObject::getChild>(cx,args); }
bool getChildren(JSContext* cx,unsigned argc,JS::Value* vp)
{ auto args=JS::CallArgsFromVp(argc,vp); return axjs::invoke<&TestObject::getChildren>(cx,args); }
bool allocate(JSContext* cx,unsigned argc,JS::Value* vp)
{ auto args=JS::CallArgsFromVp(argc,vp); return axjs::constructUninitialized<TestObject>(cx,args); }
bool ownedConstructor(JSContext* cx,unsigned argc,JS::Value* vp)
{ auto args=JS::CallArgsFromVp(argc,vp);return axjs::constructUninitialized<OwnedValue>(cx,args); }
bool ownedChild(JSContext* cx,unsigned argc,JS::Value* vp)
{auto args=JS::CallArgsFromVp(argc,vp);return axjs::invoke<&OwnedValue::getChild>(cx,args);}
bool createTestNamespace(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    std::string name;
    if(argc!=1 || !axjs::fromJsValue(cx,args[0],name))return false;
    JS::RootedObject global(cx,axjs::engine(cx)->getGlobalObject()),result(cx);
    if(!axjs::getOrCreateNamespace(cx,global,name.c_str(),&result))return false;
    args.rval().setObject(*result);return true;
}
const JSFunctionSpec functions[]={JS_FN("add",add,1,0),JS_FN("stringSize",stringSize,1,0),
    JS_FN("boolIdentity",boolIdentity,1,0),
    JS_FN("sumSpan",sumSpan,1,0),JS_FN("getChild",getChild,0,0),JS_FN("getChildren",getChildren,0,0),JS_FS_END};
const JSFunctionSpec staticFunctions[]={JS_FN("__allocate",allocate,0,0),JS_FS_END};
bool registerTests(JSContext* cx,JS::HandleObject global)
{
    JS::RootedObject prototype(cx,JS_InitClass(cx,global,JS::NullPtr(),&nativeClass,constructor,0,
        nullptr,functions,nullptr,staticFunctions));
    if(!prototype || !axjs::registerType<TestObject,ax::Object>(cx,prototype,&nativeClass)) return false;
    JS::RootedObject borrowed(cx,JS_InitClass(cx,global,JS::NullPtr(),&borrowedClass,nullptr,0,
        nullptr,nullptr,nullptr,nullptr));
    if(!borrowed||!axjs::registerType<Borrowed>(cx,borrowed,&borrowedClass))return false;
    static JSClass ownedClass=nativeClass;ownedClass.name="RuntimeOwnedValue";
    const JSFunctionSpec ownedMethods[]={JS_FN("getChild",ownedChild,0,0),JS_FS_END};
    JS::RootedObject owned(cx,JS_InitClass(cx,global,JS::NullPtr(),&ownedClass,ownedConstructor,0,nullptr,ownedMethods,nullptr,nullptr));
    static JSClass offsetClass=nativeClass;offsetClass.name="RuntimeOffsetValue";
    JS::RootedObject offset(cx,JS_InitClass(cx,global,JS::NullPtr(),&offsetClass,nullptr,0,nullptr,nullptr,nullptr,nullptr));
    if(!offset || !axjs::registerType<OffsetValue>(cx,offset,&offsetClass))return false;
    return owned && axjs::registerType<OwnedValue>(cx,owned,&ownedClass) &&
        JS_DefineFunction(cx,global,"createTestNamespace",createTestNamespace,1,0) &&
        JS_DefineFunction(cx,global,"associateNative",axjs::associateObjectWithNative,2,0);
}
const void* checkOwnedValueLookups(JSContext* cx,JS::HandleValue owner,JS::HandleValue child)
{
    OwnedValue* native{};Borrowed* nativeChild{};
    check(axjs::toNative(cx,owner,&native) && native && axjs::toNative(cx,child,&nativeChild) &&
        nativeChild==&native->child,"Owned/borrowed exact lookup fixture conversion failed");
    const void* key=native;
    check(key==static_cast<const void*>(nativeChild),"Owned/first-member fixture does not share an address");
    check(axjs::detail::findNativeValueObject(cx,key,typeid(OwnedValue))==&owner.toObject() &&
        axjs::detail::findNativeValueObject(cx,key,typeid(Borrowed))==&child.toObject(),
        "Exact same-address types did not find their distinct live wrappers");
    check(!axjs::detail::findNativeValueObject(cx,key,typeid(OffsetValue)),"Wrong exact non-Object type matched");
    check(!axjs::detail::findNativeObject(cx,key),"Untyped lookup accepted ambiguous same-address values");
    return key;
}
}
int main()
{
    std::filesystem::path temporaryDirectory;
    auto cleanupFiles=[&] {
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
        ax::FileUtils::destroyInstance();
#endif
        if(!temporaryDirectory.empty()) {
            std::error_code error;
            std::filesystem::remove_all(temporaryDirectory,error);
        }
    };
    auto* engine=ax::JsEngine::getInstance();
    ax::ScriptEngineManager::getInstance()->setScriptEngine(engine);
    engine->addRegisterCallback(registerTests);
    std::vector<std::string> errors;
    int mutableUpdates=0,nestedUpdates=0;
    engine->setErrorHandler([&errors](std::string_view message){ errors.emplace_back(message); });
    try
    {
        check(engine->start(),"start failed");
        checkNumericOutputs(engine);
        JS_ReportWarning(engine->getContext(),"Expected runtime warning");
        check(errors.empty(),"SpiderMonkey warning was reported as a runtime failure");
        {
            JSContext* cx=engine->getContext();
            JS::RootedObject global(cx,engine->getGlobalObject()),nested(cx),again(cx);
            check(axjs::getOrCreateNamespace(cx,global,"runtimeNamespace.rhi.enums",&nested),
                "Dotted native namespace creation failed");
            check(axjs::getOrCreateNamespace(cx,global,"runtimeNamespace.rhi.enums",&again)&&nested==again,
                "Dotted native namespace identity changed");
            JS::RootedValue marker(cx,JS::Int32Value(7));
            check(JS_DefineProperty(cx,nested,"marker",marker,JSPROP_ENUMERATE),"Namespace marker failed");
            JS::RootedValue result(cx);
            check(engine->evalString("runtimeNamespace.rhi.enums.marker===7 && "
                "typeof this['runtimeNamespace.rhi.enums']==='undefined'",&result)&&result.isTrue(),
                "Generated namespace was exposed as a literal dotted global key");
            // SM33 reports errors straight to the reporter outside JS execution.
            // Enter through a real native call so these checks validate the JS
            // exception as well as preservation of the existing property.
            check(engine->evalString("(function(){try{createTestNamespace('runtimeNamespace.rhi.enums.marker.child');}"
                "catch(e){return /non-object/.test(String(e)) && runtimeNamespace.rhi.enums.marker===7;}return false;})()",
                &result)&&result.isTrue(),"Non-object namespace conflict was silently overwritten");
            check(engine->evalString("(function(){try{createTestNamespace('invalid..namespace');}"
                "catch(e){return /non-empty/.test(String(e)) && typeof invalid==='undefined';}return false;})()",
                &result)&&result.isTrue(),"Empty namespace component was accepted");
        }
        {
            JSContext* cx=engine->getContext();JS::RootedValue value(cx,JS::DoubleValue(127.5));
            uint8_t byte{};int integer{};
            check(axjs::fromJsValue(cx,value,byte)&&byte==127&&!axjs::fromJsValue(cx,value,integer),
                "Fractional byte conversion must truncate while wider integers remain exact");
            // Byte arguments validate the truncated value. The old numeric
            // uint16->byte path defines (-1,0) as zero; it does not justify
            // restoring non-number coercions or wider byte wraparound here.
            struct ValidByte { double input; uint8_t expected; };
            for(const ValidByte valid:{ValidByte{-0.5,0},ValidByte{std::nextafter(-1.0,0.0),0},
                ValidByte{-0.0,0},ValidByte{0.0,0},ValidByte{127.5,127},ValidByte{255.0,255},
                ValidByte{255.5,255},ValidByte{std::nextafter(256.0,0.0),255}})
            {
                value.setDouble(valid.input);byte=73;integer=73;
                check(axjs::fromJsValue(cx,value,byte)&&byte==valid.expected,
                    "Finite fractional byte did not truncate before byte range validation");
                const bool exactInteger=std::trunc(valid.input)==valid.input;
                check(axjs::fromJsValue(cx,value,integer)==exactInteger &&
                    integer==(exactInteger?static_cast<int>(valid.input):73),
                    "Byte truncation changed wider integer exactness or rejected-value atomicity");
            }
            for(double invalid:{-1.0,std::nextafter(-1.0,-2.0),256.0,std::nextafter(256.0,257.0),
                65535.75,std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::quiet_NaN()})
            {
                value.setDouble(invalid);byte=73;
                check(!axjs::fromJsValue(cx,value,byte)&&byte==73,"Invalid byte range accepted or output modified");
            }
            for(const char* expression:{"undefined","null","true","false","'255.5'","new Number(255.5)",
                "({valueOf:function(){throw Error('Byte must not perform ToNumber coercion');}})"})
            {
                check(engine->evalString(expression,&value),"Nonnumeric byte fixture evaluation failed");byte=73;
                check(!axjs::fromJsValue(cx,value,byte)&&byte==73,"Nonnumeric byte was coerced or output modified");
            }
            value.setDouble(255.5);
            uint16_t widerByte=73;int64_t signedWide=73;uint64_t unsignedWide=73;
            check(!axjs::fromJsValue(cx,value,widerByte)&&widerByte==73 &&
                !axjs::fromJsValue(cx,value,signedWide)&&signedWide==73 &&
                !axjs::fromJsValue(cx,value,unsignedWide)&&unsignedWide==73,
                "Fractional byte changed wider unsigned/signed integer validation");
            float floating{};double doubleValue{};
            check(axjs::fromJsValue(cx,value,floating)&&floating==255.5f &&
                axjs::fromJsValue(cx,value,doubleValue)&&doubleValue==255.5,
                "Byte truncation changed floating parameter conversion");
            value.setDouble(9007199254740991.0);
            check(axjs::fromJsValue(cx,value,signedWide)&&signedWide==9007199254740991LL &&
                axjs::fromJsValue(cx,value,unsignedWide)&&unsignedWide==9007199254740991ULL,
                "Exact safe 64-bit integer boundary rejected");
            value.setDouble(9007199254740992.0);
            check(!axjs::fromJsValue(cx,value,signedWide)&&!axjs::fromJsValue(cx,value,unsignedWide),
                "Lossy 64-bit identifier accepted");
        }
        engine->addUpdateCallback([engine,&mutableUpdates,&nestedUpdates,calls=0](float) mutable {
            mutableUpdates=++calls;
            if(calls==1) engine->addUpdateCallback([&nestedUpdates](float){++nestedUpdates;});
        });
        engine->update(0.1f);
        check(mutableUpdates==1&&nestedUpdates==0,"update callback insertion changed current iteration");
        engine->update(0.1f);
        check(mutableUpdates==2&&nestedUpdates==1,"mutable callback state did not survive across frames");
        check(engine->executeString("function runtimeResult(){return 42;}")==0,"executeString failed");
        check(engine->executeGlobalFunction("runtimeResult")==42,"global function did not return integer");
        check(!engine->evalString("throw new Error('expected runtime test error');"),"exception accepted");
        check(!errors.empty(),"exception was not reported");
        {
            ax::AutoreleasePool pool("js runtime constructors");
            check(engine->evalString("var constructorObject = new RuntimeTestObject(); if(constructorObject.add(2)!==9) throw Error('invoke');"),"constructor failed");
            JSContext* cx=engine->getContext();
            JS::RootedValue value(cx);
            check(engine->evalString("constructorObject",&value),"constructor lookup failed");
            TestObject* native{};
            check(axjs::toNative(cx,value,&native) && native,"checked unwrap failed");
            check(native->getReferenceCount()==1,"constructor retain count changed");
            check(native->initCalls==1,"normal constructor must initialize exactly once");
            check(ax::PoolManager::getInstance()->isObjectInPools(native),"constructor not autoreleased");
            JS::RootedValue again(cx);
            check(axjs::wrapNative(cx,native,&again) && &again.toObject()==&value.toObject(),"wrapper identity changed");
            ax::Object* base{};
            check(axjs::toNative(cx,value,&base) && base==native,"derived to base conversion failed");
            TestProtocol* protocol{};
            check(axjs::toNative(cx,value,&protocol) && protocol->protocolValue()==7,"secondary base protocol conversion failed");
            check(engine->evalString("constructorObject.stringSize('中文\\0😀') === 11",&again) && again.isTrue(),"UTF8 or string_view lifetime conversion failed");
            check(engine->evalString("constructorObject.boolIdentity(9)===true && constructorObject.boolIdentity(0)===false && "
                "constructorObject.boolIdentity(1)===true && constructorObject.boolIdentity(null)===false && "
                "constructorObject.boolIdentity(undefined)===false && constructorObject.boolIdentity(NaN)===false && "
                "constructorObject.boolIdentity('')===false && constructorObject.boolIdentity('flag')===true && "
                "constructorObject.boolIdentity({})===true",&again) && again.isTrue(),
                "Single-signature native bool argument lost legacy JavaScript truthiness");
            check(engine->evalString("constructorObject.sumSpan([{x:1,y:2},{x:3,y:4}]) === 10",&again) && again.isTrue(),
                "span argument did not own converted element storage through native invocation");
            check(engine->evalString("constructorObject.getChild()===constructorObject.getChildren()[0]",&again) && again.isTrue(),
                "borrowed getter/container identity changed");
            JS::RootedValue child(cx);
            check(engine->evalString("constructorObject.getChild()",&child),"borrowed getter failed");
            engine->garbageCollect();
            check(axjs::detail::isNativeValid(cx,value),"GC collected live native wrapper");
            pool.clear();
            check(!axjs::detail::isNativeValid(cx,value),"destroyed native remained valid");
            check(!axjs::detail::isNativeValid(cx,child),"non-Object child outlived its native getter owner");
            check(!engine->evalString("constructorObject.add(1)"),"stale native receiver called");
        }
        {
            ax::AutoreleasePool pool("uninitialized native allocation");
            JSContext* cx=engine->getContext();
            JS::RootedValue value(cx);
            check(engine->evalString("RuntimeTestObject.__allocate()",&value),"raw native allocation failed");
            TestObject* native{};
            check(axjs::toNative(cx,value,&native) && native && native->initCalls==0,"raw allocation initialized twice");
            check(native->getReferenceCount()==1 && ax::PoolManager::getInstance()->isObjectInPools(native),
                "raw allocation changed constructor ownership");
            pool.clear();
            check(!axjs::detail::isNativeValid(cx,value),"raw allocation did not autorelease once");
        }
        {
            JSContext* cx=engine->getContext();
            const int before=OwnedValue::destroyed;
            const void* ownedKey{};
            check(engine->evalString("var ownedValue=new RuntimeOwnedValue();var ownedChild=ownedValue.getChild();"),
                "JS-owned non-Object constructor/child lease failed");
            {
                JS::RootedValue distinct(cx),child(cx),owner(cx);Borrowed* nativeChild{};
                check(engine->evalString("ownedValue!==ownedChild",&distinct)&&distinct.isTrue()&&
                    engine->evalString("ownedChild",&child)&&axjs::toNative(cx,child,&nativeChild)&&nativeChild&&nativeChild->value==19,
                    "Same-address parent and first member shared an incorrect native wrapper");
                check(engine->evalString("ownedValue",&owner),"Owned exact lookup receiver missing");
                ownedKey=checkOwnedValueLookups(cx,owner,child);
            }
            engine->garbageCollect();
            check(OwnedValue::destroyed==before,"Live JS-owned non-Object was finalized");
            {
                JS::RootedValue value(cx),again(cx),child(cx);
                OwnedValue* native{};
                check(engine->evalString("ownedValue",&value)&&axjs::toNative(cx,value,&native)&&native&&
                    axjs::wrapNative(cx,native,&again)&&&value.toObject()==&again.toObject(),
                    "Weak JS-owned wrapper identity was lost across GC");
                check(engine->evalString("ownedChild",&child)&&axjs::detail::isNativeValid(cx,child),
                    "Child lease expired while JS-owned parent remained live");
                check(checkOwnedValueLookups(cx,value,child)==ownedKey,"GC changed the live owned native key");
            }
            check(engine->evalString("ownedValue=null;"),"Owned value reference removal failed");
            engine->garbageCollect();
            check(OwnedValue::destroyed==before+1,"Unreachable JS-owned non-Object was permanently rooted");
            JS::RootedValue child(cx);
            check(engine->evalString("ownedChild",&child)&&!axjs::detail::isNativeValid(cx,child),
                "Borrowed child survived JS-owned parent's finalizer");
            check(!axjs::detail::findNativeValueObject(cx,ownedKey,typeid(OwnedValue)) &&
                !axjs::detail::findNativeValueObject(cx,ownedKey,typeid(Borrowed)) &&
                !axjs::detail::findNativeObject(cx,ownedKey),"Exact lookup revived a collected owner or expired child");
        }
        {
            JSContext* cx=engine->getContext();
            OffsetValue native;OffsetBase* base=&native;
            check(static_cast<const void*>(base)!=static_cast<const void*>(&native),
                "Exact lookup offset fixture must have a nonzero base-subobject offset");
            axjs::ScopedNative scope;JS::RootedValue wrapper(cx);
            check(axjs::wrapBorrowed(cx,&native,&wrapper,scope.token()),"Offset native wrapper setup failed");
            check(axjs::detail::findNativeValueObject(cx,static_cast<OffsetValue*>(base),typeid(OffsetValue))==&wrapper.toObject(),
                "Restored concrete pointer did not find the exact non-Object wrapper");
            check(!axjs::detail::findNativeValueObject(cx,base,typeid(OffsetValue)) &&
                !axjs::detail::findNativeValueObject(cx,&native,typeid(OffsetBase)),
                "Exact lookup accepted an unadjusted base address or base type");
            engine->garbageCollect();
            check(axjs::detail::findNativeValueObject(cx,static_cast<OffsetValue*>(base),typeid(OffsetValue))==&wrapper.toObject(),
                "GC lost the exact adjusted pointer identity");
            scope.token().expire();
            check(!axjs::detail::findNativeValueObject(cx,&native,typeid(OffsetValue)),
                "Exact lookup accepted an expired offset native lease");
        }
        {
            ax::AutoreleasePool pool("reference container conversion");
            JSContext* cx=engine->getContext();JS::RootedValue value(cx),array(cx);
            check(engine->evalString("var vectorObject=new RuntimeTestObject();vectorObject",&value),"container fixture failed");
            TestObject* native{};check(axjs::toNative(cx,value,&native)&&native,"container native lookup failed");
            check(engine->evalString("[vectorObject,vectorObject]",&array),"container array setup failed");
            ax::Vector<TestObject*> converted;
            check(axjs::fromJsValue(cx,array,converted)&&converted.size()==2&&native->getReferenceCount()==3,
                "Axmol Vector conversion did not retain each native element");
            converted.clear();check(native->getReferenceCount()==1,"Axmol Vector conversion leaked ownership");
            check(engine->evalString("[vectorObject,{}]",&array)&&!axjs::fromJsValue(cx,array,converted)&&converted.empty()&&native->getReferenceCount()==1,
                "Failed Axmol Vector conversion leaked or partially committed its elements");
        }
        {
            ax::AutoreleasePool pool("ordinary JS constructor receiver");
            JSContext* cx=engine->getContext();
            JS::RootedValue receiver(cx),donor(cx),wrapped(cx);
            check(engine->evalString("var associatedDonor=new RuntimeTestObject();var associatedReceiver=Object.create(RuntimeTestObject.prototype);associatedReceiver.marker=42;associateNative(associatedReceiver,associatedDonor);associatedReceiver",&receiver),
                "ordinary JS subclass receiver association failed");
            TestObject* native{};
            check(axjs::toNative(cx,receiver,&native)&&native&&native->getReferenceCount()==1,
                "receiver association changed native ownership");
            check(engine->evalString("associatedDonor",&donor)&&!axjs::detail::isNativeValid(cx,donor),
                "donor retained a second native identity");
            engine->garbageCollect();
            check(axjs::wrapNative(cx,native,&wrapped)&&&wrapped.toObject()==&receiver.toObject(),
                "ordinary receiver identity changed across GC");
            check(engine->evalString("if(associatedReceiver.add(3)!==10||associatedReceiver.marker!==42)throw Error('receiver');"),
                "associated native method did not use original JS receiver");
            pool.clear();
            check(!axjs::detail::isNativeValid(cx,receiver)&&!engine->evalString("associatedReceiver.add(1)"),
                "ordinary receiver survived native invalidation");
        }
        {
            JSContext* cx=engine->getContext();
            JS::RootedValue value(cx),roundTrip(cx);
            ax::Quat quaternion;
            check(engine->evalString("({x:0,y:0.5,z:0,w:0.5})",&value) && axjs::fromJsValue(cx,value,quaternion),
                "legacy Quaternion value conversion failed");
            check(axjs::toJsValue(cx,quaternion,&roundTrip) && axjs::fromJsValue(cx,roundTrip,quaternion) && quaternion.y==0.5f,
                "Quaternion round trip failed");
            ax::AABB bounds;
            check(engine->evalString("({min:{x:-1,y:-1,z:-1},max:{x:1,y:1,z:1}})",&value) && axjs::fromJsValue(cx,value,bounds),
                "legacy AABB min/max conversion failed");
            ax::Ray ray;
            check(engine->evalString("({origin:{x:-2,y:0,z:0},direction:{x:1,y:0,z:0}})",&value) && axjs::fromJsValue(cx,value,ray),
                "legacy Ray conversion failed");
            float distance=0;
            check(ray.intersects(bounds,&distance) && distance==1,"converted Ray/AABB native intersection failed");
            ax::OBB oriented(bounds),restored;
            check(axjs::toJsValue(cx,oriented,&value) && axjs::fromJsValue(cx,value,restored) && ray.intersects(restored,&distance),
                "legacy OBB round trip/native intersection failed");
            std::array<ax::Vec3,2> points;
            check(engine->evalString("[{x:1,y:2,z:3},{x:4,y:5,z:6}]",&value) && axjs::fromJsValue(cx,value,points) && points[1].z==6,
                "fixed-array element conversion failed");
            check(engine->evalString("[{x:1,y:2,z:3}]",&value) && !axjs::fromJsValue(cx,value,points),
                "fixed-array accepted the wrong element count");
            ax::Viewport viewport;
            check(engine->evalString("({x:1,y:2,width:640,height:480})",&value) && axjs::fromJsValue(cx,value,viewport) && viewport.height==480,
                "native Viewport conversion failed");
            check(engine->evalString("({x:1,y:2,width:-1,height:480})",&value) && !axjs::fromJsValue(cx,value,viewport),
                "negative Viewport accepted");
            ax::PhysicsMaterial2D material;
            check(engine->evalString("({density:2,restitution:0.5,friction:0.25})",&value) &&
                axjs::fromJsValue(cx,value,material) && material.density==2 && material.friction==0.25f,
                "native 2D physics material conversion failed");
            ax::BlendFunc blend;
            check(engine->evalString("({src:0x302,dst:0x303})",&value) && axjs::fromJsValue(cx,value,blend) &&
                blend.src==ax::rhi::BlendFactor::SRC_ALPHA && blend.dst==ax::rhi::BlendFactor::ONE_MINUS_SRC_ALPHA,
                "legacy GL blend factors were passed through as invalid RHI enums");
            check(axjs::toJsValue(cx,blend,&roundTrip) && axjs::fromJsValue(cx,roundTrip,blend) &&
                blend.dst==ax::rhi::BlendFactor::ONE_MINUS_SRC_ALPHA,"legacy blend factor round trip failed");
            check(engine->evalString("({src:0xdead,dst:1})",&value) && !axjs::fromJsValue(cx,value,blend),
                "invalid blend factor accepted");
            ax::Color32 color32;
            check(engine->evalString("({r:254.99,g:64.5,b:128.75,a:127.9})",&value) &&
                axjs::fromJsValue(cx,value,color32) && color32.r==254 && color32.g==64 && color32.b==128 && color32.a==127,
                "legacy floating color channels were not truncated to bytes");
            ax::Color color;
            check(engine->evalString("({r:255,g:127.5,b:0,a:255})",&value) && axjs::fromJsValue(cx,value,color) &&
                color.r==1 && color.g==0.5f && color.a==1,"legacy byte-scale color did not normalize for native float colors");
            check(axjs::toJsValue(cx,color,&roundTrip) && axjs::fromJsValue(cx,roundTrip,color32) &&
                color32.r==255 && color32.g==127 && color32.a==255,"native float color did not return legacy byte-scale channels");
#if defined(AX_ENABLE_NAVMESH)
            ax::NavMeshAgentParam parameters;
            check(engine->evalString("({radius:2,height:8,maxSpeed:8})",&value) &&
                axjs::fromJsValue(cx,value,parameters) && parameters.radius==2 && parameters.maxAcceleration==8 && parameters.queryFilterType==0,
                "NavMesh parameters lost native defaults");
            check(engine->evalString("({updateFlags:256})",&value) && !axjs::fromJsValue(cx,value,parameters),
                "NavMesh uint8 flags overflow accepted");
            ax::OffMeshLinkData link;
            check(engine->evalString("({startPosition:{x:1,y:2,z:3},endPosition:{x:4,y:5,z:6}})",&value) &&
                axjs::fromJsValue(cx,value,link) && link.endPosition.z==6,"OffMeshLink positions conversion failed");
#endif
#if defined(AX_ENABLE_3D)
            ax::Terrain::TerrainData terrain;
            const bool terrainConverted=engine->evalString("({heightMap:'height.png',alphaMap:'alpha.png',chunkSize:{width:32,height:32},"
                "mapHeight:2,mapScale:0.1,detailMap:[{file:'grass.png',size:35}]})",&value) &&
                axjs::fromJsValue(cx,value,terrain);
            check(terrainConverted && terrain._detailMapAmount==1 && terrain._detailMaps[0]._detailMapSrc=="grass.png" &&
                terrain._skirtHeightRatio==1.0f,
                "legacy Terrain data conversion failed");
            if(terrainConverted)
            {
                JS::RootedObject terrainInput(cx,&value.toObject());
                JS::RootedValue ratio(cx);
                for(float explicitRatio : {3.0f,0.0f,0.25f})
                {
                    ratio.setDouble(explicitRatio);
                    check(JS_SetProperty(cx,terrainInput,"skirtHeightRatio",ratio) &&
                        axjs::fromJsValue(cx,value,terrain) && terrain._skirtHeightRatio==explicitRatio,
                        "Terrain explicit skirt ratio was replaced by the legacy default");
                    ratio.setUndefined();
                    check(JS_SetProperty(cx,terrainInput,"skirtHeightRatio",ratio) &&
                        axjs::fromJsValue(cx,value,terrain) && terrain._skirtHeightRatio==1.0f,
                        "Terrain undefined skirt ratio reused an earlier native value");
                }
                check(engine->evalString("'invalid-ratio'",&ratio) &&
                    JS_SetProperty(cx,terrainInput,"skirtHeightRatio",ratio) &&
                    !axjs::fromJsValue(cx,value,terrain) && terrain._skirtHeightRatio==1.0f &&
                    terrain._heightMapSrc=="height.png" && terrain._detailMapAmount==1,
                    "Terrain invalid skirt ratio changed the previous native result");
            }
            check(engine->evalString("({heightMap:'height.png',alphaMap:'alpha.png',chunkSize:{width:32,height:32},"
                "mapHeight:2,mapScale:0.1,detailMap:[{file:'a',size:1},{file:'b',size:1},{file:'c',size:1},{file:'d',size:1},{file:'e',size:1}]})",&value) &&
                !axjs::fromJsValue(cx,value,terrain),"Terrain accepted detailMap array overflow");
#endif
        }
        {
            ax::AutoreleasePool pool("js runtime factory");
            auto* native=new TestObject;
            native->autorelease();
            JSContext* cx=engine->getContext();
            JS::RootedValue factory(cx),borrowed(cx);
            check(axjs::wrapNative(cx,native,&factory,axjs::NativeOwnership::Factory),"factory wrapping failed");
            check(axjs::wrapNative(cx,native,&borrowed,axjs::NativeOwnership::Borrowed),"getter wrapping failed");
            check(native->getReferenceCount()==1 && &factory.toObject()==&borrowed.toObject(),"factory/getter changed ownership");
            native->retain(); pool.clear();
            check(native->getReferenceCount()==1 && axjs::detail::isNativeValid(cx,factory),"factory was autoreleased twice");
            native->release();
            check(!axjs::detail::isNativeValid(cx,factory),"released factory not invalidated");
        }
        {
            JSContext* cx=engine->getContext();
            Borrowed native;
            JS::RootedValue value(cx);
            axjs::LifetimeToken token;
            check(axjs::wrapBorrowed(cx,&native,&value,token),"borrowed handle failed");
            Borrowed* result{};
            check(axjs::toNative(cx,value,&result) && result==&native,"borrowed unwrap failed");
            check(axjs::detail::findNativeValueObject(cx,&native,typeid(Borrowed))==&value.toObject(),
                "Exact lookup lost a live borrowed wrapper");
            token.expire();
            check(!axjs::detail::isNativeValid(cx,value),"expired scope still valid");
            check(!axjs::detail::findNativeValueObject(cx,&native,typeid(Borrowed)),
                "Exact lookup returned an expired borrowed wrapper");
            axjs::LifetimeToken newScope;
            JS::RootedValue replacement(cx);
            check(axjs::wrapBorrowed(cx,&native,&replacement,newScope),"reused address wrapping failed");
            check(&replacement.toObject()!=&value.toObject(),"scope reused stale wrapper");
            check(!axjs::detail::isNativeValid(cx,value),"old scope became valid after address reuse");
            check(axjs::detail::findNativeValueObject(cx,&native,typeid(Borrowed))==&replacement.toObject(),
                "Exact lookup did not select the new lease at a reused address");
        }
        std::shared_ptr<axjs::Callback> staleCallback;
        std::shared_ptr<axjs::Callback> staleOwnerBoundCallback;
        axjs::tests::CallbackRootsWatchPtr staleOwnerBoundWatch;
        {
            JSContext* cx=engine->getContext();
            JS::RootedValue function(cx),record(cx),result(cx),input(cx,JS::Int32Value(9));
            JS::RootedObject owner(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
            JS::RootedValue offset(cx,JS::Int32Value(10));
            check(engine->evalString("(function(x){return this.offset+x;})",&function),"owner callback compilation failed");
            check(JS_SetProperty(cx,owner,"offset",offset),"owner callback receiver setup failed");
            auto callback=axjs::Callback::createOwnerBound(cx,function,owner,&record);
            check(callback && axjs::detail::setOwnerDependency(cx,owner,record),"owner callback publication failed");
            auto watch=callback->watchRootsForTests();
            record.setUndefined();function.setUndefined();engine->garbageCollect();
            check(callback->call(JS::HandleValueArray(input),&result) && result.toInt32()==19,
                  "live weak-map owner lost its callback across GC");
            axjs::tests::CallbackRootsSnapshot facts;
            check(axjs::tests::snapshotCallbackRoots(watch,facts) && facts.ownerBoundCaptured &&
                  facts.ownerBoundReleaseCount==0 && !facts.functionRootCaptured && !facts.receiverRootCaptured,
                  "owner callback unexpectedly uses permanent roots");
            owner=nullptr;engine->garbageCollect();
            check(!callback->isValid() && !callback->call(JS::HandleValueArray(input),&result),
                  "unreachable owner/record/receiver cycle survived GC");
            check(axjs::tests::snapshotCallbackRoots(watch,facts) && facts.ownerBoundReleaseCount==1 &&
                  facts.functionRootReleaseCount==0 && facts.receiverRootReleaseCount==0,
                  "owner callback GC release facts are not exact");
            callback->cancel();engine->drainDeferredReleases();
            check(axjs::tests::snapshotCallbackRoots(watch,facts) && facts.ownerBoundReleaseCount==1,
                  "cancel after weak sweep released the owner record twice");

            owner=JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr());
            check(owner && engine->evalString("(function(){return 7;})",&function),"threaded owner callback setup failed");
            callback=axjs::Callback::createOwnerBound(cx,function,owner,&record);
            check(callback && axjs::detail::setOwnerDependency(cx,owner,record),"threaded owner callback publication failed");
            watch=callback->watchRootsForTests();
            std::thread cancelThread([callback]{callback->cancel();});cancelThread.join();
            check(!callback->isValid() && axjs::tests::snapshotCallbackRoots(watch,facts) && facts.ownerBoundReleaseCount==0,
                  "worker cancellation touched JS record before owner-thread drain");
            engine->drainDeferredReleases();
            check(axjs::tests::snapshotCallbackRoots(watch,facts) && facts.ownerBoundReleaseCount==1,
                  "owner-thread drain did not release cancelled owner callback");

            staleOwnerBoundCallback=axjs::Callback::createOwnerBound(cx,function,owner,&record);
            check(staleOwnerBoundCallback && axjs::detail::setOwnerDependency(cx,owner,record),"restart owner callback publication failed");
            staleOwnerBoundWatch=staleOwnerBoundCallback->watchRootsForTests();
            JS::RootedObject global(cx,engine->getGlobalObject());JS::RootedValue ownerValue(cx,JS::ObjectValue(*owner));
            check(JS_SetProperty(cx,global,"ownerBoundRestartFixture",ownerValue),"restart owner retention failed");
        }
        {
            JSContext* cx=engine->getContext();
            JS::RootedValue function(cx),result(cx);
            check(engine->evalString("(function(x){return this.offset+x;})",&function),"callback compilation failed");
            JS::RootedObject receiver(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
            JS::RootedValue offset(cx,JS::Int32Value(10));
            check(JS_SetProperty(cx,receiver,"offset",offset),"callback receiver setup failed");
            auto callback=axjs::Callback::create(cx,function,receiver);
            check(callback!=nullptr,"callback creation failed");
            engine->garbageCollect();
            JS::RootedValue input(cx,JS::Int32Value(9));
            check(callback->call(JS::HandleValueArray(input),&result) && result.toInt32()==19,"callback receiver/rooting failed");
            staleCallback=callback;
            std::thread releaseThread([callback=std::move(callback)]() mutable { callback.reset(); });
            releaseThread.join();
            check(staleCallback->isValid(),"shared callback prematurely cancelled");
            engine->drainDeferredReleases();
        }
        {
            JSContext* cx=engine->getContext();
            JS::RootedValue value(cx),roundtrip(cx);
            check(engine->evalString("({message:'中文\\0😀', point:{width:4,height:5},items:[1,true,{n:2}]})",&value),"value setup failed");
            ax::Value native;
            check(axjs::fromJsValue(cx,value,native) && axjs::toJsValue(cx,native,&roundtrip),"recursive Value conversion failed");
            JS::RootedValue cyclic(cx);
            check(engine->evalString("(function(){var x={};x.x=x;return x;})()",&cyclic),"cycle setup failed");
            check(!axjs::fromJsValue(cx,cyclic,native),"cyclic Value should fail");
            JS_ClearPendingException(cx);
        }
        const char* overrideDirectory=std::getenv("AX_JS_TEST_TEMP_DIR");
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
        const auto temporaryRoot=overrideDirectory?std::filesystem::path(overrideDirectory):
            std::filesystem::path("/data/local/tmp");
#else
        const auto temporaryRoot=overrideDirectory?std::filesystem::path(overrideDirectory):
            std::filesystem::temp_directory_path();
#endif
        auto tempPattern=(temporaryRoot/"axmol-js-runtime-tests-XXXXXX").string();
        std::vector<char> writablePattern(tempPattern.begin(),tempPattern.end());
        writablePattern.push_back('\0');
        const char* tempDir=mkdtemp(writablePattern.data());
        check(tempDir!=nullptr,"temporary directory failed");
        temporaryDirectory=tempDir;
#if AX_TARGET_PLATFORM == AX_PLATFORM_ANDROID
        ax::FileUtils::setDelegate(new NativeFiles(temporaryDirectory.string()+"/"));
#endif
        auto* files=ax::FileUtils::getInstance();
        const std::string source=std::string(tempDir)+"/bytecode.js";
        const std::string bytecode=std::string(tempDir)+"/bytecode.jsc";
        check(files->writeStringToFile("var loadedBytecode=81;loadedBytecode;",source),"source write failed");
        check(engine->compileBytecode(source,bytecode),"bytecode compile failed");
        check(files->writeStringToFile("throw new Error('source should not run');",source),"source overwrite failed");
        {
            JS::RootedValue completion(engine->getContext());
            check(engine->runScript(source,&completion)&&completion.isInt32()&&completion.toInt32()==81,
                "Matching .jsc was not preferred or lost its completion value");
            check(engine->runScript(source,&completion)&&completion.isInt32()&&completion.toInt32()==81,
                "Cached script execution lost its completion value");
        }
        engine->cleanAllScripts();
        check(files->writeStringToFile("broken",bytecode) && files->writeStringToFile(
            "var fallbackWorked=true;var scriptClass=function(){this.value=37;};scriptClass;",source),"fallback setup failed");
        {
            JS::RootedValue completion(engine->getContext()),instance(engine->getContext());
            check(engine->runScript(source,&completion)&&completion.isObject()&&
                JS_ObjectIsFunction(engine->getContext(),&completion.toObject()),
                "Damaged .jsc source fallback lost the returned script class");
            check(engine->evalString("new scriptClass().value",&instance)&&instance.isInt32()&&instance.toInt32()==37,
                "Returned script class did not execute in the game global");
        }
        engine->cleanAllScripts();
        std::filesystem::remove(source);
        check(!engine->runScript(bytecode),"damaged .jsc without source accepted");
        std::filesystem::remove_all(tempDir);
        const int ownedBeforeRestart=OwnedValue::destroyed;
        check(engine->evalString("var shutdownOwned=new RuntimeOwnedValue();var shutdownBorrowed=shutdownOwned.getChild();"),
            "Shutdown owned value and same-address borrowed child setup failed");
        check(OwnedValue::destroyed==ownedBeforeRestart,"Live shutdown fixture was prematurely destroyed");
        const void* shutdownOwnedKey{};
        Borrowed acrossVm;axjs::LifetimeToken oldVmLease;
        {
            JSContext* cx=engine->getContext();JS::RootedValue owner(cx),child(cx),borrowed(cx);
            check(engine->evalString("shutdownOwned",&owner) && engine->evalString("shutdownBorrowed",&child),
                "Shutdown exact lookup fixtures missing");
            shutdownOwnedKey=checkOwnedValueLookups(cx,owner,child);
            check(axjs::wrapBorrowed(cx,&acrossVm,&borrowed,oldVmLease) &&
                axjs::detail::findNativeValueObject(cx,&acrossVm,typeid(Borrowed))==&borrowed.toObject(),
                "Cross-VM fixed-address borrowed setup failed");
        }
        const auto generation=engine->getGeneration();
        check(engine->restart(),"VM restart failed");
        check(OwnedValue::destroyed==ownedBeforeRestart+1,"Restart did not release the old VM's globally reachable owned value exactly once");
        check(engine->getGeneration()>generation && !staleCallback->isValid(),"VM generation not invalidated");
        axjs::tests::CallbackRootsSnapshot stoppedOwnerFacts;
        check(!staleOwnerBoundCallback->isValid() &&
              axjs::tests::snapshotCallbackRoots(staleOwnerBoundWatch,stoppedOwnerFacts) &&
              stoppedOwnerFacts.ownerBoundReleaseCount==1 && stoppedOwnerFacts.functionRootReleaseCount==0 &&
              stoppedOwnerFacts.receiverRootReleaseCount==0,"restart did not release live owner-bound callback exactly once");
        staleOwnerBoundCallback.reset();
        {
            // All old JS roots and the old context are gone. Reuse only native
            // address keys, looking exclusively in the newly started registry.
            JSContext* cx=engine->getContext();
            check(!axjs::detail::findNativeValueObject(cx,shutdownOwnedKey,typeid(OwnedValue)) &&
                !axjs::detail::findNativeValueObject(cx,shutdownOwnedKey,typeid(Borrowed)) &&
                !axjs::detail::findNativeValueObject(cx,&acrossVm,typeid(Borrowed)),
                "New VM found a prior generation's exact non-Object wrapper");
            axjs::ScopedNative scope;JS::RootedValue replacement(cx);
            check(axjs::wrapBorrowed(cx,&acrossVm,&replacement,scope.token()),"New VM fixed-address wrap failed");
            oldVmLease.expire();
            check(axjs::detail::findNativeValueObject(cx,&acrossVm,typeid(Borrowed))==&replacement.toObject() &&
                axjs::detail::isNativeValid(cx,replacement),"New VM reused the old wrapper or its expired lease");
        }
        staleCallback.reset();
        check(engine->executeString("new RuntimeTestObject();")==0,"registration lost across restart");
        ax::PoolManager::getInstance()->getCurrentPool()->clear();
        const int ownedBeforeStop=OwnedValue::destroyed;
        check(engine->evalString("var stopOwnedA=new RuntimeOwnedValue();var stopBorrowedA=stopOwnedA.getChild();"
                                 "var stopOwnedB=new RuntimeOwnedValue();var stopBorrowedB=stopOwnedB.getChild();"),
            "Stop owned and borrowed value setup failed");
        engine->stop();
        check(OwnedValue::destroyed==ownedBeforeStop+2,"Stop did not release each owned allocation exactly once");
        check(!engine->isRunning(),"stop left VM running");
        engine->stop();
        check(OwnedValue::destroyed==ownedBeforeStop+2,"Repeated stop double-destroyed owned values");
        check(engine->start(),"start after cleanup failed");
        engine->stop();
        check(OwnedValue::destroyed==ownedBeforeStop+2,"Subsequent VM teardown destroyed a prior owned allocation twice");
        ax::ScriptEngineManager::getInstance()->removeScriptEngine();
        cleanupFiles();
        std::cout<<"SpiderMonkey 33.1 JS runtime tests passed\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<"JS runtime test failed: "<<error.what()<<'\n';
        engine->stop();
        ax::ScriptEngineManager::getInstance()->removeScriptEngine();
        cleanupFiles();
        return 1;
    }
}
