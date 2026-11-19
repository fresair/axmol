/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "runtime/JsRuntime.h"
#include "axmol/scene/Node.h"
#include <jsfriendapi.h>
#include <js/CharacterEncoding.h>
#include <algorithm>
#include <cstring>

namespace axjs
{
namespace
{
struct CallbackRootsBase
{
    virtual ~CallbackRootsBase() = default;
    virtual void release() = 0;
    virtual bool isReleased() const = 0;
    virtual void sweepWeakEdges() {}
};
struct TypeInfo
{
    std::type_index type{typeid(void)};
    std::type_index base{typeid(void)};
    TypeInfo* baseInfo{};
    detail::CastFunction cast{};
    JSClass* jsClass{};
    std::unique_ptr<JS::PersistentRootedObject> prototype;
    std::unique_ptr<JS::PersistentRootedObject> constructor;
    bool staticLifetime{};
};
}
struct NativeHandle
{
    std::weak_ptr<RuntimeState> state;
    RuntimeState* rawState{};
    uint64_t generation{};
    std::type_index type{typeid(void)};
    TypeInfo* typeInfo{};
    void* pointer{};
    const void* key{};
    ax::WeakPtr<ax::Object> object;
    ax::Node* nodeBase{};
    bool hasObject{};
    std::unique_ptr<LifetimeToken> token;
    bool staticLifetime{};
    bool ownsToken{};
    bool externalWrapper{};
    std::function<void(void*)> deleter;
    std::unique_ptr<JS::PersistentRootedObject> root;
    JS::Heap<JSObject*> weakWrapper{nullptr};
    void* registry{};
    JSObject* wrapper() const {return root?root->get():weakWrapper.get();}
    bool validFast(const RuntimeState* rt) const
    {
        return pointer && wrapper() && rt && rt==rawState && rt->alive && rt->generation==generation &&
            (!hasObject || !object.expired()) && (!token || token->isAlive());
    }
    bool valid() const
    {
        const auto rt=state.lock();
        return validFast(rt.get());
    }
};
namespace
{
struct NonObjectKey
{
    const void* pointer{};
    std::type_index type{typeid(void)};
    bool operator==(const NonObjectKey&) const = default;
};
struct NonObjectKeyHash
{
    size_t operator()(const NonObjectKey& key) const
    {
        return std::hash<const void*>{}(key.pointer) ^ (key.type.hash_code() << 1);
    }
};
struct Registry
{
    std::unordered_map<std::type_index,std::unique_ptr<TypeInfo>> types;
    // Object identity is its canonical Object base address. Non-Object values
    // additionally need their type: a parent and its first member can legally
    // have the same address while being separate native allocations/leases.
    std::unordered_map<const void*,NativeHandle*> native;
    std::unordered_map<NonObjectKey,NativeHandle*,NonObjectKeyHash> nativeValues;
    std::unordered_set<NativeHandle*> allHandles;
    std::unordered_set<NativeHandle*> leasedHandles;
    std::unordered_set<NativeHandle*> externalHandles;
    std::unordered_set<NativeHandle*> weakHandles;
    std::unordered_set<const JSClass*> nativeClasses;
    std::vector<std::shared_ptr<CallbackRootsBase>> callbacks;
    std::unique_ptr<JS::PersistentRootedObject> ownerDependencies;
};
Registry* registry(JSContext* cx)
{
    auto state=static_cast<RuntimeState*>(JS_GetContextPrivate(cx));
    return state ? static_cast<Registry*>(state->nativeRegistry) : nullptr;
}
void sweepWeakWrappers(JSFreeOp*,JSFinalizeStatus status,bool,void* data)
{
    if(status!=JSFINALIZE_GROUP_START)return;
    auto* r=static_cast<Registry*>(data);
    for(auto* handle:r->weakHandles)
        if(handle->weakWrapper && JS_IsAboutToBeFinalized(&handle->weakWrapper))
            handle->weakWrapper=nullptr;
    for(auto& callback:r->callbacks) callback->sweepWeakEdges();
}
NativeHandle* getHandle(Registry* r,JS::HandleValue value)
{
    if(!r || !value.isObject()) return nullptr;
    JSObject* obj=&value.toObject();
    const JSClass* clasp=JS_GetClass(obj);
    if(clasp && (clasp->finalize==finalizeNative || r->nativeClasses.contains(clasp)))
        return static_cast<NativeHandle*>(JS_GetPrivate(obj));
    // Persistent roots follow nursery/compacting GC. An ordinary object's
    // moving address must not be used as an untraced unordered-map key.
    for(auto* handle:r->externalHandles)
        if(handle->root && handle->root->get()==obj) return handle;
    return nullptr;
}
NativeHandle* getHandle(JSContext* cx,JS::HandleValue value)
{
    return getHandle(registry(cx),value);
}
void* castToType(Registry* r,TypeInfo* info,void* pointer,std::type_index targetType)
{
    for(size_t depth=0;info && depth<64;++depth)
    {
        if(info->type==targetType) return pointer;
        if(info->base==typeid(void) || !info->cast) return nullptr;
        if(!info->baseInfo)
        {
            auto it=r->types.find(info->base);
            if(it==r->types.end()) return nullptr;
            info->baseInfo=it->second.get();
        }
        pointer=info->cast(pointer);
        info=info->baseInfo;
    }
    return nullptr;
}
NativeHandle* findHandle(Registry* r,const void* pointer,ax::Object* object,std::type_index type)
{
    if(object)
    {
        auto it=r->native.find(object);
        return it!=r->native.end()?it->second:nullptr;
    }
    auto it=r->nativeValues.find(NonObjectKey{pointer,type});
    return it!=r->nativeValues.end()?it->second:nullptr;
}
void eraseHandleLookup(Registry* r,NativeHandle* handle)
{
    if(handle->hasObject)
    {
        auto it=r->native.find(handle->key);
        if(it!=r->native.end()&&it->second==handle)r->native.erase(it);
    }
    else
    {
        auto it=r->nativeValues.find(NonObjectKey{handle->key,handle->type});
        if(it!=r->nativeValues.end()&&it->second==handle)r->nativeValues.erase(it);
    }
}
void invalidateHandle(NativeHandle* handle)
{
    if(!handle) return;
    if(auto* r=static_cast<Registry*>(handle->registry))
    {
        // A stale but still reachable Object wrapper must not keep its former
        // native owner's dependencies alive after Object destruction. Lookup
        // and clearing an existing reserved slot allocate no JS storage.
        if(handle->root && r->ownerDependencies)
            if(auto rt=handle->state.lock(); rt && rt->onOwnerThread())
            {
                JSContext* cx=rt->context;
                JS::RootedObject owner(cx,handle->root->get()),map(cx,r->ownerDependencies->get());
                JS::RootedValue record(cx);
                if(JS::GetWeakMapEntry(cx,map,owner,&record) && record.isObject())
                    JS_SetReservedSlot(&record.toObject(),0,JS::UndefinedValue());
            }
        eraseHandleLookup(r,handle);
        r->leasedHandles.erase(handle);
        if(handle->externalWrapper)
        {
            r->externalHandles.erase(handle);
            r->allHandles.erase(handle);
        }
    }
    if(handle->ownsToken && handle->token) handle->token->expire();
    handle->root.reset();
    // Owned non-Object values live until finalization. A deleted Object can
    // never be accessed through a lingering JS wrapper.
    if(!handle->deleter)
    {
        handle->pointer=nullptr;
        handle->nodeBase=nullptr;
    }
    // Ordinary JS receivers have no native finalize hook/private slots.
    // Their Object-only handle is released here after its root is removed.
    if(handle->externalWrapper) delete handle;
}
bool forwardedGet(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    JS::RootedObject function(cx,&args.callee());
    JS::RootedObject owner(cx,&js::GetFunctionNativeReserved(function,0).toObject());
    JS::RootedValue key(cx,js::GetFunctionNativeReserved(function,1));
    JS::RootedId id(cx);
    return JS_ValueToId(cx,key,&id) && JS_GetPropertyById(cx,owner,id,args.rval());
}
bool forwardedSet(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    JS::RootedObject function(cx,&args.callee());
    JS::RootedObject owner(cx,&js::GetFunctionNativeReserved(function,0).toObject());
    JS::RootedValue key(cx,js::GetFunctionNativeReserved(function,1));
    JS::RootedId id(cx);
    if(!argc || !JS_ValueToId(cx,key,&id) || !JS_SetPropertyById(cx,owner,id,args[0])) return false;
    args.rval().setUndefined(); return true;
}
bool copyPeerProperties(JSContext* cx,JS::HandleObject owner,JS::HandleObject peer)
{
    JS::AutoIdVector keys(cx);
    if(!js::GetPropertyNames(cx,owner,JSITER_OWNONLY|JSITER_HIDDEN|JSITER_SYMBOLS,&keys)) return false;
    for(size_t index=0;index<keys.length();++index)
    {
        JS::RootedId id(cx,keys[index]);
        JS::RootedValue key(cx);
        if(!JS_IdToValue(cx,id,&key)) return false;
        std::string name;
        if(key.isString() && !detail::stringFromValue(cx,key,name)) return false;
        // _super is a temporary per-invocation binding, not shared instance state.
        if(name=="_super") continue;
        JS::Rooted<JSPropertyDescriptor> descriptor(cx);
        if(!JS_GetOwnPropertyDescriptorById(cx,owner,id,&descriptor)) return false;
        if(!descriptor.object()) continue;
        if(name=="update" || descriptor.hasGetterOrSetter())
        {
            if(name=="update" && descriptor.isPermanent())
            { JS_ReportError(cx,"A non-configurable action update cannot be rebound"); return false; }
            if(!JS_DefinePropertyById(cx,peer,id,descriptor.value(),descriptor.attributes(),
                descriptor.getter(),descriptor.setter())) return false;
            continue;
        }
        JS::RootedFunction getter(cx,js::NewFunctionWithReserved(cx,forwardedGet,0,0,nullptr,"get action state"));
        JS::RootedFunction setter(cx);
        if(!getter) return false;
        if(!descriptor.isReadonly())
        {
            setter=js::NewFunctionWithReserved(cx,forwardedSet,1,0,nullptr,"set action state");
            if(!setter) return false;
        }
        JS::RootedObject getterObject(cx,JS_GetFunctionObject(getter));
        JS::RootedObject setterObject(cx,setter ? JS_GetFunctionObject(setter) : nullptr);
        for(JSObject* function:{getterObject.get(),setterObject.get()})
            if(function)
            {
                js::SetFunctionNativeReserved(function,0,JS::ObjectValue(*owner));
                js::SetFunctionNativeReserved(function,1,key);
            }
        const unsigned attributes=(descriptor.attributes()&(JSPROP_ENUMERATE|JSPROP_PERMANENT))|
            JSPROP_GETTER|JSPROP_SETTER|JSPROP_SHARED;
        if(!JS_DefinePropertyById(cx,peer,id,JS::UndefinedHandleValue,attributes,
            reinterpret_cast<JSPropertyOp>(getterObject.get()),reinterpret_cast<JSStrictPropertyOp>(setterObject.get()))) return false;
    }
    return true;
}
}

LifetimeToken::LifetimeToken() : _alive(std::make_shared<std::atomic<bool>>(true)) {}
LifetimeToken::LifetimeToken(ax::Object* owner) : LifetimeToken()
{ _owner=owner; _hasOwner=true; }
LifetimeToken::LifetimeToken(std::function<bool()> validator) : LifetimeToken()
{ _validator=std::move(validator); }
LifetimeToken::LifetimeToken(ax::Object* owner,std::function<bool()> validator) : LifetimeToken(owner)
{ _validator=std::move(validator); }
void LifetimeToken::expire() const { _alive->store(false,std::memory_order_release); }
bool LifetimeToken::isAlive() const
{ return _alive->load(std::memory_order_acquire) && (!_hasOwner || !_owner.expired()) && (!_validator || _validator()); }

void RuntimeState::enqueue(std::function<void()> task)
{
    if(onOwnerThread()) { task(); return; }
    std::lock_guard lock(queueMutex);
    releaseQueue.push_back(std::move(task));
}
void RuntimeState::drain()
{
    if(!onOwnerThread()) return;
    for(;;)
    {
        std::vector<std::function<void()>> tasks;
        { std::lock_guard lock(queueMutex); tasks.swap(releaseQueue); }
        if(tasks.empty()) return;
        for(auto& task:tasks) task();
    }
}
std::shared_ptr<RuntimeState> runtimeState(JSContext* cx)
{
    if(!cx) return {};
    auto* state=static_cast<RuntimeState*>(JS_GetContextPrivate(cx));
    return state && state->engine ? state->engine->getRuntimeState() : nullptr;
}
ax::JsEngine* engine(JSContext* cx)
{
    auto state=runtimeState(cx);
    return state ? state->engine : nullptr;
}

namespace detail
{
bool createRegistry(JSContext* cx)
{
    auto state=runtimeState(cx);
    auto* r=new Registry;
    state->nativeRegistry=r;
    return JS_AddFinalizeCallback(JS_GetRuntime(cx),sweepWeakWrappers,r);
}
void releaseCallbackRoots(JSContext* cx)
{
    auto* r=registry(cx);
    if(!r) return;
    for(auto& roots:r->callbacks) roots->release();
    r->callbacks.clear();
}
void collectExpiredNativeHandles(JSContext* cx)
{
    auto* r=registry(cx); if(!r) return;
    for(auto it=r->leasedHandles.begin();it!=r->leasedHandles.end();)
    {
        auto* handle=*it++;
        if(!handle->valid()) invalidateHandle(handle);
    }
}
void destroyRegistry(JSContext* cx)
{
    auto* r=registry(cx);
    if(!r) return;
    JS_RemoveFinalizeCallback(JS_GetRuntime(cx),sweepWeakWrappers);
    releaseCallbackRoots(cx);
    // Revoke all handles before deleting any owned value. A value reachable
    // from the last global can survive the shutdown GC, so VM-owned native
    // values cannot rely on a later JS finalizer to release their allocation.
    // Clear each pointer/deleter first: native destruction can invalidate other
    // peers, and any later JS finalizer must only dispose its empty handle.
    std::vector<std::pair<void*,std::function<void(void*)>>> owned;
    auto handles=r->allHandles;
    for(auto* handle:handles)
    {
        const bool external=handle->externalWrapper;
        if(!external && handle->pointer && handle->deleter)
        {
            owned.emplace_back(handle->pointer,std::move(handle->deleter));
            handle->pointer=nullptr;
        }
        invalidateHandle(handle);
        if(!external)handle->registry=nullptr;
    }
    r->native.clear(); r->nativeValues.clear(); r->allHandles.clear(); r->types.clear();
    auto state=runtimeState(cx);
    state->nativeRegistry=nullptr;
    delete r;
    for(auto& [pointer,deleter]:owned) deleter(pointer);
}
bool registerTypeInfo(JSContext* cx,std::type_index type,std::type_index base,CastFunction cast,
                      JS::HandleObject prototype,JSClass* jsClass)
{
    auto* r=registry(cx);
    if(!r || !prototype || !jsClass || !(jsClass->flags & JSCLASS_HAS_PRIVATE))
    { JS_ReportError(cx,"Native classes require a prototype and JSCLASS_HAS_PRIVATE"); return false; }
    if(r->types.contains(type))
    { JS_ReportError(cx,"Native class registered twice: %s",jsClass->name); return false; }
    JS::RootedValue ctor(cx);
    if(!JS_GetProperty(cx,prototype,"constructor",&ctor)) return false;
    auto info=std::make_unique<TypeInfo>();
    info->type=type; info->base=base; info->cast=cast; info->jsClass=jsClass;
    if(base!=typeid(void))
        if(auto baseIt=r->types.find(base);baseIt!=r->types.end()) info->baseInfo=baseIt->second.get();
    info->prototype=std::make_unique<JS::PersistentRootedObject>(cx,prototype);
    if(ctor.isObject()) info->constructor=std::make_unique<JS::PersistentRootedObject>(cx,&ctor.toObject());
    auto* rawInfo=info.get();
    r->nativeClasses.insert(jsClass);
    r->types.emplace(type,std::move(info));
    for(auto& [_,entry]:r->types)
        if(entry->base==type) entry->baseInfo=rawInfo;
    return true;
}
JSObject* getPrototypeInfo(JSContext* cx,std::type_index type)
{
    auto* r=registry(cx); if(!r) return nullptr;
    auto it=r->types.find(type); return it!=r->types.end() ? it->second->prototype->get() : nullptr;
}
JSObject* getClassObjectInfo(JSContext* cx,std::type_index type)
{
    auto* r=registry(cx); if(!r) return nullptr;
    auto it=r->types.find(type);
    return it!=r->types.end() && it->second->constructor ? it->second->constructor->get() : nullptr;
}
JSClass* getClassInfo(JSContext* cx,std::type_index type)
{
    auto* r=registry(cx); if(!r) return nullptr;
    auto it=r->types.find(type); return it!=r->types.end() ? it->second->jsClass : nullptr;
}
void setStaticLifetimeInfo(JSContext* cx,std::type_index type)
{
    if(auto* r=registry(cx))
        if(auto it=r->types.find(type);it!=r->types.end()) it->second->staticLifetime=true;
}
bool bindNativeInfo(JSContext* cx,JS::HandleObject wrapper,void* pointer,ax::Object* object,
                    std::type_index type,NativeOwnership ownership,const LifetimeToken* token,
                    std::function<void(void*)> deleter)
{
    auto* r=registry(cx);
    if(!r || !wrapper || !pointer) return false;
    if(token) object=nullptr;
    auto info=r->types.find(type);
    if(info==r->types.end()) { JS_ReportError(cx,"Unregistered native type %s",type.name()); return false; }
    if(!r->nativeClasses.contains(JS_GetClass(wrapper)))
    { JS_ReportError(cx,"Object is not a registered native wrapper"); return false; }
    if(JS_GetPrivate(wrapper)) { JS_ReportError(cx,"Native wrapper already initialized"); return false; }
    const void* key=object ? static_cast<void*>(object) : pointer;
    if(auto* existing=findHandle(r,pointer,object,type))
    {
        if(existing->valid()) { JS_ReportError(cx,"Native object already has a wrapper"); return false; }
        invalidateHandle(existing);
    }
    if(!object && !token && !info->second->staticLifetime && !deleter)
    { JS_ReportError(cx,"Borrowed non-Object type %s requires a lifetime token",type.name()); return false; }
    auto* handle=new NativeHandle;
    auto state=runtimeState(cx);
    handle->state=state; handle->rawState=state.get(); handle->generation=state->generation;
    handle->type=type; handle->typeInfo=info->second.get();
    handle->pointer=pointer; handle->key=key; handle->registry=r;
    handle->object=object; handle->hasObject=object!=nullptr;
    handle->nodeBase=object ? static_cast<ax::Node*>(castToType(r,handle->typeInfo,pointer,typeid(ax::Node))) : nullptr;
    handle->staticLifetime=info->second->staticLifetime;
    handle->deleter=std::move(deleter);
    if(token) handle->token=std::make_unique<LifetimeToken>(*token);
    else if(handle->deleter)
    {
        // Children of a JS-owned non-Object share this lease, so none can
        // outlive the owning native allocation when its wrapper is finalized.
        handle->token=std::make_unique<LifetimeToken>();
        handle->ownsToken=true;
    }
    if(!object&&handle->deleter)
    {
        // JS-created non-Object allocations (Properties, Chipmunk peers, value
        // records) are owned by their wrapper and must finalize when JS drops
        // its last reference. NativeGC=0 applies to Axmol Objects, not these
        // allocations. The Heap barrier and weak sweep track moving GC.
        handle->weakWrapper=wrapper;r->weakHandles.insert(handle);
    }
    else handle->root=std::make_unique<JS::PersistentRootedObject>(cx,wrapper);
    JS_SetPrivate(wrapper,handle);
    if(object)r->native[key]=handle;
    else r->nativeValues[NonObjectKey{key,type}]=handle;
    r->allHandles.insert(handle);
    if(token) r->leasedHandles.insert(handle);
    if(object)
    {
        object->markScriptBindingExposed();
        if(ownership==NativeOwnership::Constructor) object->autorelease();
    }
    return true;
}
bool wrapNativeInfo(JSContext* cx,void* pointer,ax::Object* object,std::type_index type,
                    NativeOwnership ownership,const LifetimeToken* token,JS::MutableHandleValue result)
{
    if(!pointer) { result.setNull(); return true; }
    auto* r=registry(cx); if(!r) return false;
    if(auto* handle=findHandle(r,pointer,object,type))
    {
        if(handle->valid()) { result.setObject(*handle->wrapper()); return true; }
        invalidateHandle(handle);
    }
    if(token && object)
    {
        object=nullptr;
        if(auto* handle=findHandle(r,pointer,nullptr,type))
        {
            if(handle->valid()) { result.setObject(*handle->wrapper()); return true; }
            invalidateHandle(handle);
        }
    }
    auto info=r->types.find(type);
    if(info==r->types.end()) { JS_ReportError(cx,"Unregistered native type %s",type.name()); return false; }
    JS::RootedObject prototype(cx,info->second->prototype->get());
    JS::RootedObject wrapper(cx,JS_NewObject(cx,info->second->jsClass,prototype,JS::NullPtr()));
    if(!wrapper || !bindNativeInfo(cx,wrapper,pointer,object,type,ownership,token,{})) return false;
    result.setObject(*wrapper); return true;
}
bool rebindNativeInfo(JSContext* cx,JS::HandleObject wrapper,void* pointer,ax::Object* object,std::type_index type)
{
    JS::RootedValue wrapped(cx,JS::ObjectValue(*wrapper));
    auto* old=getHandle(cx,wrapped);
    auto* r=registry(cx);
    if(!r || !old || !old->valid() || !old->hasObject || old->deleter || !pointer || !object)
    { JS_ReportError(cx,"Rebinding requires a live Object wrapper and a new Object"); return false; }
    if(old->key==object) return true;
    if(auto it=r->native.find(object);it!=r->native.end() && it->second->valid())
    { JS_ReportError(cx,"The replacement native object already has a JS wrapper"); return false; }
    JS::RootedObject nativePrototype(cx,getPrototypeInfo(cx,type)),oldPrototype(cx);
    auto info=r->types.find(type);
    if(!nativePrototype || info==r->types.end() || !JS_GetPrototype(cx,wrapper,&oldPrototype)) return false;
    JS::RootedObject peer(cx,JS_NewObject(cx,getClassInfo(cx,old->type),oldPrototype,JS::NullPtr()));
    JS::RootedObject staging(cx,JS_NewObject(cx,info->second->jsClass,nativePrototype,JS::NullPtr()));
    if(!peer || !staging || !copyPeerProperties(cx,wrapper,peer)) return false;
    // Prepare the new handle while the original wrapper is still valid. Neither
    // reference is retained or autoreleased here; outer owns inner natively.
    if(!bindNativeInfo(cx,staging,pointer,object,type,NativeOwnership::Factory,nullptr,{})) return false;
    if(!JS_SetPrototype(cx,wrapper,nativePrototype) || !JS_DeleteProperty(cx,wrapper,"update"))
    {
        auto* replacement=static_cast<NativeHandle*>(JS_GetPrivate(staging));
        invalidateHandle(replacement);
        return false;
    }
    auto* replacement=static_cast<NativeHandle*>(JS_GetPrivate(staging));
    JS_SetPrivate(staging,nullptr);
    const bool external=old->externalWrapper;
    if(external)
    {
        old->externalWrapper=false;r->externalHandles.erase(old);
        replacement->externalWrapper=true;r->externalHandles.insert(replacement);
    }
    JS_SetPrivate(peer,old);
    if(!external)JS_SetPrivate(wrapper,replacement);
    old->root->set(peer);
    replacement->root->set(wrapper);
    return true;
}
void* unwrapNativeInfo(JSContext* cx,JS::HandleValue value,std::type_index type,bool reportInvalid)
{
    auto* state=static_cast<RuntimeState*>(JS_GetContextPrivate(cx));
    auto* r=state ? static_cast<Registry*>(state->nativeRegistry) : nullptr;
    auto* handle=getHandle(r,value);
    if(!handle) return nullptr;
    if(!handle->validFast(state))
    {
        if(reportInvalid) JS_ReportError(cx,"Native object is no longer valid");
        return nullptr;
    }
    if(handle->type==type) return handle->pointer;
    if(type==typeid(ax::Node) && handle->nodeBase) return handle->nodeBase;
    if(type==typeid(ax::Object) && handle->hasObject) return handle->object.get();
    return castToType(r,handle->typeInfo,handle->pointer,type);
}
JSObject* findNativeObject(JSContext* cx,const void* pointer)
{
    auto* r=registry(cx); if(!r) return nullptr;
    auto it=r->native.find(pointer);
    if(it!=r->native.end()&&it->second->valid())return it->second->wrapper();
    // The untyped lookup is used by native callbacks. Refuse ambiguous
    // non-Object addresses; typed wrapNativeInfo always uses the exact key.
    NativeHandle* match{};
    for(const auto& [key,handle]:r->nativeValues)
        if(key.pointer==pointer&&handle->valid())
        {if(match)return nullptr;match=handle;}
    return match?match->wrapper():nullptr;
}
JSObject* findNativeValueObject(JSContext* cx,const void* pointer,std::type_index exactType)
{
    auto* r=registry(cx); if(!r) return nullptr;
    auto it=r->nativeValues.find(NonObjectKey{pointer,exactType});
    return it!=r->nativeValues.end()&&it->second->valid()?it->second->wrapper():nullptr;
}
bool setOwnerDependency(JSContext* cx,JS::HandleObject owner,JS::HandleValue dependency)
{
    auto* r=registry(cx); if(!r) return false;
    if(!r->ownerDependencies)
    {
        JS::RootedObject map(cx,JS::NewWeakMapObject(cx)); if(!map) return false;
        r->ownerDependencies=std::make_unique<JS::PersistentRootedObject>(cx,map);
    }
    JS::RootedObject map(cx,r->ownerDependencies->get());
    JS::RootedValue record(cx);
    if(!JS::GetWeakMapEntry(cx,map,owner,&record)) return false;
    if(record.isObject()) { JS_SetReservedSlot(&record.toObject(),0,dependency); return true; }
    static const JSClass dependencyClass={
        "AxmolOwnerDependency",JSCLASS_HAS_RESERVED_SLOTS(1),
        JS_PropertyStub,JS_DeletePropertyStub,JS_PropertyStub,JS_StrictPropertyStub,
        JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub,nullptr
    };
    JS::RootedObject value(cx,JS_NewObject(cx,&dependencyClass,JS::NullPtr(),JS::NullPtr()));
    if(!value) return false;
    JS_SetReservedSlot(value,0,dependency); record.setObject(*value);
    return JS::SetWeakMapEntry(cx,map,owner,record);
}
JSObject* getNativePrototype(JSContext* cx,JS::HandleValue wrapper)
{
    auto* handle=getHandle(cx,wrapper);
    if(!handle || !handle->valid()) return nullptr;
    return getPrototypeInfo(cx,handle->type);
}
bool isNativeValid(JSContext* cx,JS::HandleValue value)
{ auto* handle=getHandle(cx,value); return handle && handle->valid(); }
ax::Object* nativeObjectBase(JSContext* cx,JS::HandleValue value)
{
    auto* handle=getHandle(cx,value);
    if(!handle || !handle->valid()) return nullptr;
    if(handle->hasObject) return handle->object.get();
    return static_cast<ax::Object*>(castToType(registry(cx),handle->typeInfo,handle->pointer,typeid(ax::Object)));
}
void invalidateExpiredNative(JSContext* cx,const void* pointer)
{
    auto* r=registry(cx); if(!r) return;
    auto it=r->native.find(pointer);
    if(it!=r->native.end() && !it->second->valid()) invalidateHandle(it->second);
    std::vector<NativeHandle*> expired;
    for(const auto& [key,handle]:r->nativeValues)
        if(key.pointer==pointer&&!handle->valid())expired.push_back(handle);
    for(auto* handle:expired)invalidateHandle(handle);
}
bool getNativeLifetime(JSContext* cx,JS::HandleValue value,LifetimeToken& out)
{
    auto* handle=getHandle(cx,value);
    if(!handle || !handle->valid()) return false;
    if(handle->token) out=*handle->token;
    else if(handle->hasObject) out=LifetimeToken(handle->object.get());
    else if(!handle->staticLifetime) return false;
    return true;
}
void invalidateNative(JSContext* cx,const void* pointer)
{
    auto* r=registry(cx); if(!r) return;
    auto it=r->native.find(pointer); if(it!=r->native.end()) invalidateHandle(it->second);
    std::vector<NativeHandle*> values;
    for(const auto& [key,handle]:r->nativeValues)
        if(key.pointer==pointer)values.push_back(handle);
    for(auto* handle:values)invalidateHandle(handle);
}
bool stringFromValue(JSContext* cx,JS::HandleValue value,std::string& result)
{
    if(!value.isString()) return false;
    JS::RootedString str(cx,value.toString());
    size_t bytes=0;
    {
        JS::AutoCheckCannotGC nogc;
        size_t length{};
        if(JS_StringHasLatin1Chars(str))
        {
            const auto* chars=JS_GetLatin1StringCharsAndLength(cx,nogc,str,&length);
            if(!chars) return false;
            for(size_t i=0;i<length;++i) bytes+=chars[i]<0x80 ? 1 : 2;
        }
        else
        {
            const auto* chars=JS_GetTwoByteStringCharsAndLength(cx,nogc,str,&length);
            if(!chars) return false;
            for(size_t i=0;i<length;++i)
            {
                const uint32_t c=chars[i];
                if(c>=0xd800 && c<=0xdbff && i+1<length && chars[i+1]>=0xdc00 && chars[i+1]<=0xdfff)
                { bytes+=4; ++i; }
                else bytes+=c<0x80 ? 1 : c<0x800 ? 2 : 3;
            }
        }
    }
    char* utf8=JS_EncodeStringToUTF8(cx,str);
    if(!utf8) return false;
    result.assign(utf8,bytes); JS_free(cx,utf8); return true;
}
bool stringToValue(JSContext* cx,std::string_view value,JS::MutableHandleValue result)
{
    size_t length{};
    auto chars=JS::UTF8CharsToNewTwoByteCharsZ(cx,JS::UTF8Chars(value.data(),value.size()),&length);
    if(!chars) return false;
    JS::RootedString string(cx,JS_NewUCStringCopyN(cx,chars.get(),length));
    JS_free(cx,chars.get());
    if(!string) return false; result.setString(string); return true;
}
bool readProperty(JSContext* cx,JS::HandleValue value,const char* name,JS::MutableHandleValue result)
{
    if(!value.isObject()) return false;
    JS::RootedObject object(cx,&value.toObject());
    return JS_GetProperty(cx,object,name,result) && !result.isUndefined();
}
}

bool associateObjectWithNative(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    if(argc!=2||!args[0].isObject()||!args[1].isObject())
    {JS_ReportError(cx,"Native association expects an object receiver and a native wrapper");return false;}
    auto* r=registry(cx);
    auto* source=getHandle(cx,args[1]);
    if(!r||!source||!source->valid()||!source->hasObject||source->deleter)
    {JS_ReportError(cx,"Native association requires a live Axmol Object");return false;}
    JS::RootedObject receiver(cx,&args[0].toObject()),donor(cx,&args[1].toObject());
    if(receiver==donor) {args.rval().setObject(*receiver);return true;}
    if(getHandle(cx,args[0]))
    {JS_ReportError(cx,"The receiver already has a native association");return false;}
    if(r->ownerDependencies)
    {
        JS::RootedObject map(cx,r->ownerDependencies->get()); JS::RootedValue dependency(cx);
        if(!JS::GetWeakMapEntry(cx,map,donor,&dependency)) return false;
        if(!dependency.isUndefined())
        {
            if(!JS::SetWeakMapEntry(cx,map,receiver,dependency)) return false;
            JS::RootedValue empty(cx,JS::UndefinedValue());
            if(!JS::SetWeakMapEntry(cx,map,donor,empty)) return false;
        }
    }
    const bool receiverHasPrivate=r->nativeClasses.contains(JS_GetClass(receiver));
    if(!source->externalWrapper)JS_SetPrivate(donor,nullptr);
    else r->externalHandles.erase(source);
    source->externalWrapper=!receiverHasPrivate;
    source->root->set(receiver);
    if(receiverHasPrivate)JS_SetPrivate(receiver,source);
    else r->externalHandles.insert(source);
    args.rval().setObject(*receiver);
    return true;
}

void finalizeNative(JSFreeOp*,JSObject* object)
{
    auto* handle=static_cast<NativeHandle*>(JS_GetPrivate(object));
    if(!handle) return;
    if(auto* r=static_cast<Registry*>(handle->registry))
    {
        eraseHandleLookup(r,handle);
        r->allHandles.erase(handle);
        r->leasedHandles.erase(handle);
        r->weakHandles.erase(handle);
    }
    if(handle->ownsToken && handle->token) handle->token->expire();
    if(handle->pointer && handle->deleter) handle->deleter(handle->pointer);
    delete handle;
    JS_SetPrivate(object,nullptr);
}
bool inheritNativeSubclass(JSContext* cx,JS::HandleValue source,JS::HandleValue result)
{
    auto* original=getHandle(cx,source);
    auto* copy=getHandle(cx,result);
    if(!original || !copy || !original->valid() || !copy->valid() || original->type!=copy->type) return true;
    JS::RootedObject originalObject(cx,&source.toObject()),copyObject(cx,&result.toObject()),prototype(cx);
    if(!JS_GetPrototype(cx,originalObject,&prototype)) return false;
    if(prototype && prototype!=detail::getPrototypeInfo(cx,original->type))
        return JS_SetPrototype(cx,copyObject,prototype);
    return true;
}
bool getOrCreateNamespace(JSContext* cx,JS::HandleObject global,const char* name,JS::MutableHandleObject out)
{
    const std::string_view path=name ? std::string_view(name) : std::string_view{};
    if(path.empty() || path.front()=='.' || path.back()=='.' || path.find("..")!=path.npos)
    { JS_ReportError(cx,"Native namespace must contain non-empty path components"); return false; }
    JS::RootedObject parent(cx,global),object(cx);
    JS::RootedValue value(cx);
    size_t begin=0;
    while(begin<path.size())
    {
        const size_t end=path.find('.',begin);
        const std::string component(path.substr(begin,end==path.npos ? path.size()-begin : end-begin));
        if(!JS_GetProperty(cx,parent,component.c_str(),&value)) return false;
        if(value.isObject()) object=&value.toObject();
        else
        {
            if(!value.isUndefined())
            { JS_ReportError(cx,"Namespace %s already contains a non-object at %s",name,component.c_str()); return false; }
            object=JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr());
            if(!object) return false;
            value.setObject(*object);
            if(!JS_DefineProperty(cx,parent,component.c_str(),value,JSPROP_ENUMERATE)) return false;
        }
        parent=object;
        if(end==path.npos) break;
        begin=end+1;
    }
    out.set(parent); return true;
}

