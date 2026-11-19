/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/SpineBindings.h"
#include "runtime/JsRuntime.h"
#if defined(AX_ENABLE_EXT_SPINE) && AX_ENABLE_EXT_SPINE
#include <spine/SkeletonAnimation.h>
#include <spine/SkeletonAssetCache.h>
#include <array>
#include <unordered_map>

namespace axjs
{
namespace
{
using Node=spine::SkeletonAnimation;
using Entry=spine::TrackEntry;
LifetimeToken skeletonLease(Node* node)
{
    return LifetimeToken(node,[weak=ax::WeakPtr<Node>(node),expected=node->getSkeleton(),
        asset=ax::WeakPtr<spine::SkeletonAsset>(node->getAsset()),hadAsset=node->getAsset()!=nullptr] {
        auto* owner=weak.get();
        return owner && owner->getSkeleton()==expected &&
            (hadAsset ? asset.get() && owner->getAsset()==asset.get() : owner->getAsset()==nullptr);
    });
}
struct Track
{
    LifetimeToken token;
    std::array<std::shared_ptr<Callback>,6> callbacks;
    explicit Track(Node* owner):token(owner,[weak=ax::WeakPtr<Node>(owner),expected=owner->getAnimationState(),
        asset=ax::WeakPtr<spine::SkeletonAsset>(owner->getAsset()),hadAsset=owner->getAsset()!=nullptr] {
            auto* node=weak.get();
            return node && node->getAnimationState()==expected &&
                (hadAsset ? asset.get() && node->getAsset()==asset.get() : node->getAsset()==nullptr);
        }) {}
};
struct Record
{
    ax::WeakPtr<Node> owner;
    spine::Skeleton* skeleton{};
    LifetimeToken skeletonToken;
    std::unordered_map<Entry*,std::shared_ptr<Track>> tracks;
    std::array<std::shared_ptr<Callback>,6> callbacks;
    explicit Record(Node* node):owner(node),skeleton(node->getSkeleton()),
        skeletonToken(skeletonLease(node)) {}
    void expireHandles()
    {
        skeletonToken.expire();
        for(auto& [entry,track]:tracks) track->token.expire();
        tracks.clear();
    }
};
struct Vm
{
    std::weak_ptr<RuntimeState> state;
    std::unordered_map<Node*,std::shared_ptr<Record>> records;
};
std::unordered_map<JSContext*,std::shared_ptr<Vm>> vms;

bool error(JSContext* cx,const char* message)
{ if(!JS_IsExceptionPending(cx)) JS_ReportError(cx,"%s",message); return false; }
template<class T> T* native(JSContext* cx,JS::CallArgs& args)
{ T* result{}; if(!toNative(cx,args.thisv(),&result) || !result) error(cx,"Invalid Spine native receiver"); return result; }
template<class T> bool property(JSContext* cx,JS::HandleObject object,const char* name,const T& value)
{
    JS::RootedValue js(cx);
    return toJsValue(cx,value,&js) && JS_DefineProperty(cx,object,name,js,JSPROP_ENUMERATE);
}
std::string_view string(const spine::String& s) { return {s.buffer(),static_cast<size_t>(s.length())}; }
std::shared_ptr<Track> trackFor(const std::shared_ptr<Record>& record,Entry* entry)
{
    auto& track=record->tracks[entry];
    if(!track || !track->token.isAlive()) track=std::make_shared<Track>(record->owner.get());
    return track;
}
bool wrapTrack(JSContext* cx,const std::shared_ptr<Record>& record,Entry* entry,JS::MutableHandleValue out)
{
    if(!entry) { out.setNull(); return true; }
    return wrapBorrowed(cx,entry,out,trackFor(record,entry)->token);
}

bool eventValue(JSContext* cx,spine::Event* event,JS::MutableHandleValue out)
{
    if(!event) { out.setNull(); return true; }
    // Old JSB event records are detached snapshots. They contain no pointer
    // into Spine's transient Event instance and are safe to store in JS.
    JS::RootedObject result(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
    JS::RootedObject data(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
    if(!result || !data || !property(cx,data,"name",string(event->getData().getName())) ||
        !property(cx,result,"name",string(event->getData().getName())) ||
        !property(cx,result,"intValue",event->getInt()) || !property(cx,result,"floatValue",event->getFloat()) ||
        !property(cx,result,"stringValue",string(event->getString())) || !property(cx,result,"time",event->getTime())) return false;
    JS::RootedValue dataValue(cx,JS::ObjectValue(*data));
    if(!JS_DefineProperty(cx,result,"data",dataValue,JSPROP_ENUMERATE)) return false;
    out.setObject(*result); return true;
}
void notify(const std::shared_ptr<Record>& record,int type,Entry* entry,spine::Event* event,bool trackSpecific)
{
    auto* owner=record->owner.get();
    if(!owner || type<0 || type>=6) return;
    auto* active=ax::ScriptEngineManager::getScriptEngineIfExists();
    if(!active || active->getScriptType()!=ax::kScriptTypeJavascript) return;
    auto* js=static_cast<ax::JsEngine*>(active);
    if(!js->isRunning() || !js->isOwnerThread()) return;
    auto track=trackFor(record,entry);
    auto callback=trackSpecific ? track->callbacks[type] : record->callbacks[type];
    if(callback && callback->isValid())
    {
        JSContext* cx=js->getContext();
        JSAutoCompartment compartment(cx,js->getGlobalObject());
        JS::AutoValueVector args(cx);
        JS::RootedValue value(cx);
        bool ok=wrapTrack(cx,record,entry,&value) && args.append(value);
        if(type==spine::EventType_Event)
        { ok=ok && eventValue(cx,event,&value) && args.append(value); }
        else if(type==spine::EventType_Complete)
        {
            const float duration=entry->getAnimationEnd()-entry->getAnimationStart();
            const int loops=duration>0 ? static_cast<int>(std::floor(entry->getTrackTime()/duration)) : 1;
            ok=ok && toJsValue(cx,loops,&value) && args.append(value);
        }
        if(ok) callback->call(JS::HandleValueArray(args));
    }
    // The track-specific dispose listener executes before the state listener.
    // Invalidate only after both have observed the live entry.
    if(type==spine::EventType_Dispose && !trackSpecific)
    { track->token.expire(); record->tracks.erase(entry); }
}
void installObservers(const std::shared_ptr<Record>& record)
{
    auto* node=record->owner.get();
    std::weak_ptr<Record> weak=record;
    auto listener=[weak](int type,Entry* entry,spine::Event* event=nullptr) {
        if(auto record=weak.lock()) notify(record,type,entry,event,false);
    };
    node->setStartListener([listener](Entry* e){listener(spine::EventType_Start,e);});
    node->setInterruptListener([listener](Entry* e){listener(spine::EventType_Interrupt,e);});
    node->setEndListener([listener](Entry* e){listener(spine::EventType_End,e);});
    node->setDisposeListener([listener](Entry* e){listener(spine::EventType_Dispose,e);});
    node->setCompleteListener([listener](Entry* e){listener(spine::EventType_Complete,e);});
    node->setEventListener([listener](Entry* e,spine::Event* event){listener(spine::EventType_Event,e,event);});
}
std::shared_ptr<Record> recordFor(JSContext* cx,Node* node)
{
    auto vm=vms.at(cx);
    auto& record=vm->records[node];
    if(!record || record->owner.get()!=node)
    { record=std::make_shared<Record>(node); installObservers(record); }
    else if(record->skeleton!=node->getSkeleton() || !record->skeletonToken.isAlive())
    {
        record->expireHandles(); record->skeleton=node->getSkeleton();
        record->skeletonToken=skeletonLease(node);
    }
    return record;
}
void removeObservers(const std::shared_ptr<Record>& record)
{
    if(auto* node=record->owner.get())
    {
        node->setStartListener({}); node->setInterruptListener({}); node->setEndListener({});
        node->setDisposeListener({}); node->setCompleteListener({}); node->setEventListener({});
        for(auto& [entry,track]:record->tracks)
            if(track->token.isAlive())
            {
                node->setTrackStartListener(entry,{}); node->setTrackInterruptListener(entry,{});
                node->setTrackEndListener(entry,{}); node->setTrackDisposeListener(entry,{});
                node->setTrackCompleteListener(entry,{}); node->setTrackEventListener(entry,{});
            }
    }
    record->expireHandles(); record->callbacks={};
}

bool create(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    Node* node{};
    if(argc==0) node=Node::create();
    else if(argc==1 && args[0].isObject())
    {
        spine::SkeletonAsset* asset{};
        if(!toNative(cx,args[0],&asset) || !asset) return error(cx,"Invalid SkeletonAsset");
        node=Node::create(asset);
    }
    else
    {
        if(argc<2 || argc>3) return error(cx,"Expected skeleton file, atlas file and optional scale");
        Argument<std::string_view> skeleton,atlas; float scale=1.f;
        if(!skeleton.read(cx,args[0]) || !atlas.read(cx,args[1]) ||
            (argc==3 && !fromJsValue(cx,args[2],scale)) || scale<=0) return error(cx,"Invalid Spine asset arguments");
        node=Node::create(skeleton.get(),atlas.get(),scale);
    }
    if(node) recordFor(cx,node);
    return wrapNative(cx,node,args.rval(),NativeOwnership::Factory);
}

template<class T> void copyPoses(T& source,T& target)
{
    target.getPose().set(source.getPose());
    if(!source.isPoseEqualToApplied()) target.constrained();
    target.getAppliedPose().set(source.getAppliedPose());
    if constexpr(requires { target.setActive(source.isActive()); })
        target.setActive(source.isActive());
}
void copyBoneWorld(spine::BonePose& source,spine::BonePose& target)
{
    // Copy through the public pose API so its owning Bone remains the clone's
    // bone. Assigning BonePose would also copy its private _bone pointer.
    target.setA(source.getA()); target.setB(source.getB());
    target.setC(source.getC()); target.setD(source.getD());
    target.setWorldX(source.getWorldX()); target.setWorldY(source.getWorldY());
}
template<class T> bool copyConstraint(spine::Constraint& source,spine::Constraint& target)
{
    if(!source.getRTTI().isExactly(T::rtti)) return false;
    copyPoses(static_cast<T&>(source),static_cast<T&>(target));
    return true;
}
class OwnedSkeletonClone final : public Node
{
public:
    OwnedSkeletonClone(spine::SkeletonAnimation* source,int firstSlot,int lastSlot)
        :spine::SkeletonAnimation(source->getAsset())
    {
        // Native clone() is a rendering view of the original Skeleton. A JS
        // clone owns a Skeleton and retains its asset, so native destruction
        // or reinitialization of the source cannot leave dangling pose data.
        setAnimationStateEnabled(false);
        auto& original=*source->getSkeleton();
        auto& target=*_skeleton;
        if(auto* skin=original.getSkin())
        {
            _combinedSkin=new spine::Skin(skin->getName());
            _combinedSkin->addSkin(*skin);
            target.setSkin(_combinedSkin);
        }
        target.setPosition(original.getX(),original.getY());
        target.setScale(original.getScaleX(),original.getScaleY());
        target.setColor(original.getColor());
        target.setTime(original.getTime());
        target.setWindX(original.getWindX()); target.setWindY(original.getWindY());
        target.setGravityX(original.getGravityX()); target.setGravityY(original.getGravityY());
        auto& bones=original.getBones();
        for(size_t i=0;i<bones.size();++i)
        {
            auto& originalBone=*bones[i];
            auto& targetBone=*target.getBones()[i];
            copyPoses(originalBone,targetBone);
            copyBoneWorld(originalBone.getPose(),targetBone.getPose());
            copyBoneWorld(originalBone.getAppliedPose(),targetBone.getAppliedPose());
        }
        auto& slots=original.getSlots();
        for(size_t i=0;i<slots.size();++i) copyPoses(*slots[i],*target.getSlots()[i]);
        auto& constraints=original.getConstraints();
        for(size_t i=0;i<constraints.size();++i)
        {
            auto& originalConstraint=*constraints[i];
            auto& targetConstraint=*target.getConstraints()[i];
            if(copyConstraint<spine::IkConstraint>(originalConstraint,targetConstraint) ||
                copyConstraint<spine::PathConstraint>(originalConstraint,targetConstraint) ||
                copyConstraint<spine::TransformConstraint>(originalConstraint,targetConstraint) ||
                copyConstraint<spine::PhysicsConstraint>(originalConstraint,targetConstraint)) continue;
            copyConstraint<spine::Slider>(originalConstraint,targetConstraint);
        }
        auto copyOrder=[&](spine::Array<spine::Slot*>& order,spine::Array<spine::Slot*>& output) {
            output.clear();
            for(size_t i=0;i<order.size();++i) output.add(target.getSlots()[order[i]->getData().getIndex()]);
        };
        copyOrder(original.getDrawOrder().getPose(),target.getDrawOrder().getPose());
        copyOrder(original.getDrawOrder().getAppliedPose(),target.getDrawOrder().getAppliedPose());
        setSlotsRange(firstSlot,lastSlot);
    }
};
bool clone(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    auto* source=native<Node>(cx,args); if(!source) return false;
    int first=-1,last=-1;
    if(argc>2 || (argc>0 && !fromJsValue(cx,args[0],first)) ||
        (argc>1 && !fromJsValue(cx,args[1],last))) return error(cx,"clone expects optional start and end slot indices");
    if(!source->getAsset() || !source->getSkeleton()) return error(cx,"Cannot clone a SkeletonAnimation without an owned asset");
    auto* node=new OwnedSkeletonClone(source,first,last);
    node->autorelease();
    recordFor(cx,node);
    return wrapNative<Node>(cx,node,args.rval(),NativeOwnership::Factory);
}

template<int Kind>
bool animation(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    auto* node=native<Node>(cx,args); if(!node) return false;
    if(!node->getSkeleton()) return error(cx,"SkeletonAnimation has no asset");
    auto record=recordFor(cx,node);
    int index=0; Entry* entry{};
    if constexpr(Kind==0 || Kind==1)
    {
        if(argc<3 || argc>(Kind==0?3:4)) return error(cx,"Expected track index, animation name, loop and optional delay");
        Argument<std::string_view> name; bool loop{}; float delay{};
        if(!fromJsValue(cx,args[0],index) || index<0 || !name.read(cx,args[1]) ||
            !fromJsValue(cx,args[2],loop) || (argc==4 && !fromJsValue(cx,args[3],delay))) return error(cx,"Invalid animation arguments");
        if constexpr(Kind==0) entry=node->setAnimation(index,name.get(),loop);
        else entry=node->addAnimation(index,name.get(),loop,delay);
    }
    else if constexpr(Kind==2)
    {
        if(argc>1 || (argc==1 && !fromJsValue(cx,args[0],index)) || index<0) return error(cx,"Invalid track index");
        entry=node->getTrack(index);
    }
    else
    {
        float mix{},delay{};
        if(argc<2 || argc>(Kind==3?2:3) || !fromJsValue(cx,args[0],index) || index<0 ||
            !fromJsValue(cx,args[1],mix) || mix<0 || (argc==3 && !fromJsValue(cx,args[2],delay))) return error(cx,"Invalid empty animation arguments");
        if constexpr(Kind==3) entry=node->setEmptyAnimation(index,mix);
        else entry=node->addEmptyAnimation(index,mix,delay);
    }
    return wrapTrack(cx,record,entry,args.rval());
}

template<int Kind>
bool skeletonQuery(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    auto* node=native<Node>(cx,args); if(!node) return false;
    auto record=recordFor(cx,node);
    if constexpr(Kind==0)
    { if(argc) return error(cx,"getSkeleton takes no arguments"); return wrapBorrowed(cx,node->getSkeleton(),args.rval(),record->skeletonToken); }
    else
    {
        Argument<std::string_view> name;
        if(argc!=1 || !name.read(cx,args[0])) return error(cx,"Expected a Spine bone or slot name");
        if(!node->getSkeleton()) { args.rval().setNull(); return true; }
        if constexpr(Kind==1) return wrapBorrowed(cx,node->findBone(name.get()),args.rval(),record->skeletonToken);
        else return wrapBorrowed(cx,node->findSlot(name.get()),args.rval(),record->skeletonToken);
    }
}

template<int Kind>
bool setupPose(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    auto* node=native<Node>(cx,args); if(!node) return false;
    if(argc || !node->getSkeleton()) return error(cx,"Invalid setup pose call");
    if constexpr(Kind==0) node->getSkeleton()->setupPose();
    else if constexpr(Kind==1) node->getSkeleton()->setupPoseBones();
    else node->getSkeleton()->setupPoseSlots();
    args.rval().setUndefined(); return true;
}

template<int Kind>
bool resetAsset(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    auto* node=native<Node>(cx,args); if(!node) return false;
    spine::SkeletonAsset* asset{};
    if constexpr(Kind==0)
    {
        if(argc!=1 || !toNative(cx,args[0],&asset) || !asset) return error(cx,"Expected a SkeletonAsset");
    }
    else
    {
        Argument<std::string_view> skeleton,atlas; float scale=1.f;
        if(argc<2 || argc>3 || !skeleton.read(cx,args[0]) || !atlas.read(cx,args[1]) ||
            (argc==3 && !fromJsValue(cx,args[2],scale)) || scale<=0) return error(cx,"Invalid Spine asset paths");
        asset=spine::SkeletonAssetCache::getInstance()->loadAsset(skeleton.get(),atlas.get(),scale);
        if(!asset) return error(cx,"Cannot load Spine 4.3 asset");
    }
    auto record=recordFor(cx,node);
    if(asset!=node->getAsset()) { record->expireHandles(); node->setAsset(asset); recordFor(cx,node); }
    args.rval().setUndefined(); return true;
}

bool legacyConstructor(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    auto* node=native<Node>(cx,args); if(!node) return false;
    if(!node->init()) return error(cx,"Spine node initialization failed");
    if(argc==0)
    {
        recordFor(cx,node);
        args.rval().setBoolean(true);
        return true;
    }
    // This native ctor is used by both the bootstrap facade and .extend()
    // _super calls. An inherited Node.init cannot accept the original Spine
    // skeleton/atlas/scale arguments or initialize the animation state.
    if(argc==1 && args[0].isObject())
    {
        if(!resetAsset<0>(cx,argc,vp)) return false;
    }
    else if(!resetAsset<1>(cx,argc,vp)) return false;
    args.rval().setBoolean(true);
    return true;
}

template<int Kind,bool TrackSpecific=false>
bool setListener(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp);
    auto* node=native<Node>(cx,args); if(!node) return false;
    const unsigned min=TrackSpecific?2:1;
    if(argc<min || argc>min+1) return error(cx,"Invalid Spine listener arguments");
    auto record=recordFor(cx,node);
    Entry* entry{};
    if constexpr(TrackSpecific)
    {
        if(!toNative(cx,args[0],&entry) || !entry || !record->tracks.contains(entry)) return error(cx,"Track entry does not belong to this skeleton");
    }
    const auto functionIndex=TrackSpecific?1:0;
    JS::RootedObject receiver(cx,&args.thisv().toObject());
    if(argc>min)
    {
        if(!args[min].isObject()) return error(cx,"Spine listener receiver must be an object");
        receiver=&args[min].toObject();
    }
    std::shared_ptr<Callback> callback;
    if(!args[functionIndex].isNullOrUndefined())
    {
        callback=Callback::create(cx,args[functionIndex],receiver);
        if(!callback) return error(cx,"Spine listener must be a function or null");
    }
    if constexpr(!TrackSpecific) record->callbacks[Kind]=std::move(callback);
    else
    {
        auto track=trackFor(record,entry);
        track->callbacks[Kind]=std::move(callback);
        std::weak_ptr<Record> weak=record;
        auto listener=[weak](Entry* e,spine::Event* event=nullptr) {
            if(auto record=weak.lock()) notify(record,Kind,e,event,true);
        };
        if constexpr(Kind==spine::EventType_Start) node->setTrackStartListener(entry,[listener](Entry* e){listener(e);});
        else if constexpr(Kind==spine::EventType_Interrupt) node->setTrackInterruptListener(entry,[listener](Entry* e){listener(e);});
        else if constexpr(Kind==spine::EventType_End) node->setTrackEndListener(entry,[listener](Entry* e){listener(e);});
        else if constexpr(Kind==spine::EventType_Dispose) node->setTrackDisposeListener(entry,[listener](Entry* e){listener(e);});
        else if constexpr(Kind==spine::EventType_Complete) node->setTrackCompleteListener(entry,[listener](Entry* e){listener(e);});
        else if constexpr(Kind==spine::EventType_Event) node->setTrackEventListener(entry,listener);
    }
    args.rval().setUndefined(); return true;
}

// Track, skeleton, bone and slot objects expose live fields, while every
// dereference passes the owner's generation/lifetime check.
template<class T,auto Getter>
bool get(JSContext* cx,unsigned argc,JS::Value* vp)
{ auto args=JS::CallArgsFromVp(argc,vp); return invoke<Getter>(cx,args); }
template<class T,auto Setter>
bool set(JSContext* cx,unsigned argc,JS::Value* vp)
{ auto args=JS::CallArgsFromVp(argc,vp); return invoke<Setter>(cx,args); }
bool animationInfo(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp); auto* entry=native<Entry>(cx,args); if(!entry) return false;
    auto& animation=entry->getAnimation();
    JS::RootedObject result(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
    if(!result || !property(cx,result,"name",string(animation.getName())) || !property(cx,result,"duration",animation.getDuration())) return false;
    args.rval().setObject(*result); return true;
}
template<auto Getter> bool linkedTrack(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp); auto* entry=native<Entry>(cx,args); if(!entry) return false;
    Entry* linked=std::invoke(Getter,entry);
    if(!linked) { args.rval().setNull(); return true; }
    // A linked entry must already have its own disposal token. Reusing the
    // source token would leave it valid when only the linked track is disposed.
    for(auto& [node,record]:vms.at(cx)->records)
        if(record->tracks.contains(entry)) return wrapTrack(cx,record,linked,args.rval());
    return error(cx,"Unowned Spine track");
}

template<auto Getter,auto Setter=nullptr>
bool boneFieldGet(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp); auto* bone=native<spine::Bone>(cx,args); if(!bone) return false;
    return toJsValue(cx,std::invoke(Getter,&bone->getPose()),args.rval());
}
template<auto Getter,auto Setter>
bool boneFieldSet(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp); auto* bone=native<spine::Bone>(cx,args); if(!bone) return false;
    float number{};
    if(argc!=1 || !fromJsValue(cx,args[0],number)) return error(cx,"Expected a bone numeric field");
    std::invoke(Setter,&bone->getPose(),number); args.rval().setUndefined(); return true;
}
template<auto Getter>
bool boneWorldGet(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp); auto* bone=native<spine::Bone>(cx,args); if(!bone) return false;
    return toJsValue(cx,std::invoke(Getter,&bone->getAppliedPose()),args.rval());
}
bool boneData(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp); auto* bone=native<spine::Bone>(cx,args); if(!bone) return false;
    auto& data=bone->getData();
    JS::RootedObject result(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
    if(!result || !property(cx,result,"index",data.getIndex()) || !property(cx,result,"name",string(data.getName())) ||
        !property(cx,result,"length",data.getLength())) return false;
    args.rval().setObject(*result); return true;
}
bool boneParent(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp); auto* bone=native<spine::Bone>(cx,args); if(!bone) return false;
    LifetimeToken token; if(!detail::getNativeLifetime(cx,args.thisv(),token)) return false;
    return wrapBorrowed(cx,bone->getParent(),args.rval(),token);
}
bool slotBone(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp); auto* slot=native<spine::Slot>(cx,args); if(!slot) return false;
    LifetimeToken token; if(!detail::getNativeLifetime(cx,args.thisv(),token)) return false;
    return wrapBorrowed(cx,&slot->getBone(),args.rval(),token);
}
bool slotData(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp); auto* slot=native<spine::Slot>(cx,args); if(!slot) return false;
    auto& data=slot->getData();
    JS::RootedObject result(cx,JS_NewObject(cx,nullptr,JS::NullPtr(),JS::NullPtr()));
    if(!result || !property(cx,result,"index",data.getIndex()) || !property(cx,result,"name",string(data.getName()))) return false;
    args.rval().setObject(*result); return true;
}
bool slotColor(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp); auto* slot=native<spine::Slot>(cx,args); if(!slot) return false;
    auto& color=slot->getPose().getColor();
    return toJsValue(cx,ax::Color{color.r,color.g,color.b,color.a},args.rval());
}
template<int Kind>
bool skeletonArray(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp); auto* skeleton=native<spine::Skeleton>(cx,args); if(!skeleton) return false;
    LifetimeToken token; if(!detail::getNativeLifetime(cx,args.thisv(),token)) return false;
    JS::RootedObject array(cx,JS_NewArrayObject(cx,0)); if(!array) return false;
    auto add=[&](auto& entries) {
        for(size_t i=0;i<entries.size();++i)
        {
            JS::RootedValue value(cx);
            if(!wrapBorrowed(cx,entries[i],&value,token) || !JS_SetElement(cx,array,static_cast<uint32_t>(i),value)) return false;
        }
        return true;
    };
    bool ok;
    if constexpr(Kind==0) ok=add(skeleton->getBones());
    else ok=add(skeleton->getSlots());
    if(!ok) return false;
    args.rval().setObject(*array); return true;
}
template<int Kind>
bool skeletonFind(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp); auto* skeleton=native<spine::Skeleton>(cx,args); if(!skeleton) return false;
    Argument<std::string_view> name; LifetimeToken token;
    if(argc!=1 || !name.read(cx,args[0]) || !detail::getNativeLifetime(cx,args.thisv(),token)) return error(cx,"Expected bone or slot name");
    if constexpr(Kind==0) return wrapBorrowed(cx,skeleton->findBone(spine::String{name.get()}),args.rval(),token);
    else return wrapBorrowed(cx,skeleton->findSlot(spine::String{name.get()}),args.rval(),token);
}
#define TRACK_PROPERTY(Name,Get,Set) JS_PSGS(Name,(get<Entry,&Entry::Get>),(set<Entry,&Entry::Set>),JSPROP_ENUMERATE)
const JSPropertySpec trackProperties[]={
    JS_PSG("trackIndex",(get<Entry,&Entry::getTrackIndex>),JSPROP_ENUMERATE),
    JS_PSG("animation",animationInfo,JSPROP_ENUMERATE),
    JS_PSG("next",(linkedTrack<&Entry::getNext>),JSPROP_ENUMERATE),
    JS_PSG("mixingFrom",(linkedTrack<&Entry::getMixingFrom>),JSPROP_ENUMERATE),
    TRACK_PROPERTY("loop",getLoop,setLoop), TRACK_PROPERTY("delay",getDelay,setDelay),
    TRACK_PROPERTY("trackTime",getTrackTime,setTrackTime), TRACK_PROPERTY("trackEnd",getTrackEnd,setTrackEnd),
    TRACK_PROPERTY("animationStart",getAnimationStart,setAnimationStart), TRACK_PROPERTY("animationEnd",getAnimationEnd,setAnimationEnd),
    TRACK_PROPERTY("animationLast",getAnimationLast,setAnimationLast), TRACK_PROPERTY("timeScale",getTimeScale,setTimeScale),
    TRACK_PROPERTY("alpha",getAlpha,setAlpha), TRACK_PROPERTY("mixTime",getMixTime,setMixTime),
    JS_PSGS("mixDuration",(get<Entry,&Entry::getMixDuration>),(set<Entry,static_cast<void(Entry::*)(float)>(&Entry::setMixDuration)>),JSPROP_ENUMERATE),
    JS_PSG("animationTime",(get<Entry,&Entry::getAnimationTime>),JSPROP_ENUMERATE),JS_PS_END};
