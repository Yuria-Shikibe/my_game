
export module mo_yanxi.game.ecs.component.damage;

import std;

namespace mo_yanxi::game::ecs{
	export
	struct damage {
		float direct{};

		[[nodiscard]] constexpr bool heal() const noexcept{
			return direct < 0;
		}
	};

	export
	struct damage_group {
		damage material_damage{};

		[[nodiscard]] constexpr float sum() const noexcept {
			return material_damage.direct;
		}

		[[nodiscard]] constexpr bool exhausted() const noexcept{
			return sum() <= 0.f;
		}

		constexpr float consume(const float amount) noexcept{
			if(amount <= 0.f || exhausted()){
				return 0.f;
			}

			const float consumed = std::min(amount, material_damage.direct);
			material_damage.direct -= consumed;
			return consumed;
		}
	};

	export
	struct static_hit_point{
		float max{100.f};
		float capability_from{0.25f};
		float capability_to{0.75f};
	};

	export
	struct hit_point : static_hit_point{
		float current{100.f};

		constexpr void reset() noexcept{
			current = max;
		}

		constexpr void reset_to(const float value) noexcept{
			max = value;
			current = value;
		}

		constexpr void set_invincible() noexcept{
			max = std::numeric_limits<float>::infinity();
			current = max;
		}

		[[nodiscard]] constexpr float factor() const noexcept{
			if(max <= 0.f){
				return 0.f;
			}
			return std::clamp(current / max, 0.f, 1.f);
		}

		[[nodiscard]] constexpr float get_capability_factor() const noexcept{
			const float width = capability_to - capability_from;
			if(width <= 0.f){
				return factor() >= capability_to ? 1.f : 0.f;
			}
			return std::clamp((factor() - capability_from) / width, 0.f, 1.f);
		}

		[[nodiscard]] constexpr float sum() const noexcept{
			return current;
		}

		[[nodiscard]] constexpr bool is_killed() const noexcept{
			return current <= 0.f;
		}

		explicit constexpr operator bool() const noexcept{
			return !is_killed();
		}

		constexpr float accept(const float amount) noexcept{
			if(amount <= 0.f || is_killed()){
				return 0.f;
			}
			const float applied = std::min(amount, current);
			current -= applied;
			return applied;
		}

		constexpr float heal(const float amount) noexcept{
			if(amount <= 0.f){
				return 0.f;
			}
			const float previous = current;
			current = std::clamp(current + amount, 0.f, max);
			return current - previous;
		}

		constexpr float cure_and_get_healed() noexcept{
			const float previous = current;
			current = max;
			return current - previous;
		}

		constexpr void cure() noexcept{
			current = max;
		}
	};

	export
	struct damage_event{
		float actual_damage{};
	};
}
