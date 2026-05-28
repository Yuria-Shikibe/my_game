//
// Created by Matrix on 2025/4/22.
//

export module mo_yanxi.game.ecs.component.projectile.manifold;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.component.damage;

import mo_yanxi.graphic.trail;
import mo_yanxi.graphic.color;
import mo_yanxi.math;
import mo_yanxi.math.timed;


namespace mo_yanxi::game::ecs{
	export struct projectile_manifold{
		entity_id owner{};
		damage_group current_damage_group{};
		damage_group max_damage_group{};

		math::timed duration{};

		void set_damage(const damage_group& dmg){
			max_damage_group = current_damage_group = dmg;
		}
	};
}

