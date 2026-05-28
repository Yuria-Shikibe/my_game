//
// Created by Matrix on 2025/5/12.
//

export module mo_yanxi.game.meta.graphic;

export import mo_yanxi.graphic.color;
export import mo_yanxi.math.vector2;
export import mo_yanxi.math.trans2;
export import mo_yanxi.graphic.trail;
export import mo_yanxi.game.ecs.component.drawer;

import std;

namespace mo_yanxi::game::meta{
	export struct palette{
		graphic::color primary;
		graphic::color secondary;

		[[nodiscard]] constexpr graphic::color operator[](float lerpT) const noexcept{
			return primary.create_lerp(secondary, lerpT);
		}
	};

	export struct trail_style{
		float radius{};
		math::section<graphic::color> color{};
		ecs::drawer::part_pos_transform trans{};

		void dump_to_effect(
			fx::effect& effect,
			graphic::uniformed_trail&& trail,
			const ecs::drawer::part_pos_transform& trans,
			const float object_speed
			) const noexcept{
			effect.set_data(fx::effect_data{
				.style = fx::trail_effect{
					.trail = std::move(trail),
					.color = {
						.in = {color.from, color.from},
						.out = {color.to, color.to.copy().set_a(0)}
					}
				},
				.trans = {trans.vec, radius},
				.depth = trans.z_offset,
				.duration = {std::max<float>(30, trail.estimate_duration(object_speed))},
			});
		}
	};

	export struct trail_meta_style : trail_style{
		unsigned length{50};
	};
}
