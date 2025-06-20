
#include "Core/Math/Vec4.h"
#include "mc/client/RenderDragon/Materials/MaterialResourceManager.h"
#include "mc/client/Renderdragon/Materials/MaterialUniformName.h"
#include "mc/client/Renderdragon/Materials/ShaderCodePlatform.h"
#include "mc/client/Renderdragon/Rendering/LightingModels.h"
#include "mc/deps/core/resource/ResourceLocation.h"

#include "gui/Options.h"

#include <iostream>
#include <stacktrace>

#include <iostream>
//=====================================================Vanilla2Deferred=====================================================

char globalGraphicsMode = 0;
int MaterialResourceManagerOffset = 0;

using dragon::rendering::LightingModels;

bool shouldForceEnableNewVideoSettings() {
  return brd::Options::vanilla2DeferredAvailable &&
         brd::Options::vanilla2DeferredEnabled &&
         brd::Options::newVideoSettingsAvailable &&
         brd::Options::forceEnableDeferredTechnicalPreview;
}
//======================================================CustomUniforms======================================================

//=====================================================MaterialBinLoader====================================================

typedef bool (*PFN_ResourcePackManager_load)(void *This,
                                             const ResourceLocation &location,
                                             std::string &resourceStream);

//==========================================================================================================================

#include "api/memory/Hook.h"

int NEW_VIDEO_SETTINGS = 0;

SKY_AUTO_STATIC_HOOK(getGameVersionString, memory::HookPriority::Normal,
                     std::initializer_list<const char *>(
                         {// Win 1.21.60
                          "48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 54 41 56 41 57 "
                          "48 83 EC 60 48 8B F9 48 89 4C 24 ? 45 33 E4"}),
                     std::string, std::string *result) {
  auto version = origin(result);
  if (version.find("1.21.6") != std::string::npos) {
    NEW_VIDEO_SETTINGS = 723;
    MaterialResourceManagerOffset = 1008;
  } else if (version.find("1.21.7") != std::string::npos) {
    NEW_VIDEO_SETTINGS = 726;
    MaterialResourceManagerOffset = 1008;
  } else if (version.find("1.21.8") != std::string::npos) {
    NEW_VIDEO_SETTINGS = 730;
    MaterialResourceManagerOffset = 960;
  } else if (version.find("1.21.9") != std::string::npos) {
    MaterialResourceManagerOffset = 960;
  }
  return version;
}

SKY_AUTO_STATIC_HOOK(HOOK1, memory::HookPriority::Normal,
                     " 80 79 ? ? 75 ? 80 79 ? ? 74 ? B0 01 C3 32 C0 C3 CC CC "
                     "CC CC CC CC CC CC CC CC CC CC CC CC 88 51 ? C3",
                     bool, __int64 a1) {
  return true;
}

SKY_AUTO_STATIC_HOOK(HOOK2, memory::HookPriority::Normal,
                     "88 51 ? C3 CC CC CC CC CC CC CC CC CC CC CC CC 80 79 ? "
                     "? 75 ? 83 79 ? ? 74 ? 32 C0 C3",
                     void, __int64 a1, bool a2) {
  origin(a1, false);
}

