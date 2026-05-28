export module mo_yanxi.game.physics.collision_filter;

import std;

namespace mo_yanxi::game::physics{
	export using collision_channel_mask = std::uint64_t;

	export
	struct collision_filter{
		collision_channel_mask category{1};
		collision_channel_mask mask{std::numeric_limits<collision_channel_mask>::max()};
		std::int32_t group{};
		bool sensor{};

		[[nodiscard]] constexpr bool can_collide_with(const collision_filter& other) const noexcept{
			if(group != 0 && group == other.group){
				return group > 0;
			}

			return (category & other.mask) != 0 && (other.category & mask) != 0;
		}

		[[nodiscard]] constexpr bool should_solve_with(const collision_filter& other) const noexcept{
			return can_collide_with(other) && !sensor && !other.sensor;
		}
	};
}
