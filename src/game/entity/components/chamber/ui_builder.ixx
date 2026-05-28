//
// Created by Matrix on 2025/5/18.
//

export module mo_yanxi.game.ecs.component.chamber.ui_builder;

export import mo_yanxi.game.ecs.component.ui.builder;
export import mo_yanxi.game.ecs.component.manage;

namespace mo_yanxi::game::ecs{

	namespace chamber{
		export
		struct chamber_ui_builder : ui_builder{
			void build_hud(ui::list& where, const entity_ref& eref) const override;
		};
	}

	template <>
	struct component_custom_behavior<chamber::chamber_ui_builder> :
		component_custom_behavior_base<chamber::chamber_ui_builder, void>,
		ui_builder_behavior_base{
	};
}
