export module mo_yanxi.game.runtime.draw.chamber_component;

export import mo_yanxi.game.ecs.component.chamber.damage_grid;
export import mo_yanxi.game.ecs.component.physics;
export import mo_yanxi.game.runtime.draw.collision_shape;
export import mo_yanxi.game.runtime.game_renderer;
export import mo_yanxi.game.runtime.draw.collision_shape_style;

namespace mo_yanxi::game::ecs{
export
struct chamber_drawer{
	float screen_clip_margin{24.f};
	bool enabled{true};

	[[nodiscard]] game::game_draw_cull_bounds cull_bounds(
		const game::game_draw_context& context) const;
	void draw(
		gui::renderer_frontend& renderer,
		const game::game_draw_context& context) const;
};
}
