add_rules("mode.debug", "mode.release")

add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")

if is_plat("windows") then
    add_requires("detours v4.0.1-xmake.1")
    add_requires("imgui v1.91.0-docking", {configs = {dx11 = true, dx12 = true}})
elseif is_plat("android") then   

end

set_runtimes("MD")

add_requires("fmt 10.2.1","ctre 3.8.1","magic_enum v0.9.7")
add_requires("entt v3.14.0")
add_requires("gsl v4.0.0")
add_requires("glm 1.0.1")
add_requires("leveldb 1.23")
add_requires("rapidjson v1.1.0")
add_requires("type_safe v0.2.4")
add_requires("expected-lite v0.8.0")
add_requires("memorymodulepp")
add_requires("nlohmann_json v3.11.3")

target("BetterRenderDragon")
    set_kind("shared")
    set_strip("all")
    set_languages("c++20")
    set_exceptions("none")
    add_headerfiles("src/(**.h)")
    add_includedirs("./src")
    add_includedirs("./include")
    add_defines("UNICODE","_HAS_CXX23=1")
    add_files("src/**.cpp")

    if is_plat("windows") then
        add_linkdirs("lib")
        add_packages("memorymodulepp","cpr","detours","fmt","ctre","magic_enum","imgui","nlohmann_json","entt","glm","gsl","leveldb","rapidjson","type_safe","expected-lite")
        remove_files("src/api/memory/android/**.cpp","src/api/memory/android/**.h")
        add_cxflags("/utf-8", "/EHa")
        add_links("ntdll","userenv","materialbin","windowsapp")
    end