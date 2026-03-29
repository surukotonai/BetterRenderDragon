#include "Options.h"
#include "imgui.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <shlobj.h>
#include <string>
#include <vector>
#include <windows.h>

#include "Global.h"
#include "api/Logger.h"

using nlohmann::json;

namespace brd {
// --- Option defs ---
namespace Options {
Option<bool> showImGui = true;
Option<bool> performanceEnabled = true;
Option<bool> settingsEnabled = true;
bool vanilla2DeferredAvailable = true;
bool newVideoSettingsAvailable = false;
Option<bool> graphicsEnabled = true;
Option<bool> disableRendererContextD3D12RTX = false;
Option<bool> materialBinLoaderEnabled = true;
Option<bool> redirectShaders = true;
bool reloadShadersAvailable = true;
std::atomic_bool reloadShaders = false;
Option<bool> customUniformsEnabled = false;
Option<bool> forceEnableVibrantVisuals = true;
Option<int> uiKey = ImGuiKey_F6;
Option<int> reloadShadersKey = ImGuiKey_None;
Option<float> fontSize = 1.0f;

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

// ----------------- 选项处理函数 -----------------
static std::vector<IOption *> options;
bool Options::init() {
  options.clear();
  options.push_back(&showImGui);
  options.push_back(&performanceEnabled);
  options.push_back(&settingsEnabled);
  options.push_back(&graphicsEnabled);
  options.push_back(&disableRendererContextD3D12RTX);
  options.push_back(&materialBinLoaderEnabled);
  options.push_back(&redirectShaders);
  options.push_back(&customUniformsEnabled);
  options.push_back(&uiKey);
  options.push_back(&reloadShadersKey);
  options.push_back(&forceEnableVibrantVisuals);
  options.push_back(&fontSize);

  if (optionsDir.empty()) {
    optionsDir = Global::GetBRDRaomingPath();
    Logger::log("Settings Path: %s", optionsDir.c_str());
    if (optionsDir.empty()) {
      return false;
    }

    optionsFile = optionsDir + "\\BetterRenderDragon.json";
  }

  if (!std::filesystem::exists(optionsDir)) {
    if (!std::filesystem::create_directories(optionsDir)) {
      Logger::log("Failed to create options directory: %s", optionsDir.c_str());
      return false;
    }
  }
  if (!std::filesystem::is_directory(optionsDir)) {
    Logger::log("optionsDir not a directory: %s", optionsDir.c_str());
    return false;
  }
  return true;
}

bool Options::load() {
  if (!std::filesystem::exists(optionsFile)) {
    Logger::log("Options file does not exist: %s", optionsFile.c_str());
    return save();
  }
  if (!std::filesystem::is_regular_file(optionsFile)) {
    Logger::log("Options file is not a regular file: %s", optionsFile.c_str());
    return false;
  }

  json data;
  try {
    std::ifstream ifs(optionsFile, std::ifstream::binary);
    if (!ifs) {
      Logger::log("Cannot open options file: %s", optionsFile.c_str());
      return save();
    }
    ifs >> data;
  } catch (json::parse_error &e) {
    Logger::log("Failed to parse json: %s", e.what());
    return false;
  } catch (...) {
    Logger::log("Failed to read options file: %s (unknown error)",
                optionsFile.c_str());
    return false;
  }

  if (data.contains("showImGui"))
    showImGui = data["showImGui"];
  if (data.contains("uiKey"))
    uiKey = data["uiKey"];
  if (data.contains("performanceEnabled"))
    performanceEnabled = data["performanceEnabled"];
  if (data.contains("graphicsEnabled"))
    graphicsEnabled = data["graphicsEnabled"];
  if (data.contains("settingsEnabled"))
    settingsEnabled = data["settingsEnabled"];
  if (data.contains("disableRendererContextD3D12RTX"))
    disableRendererContextD3D12RTX = data["disableRendererContextD3D12RTX"];
  if (data.contains("materialBinLoaderEnabled"))
    materialBinLoaderEnabled = data["materialBinLoaderEnabled"];
  if (data.contains("redirectShaders"))
    redirectShaders = data["redirectShaders"];
  if (data.contains("forceEnableVibrantVisuals"))
    forceEnableVibrantVisuals = data["forceEnableVibrantVisuals"];
  if (data.contains("fontSize"))
    fontSize = data["fontSize"];

  return true;
}

bool Options::save() {
  json data;
  data["showImGui"] = showImGui.get();
  data["uiKey"] = uiKey.get();
  data["performanceEnabled"] = performanceEnabled.get();
  data["graphicsEnabled"] = graphicsEnabled.get();
  data["settingsEnabled"] = settingsEnabled.get();
  data["disableRendererContextD3D12RTX"] = disableRendererContextD3D12RTX.get();
  data["materialBinLoaderEnabled"] = materialBinLoaderEnabled.get();
  data["redirectShaders"] = redirectShaders.get();
  data["forceEnableVibrantVisuals"] = forceEnableVibrantVisuals.get();
  data["fontSize"] = fontSize.get();

  try {
    std::ofstream ofs(optionsFile,
                      std::ofstream::binary | std::ofstream::trunc);
    if (!ofs) {
      Logger::log("Failed to open options file for write: %s",
                  optionsFile.c_str());
      return false;
    }
    ofs << std::setw(4) << data << '\n';
  } catch (...) {
    Logger::log("Failed to write options file: %s (unknown error)",
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