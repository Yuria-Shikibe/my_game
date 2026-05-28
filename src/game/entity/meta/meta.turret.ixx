module;

#include "mo_yanxi/adapted_attributes.hpp"
#include "plf_hive.h"

export module mo_yanxi.game.meta.turret;

export import mo_yanxi.game.meta.projectile;

import mo_yanxi.math;
import mo_yanxi.array_stack;
import mo_yanxi.game.ecs.component.drawer;
import mo_yanxi.graphic.image_atlas.util;
import mo_yanxi.graphic.draw.func;

import std;

namespace mo_yanxi::game::meta::turret{
	export
	struct salvo_mode{
		unsigned count{1};
		float spacing{};

		// [[nodiscard]] float duration() const noexcept{
		// 	return count * spacing;
		// }
	};

	export
	struct burst_mode{
		unsigned count{1};
		float spacing{};

		float angular_spacing{};
		float horizontal_spacing{};

		[[nodiscard]] float duration() const noexcept{
			return count * spacing;
		}
	};

	export
	struct shoot_offset{
		math::vec2 trunk{};
		std::array<math::trans2, 3> branch{};
		unsigned count{1};

		FORCE_INLINE constexpr math::trans2 operator[](unsigned idx) const noexcept{
			assert(idx < count);
			return math::trans2{branch[idx]} += trunk;
		}
	};

	export
	struct scaling{
		float velocity_scl{1};
		float lifetime_scl{1};
	};

	export
	struct shoot_index{
		unsigned salvo_index;
		unsigned burst_index;
	};



	export
	enum struct turret_state : std::uint8_t{
		reloading,
		shoot_staging,
		shooting
	};

	export
	struct turret_param{
		float reload_progress;
		float shoot_progress;

		turret_state state;
	};

	export
	template <typename T, T def_value = T{}>
	struct turret_param_expr{
		using return_type = T;
		static constexpr return_type default_value = def_value;
	private:
		return_type (*expr)(turret_param) noexcept = nullptr;

	public:
		[[nodiscard]] turret_param_expr() = default;

		template <std::convertible_to<decltype(expr)> Fn>
			requires (std::is_nothrow_invocable_r_v<return_type, Fn, turret_param>)
		[[nodiscard]] explicit(false) turret_param_expr(Fn fn)
			: expr(fn){
		}

		return_type operator()(turret_param param) const noexcept{
			return expr ? expr(param) : default_value;
		}
	};

	export
	struct shoot_type{
		projectile* projectile{};

		shoot_offset offset{};


		float angular_inaccuracy{};
		scaling inaccuracy{};
		scaling scaling{};


		burst_mode burst{};
		salvo_mode salvo{};

		[[nodiscard]] unsigned get_total_shoots() const noexcept{
			return burst.count * salvo.count;
		}


		[[nodiscard]] float get_shoot_duration() const noexcept{
			// const auto d1 = salvo.duration();
			const auto d2 = salvo.count * burst.duration();

			return (d2 > 0 ? d2 : 1);
		}


		[[nodiscard]] shoot_index get_current_shoot_index(float duration) const noexcept{
			duration += salvo.spacing;
			float unitDuration = salvo.spacing + burst.duration();
			shoot_index idx;
			idx.salvo_index = static_cast<unsigned>(duration / unitDuration);

			auto rem = std::fdim(math::mod(duration, unitDuration), salvo.spacing);
			idx.burst_index = rem == 0 ? 0 : burst.spacing == 0 ? burst.count : static_cast<unsigned>(rem / burst.spacing);
			return idx;
		}

		[[nodiscard]] shoot_index get_shoot_index(unsigned total_shoot_index) const noexcept{
			return {total_shoot_index / burst.count, total_shoot_index % burst.count};
		}

		math::trans2 operator[](shoot_index idx, const unsigned barrelIdx) const noexcept{
			auto t0 = offset[barrelIdx];
			t0.rot += idx.burst_index * burst.angular_spacing - (burst.count * burst.angular_spacing / 2);
			t0.vec.y += idx.burst_index * burst.horizontal_spacing - (burst.count * burst.horizontal_spacing / 2);
			return t0;
		}
	};

	export
	struct turret_base_component{
		graphic::cached_image_region base_image{};
		math::vec2 draw_extent{};

		void set_extent_by_scale(const math::vec2 scale = {1, 1}) noexcept{
			assert(base_image != nullptr);
			draw_extent = base_image->uv.size.as<float>() * scale;
		}

		FORCE_INLINE void draw_impl(
			ecs::drawer::draw_acquirer& acquirer, ecs::drawer::part_transform current_trans) const{
			using namespace graphic;

			if(base_image){
				acquirer.proj.depth = current_trans.z_offset;
				acquirer << base_image.get_cache();
				draw::fill::rect(acquirer.get(), current_trans, draw_extent);
			}
		}

	};


	export
	struct turret_light_component{
		graphic::cached_image_region image{};
		graphic::color color{};
		turret_param_expr<float, 1.f> opacity_expr{};

		FORCE_INLINE void draw_impl(
			ecs::drawer::draw_acquirer& acquirer,
			ecs::drawer::part_transform current_trans,
			math::vec2 draw_extent,
			const turret_param& param
			) const{
			using namespace graphic;

			const auto opacity = opacity_expr(param);
			if(image && opacity > 0){
				acquirer.proj.depth = current_trans.z_offset;
				acquirer << image.get_cache();
				draw::fill::rect(acquirer.get(), current_trans, draw_extent, color.to_light().set_a(opacity));
			}
		}
	};

	export
	struct turret_drawer{
		ecs::drawer::part_transform transform{};
		turret_param_expr<math::trans2> sub_transform{};

		turret_base_component base_component{};
		turret_light_component light_component{};
		turret_light_component heat_component{};

		//TODO use small vector?
		std::vector<turret_drawer> sub_components{};

		FORCE_INLINE void draw(
			const world::graphic_context& draw_ctx,
			ecs::drawer::part_transform current_trans,
			const turret_param& param
		) const{
			current_trans = ecs::drawer::part_transform{sub_transform(param), 0} | transform | current_trans;
			ecs::drawer::draw_acquirer acquirer{draw_ctx.renderer().batch};

			base_component.draw_impl(acquirer, current_trans);
			light_component.draw_impl(acquirer, current_trans, base_component.draw_extent, param);
			heat_component.draw_impl(acquirer, current_trans, base_component.draw_extent, param);

			for (const auto & sub_component : sub_components){
				sub_component.draw(draw_ctx, current_trans, param);
			}
		}
	};

	export
	struct turret_body{
		std::string_view name{};
		math::section<short> energy_section{};

		math::range range{0, 2000};

		float rotational_inertia{50};
		float max_rotate_speed{std::numbers::pi / 60. / 2.f};
		float shoot_rotate_power_scale{1};

		float reload_duration{90};
		// unsigned max_reload_storage{};

		// bool skip_first_salvo_reload{};
		bool actively_reload{};
		// bool reserve_storage_if_power_off{};
		// bool reload_while_shooting{};

		shoot_type shoot_type{};

		const turret_drawer* drawer{};
	};
}