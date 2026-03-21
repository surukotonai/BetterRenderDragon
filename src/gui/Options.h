// Options.h
#pragma once
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>
namespace brd {
class IOption {
public:
  virtual void record() = 0;
  virtual bool isChanged() = 0;
};

template <typename T> class Option : public IOption {
public:
  Option() {}
  Option(const T &value) : value(value), prevValue(value) {}
  virtual void record() override { prevValue = value; }
  virtual bool isChanged() override { return value != prevValue; }
  T &get() { return value; }
  const T &get() const { return value; }
  void set(const T &value) { this->value = value; }
  T *ptr() { return &this->value; }
  const T *ptr() const { return &this->value; }
  operator T() { return value; }
  Option<T> &operator=(const T &value) {
    this->value = value;
    return *this;
  }

private:
  T value;
  T prevValue;
};

namespace Options {
  extern Option<bool> showImGui;
  extern Option<bool> performanceEnabled;
  extern Option<bool> settingsEnabled;

  extern bool vanilla2DeferredAvailable;
  extern bool newVideoSettingsAvailable;
  extern Option<bool> graphicsEnabled;
  extern Option<bool> disableRendererContextD3D12RTX;
  extern Option<bool> materialBinLoaderEnabled;
  extern Option<bool> redirectShaders;
  extern Option<bool> forceEnableVibrantVisuals;
  extern bool reloadShadersAvailable;
  extern std::atomic_bool reloadShaders;
  extern Option<bool> customUniformsEnabled;
  extern Option<int> uiKey;
  extern Option<int> reloadShadersKey;

  extern std::string optionsDir;
  extern std::string optionsFile;

  bool init();
  bool load();
  bool save();
  void record();
  bool isDirty();
  } // namespace Options
} // namespace brd