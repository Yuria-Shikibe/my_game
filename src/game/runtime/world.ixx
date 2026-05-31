export module mo_yanxi.game.runtime.world;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.component.physics;
export import mo_yanxi.game.ecs.system.physics;
export import mo_yanxi.game.ecs.system.projectile;
export import mo_yanxi.game.runtime.draw.collision_shape_component;

import std;

namespace mo_yanxi::game{
export
	struct motion_system{
	void run(ecs::component_manager& manager) const{
		manager.each([](
			const ecs::component_manager& current_manager,
			const ecs::chunk_meta& meta,
			ecs::mech_motion& motion){
			const ecs::entity_id id = meta.id();
			if(id && id.try_get<ecs::physics_body>() != nullptr){
				return;
			}

			motion.apply_and_reset(current_manager.get_update_delta());
		});
	}
};

export
struct game_systems{
	motion_system motion{};
	ecs::system::projectile_system projectile{};
	ecs::system::physics_system physics{};

	void clear(){
		physics.clear();
	}
};

export
struct game_world{
	ecs::component_manager component_manager{};
	game_systems systems{};

	[[nodiscard]] ecs::component_manager& components() noexcept{
		return component_manager;
	}

	[[nodiscard]] const ecs::component_manager& components() const noexcept{
		return component_manager;
	}

	[[nodiscard]] ecs::system::physics_system& physics() noexcept{
		return systems.physics;
	}

	[[nodiscard]] const ecs::system::physics_system& physics() const noexcept{
		return systems.physics;
	}

	void begin_step(const float step_seconds) noexcept{
		component_manager.update_update_delta(step_seconds);
	}

	void run_systems(){
		component_manager.commit();
		systems.motion.run(component_manager);
		systems.projectile.pre_step(component_manager);
		systems.physics.step(component_manager);
		systems.projectile.resolve_hits(component_manager, systems.physics);
		component_manager.commit();
	}

	void step(const float step_seconds){
		begin_step(step_seconds);
		run_systems();
	}

	void clear_runtime_caches(){
		systems.clear();
	}
};
}
