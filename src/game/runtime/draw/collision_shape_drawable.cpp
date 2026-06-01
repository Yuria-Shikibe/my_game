module mo_yanxi.game.runtime.draw.collision_shape_component;

import std;

namespace mo_yanxi::game::ecs{
namespace{
	[[nodiscard]] const collider& require_collider(const game::game_draw_context& context){
		const auto* collider_ptr = context.entity.try_get<::mo_yanxi::game::ecs::collider>();
		if(collider_ptr == nullptr){
			throw std::logic_error{"collision_shape_drawer requires collider"};
		}
		return *collider_ptr;
	}

	[[nodiscard]] const mech_motion& require_motion(const game::game_draw_context& context){
		const auto* motion_ptr = context.entity.try_get<::mo_yanxi::game::ecs::mech_motion>();
		if(motion_ptr == nullptr){
			throw std::logic_error{"collision_shape_drawer requires mech_motion"};
		}
		return *motion_ptr;
	}
}

game::game_draw_cull_bounds collision_shape_drawer::cull_bounds(
	const game::game_draw_context& context) const{
	const collider& collider = require_collider(context);
	const mech_motion& motion = require_motion(context);
	if(!enabled || !collider.enabled || collider.shape.empty()){
		return {.enabled = false};
	}

	return {
		.world_aabb = collider.world_aabb(motion),
		.screen_clip_margin = screen_clip_margin,
		.enabled = true
	};
}

void collision_shape_drawer::draw(
	gui::renderer_frontend& renderer,
	const game::game_draw_context& context) const{
	const collider& collider = require_collider(context);
	const mech_motion& motion = require_motion(context);
	if(!enabled || !collider.enabled || collider.shape.empty()){
		return;
	}

	auto draw_style = style;
	draw_style.depth += motion.depth + collider.depth;
	draw::draw_render_shape(renderer, collider.shape, collider.world_transform(motion), draw_style, surface);
}
}
