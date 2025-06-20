// Options.cpp
#include "Options.h"
#include "imgui.h"
#include <Windows.Storage.h>
#include <iomanip> // For std::setw
#include <nlohmann/json.hpp>
#include <string>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/base.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>

#include <shlobj.h>
#include <windows.h>

using nlohmann::json;

using ABI::Windows::Storage::IApplicationData;
using ABI::Windows::Storage::IApplicationDataStatics;
using ABI::Windows::Storage::IStorageFolder;
using ABI::Windows::Storage::IStorageItem;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;
namespace brd {
// --- Option defs ---
namespace Options {
Option<bool> showImGui = true;
Option<bool> performanceEnabled = true;
Option<bool> windowSettingsEnabled = true;
bool vanilla2DeferredAvailable = true;
bool newVideoSettingsAvailable = false;
Option<bool> vanilla2DeferredEnabled = true;
Option<bool> deferredRenderingEnabled = false;
Option<bool> forceEnableDeferredTechnicalPreview = false;
Option<bool> disableRendererContextD3D12RTX = false;
Option<bool> materialBinLoaderEnabled = true;
Option<bool> redirectShaders = true;
bool reloadShadersAvailable = true;
std::atomic_bool reloadShaders = false;
Option<bool> customUniformsEnabled = false;
Option<int> uiKey = ImGuiKey_F6;
Option<int> reloadShadersKey = ImGuiKey_None;

std::string optionsDir;
std::string optionsFile;
} // namespace Options

std::string wstringToString(const std::wstring &wstr) {
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(),
                                        (int)wstr.size(), NULL, 0, NULL, NULL);
  std::string strTo(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &strTo[0],
                      size_needed, NULL, NULL);
  return strTo;
}

static void DebugTrace(const char *fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  OutputDebugStringA(buf);
}

struct WinRTApartmentInit {
  WinRTApartmentInit() {
    static bool inited = false;
    if (!inited) {
      winrt::init_apartment();
      inited = true;
    }
  }
};
static WinRTApartmentInit _apartment_init;

std::string getMinecraftModsPath() {
  winrt::init_apartment();

  winrt::Windows::Storage::StorageFolder localFolder =
      winrt::Windows::Storage::ApplicationData::Current().LocalFolder();

  std::wstring path = localFolder.Path().c_str();
  path += L"\\mods";

  return std::string(path.begin(), path.end());
}

// ----------------- 选项处理函数 -----------------
static std::vector<IOption *> options;
bool Options::init() {
  options.clear();
  options.push_back(&showImGui);
  options.push_back(&performanceEnabled);
  options.push_back(&windowSettingsEnabled);
  options.push_back(&vanilla2DeferredEnabled);
  options.push_back(&deferredRenderingEnabled);
  options.push_back(&forceEnableDeferredTechnicalPreview);
  options.push_back(&disableRendererContextD3D12RTX);
  options.push_back(&materialBinLoaderEnabled);
  options.push_back(&redirectShaders);
  options.push_back(&customUniformsEnabled);
  options.push_back(&uiKey);
  options.push_back(&reloadShadersKey);

  if (optionsDir.empty()) {
    std::string localStatePath = getMinecraftModsPath();
    printf("getMinecraftModsPath: %s\n", localStatePath.c_str());
    if (localStatePath.empty()) {
      return false;
    }
    optionsDir = localStatePath + "\\BetterRenderDragon";
    optionsFile = optionsDir + "\\BetterRenderDragon.json";
  }

  if (!std::filesystem::exists(optionsDir)) {
    if (!std::filesystem::create_directories(optionsDir)) {
      printf("Failed to create options directory: %s\n", optionsDir.c_str());
      return false;
    }
  }
  if (!std::filesystem::is_directory(optionsDir)) {
    printf("optionsDir not a directory: %s\n", optionsDir.c_str());
    return false;
  }
  return true;
}

// 读选项
bool Options::load() {
  if (!std::filesystem::exists(optionsFile)) {
    printf("Options file does not exist: %s\n", optionsFile.c_str());
    return save();
  }
  if (!std::filesystem::is_regular_file(optionsFile)) {
    printf("Options file is not a regular file: %s\n", optionsFile.c_str());
    return false;
  }

  json data;
  try {
    std::ifstream ifs(optionsFile, std::ifstream::binary);
    if (!ifs) {
      printf("Cannot open options file: %s\n", optionsFile.c_str());
      // 尝试保存默认配置
      return save();
    }
    ifs >> data;
  } catch (json::parse_error &e) {
    printf("Failed to parse json: %s\n", e.what());
    return false;
  } catch (...) {
    printf("Failed to read options file: %s (unknown error)\n",
           optionsFile.c_str());
    return false;
  }

  if (data.contains("showImGui"))
    showImGui = data["showImGui"];
  if (data.contains("performanceEnabled"))
    performanceEnabled = data["performanceEnabled"];
  if (data.contains("vanilla2DeferredEnabled"))
    vanilla2DeferredEnabled = data["vanilla2DeferredEnabled"];
  if (data.contains("deferredRenderingEnabled"))
    deferredRenderingEnabled = data["deferredRenderingEnabled"];
  if (data.contains("forceEnableDeferredTechnicalPreview"))
    forceEnableDeferredTechnicalPreview =
        data["forceEnableDeferredTechnicalPreview"];
  if (data.contains("disableRendererContextD3D12RTX"))
    disableRendererContextD3D12RTX = data["disableRendererContextD3D12RTX"];
  if (data.contains("materialBinLoaderEnabled"))
    materialBinLoaderEnabled = data["materialBinLoaderEnabled"];
  if (data.contains("redirectShaders"))
    redirectShaders = data["redirectShaders"];

  // 可扩展 : customUniformsEnabled 等

  return true;
}

// 写选项
bool Options::save() {
  json data;
  data["showImGui"] = showImGui.get();
  data["performanceEnabled"] = performanceEnabled.get();
  data["vanilla2DeferredEnabled"] = vanilla2DeferredEnabled.get();
  data["deferredRenderingEnabled"] = deferredRenderingEnabled.get();
  data["forceEnableDeferredTechnicalPreview"] =
      forceEnableDeferredTechnicalPreview.get();
  data["disableRendererContextD3D12RTX"] = disableRendererContextD3D12RTX.get();
  data["materialBinLoaderEnabled"] = materialBinLoaderEnabled.get();
  data["redirectShaders"] = redirectShaders.get();
  // 可扩展 ...

  try {
    std::ofstream ofs(optionsFile,
                      std::ofstream::binary | std::ofstream::trunc);
    if (!ofs) {
      printf("Failed to open options file for write: %s\n",
             optionsFile.c_str());
      return false;
    }
    ofs << std::setw(4) << data << '\n';
  } catch (...) {
    printf("Failed to write options file: %s (unknown error)\n",
           optionsFile.c_str());
    return false;
  }
  return true;
}

void Options::record() {
  for (auto opt : options)
    opt->record();
}
bool Options::isDirty() {
  for (auto opt : options) {
    if (opt->isChanged())
      return true;
  }
  return false;
}
} // namespace brd