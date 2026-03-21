#pragma once

#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include <initializer_list>

#include <windows.h>
#include <psapi.h>

#include "MinHook.h"
#include "api/Logger.h"
#include "libhat/scanner.hpp"
#include "libhat/signature.hpp"

#define FindSignature(signature) FindSig(signature)
#define FindSignatures(...) FindSigs({__VA_ARGS__})

#define DeclareHook(name, ret, ...) \
struct _Hook_##name { \
    static ret (*_original)(__VA_ARGS__); \
    template <typename ...Params> \
    static ret original(Params&&... params) { return (*_original)(std::forward<Params>(params)...); } \
    static ret _hook(__VA_ARGS__); \
}; \
ret (*_Hook_##name::_original)(__VA_ARGS__) = nullptr; \
ret _Hook_##name::_hook(__VA_ARGS__)

#define CallOriginal(name, ...) _Hook_##name::original(__VA_ARGS__)
#define IsHooked(name) (_Hook_##name::_original != nullptr)
#define Hook(name, ptr) HookFunction(ptr, (void**)&_Hook_##name::_original, &_Hook_##name::_hook)
#define TrySigHook(name, ...) SigHook(#name, {__VA_ARGS__}, (void**)&_Hook_##name::_original, &_Hook_##name::_hook)
#define UnhookAllHooks() UnhookAllFunctions()

inline std::vector<uintptr_t> installedHooks;

inline void HookFunction(void* oldFunc, void** outOldFunc, void* newFunc) {
    MH_CreateHook(oldFunc, newFunc, outOldFunc);
    MH_EnableHook(oldFunc);
    installedHooks.emplace_back(reinterpret_cast<uintptr_t>(oldFunc));
}

inline void UnhookAllFunctions() {
    for (const uintptr_t installedHook : installedHooks) {

        MH_DisableHook(reinterpret_cast<void*>(installedHook));
        MH_RemoveHook(reinterpret_cast<void*>(installedHook));
        auto it = std::ranges::find(installedHooks.begin(), installedHooks.end(), installedHook);
        installedHooks.erase(it);
    }
    MH_Uninitialize();
}

inline void ReplaceVtable(void* _vptr, size_t index, void** outOldFunc, void* newFunc) {
    void** vptr = (void**)_vptr;
    void* oldFunc = vptr[index];
    if (oldFunc == newFunc) {
        return;
    }
    if (outOldFunc != nullptr) {
	    *outOldFunc = oldFunc;
    }

    DWORD oldProtect, tmp;
    VirtualProtect(vptr + index, sizeof(void*), PAGE_READWRITE, &oldProtect);
    vptr[index] = newFunc;
    VirtualProtect(vptr + index, sizeof(void*), oldProtect, &tmp);
}

inline uintptr_t FindSig(const std::string& signature) {

    hat::result<std::vector<hat::signature_element>, hat::signature_error> parsed = hat::parse_signature(signature);
    if (!parsed.has_value()) {
        Logger::log("failed to parse signature %s", signature.c_str());
        return 0;
    }

    const hat::scan_result result = hat::find_pattern(parsed.value(), ".text");
    if (!result.has_result()) {
        Logger::log("failed to find signature %s", signature.c_str());
        return 0;
    }

    return reinterpret_cast<uintptr_t>(result.get());
}

inline uintptr_t FindSigs(const std::initializer_list<std::string>& signatures) {
    uintptr_t ptr = 0;
    for (auto& sig : signatures) {
        if (ptr = FindSig(sig)) {
            break;
        }
    }
    return ptr;
}

inline bool SigHook(const std::string& name, const std::initializer_list<std::string>& signatures, void** outOldFunc, void* newFunc) {
    uintptr_t ptr = FindSigs(signatures);
    if (!ptr) {
        Logger::log("Failed to hook %s (signature not found)", name.c_str());
        return false;
    }

    HookFunction((void*)ptr, outOldFunc, newFunc);
    Logger::log("Hooked %s", name.c_str());

    return true;
}