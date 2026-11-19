/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
#include "manual/Bindings.h"
#include "manual/FileBindings.h"
#include "manual/SocketIOBindings.h"
#include "manual/ResourceBindings.h"
#include "manual/LegacyRenderBindings.h"
#include "manual/LegacyExtensionBindings.h"
#include "manual/LegacyActionBindings.h"
#include "manual/LegacyUIBindings.h"
#include "manual/LegacyPhysicsBindings.h"
#include "manual/PropertiesBindings.h"
#include "manual/LegacyRenderTextureBindings.h"
#include "manual/LegacyGLBindings.h"
#include "manual/Legacy3DBindings.h"
#include "manual/LegacyMaterialBindings.h"
#include "manual/LegacyComponentBindings.h"
#include "manual/LegacyTMXBindings.h"
#include "manual/LegacySchedulerBindings.h"
#include "manual/LegacyAnimationBindings.h"
#include "manual/LegacyLabelBindings.h"
#include "manual/LegacyBMFontBindings.h"
#include "manual/LegacyEffect3DBindings.h"
#include "manual/LegacyPolygonBindings.h"
#include "manual/LegacyAudioBindings.h"
#include "generated/axjs_register_gen.h"
namespace axjs
{
bool registerAll(JSContext* cx, JS::HandleObject global)
{
    return registerAllGenerated(cx, global) && registerCore(cx, global) && registerLegacyScheduler(cx, global) &&
           registerLegacyAnimation(cx, global) && registerLegacyLabel(cx, global) && registerLegacyBMFont(cx, global) &&
           registerLegacyPolygon(cx, global) && registerLegacyAudio(cx, global) &&
           registerFiles(cx, global) &&
           registerInput(cx, global) && registerLegacyComponent(cx, global) && registerLegacyTMX(cx, global) &&
           registerLegacyActions(cx, global) && registerLegacyPhysics(cx, global) &&
           registerProperties(cx, global) && registerNetwork(cx, global) && registerSocketIO(cx, global) &&
           registerResources(cx, global) && registerLegacyRendering(cx, global) &&
           registerLegacyRenderTexture(cx, global) && registerLegacyGL(cx, global) && registerLegacy3D(cx, global) &&
           registerLegacyMaterial(cx, global) && registerLegacyEffect3D(cx, global) && registerLegacyExtensions(cx, global) &&
           registerLegacyUI(cx, global) && registerExtensions(cx, global) && registerPlatform(cx, global);
}
}  // namespace axjs
