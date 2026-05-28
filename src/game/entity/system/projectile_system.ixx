module;


export module mo_yanxi.game.ecs.system.projectile;

export import mo_yanxi.game.ecs.component.manage;

export import mo_yanxi.game.ecs.component.projectile.drawer;
export import mo_yanxi.game.ecs.component.projectile.manifold;
export import mo_yanxi.game.ecs.component.manifold;

import std;

namespace mo_yanxi::game::ecs::system{
	export
	struct projectile{

	private:
		static void expire_projectile(
			component_manager& manager,
			world::graphic_context& graphic_context,
			const chunk_meta& meta,
			drawer::part_pos_transform trail_trans,
			const mech_motion& motion,
			projectile_drawer& drawer
			){
			manager.mark_expired(meta.id());
			drawer.trail_style.dump_to_effect(graphic_context.create_efx(), std::move(drawer.trail), trail_trans, motion.vel.vec.length());

		}

	public:
		void run(component_manager& manager, world::graphic_context& graphic_context){
			manager.sliced_each([&](
				component_manager& manager,
				const chunk_meta& meta,
				const manifold& manifold,
				const mech_motion& motion,
				projectile_manifold& projectile_manifold,
				projectile_drawer& drawer){
					const auto trail_trans = drawer.trail_style.trans | motion.trans;
					drawer.trail.update(manager.get_update_delta(), trail_trans.vec);

					drawer.clip = manifold.hitbox.max_wrap_bound().expand_by(drawer.trail.get_bound());

					if(projectile_manifold.duration.update_and_get(manager.get_update_delta())){
						expire_projectile(manager, graphic_context, meta, trail_trans, motion, drawer);
						return;
					}

					if(projectile_manifold.current_damage_group.sum() <= 0.f){
						expire_projectile(manager, graphic_context, meta, trail_trans, motion, drawer);
						return;
					}
				});
		}
	};
}
