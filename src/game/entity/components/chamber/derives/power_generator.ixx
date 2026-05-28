//
// Created by Matrix on 2025/7/17.
//

export module mo_yanxi.game.ecs.component.chamber.power_generator;

export import mo_yanxi.game.ecs.component.chamber;

import std;

namespace mo_yanxi::game::ecs{

	namespace chamber{
		export
		struct power_generator_build : building{
			void build_hud(ui::list& where, const entity_ref& eref) const override;
		};
	}


	template <>
	struct component_custom_behavior<
			chamber::power_generator_build> : component_custom_behavior_base<chamber::power_generator_build>,
											  chamber::building_trait_base<>{
	};
}
