module;

#include <cassert>

export module mo_yanxi.game.ecs.system.chamber;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.component.chamber;

import std;

namespace mo_yanxi::game::ecs::system{

	export
	struct chamber{
		static void update_energy_state(ecs::chamber::chamber_manifold& grid, float dt){
			unsigned valid_sum{};
			grid.manager.sliced_each(
				[&, dt](
				ecs::chamber::building_data& data, ecs::chamber::power_generator_building_tag){
					const auto valid = data.update_energy_state(dt);
					assert(valid.power >= 0);
					valid_sum += valid.power;
					if(valid.changed){
						grid.notify_grid_power_state_maybe_changed();
					}
				});

			if(grid.check_power_state_changed()){
				grid.energy_allocator.update_allocation(valid_sum);
			}
			grid.manager.sliced_each(
				[dt](
				ecs::chamber::building_data& data, ecs::chamber::power_consumer_building_tag){
					data.update_energy_state(dt);
				});
		}

		void run(world::entity_top_world& top_world) const{
			top_world.component_manager.sliced_each([&](
				component_manager& manager,
				const chunk_meta& meta,
				ecs::chamber::chamber_manifold& grid,
				const mech_motion& motion
			){
				grid.update_transform(motion.trans);

				grid.manager.do_deferred();
				grid.manager.update_update_delta(manager.get_update_delta());

				grid.manager.sliced_each([&](
					ecs::chamber::building& building){
						building.update(top_world);
					});


				float dt = manager.get_update_delta();

				const bool inbound = grid.get_wrap_bound().overlap(top_world.graphic_context.viewport());

				grid.manager.sliced_each(
					[&, dt](
					ecs::chamber::building_data& data
				){
						// data.update_energy_state(dt);

						if(inbound)
							for(auto&& damage_event : data.damage_events){
								top_world.graphic_context.create_efx().set_data(fx::effect_data{
										.style = fx::poly_outlined_out{
											.radius = {5, 25, math::interp::pow3Out},
											.stroke = {4},
											.palette = {
												{graphic::colors::aqua.to_light()},
												{graphic::colors::aqua.to_light()},
												{graphic::colors::clear},
												{
													graphic::colors::aqua.create_lerp(
														                      graphic::colors::white, .5f).
													                      to_light().set_a(.5f),
													graphic::colors::clear,
												}
											}
										},
										.trans = {
											grid.tile_coord_to_global(
												damage_event.local_coord.as<int>() + data.region().src)
										},
										.depth = 0,
										.duration = {60}
									});
							}

						switch(data.category){
						case ecs::chamber::building_data_category::general:{
							if(data.building_damage_take > 0){
								data.hit_point.accept(data.building_damage_take);
								if(data.energy_status)grid.notify_grid_power_state_maybe_changed();
							}

							if(data.hit_point.factor() < 1){
								const auto hp = data.get_tile_individual_max_hitpoint();

								//TODO do set instead of delta check?
								float healed{};
								for(auto& tile_state : data.tile_states){
									healed += math::approach_inplace_get_delta(tile_state.valid_hit_point, hp, .5 * dt);
								}

								if(healed > 0.f){
									data.hit_point.heal(healed);
								}else{
									data.hit_point.cure();
								}
							}else{

							}


							break;
						}

						case ecs::chamber::building_data_category::structural:{
							if(data.building_damage_take > 0){
								data.hit_point.accept(data.building_damage_take);
								grid.hit_point.accept(data.building_damage_take);
								if(grid.hit_point.is_killed()){
									manager.mark_expired(meta.id());
									grid.manager.mark_expired_all();
									grid.manager.do_deferred_destroy();
									return;
								}
							}

							if(data.hit_point.factor() < 1){
								const auto hp = data.get_tile_individual_max_hitpoint();

								//TODO do set instead of delta check?
								float healed{};
								for(auto& tile_state : data.tile_states){
									healed += math::approach_inplace_get_delta(tile_state.valid_hit_point, hp, .5 * dt);
								}

								if(healed > 0.f){
									data.hit_point.heal(healed);
								}else{
									healed = data.hit_point.cure_and_get_healed();
								}

								grid.hit_point.heal(healed);
							}

							break;
						}
							default: break;
						}


						data.clear_hit_events();
					});

				update_energy_state(grid, dt);
				if(manager.get_clock() & 4){
					grid.update_maneuver();
				}
			});
		}
	};
}
