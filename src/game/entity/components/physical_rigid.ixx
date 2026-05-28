module;

#include <cassert>

export module mo_yanxi.game.ecs.component.physical_property;

export import mo_yanxi.math.vector2;
export import mo_yanxi.math.trans2;
import std;

export namespace mo_yanxi::game::ecs{
	using update_tick = float;
	/**
	 * @brief MechanicalMotionProperty
	 */
	struct mech_motion{
		math::uniform_trans2 trans{};
		math::uniform_trans2 vel{};
		math::uniform_trans2 accel{};

		float depth{};

		constexpr void apply(const update_tick tick) noexcept{
			vel += accel * tick;
			trans += vel * tick;
		}

		constexpr void apply_and_reset(const update_tick tick) noexcept{
			apply(tick);
			accel = {};
		}

		[[nodiscard]] float overhead(float maximum_accel) const noexcept{
			return vel.vec.length2() / (maximum_accel * 2);
		}

		[[nodiscard]] math::trans2 get_stop_accel(float maxAccel, float maxAngularAccel) const noexcept{
			auto curVel = vel.vec.length();
			auto curRot = vel.rot.radians();
			auto vA = curVel > 0 ? vel.vec * (-maxAccel * std::tanh(curVel / 20) / curVel) : math::vec2{};
			auto vW = curRot > 0 ? -maxAngularAccel * std::tanh(curRot * 4) : 0;
			return {vA, vW};
		}

		void apply_drag(const update_tick tick, float drag = 0.05f){
			vel.vec.lerp_inplace({}, drag * tick);
			vel.rot.slerp(0, drag * tick);
		}

		[[nodiscard]] constexpr math::vec2 pos() const noexcept{
			return trans.vec;
		}

		[[nodiscard]] constexpr bool stopped() const noexcept{
			return vel.vec.is_zero(0.005f);
		}

		void check() const noexcept{
			assert(!trans.vec.is_NaN());
			assert(!vel.vec.is_NaN());
			assert(!accel.vec.is_NaN());

			assert(!std::isnan(static_cast<float>(trans.rot)));
			assert(!std::isnan(static_cast<float>(vel.rot)));
			assert(!std::isnan(static_cast<float>(accel.rot)));
		}

		[[nodiscard]] constexpr math::vec2 vel_at(const math::vec2 dst_to_self) const noexcept{
			return vel.vec - dst_to_self.cross(math::clamp_range(static_cast<float>(vel.rot), 15.f));
		}
	};
	
	struct physical_rigid{
		float inertial_mass = 1000;
		// float rotational_inertia_scale = 1 / 12.0f;

		/** @brief [0, 1]*/
		float friction_coefficient = 0.35f;
		float restitution = 0.1f;


		/** @brief Used For Force Correction*/
		float collide_force_scale = 1.0f;

		float drag{0.025f};

		double rotational_inertia = -1.;
	};
}
