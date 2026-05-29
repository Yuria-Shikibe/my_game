export module mo_yanxi.game.runtime.render_gui;

export import mo_yanxi.backend.vulkan.renderer;
export import mo_yanxi.gui.renderer.frontend;

import mo_yanxi.graphic.camera;
import mo_yanxi.graphic.color;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.examples.default_config.constants;
import mo_yanxi.gui.fx.instruction_extension;
import mo_yanxi.math.matrix3;
import mo_yanxi.math.rect_ortho;
import mo_yanxi.math.vector2;
import std;

namespace mo_yanxi::game{
export
struct game_render_context{
	backend::vulkan::renderer& renderer;
	gui::renderer_frontend& frontend;
	double delta_seconds{};

	[[nodiscard]] math::vec2 extent() const noexcept{
		return frontend.get_region().extent();
	}
};

export
struct gui_debug_render_system{
	void draw(game_render_context& context, const graphic::camera2& camera) const{
		using namespace graphic::draw::instruction;

		auto& renderer_frontend = context.frontend;
		const auto extent = context.extent();
		if(extent.x <= 0.f || extent.y <= 0.f){
			return;
		}

		renderer_frontend.push(rect_aabb{
			.generic = {},
			.v00 = {},
			.v11 = extent,
			.vert_color = {graphic::colors::black.create_lerp(graphic::colors::dark_gray, .35f)}
		});

		const auto camera_transform = camera.get_v2v_mat({});
		renderer_frontend.top_viewport().push_local_transform(camera_transform);
		renderer_frontend.notify_viewport_changed();

		renderer_frontend.update_state(gui::fx::pipeline_config{
			.pipeline_index = gui::example::gpip::idx::coordinate
		});
		const auto world_region = camera.get_viewport();
		renderer_frontend.push(rect_aabb{
			.generic = {},
			.v00 = world_region.vert_00(),
			.v11 = world_region.vert_11(),
			.vert_color = {graphic::colors::white}
		});

		renderer_frontend.update_state(gui::fx::pipeline_config{
			.pipeline_index = gui::example::gpip::idx::def
		});
		renderer_frontend.update_state(gui::fx::push_constant{
			gui::example::gpip::default_draw_constants{}
		});

		renderer_frontend.top_viewport().pop_local_transform();
		renderer_frontend.notify_viewport_changed();

		renderer_frontend.update_state(gui::fx::blit_config{
			gui::fx::blit_config::full_screen_region,
			{
				.pipeline_index = gui::example::cpip_idx::blend,
				.inout_define_index = gui::example::cpip_bind_idx::to_background
			}
		});
		renderer_frontend.update_state(gui::fx::pipeline_config{
			.pipeline_index = gui::example::gpip::idx::def
		});
		renderer_frontend.update_state(gui::fx::push_constant{
			gui::example::gpip::default_draw_constants{}
		});
	}
};
}
