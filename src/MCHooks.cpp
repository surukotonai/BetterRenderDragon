
#include "Core/Math/Vec4.h"
#include "mc/client/RenderDragon/Materials/MaterialResourceManager.h"
#include "mc/client/Renderdragon/Materials/MaterialUniformName.h"
#include "mc/client/Renderdragon/Materials/ShaderCodePlatform.h"
#include "mc/client/Renderdragon/Rendering/LightingModels.h"
#include "mc/client/bgfx/bgfx.h"
#include "mc/client/dragon/framerenderer/DeferredShadingParameters.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include <codecvt>
#include <cstdio>

#if defined(_WIN32)
#include "gui/Options.h"
#endif
#include "MinHook.h"
#include "api/Logger.h"
#include "api/memory/HookAPI.hpp"
#include <iostream>

void *resourcePackManager;

bgfx::RayTracingFeatureConfiguration *gRayTracingFeatureConfiguration = nullptr;

dragon::framerenderer::DeferredShadingParameters *gDeferredParams = nullptr;

bool shouldForceEnableVibrantVisuals() {
  return brd::Options::forceEnableVibrantVisuals;
}

using dragon::rendering::LightingModels;

typedef bool (*PFN_ResourcePackManager_load)(void *This,
                                             const ResourceLocation &location,
                                             std::string &resourceStream);
PFN_ResourcePackManager_load ResourcePackManager_load;

DeclareHook(readFile, std::string *, void *This, std::string *retstr,
            Core::Path &path) {
  std::string *result = original(This, retstr, path);
  if (brd::Options::materialBinLoaderEnabled && brd::Options::redirectShaders &&
      resourcePackManager) {
    const std::string &p = path.mPathPart.getUtf8CString();
    if (p.find("data/renderer/materials/") != std::string::npos &&
        strncmp(p.c_str() + p.size() - 13, ".material.bin", 13) == 0) {

      std::string binPath =
          "renderer/materials/" + p.substr(p.find_last_of('/') + 1);
      ResourceLocation location(binPath);
      std::string out;
      // Logger::log("ResourcePackManager::load path=%s", binPath.c_str());

      bool success =
          ResourcePackManager_load(resourcePackManager, location, out);

      if (success && !out.empty()) {
        result->assign(out);
        Logger::log("Loaded %s", binPath.c_str());
      }
      // Logger::log("ResourcePackManager::load ret=%d", success);
    }
  }
  return result;
}

DeclareHook(clientInstance_Update, __int64, void *This,
            unsigned __int8 isInitFinished) {
  using func_t = void *(*)(void *);
  static uintptr_t adrr = FindSignatures(
      // ClientInstance::getResourcePackManager
      // 1.21.120
      "48 8B ? ? ? ? ? 48 8B ? 48 8B ? ? ? ? ? 48 FF ? ? ? ? ? CC CC CC CC CC "
      "CC CC CC"
      " 48 8B ? ? ? ? ? 48 8B ? 48 8B ? ? ? ? ? 48 FF ? ? ? ? ? CC CC CC CC CC "
      "CC CC CC"
      " 40 ? 48 83 EC ? 48 8B ? ? ? ? ? 48 8B ? 48 8B ? 48 8B ? ? ? ? ? FF 15 "
      "? ? ? ?"
      " 48 8B ? 48 83 C4 ? 5B C3 CC CC CC CC CC CC CC 48 83 EC");

  if (adrr && !resourcePackManager) {
    auto func = reinterpret_cast<func_t>(adrr);
    if (func) {
      resourcePackManager = func(This);
      void **vptr = *(void ***)resourcePackManager;
      ResourcePackManager_load = (PFN_ResourcePackManager_load) * (vptr + 3);
    }
  }

  return original(This, isInitFinished);
}

using dragon::materials::MaterialResourceManager;

typedef void (*PFN_mce_framebuilder_BgfxFrameBuilder_discardFrame)(
    uintptr_t This, bool waitForPreviousFrame);
typedef void (*PFN_dragon_materials_BgfxFrameBuilder_reloadMaterial)(
    uintptr_t This);

PFN_mce_framebuilder_BgfxFrameBuilder_discardFrame discardFrame = nullptr;
PFN_dragon_materials_BgfxFrameBuilder_reloadMaterial reloadMaterial = nullptr;

// mce::framebuilder::BgfxFrameBuilder::endFrame
DeclareHook(mce_framebuilder_BgfxFrameBuilder_endFrame, void, uintptr_t This,
            uintptr_t frameBuilderContext) {
  if (brd::Options::reloadShadersAvailable && brd::Options::reloadShaders) {
    brd::Options::reloadShaders = false;
    reloadMaterial(This);
  }
  original(This, frameBuilderContext);
}

