//
// Created by Matrix on 2025/5/7.
//

export module mo_yanxi.game.ecs.component.ui.builder;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.ui.primitives;
export import mo_yanxi.ui.elem.table;
export import mo_yanxi.ui.elem.list;
import mo_yanxi.ui.action.generic;

import std;

namespace mo_yanxi::game::ecs{
	export struct entity_info_table;

	struct entity_info_table : ::mo_yanxi::ui::table{
	protected:
		entity_ref ref{};

	public:

		[[nodiscard]] entity_info_table(ui::scene* scene, group* group)
			: table(scene, group){

		}

		[[nodiscard]] entity_info_table(ui::scene* scene, group* group, entity_ref ref)
			: table(scene, group), ref(std::move(ref)){

		}

		void update(float delta_in_ticks) override{
			if(ref.drop_if_expired()){
				push_action<ui::action::alpha_action>(15, 0);
				push_action<ui::action::remove_action>();
			}
			table::update(delta_in_ticks);
		}
	};

	export
	struct ui_builder{
		virtual ~ui_builder() = default;

		/**
		 * @return hdl if no table is build, or empty handle if hdl is used
		 */
		virtual void build_hud(ui::list& where, const entity_ref& eref) const {
		}
	};

	export struct ui_builder_behavior_base{
		using base_types = ui_builder;
	};
}
