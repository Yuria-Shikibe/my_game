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
	struct projectile_system{
	private:
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

		static float apply_simple_hit(projectile_state& projectile, const entity_id target) noexcept{
			if(!target){
				return 0.f;
			}
			hit_point* hp = target.try_get<hit_point>();
			if(!hp){
				return 0.f;
			}

			const float actual_damage = hp->accept(projectile.damage.sum());
			projectile.damage.consume(actual_damage);
			return actual_damage;
		}

		static float apply_hit(
			projectile_state& projectile,
			const entity_id projectile_id,
			const entity_id target,
			const physics_contact_event& event){
			if(target){
				if(auto* chamber = target.try_get<chamber::chamber_manifold>()){
					const collider* projectile_collider = projectile_id ? projectile_id.try_get<collider>() : nullptr;
					if(!projectile_collider){
						return 0.f;
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
					return chamber::apply_projectile_hit(*chamber, context).actual_damage;
				}
			}
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

		void resolve_hits(component_manager& manager, const physics_system& physics) const{
			for(const physics_contact_event& event : physics.contact_events()){
				if(event.phase == physics_contact_phase::end){
					continue;
				}

				projectile_state* projectile = projectile_system::projectile_of(event);
				const entity_id projectile_id = projectile_system::projectile_id_of(event);
				if(!projectile || !projectile_id || projectile_id.is_expired()){
					continue;
				}

				const entity_id target = event.other(projectile_id);
				const auto& target_endpoint = event.other_endpoint(projectile_id);
				if(projectile_system::should_ignore_hit(*projectile, projectile_id, target, target_endpoint)){
					continue;
				}

				const float actual_damage = projectile_system::apply_hit(*projectile, projectile_id, target, event);
				if(actual_damage <= 0.f){
					continue;
				}

				if(projectile->expired_on_hit && projectile->damage_exhausted()){
					manager.destroy(projectile_id);
				}
			}
		}
	};
}
