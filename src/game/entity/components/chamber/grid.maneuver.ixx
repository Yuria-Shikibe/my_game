//
// Created by Matrix on 2025/7/23.
//

export module mo_yanxi.game.ecs.component.chamber:grid_maneuver;

import mo_yanxi.math;
import mo_yanxi.math.vector2;

namespace mo_yanxi::game::ecs::chamber{

	export struct chamber_manifold;
	export struct building_data;

	//TODO make it independent component

	export
	struct maneuver_component{
		float force_longitudinal;
		float force_transverse;

		float torque;
		/**
		 * @brief Torque always provide given value, no matter where the component is.
		 */
		float torque_absolute;

		float boost;

		constexpr maneuver_component operator*(float value) const noexcept{
			return {
				.force_longitudinal = force_longitudinal * value,
				.force_transverse = force_transverse * value,
				.torque = torque * value,
				.torque_absolute = torque_absolute * value,
				.boost = boost * value
			};
		}
	};

	export
	struct maneuver_subsystem{
	private:
		float force_longitudinal_{};
		float force_transverse_{};
		float torque_{};
		float boost_{};
	public:
		[[nodiscard]] float get_force_at(float yaw_angle_in_rad) const noexcept{
			auto [cos, sin] = math::cos_sin(yaw_angle_in_rad);
			return math::abs(cos) * force_longitudinal_ + math::abs(sin) * force_transverse_;
		}

		[[nodiscard]] float force_longitudinal() const noexcept{
			return force_longitudinal_;
		}

		[[nodiscard]] float force_transverse() const noexcept{
			return force_transverse_;
		}

		[[nodiscard]] float torque() const noexcept{
			return torque_;
		}

		void update(const chamber_manifold& manifold) noexcept;

		void append(const maneuver_component& component, const math::vec2 pos) noexcept{
			force_longitudinal_ += component.force_longitudinal;
			force_transverse_ += component.force_transverse;
			torque_ += component.torque_absolute + component.torque * pos.length();
			boost_ += component.boost;
		}
	};
}