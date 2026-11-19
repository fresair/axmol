// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#include "manual/ResourceBindings.h"
#include "manual/ManualUtils.h"
#include <unordered_map>
#include <algorithm>
#include "axmol/renderer/TextureCache.h"
#include "axmol/renderer/Texture2D.h"
#include "axmol/network/HttpClient.h"
#if AX_ENABLE_3D
#    include "axmol/3d/MeshRenderer.h"
#    include "axmol/3d/MeshDataCache.h"
#endif

namespace axjs
{
namespace
{
using namespace manual;
#if AX_ENABLE_3D
struct AsyncResources
{
    std::vector<std::shared_ptr<ax::CancellationState>> meshes;
    void cancel()
    {
        for (const auto& token : meshes) token->cancelRequested.store(true,std::memory_order_relaxed);
        meshes.clear();
    }
    void prune()
    {
        std::erase_if(meshes,[](const auto& token) { return token.use_count()==1; });
    }
};
std::unordered_map<JSContext*,std::shared_ptr<AsyncResources>> asyncResources;
bool stopAsyncTasks(JSContext* cx,unsigned argc,JS::Value* vp)
{
    auto args=JS::CallArgsFromVp(argc,vp); int type{};
    if(!argument(cx,args,0,type) || type<0 || type>2) return error(cx,"Invalid legacy asynchronous task category");
    // Only legacy Sprite3D submissions use this pool; networking has always had
    // its own backend. Categories without submitted jobs have nothing to cancel.
    if(type==0) asyncResources.at(cx)->cancel();
    args.rval().setUndefined(); return true;
}
bool loadMeshAsync(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string model, texture;
    if (!argument(cx, args, 0, model) || !argument(cx, args, 1, texture))
        return false;
    JS::RootedObject receiver(cx, engine(cx)->getGlobalObject());
    auto cb = callback(cx, args, 2, receiver);
    if (!cb)
        return false;
    auto tasks=asyncResources.at(cx);
    tasks->prune();
    auto cancellation=std::make_shared<ax::CancellationState>();
    tasks->meshes.push_back(cancellation);
    auto complete = [cb](ax::MeshRenderer* mesh, void*) { call(cb, mesh); };
    ax::MeshRenderer::createAsync(model, texture, complete, nullptr, cancellation);
    args.rval().setUndefined();
    return true;
}
bool clearMeshCache(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    ax::MeshDataCache::getInstance()->removeAllMeshRenderData();
    args.rval().setUndefined();
    return true;
}
#endif
bool addImageAsync(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args   = JS::CallArgsFromVp(argc, vp);
    auto* cache = self<ax::TextureCache>(cx, args);
    std::string path;
    if (!cache || !argument(cx, args, 0, path))
        return false;
    JS::RootedObject receiver(cx, &args.thisv().toObject());
    auto cb = callback(cx, args, 1, receiver);
    if (!cb)
        return false;
    cache->addImageAsync(path, [cb](ax::Texture2D* texture) { call(cb, texture); });
    args.rval().setUndefined();
    return true;
}
bool loadRemoteImage(JSContext* cx, unsigned argc, JS::Value* vp)
{
    auto args = JS::CallArgsFromVp(argc, vp);
    std::string url;
    if (!argument(cx, args, 0, url))
        return false;
    JS::RootedObject receiver(cx, engine(cx)->getGlobalObject());
    auto cb = callback(cx, args, 1, receiver);
    if (!cb)
        return false;
    auto* request = new ax::network::HttpRequest;
    request->setUrl(url);
    request->setRequestType(ax::network::HttpRequest::Type::GET);
    std::weak_ptr<RuntimeState> vm = cb->state();
    request->setCompleteCallback([cb, vm, url](ax::network::HttpClient*, ax::network::HttpResponse* response) {
        auto state = vm.lock();
        if (!state || !state->alive)
            return;
        ax::Data image;
        if (response && response->isSucceed() && response->getResponseCode() >= 200 &&
            response->getResponseCode() < 300)
        {
            const auto* bytes = response->getResponseData();
            if (bytes && !bytes->empty())
            {
                image.copy(reinterpret_cast<const unsigned char*>(bytes->data()), bytes->size());
            }
        }
        state->enqueue([cb, url, image = std::move(image)] {
            // Both graphics work and JS execution belong to the VM owner thread.
            // The callback token prevents an old request entering a restarted VM.
            if (!cb->isValid())
                return;
            auto* texture =
                image.isNull() ? nullptr : ax::Director::getInstance()->getTextureCache()->addImage(image, url);
            call(cb, texture != nullptr, texture);
        });
    });
    ax::network::HttpClient::getInstance()->send(request);
    request->release();
    args.rval().setUndefined();
    return true;
}
}  // namespace
bool registerResources(JSContext* cx, JS::HandleObject global)
{
    JS::RootedObject jsb(cx, namespaceObject(cx, global, "jsb"));
    JS::RootedObject texture(cx, getPrototype<ax::TextureCache>(cx));
#if AX_ENABLE_3D
    auto tasks=std::make_shared<AsyncResources>();
    asyncResources[cx]=tasks;
    engine(cx)->addUpdateCallback([tasks](float) { tasks->prune(); });
    engine(cx)->addCleanupCallback([tasks,cx] { tasks->cancel(); asyncResources.erase(cx); });
    if (!jsb || !function(cx, jsb, "_stopAsyncTasks", stopAsyncTasks, 1) ||
        !function(cx, jsb, "_loadMeshAsync", loadMeshAsync, 3) ||
        !function(cx, jsb, "_clearMeshDataCache", clearMeshCache, 0))
        return false;
#endif
    return jsb && texture && function(cx, jsb, "loadRemoteImg", loadRemoteImage, 2) &&
           function(cx, texture, "_addImageAsync", addImageAsync, 2) &&
           function(cx, texture, "addImageAsync", addImageAsync, 2);
}
}  // namespace axjs
