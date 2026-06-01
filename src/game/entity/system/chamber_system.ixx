export module mo_yanxi.game.ecs.system.chamber;

export import mo_yanxi.game.ecs.component.chamber.damage_grid;
export import mo_yanxi.game.ecs.component.manage;

namespace mo_yanxi::game::ecs::system{
	export
	struct chamber_system{
		void pre_step(component_manager& manager) const{
			manager.each([](
				component_manager& current_manager,
				chamber::chamber_manifold& chamber){
				(void)chamber.execute_pending_commands();
				chamber.execute(chamber::update_buildings_command{
					.delta_seconds = current_manager.get_update_delta()
				});
			});
			manager.each([](
				chamber::chamber_manifold& chamber,
				collider& collider){
				if(chamber.collider_needs_synchronization()){
					chamber::synchronize_chamber_collider(collider, chamber);
					chamber.mark_collider_synchronized();
				}
			});
		}

		void post_projectile_step(component_manager& manager) const{
			manager.each([](
				component_manager& current_manager,
				const chunk_meta& meta,
				chamber::chamber_manifold& chamber){
				const chamber::projectile_hit_result result =
					chamber.execute(chamber::settle_pending_damage_command{});
				if(result.target_destroyed && meta.id()){
					current_manager.destroy(meta.id());
					return;
				}

				if(meta.id()){
					if(collider* chamber_collider = meta.id().try_get<collider>()){
						if(chamber.collider_needs_synchronization()){
							chamber::synchronize_chamber_collider(*chamber_collider, chamber);
							chamber.mark_collider_synchronized();
						}
					}
				}
			});
		}
	};
}
