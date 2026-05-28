//
// Created by Matrix on 2025/5/12.
//

export module mo_yanxi.game.meta.projectile;

export import mo_yanxi.game.meta.hitbox;
export import mo_yanxi.game.meta.graphic;
export import mo_yanxi.game.ecs.component.physical_property;
export import mo_yanxi.game.ecs.component.damage;
export import mo_yanxi.game.ecs.component.drawer;

import mo_yanxi.flat_seq_map;

import std;

namespace mo_yanxi::game::meta{

	export enum struct effect_usage : std::uint_fast32_t{
		trail,
		hit,
		shoot,
		despawn
	};

	export
	struct projectile{
		meta::hitbox hitbox{};
		ecs::physical_rigid rigid{};
		ecs::damage_group damage{};

		float lifetime{};
		float initial_speed{};


		//style section

		ecs::drawer::local_drawer drawer{};
		meta::trail_meta_style trail_style{};

		// graphic::color_gradient palette{};
		flat_seq_map<effect_usage, fx::effect_meta> shoot_effect{};
	};

}