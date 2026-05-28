module;

export module mo_yanxi.game.ecs.entitiy_decleration;

export import mo_yanxi.game.ecs.component.manifold;
export import mo_yanxi.game.ecs.component.chamber;
export import mo_yanxi.game.ecs.component.chamber.ui_builder;
export import mo_yanxi.game.ecs.component.projectile.manifold;
export import mo_yanxi.game.ecs.component.hitbox;
export import mo_yanxi.game.ecs.component.faction;
export import mo_yanxi.game.ecs.component.command;

export import mo_yanxi.game.ecs.component.projectile.drawer;
export import mo_yanxi.game.ecs.component.projectile.ui_builder;
export import mo_yanxi.game.ecs.component.drawer;

import std;

namespace mo_yanxi::game::ecs{
	namespace desc{
		export using grid_entity = std::tuple<
			chunk_meta,
			mech_motion,
			manifold,
			physical_rigid,
			faction_data,
			move_command,
			chamber::chamber_manifold,
			chamber::chamber_ui_builder
		>;

		export using projectile = std::tuple<
			chunk_meta,
			mech_motion,
			manifold,
			physical_rigid,
			faction_data,
			projectile_manifold,
			projectile_drawer,
			projectile_ui_builder

		>;

		using View = tuple_to_comp_t<projectile>;

	}

	export
	template <>
	struct ecs::archetype_custom_behavior<desc::grid_entity> : archetype_custom_behavior_base<desc::grid_entity>{
		static constexpr unsigned expire_counter = 1;

		static void on_terminate(value_type& comps){
			// std::println(std::cerr, "grid destoried");
		}

		static void on_init(value_type& comps){
			auto [motion, mf] = get_unwrap_of<mech_motion, manifold>(comps);
			mf.hitbox.set_trans_unchecked(motion.trans);
			mf.hitbox.update(motion.trans);
			comps.chamber::chamber_manifold::update_transform(motion.trans);

			comps.hit_point = hit_point{
				static_hit_point{
					.max = 10000,
				},
				10000,
			};

			if(comps.get<physical_rigid>().rotational_inertia < 0){
				comps.get<physical_rigid>().rotational_inertia = mf.hitbox.get_rotational_inertia(comps.get<physical_rigid>().inertial_mass);
			}
			// comps.faction = faction_0;

			// auto dump_ = dump(comps);

		}
	};

	export
	template <>
	struct ecs::archetype_custom_behavior<desc::projectile> : archetype_custom_behavior_base<desc::projectile>{
		static void on_init(value_type& comps){


			auto [motion, mf, dmg] = get_unwrap_of<mech_motion, manifold, projectile_manifold>(comps);
			mf.hitbox.set_trans_unchecked(motion.trans);
			mf.hitbox.update(motion.trans);
			mf.collider = projectile_collider{};

			// dmg.max_damage_group.material_damage.direct = 3000;
			// dmg.current_damage_group = dmg.max_damage_group;

			if(!comps.faction)comps.faction = faction_1;

			auto [drawer] = get_unwrap_of<projectile_drawer>(comps);

			if(drawer.drawer.drawer.index() == 0){
				drawer::rect_drawer d{
					.extent = {mf.hitbox[0].box.get_identity().size},
					.color_scl = graphic::colors::aqua.to_light(),
				};
				drawer.drawer = d;
			}

			auto dump_ = dump(comps);

		}
	};
}
