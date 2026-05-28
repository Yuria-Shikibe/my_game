//
// Created by Matrix on 2025/5/9.
//

export module mo_yanxi.game.ecs.component.projectile.ui_builder;

export import mo_yanxi.game.ecs.component.ui.builder;
export import mo_yanxi.game.ecs.component.manage;

namespace mo_yanxi::game::ecs{
	export
	struct projectile_ui_builder : ui_builder{
		void build_hud(ui::list& where, const entity_ref& eref) const override;
	};

	template <>
	struct component_custom_behavior<projectile_ui_builder> : component_custom_behavior_base<projectile_ui_builder, void>, ui_builder_behavior_base{
	};
}
