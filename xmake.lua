set_project("my_game")
set_version("0.1.0")

set_policy("build.c++.modules", true)
add_rules("mode.debug", "mode.release")

set_arch("x64")
set_encodings("utf-8")
set_symbols("debug")
set_strip("debug")

add_vectorexts("avx", "avx2")
set_policy("build.warning", true)

if is_plat("windows") then
    set_runtimes(is_mode("debug") and "MDd" or "MD")
else
    set_runtimes("c++_shared")
end

includes("external/**/xmake.lua")

local xrgui_dir = path.join(os.scriptdir(), "external/xrgui")
local mo_yanxi_utility_dir = path.join(xrgui_dir, "external/mo_yanxi_vulkan_wrapper/external/mo_yanxi_utility")

function add_ecs_entity_port_includes()
    add_includedirs(path.join(xrgui_dir, "external/plf_hive"))
    add_includedirs(path.join(xrgui_dir, "external/small_vector/source/include"))
    add_includedirs(path.join(mo_yanxi_utility_dir, "include"))
end



target("game")
    set_kind("binary")
    set_extension(".exe")
    set_languages("c++latest")

    add_deps("xrgui.default")

    set_warnings("all", "pedantic")

    add_files("src/**.ixx")
    add_files("src/**.cpp")
    add_files("external/xrgui/src/sync/sync_processor.ixx")
    remove_files("src/game/entity/**.ixx")
    remove_files("src/ecs/**.ixx")
    remove_files("src/ecs/**.cpp")

    if is_mode("release") then
        set_policy("build.optimization.lto", true)
    end

target_end()

target("ecs_entity_test")
    set_kind("binary")
    set_extension(".exe")
    set_languages("c++latest")

    add_deps("xrgui.default")
    add_ecs_entity_port_includes()

    set_warnings("all", "pedantic")

    add_files("src/ecs/**.ixx")
    add_files("src/ecs/**.cpp")
    add_files("tests/ecs_entity_test/main.cpp")
target_end()

target("soa_vector_constexpr_test")
    set_kind("binary")
    set_extension(".exe")
    set_languages("c++latest")
    set_default(false)

    add_ecs_entity_port_includes()

    set_warnings("all", "pedantic")

    add_files("src/ecs/support/soa_vector.ixx")
    add_files("tests/soa_vector_constexpr_test/main.cpp")
target_end()

target("physics_core_test")
    set_kind("binary")
    set_extension(".exe")
    set_languages("c++latest")
    set_default(false)

    add_deps("xrgui.default")

    set_warnings("all", "pedantic")

    add_files("src/game/physics/**.ixx")
    add_files("tests/physics_core_test/main.cpp")
target_end()