#undef TRACK_PROPERTY
#define BONE_PROPERTY(Name,Get,Set) JS_PSGS(Name,(boneFieldGet<&spine::BoneLocal::Get>),(boneFieldSet<&spine::BoneLocal::Get,&spine::BoneLocal::Set>),JSPROP_ENUMERATE)
const JSPropertySpec boneProperties[]={
    BONE_PROPERTY("x",getX,setX), BONE_PROPERTY("y",getY,setY), BONE_PROPERTY("rotation",getRotation,setRotation),
    BONE_PROPERTY("scaleX",getScaleX,setScaleX), BONE_PROPERTY("scaleY",getScaleY,setScaleY),
    BONE_PROPERTY("shearX",getShearX,setShearX), BONE_PROPERTY("shearY",getShearY,setShearY),
    JS_PSG("worldX",(boneWorldGet<&spine::BonePose::getWorldX>),JSPROP_ENUMERATE),
    JS_PSG("worldY",(boneWorldGet<&spine::BonePose::getWorldY>),JSPROP_ENUMERATE),
    JS_PSG("a",(boneWorldGet<&spine::BonePose::getA>),JSPROP_ENUMERATE),
    JS_PSG("b",(boneWorldGet<&spine::BonePose::getB>),JSPROP_ENUMERATE),
    JS_PSG("c",(boneWorldGet<&spine::BonePose::getC>),JSPROP_ENUMERATE),
    JS_PSG("d",(boneWorldGet<&spine::BonePose::getD>),JSPROP_ENUMERATE),
    JS_PSG("data",boneData,JSPROP_ENUMERATE),JS_PSG("parent",boneParent,JSPROP_ENUMERATE),JS_PS_END};
