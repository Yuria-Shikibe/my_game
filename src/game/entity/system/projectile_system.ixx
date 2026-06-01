export module mo_yanxi.game.ecs.system.projectile;

export import mo_yanxi.game.ecs.component.chamber.damage_grid;
export import mo_yanxi.game.ecs.component.damage;
export import mo_yanxi.game.ecs.component.faction;
export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.component.projectile.manifold;
export import mo_yanxi.game.ecs.system.physics;

import std;

namespace mo_yanxi::game::ecs::system{
	export
	struct projectile_system_statistics{
		std::uint64_t contact_event_count{};
		std::uint64_t projectile_event_count{};
		std::uint64_t ignored_hit_count{};
		std::uint64_t simple_hit_attempt_count{};
		std::uint64_t chamber_hit_attempt_count{};
		std::uint64_t applied_hit_count{};
		std::uint64_t chamber_tile_hit_count{};
		std::uint64_t chamber_damaged_tile_count{};
		std::uint64_t chamber_hit_time_ns{};
	};

	export
	struct projectile_system{
	private:
		struct hit_application_result{
			float actual_damage{};
		};

		projectile_system_statistics last_statistics_{};

		[[nodiscard]] static projectile_state* projectile_of(const physics_contact_event& event) noexcept{
			projectile_state* subject = event.subject ? event.subject.try_get<projectile_state>() : nullptr;
			projectile_state* object = event.object ? event.object.try_get<projectile_state>() : nullptr;
			if(subject && object){
				return nullptr;
			}
			return subject ? subject : object;
		}

		[[nodiscard]] static entity_id projectile_id_of(const physics_contact_event& event) noexcept{
			if(event.subject && event.subject.try_get<projectile_state>()){
				return event.subject;
			}
			if(event.object && event.object.try_get<projectile_state>()){
				return event.object;
			}
			return {};
		}

		[[nodiscard]] static bool same_faction(const projectile_state& projectile, const entity_id target) noexcept{
			if(!target || projectile.faction_id == 0){
				return false;
			}
			const faction_data* faction = target.try_get<faction_data>();
			return faction && faction->id != 0 && faction->id == projectile.faction_id;
		}

		[[nodiscard]] static bool should_ignore_hit(
			const projectile_state& projectile,
			const entity_id projectile_id,
			const entity_id target,
			const physics_contact_endpoint_snapshot& target_endpoint) noexcept{
			if(!projectile_id || !target || projectile_id == target){
				return true;
			}
			if(projectile.owner_id() == target){
				return true;
			}
			if(target_endpoint.filter.sensor){
				return true;
			}
			return projectile_system::same_faction(projectile, target);
		}

		[[nodiscard]] static hit_application_result apply_simple_hit(projectile_state& projectile, const entity_id target) noexcept{
			if(!target){
				return {};
			}
			hit_point* hp = target.try_get<hit_point>();
			if(!hp){
				return {};
			}

			const float actual_damage = hp->accept(projectile.damage.sum());
			projectile.damage.consume(actual_damage);
			return {.actual_damage = actual_damage};
		}

		[[nodiscard]] static hit_application_result apply_hit(
			projectile_state& projectile,
			const entity_id projectile_id,
			const entity_id target,
			const physics_contact_event& event,
			projectile_system_statistics& statistics){
			if(target){
				if(auto* chamber = target.try_get<chamber::chamber_manifold>()){
					++statistics.chamber_hit_attempt_count;
					const collider* projectile_collider = projectile_id ? projectile_id.try_get<collider>() : nullptr;
					if(!projectile_collider){
						return {};
					}

					chamber::projectile_hit_context context{
						projectile_id,
						target,
						projectile_collider->shape,
						event.endpoint_for(projectile_id),
						event.other_endpoint(projectile_id),
						event.point,
						event.normal,
						projectile.damage
					};
					const auto begin = std::chrono::steady_clock::now();
					const chamber::projectile_hit_result result =
						chamber->execute(chamber::projectile_hit_command{context});
					const auto end = std::chrono::steady_clock::now();
					statistics.chamber_hit_time_ns += static_cast<std::uint64_t>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
					statistics.chamber_tile_hit_count += result.tile_hit_count;
					statistics.chamber_damaged_tile_count += result.damaged_tile_count;
					return {.actual_damage = result.actual_damage};
				}
			}
			++statistics.simple_hit_attempt_count;
			return projectile_system::apply_simple_hit(projectile, target);
		}

	public:
		void pre_step(component_manager& manager) const{
			manager.each([](
				component_manager& current_manager,
				const chunk_meta& meta,
				projectile_state& projectile){
				const entity_id id = meta.id();
				if(!id || id.is_expired()){
					return;
				}
				if(projectile.tick_lifetime(current_manager.get_update_delta())){
					current_manager.destroy(id);
				}
			});
		}

		void resolve_hits(component_manager& manager, const physics_system& physics){
			projectile_system_statistics statistics{};
			for(const physics_contact_event& event : physics.contact_events()){
				++statistics.contact_event_count;
				if(event.phase == physics_contact_phase::end){
					continue;
				}

				projectile_state* projectile = projectile_system::projectile_of(event);
				const entity_id projectile_id = projectile_system::projectile_id_of(event);
				if(!projectile || !projectile_id || projectile_id.is_expired()){
					continue;
				}
				++statistics.projectile_event_count;

				const entity_id target = event.other(projectile_id);
				const auto& target_endpoint = event.other_endpoint(projectile_id);
				if(projectile_system::should_ignore_hit(*projectile, projectile_id, target, target_endpoint)){
					++statistics.ignored_hit_count;
					continue;
				}

				const hit_application_result hit =
					projectile_system::apply_hit(*projectile, projectile_id, target, event, statistics);
				if(hit.actual_damage <= 0.f){
					continue;
				}
				++statistics.applied_hit_count;

				if(projectile->expired_on_hit && projectile->damage_exhausted()){
					manager.destroy(projectile_id);
				}
			}
			last_statistics_ = statistics;
		}

		[[nodiscard]] projectile_system_statistics last_statistics() const noexcept{
			return last_statistics_;
		}
	};
}