namespace tests
{
// No JS values, callback handles, RuntimeState, or roots are retained here.
struct CallbackRootsWatch
{
    uint64_t generation{};
    bool functionRootCaptured{}, receiverRootCaptured{};
    std::atomic<uint32_t> functionRootReleaseCount{}, receiverRootReleaseCount{};
    bool ownerBoundCaptured{};
    std::atomic<uint32_t> ownerBoundReleaseCount{};
};
bool snapshotCallbackRoots(const CallbackRootsWatchPtr& watch, CallbackRootsSnapshot& out)
{
    if(!watch) return false;
    out.generation=watch->generation;
    out.functionRootCaptured=watch->functionRootCaptured;
    out.receiverRootCaptured=watch->receiverRootCaptured;
    out.functionRootReleaseCount=watch->functionRootReleaseCount.load(std::memory_order_acquire);
    out.receiverRootReleaseCount=watch->receiverRootReleaseCount.load(std::memory_order_acquire);
    out.ownerBoundCaptured=watch->ownerBoundCaptured;
    out.ownerBoundReleaseCount=watch->ownerBoundReleaseCount.load(std::memory_order_acquire);
    return true;
}
}

struct Callback::Roots : CallbackRootsBase
{
    std::weak_ptr<RuntimeState> state;
    uint64_t generation{};
    std::unique_ptr<JS::PersistentRootedValue> function;
    std::unique_ptr<JS::PersistentRootedObject> receiver;
    JS::Heap<JSObject*> ownerRecord{nullptr};
    bool ownerBound{};
    uint32_t ownerBoundReleaseCount{};
    uint32_t functionRootReleaseCount{}, receiverRootReleaseCount{};
    std::vector<std::weak_ptr<tests::CallbackRootsWatch>> watches;
    std::atomic<bool> released{false};
    std::atomic<bool> cancelled{false};
    void release() override
    {
        cancelled.store(true,std::memory_order_release);
        if(function) { function.reset(); ++functionRootReleaseCount; }
        if(receiver) { receiver.reset(); ++receiverRootReleaseCount; }
        if(ownerBound && !ownerBoundReleaseCount)
        {
            if(ownerRecord)
            {
                JS_SetReservedSlot(ownerRecord,0,JS::UndefinedValue());
                JS_SetReservedSlot(ownerRecord,1,JS::UndefinedValue());
                ownerRecord=nullptr;
            }
            ++ownerBoundReleaseCount;
        }
        // Record only after the actual PersistentRooted objects were destroyed.
        // Callback weak_ptr expiration alone cannot prove these releases: the
        // registry can continue owning Roots until releaseCallbackRoots().
        std::erase_if(watches,[this](auto& weak) {
            auto watch=weak.lock();
            if(!watch) return true;
            watch->functionRootReleaseCount.store(functionRootReleaseCount,std::memory_order_release);
            watch->receiverRootReleaseCount.store(receiverRootReleaseCount,std::memory_order_release);
            watch->ownerBoundReleaseCount.store(ownerBoundReleaseCount,std::memory_order_release);
            return false;
        });
        released.store(true,std::memory_order_release);
    }
    bool isReleased() const override { return released.load(std::memory_order_acquire); }
    void sweepWeakEdges() override
    {
        if(ownerRecord && JS_IsAboutToBeFinalized(&ownerRecord))
        {
            // Do not access slots in an object scheduled for finalization.
            ownerRecord=nullptr;
            release();
        }
    }
};
std::shared_ptr<Callback::Roots> Callback::makeRoots(JSContext* cx)
{
    auto state=runtimeState(cx);
    if(!state || !state->alive || !state->onOwnerThread()) return {};
    auto roots=std::shared_ptr<Roots>(new Roots,[weak=std::weak_ptr<RuntimeState>(state)](Roots* p) {
        if(p->released.load(std::memory_order_acquire)) { delete p; return; }
        if(auto rt=weak.lock()) rt->enqueue([p] { p->release(); delete p; });
        else { /* VM teardown released the root before its state expired. */ delete p; }
    });
    roots->state=state; roots->generation=state->generation;
    auto* r=registry(cx);
    if(r->callbacks.size()>64) std::erase_if(r->callbacks,[](auto& item){ return item->isReleased(); });
    r->callbacks.push_back(roots);
    return roots;
}
std::shared_ptr<Callback> Callback::create(JSContext* cx,JS::HandleValue function,JS::HandleObject receiver)
{
    if(!function.isObject() || !JS_ObjectIsCallable(cx,&function.toObject())) return {};
    auto roots=makeRoots(cx); if(!roots) return {};
    roots->function=std::make_unique<JS::PersistentRootedValue>(cx,function);
    roots->receiver=std::make_unique<JS::PersistentRootedObject>(cx,receiver);
    return std::shared_ptr<Callback>(new Callback(std::move(roots)));
}
std::shared_ptr<Callback> Callback::createOwnerBound(JSContext* cx,JS::HandleValue function,
                                                  JS::HandleObject receiver,JS::MutableHandleValue out)
{
    if(!function.isObject() || !JS_ObjectIsCallable(cx,&function.toObject())) return {};
    static const JSClass recordClass={
        "AxmolCallbackRecord", JSCLASS_HAS_RESERVED_SLOTS(2),
        JS_PropertyStub, JS_DeletePropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
        // SM33 allocates classes with a finalizer in the tenured heap. This
        // weak record therefore follows the major-GC weak sweep above.
        JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, [](JSFreeOp*,JSObject*) {}
    };
    JS::RootedObject record(cx,JS_NewObject(cx,&recordClass,JS::NullPtr(),JS::NullPtr()));
    if(!record) return {};
    auto roots=makeRoots(cx); if(!roots) return {};
    JS_SetReservedSlot(record,0,function);
    JS_SetReservedSlot(record,1,receiver ? JS::ObjectValue(*receiver) : JS::NullValue());
    roots->ownerBound=true; roots->ownerRecord=record;
    out.setObject(*record);
    return std::shared_ptr<Callback>(new Callback(std::move(roots)));
}
std::shared_ptr<Callback> Callback::create(JSContext* cx,JS::HandleValue function)
{
    auto state=runtimeState(cx);
    if(!state) return {};
    JS::RootedObject global(cx,state->engine->getGlobalObject());
    return create(cx,function,global);
}
Callback::~Callback() { cancel(); }
bool Callback::isValid() const
{
    auto rt=_roots->state.lock();
    return rt && rt->alive && rt->generation==_roots->generation && !_roots->cancelled.load(std::memory_order_acquire);
}
bool Callback::pinOwnerRecord(JSContext* cx,JS::MutableHandleObject out) const
{
    auto rt=_roots->state.lock();
    if(!isValid() || !rt || !rt->onOwnerThread() || rt->context!=cx) return false;
    out.set(_roots->ownerRecord);
    return !_roots->ownerBound || static_cast<bool>(out);
}
std::shared_ptr<RuntimeState> Callback::state() const { return _roots->state.lock(); }
tests::CallbackRootsWatchPtr Callback::watchRootsForTests() const
{
    auto rt=_roots->state.lock();
    if(!rt || !rt->alive || !rt->onOwnerThread() || rt->generation!=_roots->generation) return {};
    auto watch=std::make_shared<tests::CallbackRootsWatch>();
    watch->generation=_roots->generation;
    watch->functionRootCaptured=static_cast<bool>(_roots->function);
    watch->receiverRootCaptured=static_cast<bool>(_roots->receiver);
    watch->functionRootReleaseCount.store(_roots->functionRootReleaseCount,std::memory_order_relaxed);
    watch->receiverRootReleaseCount.store(_roots->receiverRootReleaseCount,std::memory_order_relaxed);
    watch->ownerBoundCaptured=_roots->ownerBound;
    watch->ownerBoundReleaseCount.store(_roots->ownerBoundReleaseCount,std::memory_order_relaxed);
    std::erase_if(_roots->watches,[](auto& weak) { return weak.expired(); });
    _roots->watches.emplace_back(watch);
    return watch;
}
void Callback::cancel()
{
    if(_roots->cancelled.exchange(true,std::memory_order_acq_rel)) return;
    if(auto rt=_roots->state.lock())
    {
        auto roots=_roots;
        rt->enqueue([roots] { roots->release(); });
    }
}
bool Callback::call(const JS::HandleValueArray& args,JS::MutableHandleValue result)
{
    auto rt=_roots->state.lock();
    if(!isValid() || !rt->onOwnerThread()) return false;
    JSContext* cx=rt->context;
    JSAutoCompartment compartment(cx,rt->engine->getGlobalObject());
    JS::RootedObject record(cx,_roots->ownerRecord);
    if(_roots->ownerBound && !record) return false;
    JS::RootedObject receiver(cx,record ? JS_GetReservedSlot(record,1).toObjectOrNull() : _roots->receiver->get());
    JS::RootedValue function(cx,record ? JS_GetReservedSlot(record,0) : _roots->function->get());
    const bool ok=JS_CallFunctionValue(cx,receiver,function,args,result);
    if(!ok) rt->engine->reportException();
    return ok;
}
bool Callback::call(const JS::HandleValueArray& args)
{
    auto rt=_roots->state.lock();
    if(!rt || !rt->onOwnerThread()) return false;
    JS::RootedValue result(rt->context); return call(args,&result);
}
} // namespace axjs
