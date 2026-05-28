//
// Created by Matrix on 2025/7/24.
//

export module mo_yanxi.game.ecs.component.chamber.thurster;

export import mo_yanxi.game.ecs.component.chamber;
 import mo_yanxi.game.ui.building_pane;
import std;

namespace mo_yanxi::game::ecs{
	namespace chamber{
		struct meta{
			maneuver_component ideal_maneuver{};
		};

		export
		struct thurster_build : building, maneuver_component{
			meta meta{};

			void update(world::entity_top_world& top_world) override{
				this->maneuver_component::operator=(meta.ideal_maneuver * data().get_efficiency());
			}

			void build_hud(ui::list& where, const entity_ref& eref) const override{
				where.emplace<ui::building_pane>(eref);
			}
		};
	}


	template <>
	struct component_custom_behavior<
			chamber::thurster_build> : component_custom_behavior_base<chamber::thurster_build>,
											  chamber::building_trait_base<chamber::maneuver_component>{
	};

}
