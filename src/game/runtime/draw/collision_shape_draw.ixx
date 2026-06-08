export module mo_yanxi.game.runtime.draw.collision_shape;

export import mo_yanxi.game.physics.shape;
export import mo_yanxi.game.runtime.game_renderer;
export import mo_yanxi.game.runtime.draw.collision_shape_style;
export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.math.trans2;

import std;

namespace mo_yanxi::game::draw{
export
void push_line(
	gui::renderer_frontend& renderer,
	math::vec2 src,
	math::vec2 dst,
	const collision_shape_draw_style& style);

export
void push_open_line(
	gui::renderer_frontend& renderer,
	std::span<const math::vec2> vertices,
	const collision_shape_draw_style& style);

export
void push_closed_line(
	gui::renderer_frontend& renderer,
	std::span<const math::vec2> vertices,
	const collision_shape_draw_style& style);

export
void push_filled_polygon(
	gui::renderer_frontend& renderer,
	std::span<const math::vec2> vertices,
	const collision_shape_draw_style& style);

export
void fill_polygon(
	gui::renderer_frontend& renderer,
	std::span<const math::vec2> vertices,
	math::trans2 transform,
	const collision_shape_draw_style& style);

export
void draw_shape(
	gui::renderer_frontend& renderer,
	const physics::circle_shape& shape,
	math::trans2 transform,
	const collision_shape_draw_style& style = {});

export
void draw_shape(
	gui::renderer_frontend& renderer,
	const physics::capsule_shape& shape,
	math::trans2 transform,
	const collision_shape_draw_style& style = {});

export
void draw_shape(
	gui::renderer_frontend& renderer,
	const physics::box_shape& shape,
	math::trans2 transform,
	const collision_shape_draw_style& style = {});

export
void draw_shape(
	gui::renderer_frontend& renderer,
	const physics::convex_polygon_shape& shape,
	math::trans2 transform,
	const collision_shape_draw_style& style = {});

export
void draw_shape_part(
	gui::renderer_frontend& renderer,
	const physics::collision_shape_record_part& part,
	std::span<const math::vec2> polygon_vertices,
	math::trans2 transform,
	const collision_shape_draw_style& style = {});

export
void draw_shape(
	gui::renderer_frontend& renderer,
	const physics::collision_shape_record& shape,
	math::trans2 transform,
	const collision_shape_draw_style& style = {});

export
void draw_shape(
	gui::renderer_frontend& renderer,
	const physics::collision_shape& shape,
	math::trans2 transform,
	const collision_shape_draw_style& style = {});

export
void fill_shape(
	gui::renderer_frontend& renderer,
	const physics::circle_shape& shape,
	math::trans2 transform,
	const collision_shape_draw_style& style = {});

export
void fill_shape(
	gui::renderer_frontend& renderer,
	const physics::capsule_shape& shape,
	math::trans2 transform,
	const collision_shape_draw_style& style = {});

export
void fill_shape(
	gui::renderer_frontend& renderer,
	const physics::box_shape& shape,
	math::trans2 transform,
	const collision_shape_draw_style& style = {});

export
void fill_shape(
	gui::renderer_frontend& renderer,
	const physics::convex_polygon_shape& shape,
	math::trans2 transform,
	const collision_shape_draw_style& style = {});

export
void fill_shape_part(
	gui::renderer_frontend& renderer,
	const physics::collision_shape_record_part& part,
	std::span<const math::vec2> polygon_vertices,
	math::trans2 transform,
	const collision_shape_draw_style& style = {});

export
void fill_shape(
	gui::renderer_frontend& renderer,
	const physics::collision_shape_record& shape,
	math::trans2 transform,
	const collision_shape_draw_style& style = {});

export
void fill_shape(
	gui::renderer_frontend& renderer,
	const physics::collision_shape& shape,
	math::trans2 transform,
	const collision_shape_draw_style& style = {});

export
void draw_render_shape(
	gui::renderer_frontend& renderer,
	const physics::collision_shape_record& shape,
	math::trans2 transform,
	const collision_shape_draw_style& style,
	game_render_shape_surface surface);

export
void draw_render_shape(
	gui::renderer_frontend& renderer,
	const physics::box_shape& shape,
	math::trans2 transform,
	const collision_shape_draw_style& style,
	game_render_shape_surface surface);

export
void draw_render_shape(
	gui::renderer_frontend& renderer,
	const physics::collision_shape& shape,
	math::trans2 transform,
	const collision_shape_draw_style& style,
	game_render_shape_surface surface);
}