// 50/51 1.26
DeclareHook(HOOK1, bool, int64_t a1) {
  if (shouldForceEnableVibrantVisuals()) {
    *(bool *)(a1 + 48) = 1;
    *(bool *)(a1 + 49) = 1;
    *(bool *)(a1 + 50) = 1;
    *(bool *)(a1 + 51) = 0;
    *(bool *)(a1 + 52) = 1;
    return true;
  }
  return original(a1);
}

// RayTracingFeatureConfiguration
DeclareHook(RayTracingResourcesConstructor, void,
            bgfx::RayTracingFeatureConfiguration *_this, bool rtxOn,
            void *dlssOptions, void *screenResolution, float renderScale,
            void *onResolutionChangedCallback, void *debugModeInfo) {
  gRayTracingFeatureConfiguration = _this;

  original(_this, rtxOn, dlssOptions, screenResolution, renderScale,
           onResolutionChangedCallback, debugModeInfo);
}

DeclareHook(RayTracingResourcesConstrucstor, void, void *_this) {
  original(_this);
  if (!gDeferredParams) {
    gDeferredParams =
        (dragon::framerenderer::DeferredShadingParameters *)((int64_t)_this +
                                                             176);
  }
}

void initMCHooks() {

  discardFrame =
      (PFN_mce_framebuilder_BgfxFrameBuilder_discardFrame)FindSignatures(
          // 1.26.10
          "48 89 5C 24 ? 88 54 24 ? 55 56 57 41 54",
          // 1.21.130
          "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 54 41 55 41 56 41 "
          "57 48 81 EC 90 00 00 00 88 54 24",
          // 1.21.120
          "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 54 41 55 41 56 41 "
          "57 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? "
          "? 44 0F B6 EA",
          // 1.21.111
          "4C 8B DC 49 89 5B ? 49 89 6B ? 49 89 73 ? 57 41 54 41 55 41 56 "
          "41 57 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 "
          "24 ? ? ? ? 88 54 24");
  if (!discardFrame) {
    Logger::log("mce::framebuilder::BgfxFrameBuilder::discardFrame not found");
  }

  reloadMaterial =
      (PFN_dragon_materials_BgfxFrameBuilder_reloadMaterial)FindSignatures(
          // 1.21.130
          "C6 81 ? ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 83 EC 28 48 8B 41 "
          "? 4C 8D 05 ? ? ? ? 48 8D 15 ? ? ? ? 48 8D 4C 24 ? 4C 8B 48 ? 49 "
          "81 C1 08 03 00 00 E8 ? ? ? ? B8 01 00 00 00 48 83 C4 28 C3", );
  if (!reloadMaterial) {
    Logger::log("reloadMaterial not found");
  }

  TrySigHook(clientInstance_Update,
             // 1.26.10
             "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 56 41 57 48 8D "
             "AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 "
             "85 ? ? ? ? 44 0F B6 FA",
             // 1.21.111
             "48 89 5C 24 ? 48 89 74 24 ? 55 57 41 54 41 56 41 57 48 8D AC 24 "
             "? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? "
             "? ? 44 0F B6 FA 48 8B F9 33 DB");

  TrySigHook(readFile,
             // 1.26.0
             "40 53 48 83 EC ? 41 0F 10 00 48 8B DA 48 8D 54 24 ? 48 8B CB 0F "
             "11 44 24 ? E8 ? ? ? ? 48 8B C3 48 83 C4 ? 5B C3 CC CC CC CC CC "
             "CC CC CC 48 89 5C 24");
  TrySigHook(mce_framebuilder_BgfxFrameBuilder_endFrame,
             // 1.26.10
             "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? "
             "? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 0F 29 B4 24 ? ? ? ? 0F 29 BC 24 "
             "? ? ? ? 44 0F 29 84 24 ? ? ? ? 44 0F 29 8C 24 ? ? ? ? 48 8B 05",
             // 1.26.0
             "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? "
             "? B8 A0 1D 00 00",
             // 1.21.130
             "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? "
             "? B8 20 1D 00 00",
             // 1.21.120
             "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? "
             "? B8 10 1D 00 00");
  TrySigHook(HOOK1,
             // 1.21.100
             "80 79 ? ? 75 ? 80 79 ? ? 74 ? B0 01 C3 32 C0 C3 CC CC CC CC CC "
             "CC CC CC CC CC CC CC CC CC 88 51 ? C3");
  TrySigHook(RayTracingResourcesConstructor,
             // 1.21.100
             "48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 83 EC 50 0F 29 74 24 "
             "? 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 4D 8B F1");
  TrySigHook(RayTracingResourcesConstrucstor,
             // 1.21.100
             "48 89 5C 24 ? 48 89 74 24 ? 48 89 4C 24 ? 57 48 83 EC 20 48 8B "
             "F9 33 F6 48 89 31 48 89 71 ? C7 41");
}