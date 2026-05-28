//
// Created by Matrix on 2025/5/17.
//

export module mo_yanxi.game.ecs.component.chamber.radar;

export import mo_yanxi.game.ecs.component.chamber;
export import mo_yanxi.math;


namespace mo_yanxi::game::ecs{

	namespace chamber{
		export
		struct radar_meta{
			math::trans2z transform{};

			math::range targeting_range_radius{};
			math::range targeting_range_angular{-math::pi, math::pi};
			float reload_duration{};
		};

		export
		struct radar_build : building{
			radar_meta meta{};

			[[nodiscard]] radar_build() = default;

			[[nodiscard]] explicit(false) radar_build(const radar_meta& meta)
				: meta(meta){
			}


			float reload{};
			bool active{true};

			void draw_hud_on_building_selection(graphic::renderer_ui_ref renderer) const override;

			void build_hud(ui::list& where, const entity_ref& eref) const override;

			void update(world::entity_top_world& top_world) override;

		};
	}

	template <>
	struct component_custom_behavior<chamber::radar_build> :
		component_custom_behavior_base<chamber::radar_build>,
		chamber::building_trait_base<>{
	};
}