SKY_AUTO_STATIC_HOOK(
    HOOK23, memory::HookPriority::Normal,
    "48 89 5C 24 ? 48 89 74 24 ? 55 57 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 "
    "EC C0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? C7 45",
    __int64, __int64 a1) {
  return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void *resourcePackManager = nullptr;
PFN_ResourcePackManager_load ResourcePackManager_load;
// ResourcePackManager::ResourcePackManager
SKY_AUTO_STATIC_HOOK(
    ResourcePackManagerConstructor, memory::HookPriority::Normal,
    std::initializer_list<const char *>(
        {// 1.21.90
         "4C 8B DC 49 89 5B ? 49 89 53 ? 49 89 4B ? 55 56 57 41 56"}),
    void *, void *This, uintptr_t a2, uintptr_t a3, bool needsToInitialize) {

  void *result = origin(This, a2, a3, needsToInitialize);
  if (needsToInitialize && !resourcePackManager) {
    resourcePackManager = This;
    void **vptr = *(void ***)resourcePackManager;
    ResourcePackManager_load = (PFN_ResourcePackManager_load) * (vptr + 3);
  }
  return result;
}

#include "materialbin.h"
// AppPlatform::readAssetFile
SKY_AUTO_STATIC_HOOK(
    readAssetFileHOOK, memory::HookPriority::Normal,
    std::initializer_list<const char *>(
        {// 1.21.60
         "48 89 5C 24 ? 48 89 7C 24 ? 55 48 8D 6C 24 ? 48 81 EC 60 01 00 00 48 "
         "8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 48 8B FA"}),
    std::string *, void *This, std::string *retstr, Core::Path &path) {
  std::string *result = origin(This, retstr, path);
  if (brd::Options::materialBinLoaderEnabled && brd::Options::redirectShaders &&
      resourcePackManager) {
    const std::string &p = path.getUtf8StdString();
    if (p.find("/data/renderer/materials/") != std::string::npos &&
        strncmp(p.c_str() + p.size() - 13, ".material.bin", 13) == 0) {
      std::string binPath =
          "renderer/materials/" + p.substr(p.find_last_of('/') + 1);
      ResourceLocation location(binPath);
      std::string out;
      // printf("ResourcePackManager::load path=%s\n", binPath.c_str());

      bool success =
          ResourcePackManager_load(resourcePackManager, location, out);
      std::cout << success << std::endl;
      if (success && !out.empty()) {
        bool successful_update = true;
        struct Buffer outbufdata = {0, 0};
        if (update_file(out.length(), (const uint8_t *)out.c_str(),
                        &outbufdata) != 0) {
          // printf("Updating failed!");
          successful_update = false;
          free_buf(outbufdata);
        }

        if (!successful_update) {
          retstr->assign(out);
        } else {
          retstr->assign((const char *)outbufdata.data, outbufdata.len);
          free_buf(outbufdata);
        }
      }
      // printf("ResourcePackManager::load ret=%d\n", success);
    }
  }
  return result;
}

///////////////////////////////////////////////////////////////////////////////////////

using dragon::materials::MaterialResourceManager;

typedef void (*PFN_mce_framebuilder_BgfxFrameBuilder_discardFrame)(
    uintptr_t This, bool waitForPreviousFrame);
typedef void (*PFN_dragon_materials_CompiledMaterialManager_freeShaderBlobs)(
    uintptr_t This);
PFN_mce_framebuilder_BgfxFrameBuilder_discardFrame discardFrame = nullptr;
PFN_dragon_materials_CompiledMaterialManager_freeShaderBlobs freeShaderBlobs =
    nullptr;

bool discardFrameAndClearShaderCaches(uintptr_t bgfxFrameBuilder) {
  uintptr_t compiledMaterialManager =
      *(uintptr_t *)(*(uintptr_t *)(bgfxFrameBuilder + 40) + 16) + 776;
  uintptr_t mExtractor = *(uintptr_t *)(bgfxFrameBuilder + 32);
  MaterialResourceManager *mMaterialsManager = (MaterialResourceManager *)*(
      uintptr_t *)(mExtractor + MaterialResourceManagerOffset);
  if (discardFrame && freeShaderBlobs && mMaterialsManager) {
    discardFrame(bgfxFrameBuilder, true);
    mMaterialsManager->forceTrim();
    freeShaderBlobs(compiledMaterialManager);
    freeShaderBlobs(compiledMaterialManager);
    return true;
  }
  return false;
}
// mce::framebuilder::BgfxFrameBuilder::endFrame
SKY_AUTO_STATIC_HOOK(
    mce_framebuilder_BgfxFrameBuilder_endFrame, memory::HookPriority::Normal,
    std::initializer_list<const char *>(
        {// 1.21.60
         "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? "
         "B8 10 29 00 00",
         // 1.21.70
         "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? "
         "B8 00 32 00 00",
         // 1.21.80
         "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? "
         "B8 60 33 00 00",
         // 1.21.90
         "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? "
         "B8 40 34 00 00"}),
    void, uintptr_t This, uintptr_t frameBuilderContext) {
  bool clear = false;
  if (brd::Options::reloadShadersAvailable && brd::Options::reloadShaders) {
    brd::Options::reloadShaders = false;
    clear = true;
  }
  if (clear && discardFrameAndClearShaderCaches(This)) {
    return;
  }
  origin(This, frameBuilderContext);
}

////////////////////////////////////////////////////////////////////////////////
void initMCHooks() {

  discardFrame = (PFN_mce_framebuilder_BgfxFrameBuilder_discardFrame)
      memory::resolveIdentifier({"48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? "
                                 "57 41 54 41 55 41 56 41 "
                                 "57 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? "
                                 "48 33 C4 48 89 84 24 ? ? "
                                 "? ? 44 0F B6 EA"});
  if (!discardFrame) {
    printf("mce::framebuilder::BgfxFrameBuilder::discardFrame not found\n");
  }

  freeShaderBlobs =
      (PFN_dragon_materials_CompiledMaterialManager_freeShaderBlobs)
          memory::resolveIdentifier(
              {"48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 54 41 55 41 "
               "56 "
               "41 57 48 83 EC 20 4C 8B E9 48 83 C1 40"});
  if (!freeShaderBlobs) {
    printf("dragon::materials::CompiledMaterialManager::freeShaderBlobs not "
           "found\n");
  }
}