module;

#include "mo_yanxi/adapted_attributes.hpp"

export module mo_yanxi.game.ecs.component.chamber.general;

import std;
namespace mo_yanxi::game::ecs::chamber{
	export constexpr inline int tile_size_integral = 32;
	export constexpr inline float tile_size = tile_size_integral;

	struct building_;

	export using build_ref = building_&;
	export using build_ptr = building_*;

	export using const_build_ref = const building_&;

	export
	struct energy_acquisition{
		unsigned maximum_count;
		unsigned minimum_count;
		float priority;

		[[nodiscard]] constexpr unsigned get_append_count() const noexcept{
			return maximum_count - minimum_count;
		}

		constexpr bool operator==(const energy_acquisition&) const noexcept = default;
	};

	export
	struct energy_status_update_result{
		int power;
		bool changed;
	};

	export
	struct energy_status{
		int power;
		float charge_duration;

		bool reserve_energy_if_power_off;
		bool reserve_charge_reload_if_power_off;
		bool safe_under_damage;

		FORCE_INLINE explicit operator bool() const noexcept{
			return power != 0;
		}

		[[nodiscard]] bool is_consumer() const noexcept{
			return power < 0;
		}
		[[nodiscard]] unsigned abs_power() const noexcept{
			return std::abs(power);
		}
	};

	export
	struct energy_dynamic_status{
		int power;
		float charge;

		[[nodiscard]] PURE_FN FORCE_INLINE float get_energy_factor(const energy_status& status) const noexcept{
			return static_cast<float>(power) / static_cast<float>(status.power);
		}

		FORCE_INLINE explicit operator bool() const noexcept{
			return power != 0;
		}

		FORCE_INLINE bool update(
		 	const energy_status& status,
		 	unsigned assigned_energy,
		 	float capabilityFactor,
		 	float update_delta_tick
		 	) noexcept{

			const int last = power;

			assert(capabilityFactor <= (1.f + std::numeric_limits<float>::epsilon()));
			assert(capabilityFactor >= 0);

			const int max_usable = static_cast<int>(std::trunc(static_cast<float>(status.power) * capabilityFactor));

			if(max_usable == 0){
				if(!status.reserve_charge_reload_if_power_off)charge = 0;
				if(!status.reserve_energy_if_power_off)power = 0;
			}

			const int abs_cur = std::abs(power);
			const int abs_max = max_usable > 0 ? max_usable : std::min<unsigned>(-max_usable, assigned_energy);

			assert(power * max_usable >= 0);
			assert(abs_cur <= std::abs(status.power));

			if(abs_cur >= abs_max){
				if(!status.reserve_energy_if_power_off)power = max_usable > 0 ? abs_max : -abs_max;
				if(!status.reserve_charge_reload_if_power_off)charge = 0;
			}else{
				if(charge < status.charge_duration){
					charge += update_delta_tick;
				}else{
					charge = 0;
					power += status.power > 0 ? 1 : -1;
				}
			}

			return power != last;
		}
	};

}