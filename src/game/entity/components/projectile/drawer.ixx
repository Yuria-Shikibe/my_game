//
// Created by Matrix on 2025/4/22.
//

export module mo_yanxi.game.ecs.component.projectile.drawer;

export import mo_yanxi.game.ecs.component.damage;

export import mo_yanxi.game.ecs.component.drawer;
import mo_yanxi.game.meta.graphic;
import mo_yanxi.graphic.trail;
import mo_yanxi.math;

namespace mo_yanxi::game::ecs{
	export struct projectile_drawer : drawer::entity_drawer{
		graphic::uniformed_trail trail{};

		meta::trail_style trail_style{};

		drawer::local_drawer drawer{};

		void set_trail_style(const meta::trail_meta_style& trail_style){
			this->trail_style = static_cast<const meta::trail_style&>(trail_style);
			trail = decltype(trail){trail_style.length};
		}

		void draw(const world::graphic_context& draw_ctx) const override;
	};


	template <>
	struct component_custom_behavior<projectile_drawer> : component_custom_behavior_base<projectile_drawer, void>{
		using base_types = drawer::entity_drawer;
	};
}

