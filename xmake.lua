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
    add_ecs_entity_port_includes()

    set_warnings("all", "pedantic")

    add_files("src/math/**.ixx")
    add_files("src/game/physics/**.ixx")
    add_files("src/game/runtime/**.ixx")
    add_files("src/game/runtime/**.cpp")
    add_files("src/main_loop/**.ixx")
    add_files("src/main_loop/**.cpp")
    add_files("src/ecs/**.ixx")
    add_files("src/ecs/**.cpp")
    add_files("src/game/entity/components/physical_rigid.ixx")
    add_files("src/game/entity/components/physics.ixx")
    add_files("src/game/entity/components/damage.ixx")
    add_files("src/game/entity/components/faction.ixx")
    add_files("src/game/entity/components/projectile/projectile_manifold.ixx")
    add_files("src/game/entity/components/chamber/damage_grid.ixx")
    add_files("src/game/entity/system/physics_system.ixx")
    add_files("src/game/entity/system/projectile_system.ixx")
    add_files("external/xrgui/src/sync/sync_processor.ixx")

    if is_mode("release") then
        set_policy("build.optimization.lto", true)
    end

target_end()

target("game_instance_test")
    set_kind("binary")
    set_extension(".exe")
    set_languages("c++latest")
    set_default(false)

    add_deps("xrgui.default")
    add_ecs_entity_port_includes()

    set_warnings("all", "pedantic")

    add_files("src/math/**.ixx")
    add_files("src/ecs/**.ixx")
    add_files("src/ecs/**.cpp")
    add_files("src/game/physics/**.ixx")
    add_files("src/game/runtime/**.ixx")
    add_files("src/game/runtime/**.cpp")
    add_files("src/game/entity/components/physical_rigid.ixx")
    add_files("src/game/entity/components/physics.ixx")
    add_files("src/game/entity/components/damage.ixx")
    add_files("src/game/entity/components/faction.ixx")
    add_files("src/game/entity/components/projectile/projectile_manifold.ixx")
    add_files("src/game/entity/components/chamber/damage_grid.ixx")
    add_files("src/game/entity/system/physics_system.ixx")
    add_files("src/game/entity/system/projectile_system.ixx")
    add_files("tests/game_instance_test/main.cpp")
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

target("physics_ecs_test")
    set_kind("binary")
    set_extension(".exe")
    set_languages("c++latest")
    set_default(false)

    add_deps("xrgui.default")
    add_ecs_entity_port_includes()

    set_warnings("all", "pedantic")

    add_files("src/ecs/**.ixx")
    add_files("src/ecs/**.cpp")
    add_files("src/game/physics/**.ixx")
    add_files("src/game/entity/components/physical_rigid.ixx")
    add_files("src/game/entity/components/physics.ixx")
    add_files("src/game/entity/components/damage.ixx")
    add_files("src/game/entity/components/faction.ixx")
    add_files("src/game/entity/components/projectile/projectile_manifold.ixx")
    add_files("src/game/entity/components/chamber/damage_grid.ixx")
    add_files("src/game/entity/system/physics_system.ixx")
    add_files("src/game/entity/system/projectile_system.ixx")
    add_files("src/game/entity/misc/aiming.ixx")
    add_files("tests/physics_ecs_test/main.cpp")
target_end()


