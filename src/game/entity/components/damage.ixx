
export module mo_yanxi.game.ecs.component.damage;

import std;

namespace mo_yanxi::game::ecs{

	struct Health{
		// float hull{};
		// float hull{};
		// float hull{};
	};

	struct hit_point{
	private:
		float last{};

	public:
		float hull{};

		constexpr void set_invincible() noexcept{
			hull = std::numeric_limits<float>::infinity();
		}

		[[nodiscard]] constexpr float sum() const noexcept{
			return hull;
		}

		explicit constexpr operator bool() const noexcept{
			return hull >= 0;
		}

		constexpr void reset_to(const float val) noexcept{
			hull = val;
		}
	};


	export
	struct damage {
		float direct;
		// float pierceForce;

		// float splashRadius;
		// float splashAngle;

		// [[nodiscard]] constexpr bool isSplashes() const noexcept{
		// 	return splashRadius > 0;
		// }

		[[nodiscard]] constexpr bool heal() const noexcept{
			return direct < 0;
		}
	};

	export
	struct damage_group {
		damage material_damage; //Maybe basic damage
		// damage field_damage; //Maybe real damage
		// damage emp_damage; //emp damage

		[[nodiscard]] constexpr float sum() const noexcept {
			return material_damage.direct;// + field_damage.direct;
		}
	};
}