#undef BONE_PROPERTY
const JSPropertySpec slotProperties[]={JS_PSG("data",slotData,JSPROP_ENUMERATE),JS_PSG("bone",slotBone,JSPROP_ENUMERATE),
    JS_PSG("color",slotColor,JSPROP_ENUMERATE),JS_PS_END};
const JSPropertySpec skeletonProperties[]={
    JS_PSGS("x",(get<spine::Skeleton,&spine::Skeleton::getX>),(set<spine::Skeleton,&spine::Skeleton::setX>),JSPROP_ENUMERATE),
    JS_PSGS("y",(get<spine::Skeleton,&spine::Skeleton::getY>),(set<spine::Skeleton,&spine::Skeleton::setY>),JSPROP_ENUMERATE),
    JS_PSGS("scaleX",(get<spine::Skeleton,&spine::Skeleton::getScaleX>),(set<spine::Skeleton,&spine::Skeleton::setScaleX>),JSPROP_ENUMERATE),
    JS_PSGS("scaleY",(get<spine::Skeleton,&spine::Skeleton::getScaleY>),(set<spine::Skeleton,&spine::Skeleton::setScaleY>),JSPROP_ENUMERATE),
    JS_PSG("bones",(skeletonArray<0>),JSPROP_ENUMERATE),JS_PSG("slots",(skeletonArray<1>),JSPROP_ENUMERATE),JS_PS_END};
