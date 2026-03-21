#pragma once
#include <filesystem>
#include <shlobj_core.h>
#include <windows.h>

namespace Global {
    inline HMODULE hModule;
    inline bool eject;

    static std::string GetBRDRaomingPath() {
        std::filesystem::path path;
        PWSTR path_tmp;

        auto get_folder_path_ret = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path_tmp);

        if (get_folder_path_ret != S_OK) {
            CoTaskMemFree(path_tmp);
            return "";
        }

        path = path_tmp;
        CoTaskMemFree(path_tmp);

        return path.string() + "\\BetterRenderDragon";
    }
}