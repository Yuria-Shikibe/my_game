//
// Created by Matrix on 2025/4/22.
//

export module mo_yanxi.game.ecs.component.projectile.manifold;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.component.damage;

import std;

namespace mo_yanxi::game::ecs{
	export struct projectile_state{
		entity_pin owner{};
		std::uint32_t faction_id{};
		damage_group damage{};
		damage_group max_damage{};
		float remaining_lifetime{1.f};
		bool expired_on_hit{true};

		[[nodiscard]] entity_id owner_id() const noexcept{
			return owner.raw_id();
		}

		void set_damage(const damage_group& dmg){
			max_damage = damage = dmg;
		}

		[[nodiscard]] bool damage_exhausted() const noexcept{
			return damage.exhausted();
		}

		[[nodiscard]] bool tick_lifetime(const float delta_seconds) noexcept{
			if(remaining_lifetime == std::numeric_limits<float>::infinity()){
				return false;
			}
			remaining_lifetime -= delta_seconds;
			return remaining_lifetime <= 0.f;
		}
	};

	export using projectile_manifold = projectile_state;
}

