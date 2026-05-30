module mo_yanxi.game.runtime.draw.collision_shape_component;

import mo_yanxi.gui.examples.default_config.constants;
import mo_yanxi.gui.fx.instruction_extension;
import std;

namespace mo_yanxi::game::draw{
void collision_shape_draw_system::draw(
	game_render_context& context,
	ecs::component_manager& manager,
	const graphic::camera2& camera) const{
	const auto extent = context.extent();
	if(extent.x <= 0.f || extent.y <= 0.f){
		return;
	}

	auto& renderer = context.frontend;
	const auto camera_transform = camera.get_v2v_mat({});

	renderer.top_viewport().push_local_transform(camera_transform);
	renderer.notify_viewport_changed();
	renderer.update_state(gui::fx::pipeline_config{
		.pipeline_index = gui::example::gpip::idx::def
	});
	renderer.update_state(gui::fx::push_constant{
		gui::example::gpip::default_draw_constants{}
	});

	const auto viewport = camera.get_viewport();
	const float camera_scale = std::max(camera.get_scale(), 0.0001f);

	manager.each([&](
		const ecs::chunk_meta& meta,
		const ecs::collider& collider,
		const ecs::mech_motion& motion,
		const ecs::collision_shape_drawer& drawer){
		if(!meta.id() || !meta.id().is_inserted() || !drawer.enabled || !collider.enabled || collider.shape.empty()){
			return;
		}

		const float world_margin = std::max(drawer.screen_clip_margin, 0.f) / camera_scale;
		if(!collider.world_aabb(motion).expand(world_margin, world_margin).overlap_inclusive(viewport)){
			return;
		}

		auto style = drawer.style;
		style.depth += motion.depth + collider.depth;
		draw::draw_shape(renderer, collider.shape, collider.world_transform(motion), style);
	});

	renderer.top_viewport().pop_local_transform();
	renderer.notify_viewport_changed();
	renderer.update_state(gui::fx::pipeline_config{
		.pipeline_index = gui::example::gpip::idx::def
	});
	renderer.update_state(gui::fx::push_constant{
		gui::example::gpip::default_draw_constants{}
	});
}
}
