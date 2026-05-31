export module mo_yanxi.game.runtime.draw.collision_shape_gui_draw;

export import mo_yanxi.game.runtime.draw.collision_shape;
export import mo_yanxi.game.runtime.draw.collision_shape_component;
export import mo_yanxi.game.runtime.render_gui;
export import mo_yanxi.graphic.camera;

namespace mo_yanxi::game::draw{
export
struct collision_shape_draw_system{
	void draw(
		game_render_context& context,
		ecs::component_manager& manager,
		const graphic::camera2& camera) const;
};
}