const JSFunctionSpec skeletonMethods[]={
    JS_FN("findBone",skeletonFind<0>,1,0),JS_FN("findSlot",skeletonFind<1>,1,0),JS_FS_END};
#define SP_CLASS(Name) JSClass Name={#Name,JSCLASS_HAS_PRIVATE,JS_PropertyStub,JS_DeletePropertyStub,JS_PropertyStub,JS_StrictPropertyStub,JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub,finalizeNative,nullptr,nullptr,nullptr,nullptr}
SP_CLASS(trackClass); SP_CLASS(boneClass); SP_CLASS(slotClass); SP_CLASS(skeletonClass);
#undef SP_CLASS
template<class T> bool registerBorrowed(JSContext* cx,JS::HandleObject module,const char* name,JSClass& klass,
                                      const JSPropertySpec* properties,const JSFunctionSpec* methods=nullptr)
{
    klass.name=name;
    JS::RootedObject prototype(cx,JS_InitClass(cx,module,JS::NullPtr(),&klass,nullptr,0,properties,methods,nullptr,nullptr));
    return prototype && registerType<T>(cx,prototype,&klass);
}
const JSFunctionSpec nodeMethods[]={
    JS_FN("ctor",legacyConstructor,0,0),
    JS_FN("clone",clone,0,0),
    JS_FN("setAnimation",animation<0>,3,0),JS_FN("addAnimation",animation<1>,3,0),
    JS_FN("getCurrent",animation<2>,0,0),JS_FN("getTrack",animation<2>,0,0),
    JS_FN("setEmptyAnimation",animation<3>,2,0),JS_FN("addEmptyAnimation",animation<4>,2,0),
    JS_FN("getSkeleton",skeletonQuery<0>,0,0),JS_FN("findBone",skeletonQuery<1>,1,0),JS_FN("findSlot",skeletonQuery<2>,1,0),
    JS_FN("setToSetupPose",setupPose<0>,0,0),JS_FN("setBonesToSetupPose",setupPose<1>,0,0),JS_FN("setSlotsToSetupPose",setupPose<2>,0,0),
    JS_FN("setAsset",resetAsset<0>,1,0),JS_FN("initWithAsset",resetAsset<0>,1,0),
    JS_FN("initWithJsonFile",resetAsset<1>,2,0),JS_FN("initWithBinaryFile",resetAsset<1>,2,0),
    JS_FN("setStartListener",(setListener<0>),1,0),JS_FN("setInterruptListener",(setListener<1>),1,0),
    JS_FN("setEndListener",(setListener<2>),1,0),JS_FN("setDisposeListener",(setListener<3>),1,0),
    JS_FN("setCompleteListener",(setListener<4>),1,0),JS_FN("setEventListener",(setListener<5>),1,0),
    JS_FN("setTrackStartListener",(setListener<0,true>),2,0),JS_FN("setTrackInterruptListener",(setListener<1,true>),2,0),
    JS_FN("setTrackEndListener",(setListener<2,true>),2,0),JS_FN("setTrackDisposeListener",(setListener<3,true>),2,0),
    JS_FN("setTrackCompleteListener",(setListener<4,true>),2,0),JS_FN("setTrackEventListener",(setListener<5,true>),2,0),JS_FS_END};
} // namespace
bool registerSpine(JSContext* cx,JS::HandleObject global)
{
    JS::RootedObject module(cx);
    if(!getOrCreateNamespace(cx,global,"sp",&module)) return false;
    if(!registerBorrowed<Entry>(cx,module,"TrackEntry",trackClass,trackProperties) ||
        !registerBorrowed<spine::Bone>(cx,module,"Bone",boneClass,boneProperties) ||
        !registerBorrowed<spine::Slot>(cx,module,"Slot",slotClass,slotProperties) ||
        !registerBorrowed<spine::Skeleton>(cx,module,"Skeleton",skeletonClass,skeletonProperties,skeletonMethods)) return false;
    JS::RootedObject prototype(cx,getPrototype<Node>(cx));
    JS::RootedObject constructor(cx,getClassObject<Node>(cx));
    if(!prototype || !constructor) return error(cx,"Generated Spine SkeletonAnimation type was not registered");
    if(!JS_DefineFunctions(cx,prototype,nodeMethods) ||
        !JS_DefineProperty(cx,prototype,"__legacyCtor",JS::TrueHandleValue,0) ||
        !JS_DefineFunction(cx,constructor,"createWithJsonFile",create,2,0) ||
        !JS_DefineFunction(cx,constructor,"createWithBinaryFile",create,2,0) ||
        !JS_DefineFunction(cx,constructor,"create",create,2,0)) return false;
    auto vm=std::make_shared<Vm>(); vm->state=runtimeState(cx); vms[cx]=vm;
    auto* js=engine(cx);
    js->addCleanupCallback([cx,vm] {
        for(auto& [node,record]:vm->records) removeObservers(record);
        vm->records.clear(); vms.erase(cx);
    });
    js->addUpdateCallback([vm](float) {
        std::erase_if(vm->records,[](auto& item) {
            if(item.second->owner.expired()) { item.second->expireHandles(); return true; }
            return false;
        });
    });
    return true;
}
} // namespace axjs
#endif
