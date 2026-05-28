//
// Created by Matrix on 2025/5/21.
//

export module mo_yanxi.game.ui.chamber_ui_elem;

export import mo_yanxi.game.ecs.component.chamber;
export import mo_yanxi.ui.elem.table;
export import mo_yanxi.game.ui.bars;

import std;

namespace mo_yanxi::game::ui{
	export using namespace mo_yanxi::ui;

	export
	struct build_tile_status_drawer{
		static constexpr float chunk_max_draw_size = 20;

	private:
		template <typename T = int>
		static float get_unit_size(math::vec2 self_extent, math::vector2<T> extent) noexcept{
			self_extent /= extent.template as<float>();
			auto min = math::min(self_extent.x, self_extent.y);
			return std::min(chunk_max_draw_size, min);
		}

	public:
		[[nodiscard]] constexpr static bool is_transposed(const math::isize2 size) noexcept{
			return size.x < size.y;
		}

		static math::vec2 get_required_extent(optional_mastering_extent size, math::isize2 region){
			if(region.x < region.y){
				region.swap_xy();
			}

			if(size.fully_dependent()){
				return region.scl(chunk_max_draw_size).as<float>();
			}else{
				math::vec2 sz;
				if(size.width_mastering()){
					sz = {size.potential_width(), size.potential_width() * region.as<float>().slope()};
				}else{
					sz = {size.potential_height() * region.as<float>().slope_inv(), size.potential_height()};
				}

				auto usz = region.as<float>() * get_unit_size(sz, region);

				if(size.width_mastering()){
					return math::vec2{size.potential_width(), usz.y};
				}else{
					return math::vec2{usz.x, size.potential_height()};
				}
			}
		}

		static void draw(ui::rect region, float opacity, graphic::renderer_ui_ref renderer, const ecs::chamber::building_data& data);
	};

	struct build_tile_status_elem : ui::elem{
		ecs::chamber::building_entity_ref entity{};

		[[nodiscard]] build_tile_status_elem(scene* scene, group* group)
			: elem(scene, group){
		}

		std::optional<math::vec2> pre_acquire_size_impl(optional_mastering_extent size) override{
			return build_tile_status_drawer::get_required_extent(size, entity.data().region().extent());
		}

		void draw_content(const rect clipSpace) const override{
			draw_background();
			build_tile_status_drawer::draw(get_content_bound(), gprop().get_opacity(), get_renderer(), entity.data());
		}
	};

	export
	struct chamber_ui_elem : ui::table{
	private:
		ecs::entity_ref chamber_entity{};
		stalled_bar* hitpoint_bar{};
		build_tile_status_elem* status_{};

		void build();

	public:
		[[nodiscard]] chamber_ui_elem(scene* scene, group* group, ecs::entity_ref chamber_entity)
			: table(scene, group), chamber_entity{std::move(chamber_entity)}{
			// set_style();
			build();
		}

		void update(float delta_in_ticks) override;
	};


}
