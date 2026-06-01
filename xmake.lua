set_project("my_game")
set_version("0.1.0")

set_policy("build.c++.modules", true)
add_rules("mode.debug", "mode.release")

set_arch("x64")
set_encodings("utf-8")
set_symbols("debug", "embed")
if is_mode("release") then
    set_strip("none")
else
    set_strip("debug")
end

add_vectorexts("avx", "avx2")
set_policy("build.warning", true)

local current_dir = os.scriptdir()
local root_dir = os.projectdir()
local project_is_top_level = path.translate(current_dir) == path.translate(root_dir) and os.getenv("XMAKE_IN_XREPO") ~= "1"

option("enable_tests")
    set_default(project_is_top_level)
    set_showmenu(true)
    set_description("Enable my_game GoogleTest target")
option_end()

if is_plat("windows") then
    set_runtimes(is_mode("debug") and "MDd" or "MD")
    add_cxxflags("/FS", {tools = {"cl"}})
else
    set_runtimes("c++_shared")
end

includes("external/**/xmake.lua")

if has_config("enable_tests") then
    add_requires("gtest")
end

local xrgui_dir = path.join(current_dir, "external/xrgui")
local mo_yanxi_utility_dir = path.join(xrgui_dir, "external/mo_yanxi_vulkan_wrapper/external/mo_yanxi_utility")

function add_ecs_entity_port_includes()
    add_includedirs(path.join(xrgui_dir, "external/plf_hive"))
    add_includedirs(path.join(xrgui_dir, "external/small_vector/source/include"))
    add_includedirs(path.join(xrgui_dir, "external/include"))
    add_includedirs(path.join(mo_yanxi_utility_dir, "include"))
end

function add_game_physics_sources()
    add_files("src/math/**.ixx")
    add_files("src/game/physics/**.ixx")
end

function add_game_srl_sources()
    add_files("src/srl/**.ixx")
end

function add_game_ecs_sources()
    add_files("src/ecs/**.ixx")
    add_files("src/ecs/**.cpp")
end

function add_game_runtime_sources()
    add_files("src/game/runtime/**.ixx")
    add_files("src/game/runtime/**.cpp")
end

function add_game_ui_sources()
    add_files("src/game/ui/**.ixx")
    add_files("src/game/ui/**.cpp")
end

function add_game_profile_sources()
    add_files("profile/runtime/**.ixx")
    add_files("profile/runtime/**.cpp")
end

function add_game_entity_sources()
    add_files("src/game/entity/components/physical_rigid.ixx")
    add_files("src/game/entity/components/physics.ixx")
    add_files("src/game/entity/components/damage.ixx")
    add_files("src/game/entity/components/faction.ixx")
    add_files("src/game/entity/components/targeting.ixx")
    add_files("src/game/entity/components/projectile/projectile_manifold.ixx")
    add_files("src/game/entity/components/chamber/damage_grid.ixx")
    add_files("src/game/entity/system/physics_system.ixx")
    add_files("src/game/entity/system/chamber_system.ixx")
    add_files("src/game/entity/system/projectile_system.ixx")
    add_files("src/game/entity/system/targeting_system.ixx")
    add_files("src/game/entity/misc/aiming.ixx")
end



target("game")
    set_kind("binary")
    set_extension(".exe")
    set_languages("c++latest")

    add_deps("xrgui.default")
    add_ecs_entity_port_includes()

    set_warnings("all", "pedantic")

    add_game_physics_sources()
    add_game_srl_sources()
    add_game_profile_sources()
    add_game_runtime_sources()
    add_game_ui_sources()
    add_files("src/main_loop/**.ixx")
    add_files("src/main_loop/**.cpp")
    add_game_ecs_sources()
    add_game_entity_sources()
    add_files("external/xrgui/src/sync/sync_processor.ixx")

    if is_mode("release") then
        set_policy("build.optimization.lto", true)
    end

target_end()

target("cpu_profile")
    set_kind("binary")
    set_extension(".exe")
    set_languages("c++latest")

    add_deps("xrgui.default")
    add_ecs_entity_port_includes()

    set_warnings("all", "pedantic")

    add_game_physics_sources()
    add_game_srl_sources()
    add_game_profile_sources()
    add_game_ecs_sources()
    add_game_entity_sources()
    add_files("src/game/runtime/draw/collision_shape_style.ixx")
    add_files("src/game/runtime/draw/collision_shape_draw.ixx")
    add_files("src/game/runtime/draw/collision_shape_draw.cpp")
    add_files("src/game/runtime/game_renderer.ixx")
    add_files("src/game/runtime/draw/chamber_component.ixx")
    add_files("src/game/runtime/draw/chamber_component.cpp")
    add_files("src/game/runtime/draw/collision_shape_component.ixx")
    add_files("src/game/runtime/draw/collision_shape_drawable.cpp")
    add_files("src/game/runtime/world.ixx")
    add_files("profile/cpu_profile_main.cpp")
    add_files("external/xrgui/src/sync/sync_processor.ixx")

    if is_mode("release") then
        set_policy("build.optimization.lto", true)
    end
target_end()

target("game_tests")
    set_kind("binary")
    set_extension(".exe")
    set_languages("c++latest")
    set_enabled(has_config("enable_tests"))
    set_default(has_config("enable_tests"))

    add_deps("xrgui.default")
    add_ecs_entity_port_includes()
    if has_config("enable_tests") then
        add_packages("gtest", "simdutf")
    end

    set_warnings("all", "pedantic")

    add_game_physics_sources()
    add_game_srl_sources()
    add_game_profile_sources()
    add_game_ecs_sources()
    add_game_runtime_sources()
    add_game_entity_sources()
    add_files("external/xrgui/src/sync/sync_processor.ixx")
    add_files("tests/**.cpp")
target_end()
