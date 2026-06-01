module mo_yanxi.game.runtime.draw.collision_shape;

import mo_yanxi.game.runtime.game_renderer;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.fx;
import std;

namespace mo_yanxi::game::draw{
namespace instr = graphic::draw::instruction;

constexpr float collision_draw_epsilon = 1.0e-5f;

[[nodiscard]] float make_collision_clip_depth(const float style_depth) noexcept{
	const float finite_depth = std::isfinite(style_depth) ? style_depth : 0.f;
	return std::clamp(0.50f - finite_depth * 0.001f, 0.05f, 0.95f);
}

[[nodiscard]] instr::primitive_generic make_generic(const collision_shape_draw_style& style) noexcept{
	return {
		.depth = make_collision_clip_depth(style.depth)
	};
}

[[nodiscard]] math::section<graphic::float4> make_color_section(
	const collision_shape_draw_style& style) noexcept{
	return {style.color, style.color};
}

[[nodiscard]] float sane_stroke(const collision_shape_draw_style& style) noexcept{
	return std::max(style.stroke, collision_draw_epsilon);
}

[[nodiscard]] math::range stroke_radius_range(
	const float radius,
	const collision_shape_draw_style& style) noexcept{
	const float half_stroke = sane_stroke(style) * 0.5f;
	return {
		std::max(0.f, radius - half_stroke),
		std::max(0.f, radius + half_stroke)
	};
}

[[nodiscard]] std::uint32_t circle_segment_count(const float radius) noexcept{
	return std::max<std::uint32_t>(
		12u,
		graphic::draw::instruction::get_circle_vertices(std::max(radius, 1.f)));
}

void push_line(
	gui::renderer_frontend& renderer,
	const math::vec2 src,
	const math::vec2 dst,
	const collision_shape_draw_style& style){
	if((dst - src).length2() <= collision_draw_epsilon * collision_draw_epsilon){
		return;
	}

	renderer.push(instr::line{
		.generic = make_generic(style),
		.src = src,
		.dst = dst,
		.color = make_color_section(style),
		.stroke = sane_stroke(style)
	});
}

void push_closed_line(
	gui::renderer_frontend& renderer,
	const std::span<const math::vec2> vertices,
	const collision_shape_draw_style& style){
	if(vertices.size() < 3){
		return;
	}

	std::vector<instr::line_node> nodes{};
	nodes.reserve(vertices.size());
	for(const auto vertex : vertices){
		nodes.push_back(instr::line_node{
			.pos = vertex,
			.stroke = sane_stroke(style),
			.color = make_color_section(style)
		});
	}

	renderer.push(
		instr::line_segments_closed{instr::line_segments{
			.generic = make_generic(style)
		}},
		std::span<const instr::line_node>{nodes});
}

void push_filled_polygon(
	gui::renderer_frontend& renderer,
	const std::span<const math::vec2> vertices,
	const collision_shape_draw_style& style){
	if(vertices.size() < 3){
		return;
	}

	for(std::size_t index = 1; index + 1 < vertices.size(); ++index){
		renderer.push(instr::triangle{
			.generic = make_generic(style),
			.p0 = vertices.front(),
			.p1 = vertices[index],
			.p2 = vertices[index + 1],
			.c0 = style.color,
			.c1 = style.color,
			.c2 = style.color
		});
	}
}

void draw_shape(
	gui::renderer_frontend& renderer,
	const physics::circle_shape& shape,
	const math::trans2 transform,
	const collision_shape_draw_style& style){
	if(shape.radius <= 0.f){
		return;
	}

	renderer.push(instr::poly{
		.generic = make_generic(style),
		.pos = transform.vec,
		.segments = circle_segment_count(shape.radius),
		.initial_angle = static_cast<float>(transform.rot),
		.radius = stroke_radius_range(shape.radius, style),
		.color = make_color_section(style)
	});
}

void fill_shape(
	gui::renderer_frontend& renderer,
	const physics::circle_shape& shape,
	const math::trans2 transform,
	const collision_shape_draw_style& style){
	if(shape.radius <= 0.f){
		return;
	}

	renderer.push(instr::poly{
		.generic = make_generic(style),
		.pos = transform.vec,
		.segments = circle_segment_count(shape.radius),
		.initial_angle = static_cast<float>(transform.rot),
		.radius = {0.f, shape.radius},
		.color = make_color_section(style)
	});
}

void draw_shape(
	gui::renderer_frontend& renderer,
	const physics::capsule_shape& shape,
	const math::trans2 transform,
	const collision_shape_draw_style& style){
	if(shape.radius <= 0.f){
		return;
	}

	const math::vec2 begin = shape.begin >> transform;
	const math::vec2 end = shape.end >> transform;
	const math::vec2 axis = end - begin;
	const float axis_length2 = axis.length2();
	if(axis_length2 <= collision_draw_epsilon * collision_draw_epsilon){
		draw::draw_shape(renderer, physics::circle_shape{shape.radius}, math::trans2{begin, transform.rot}, style);
		return;
	}

	const math::vec2 direction = axis / std::sqrt(axis_length2);
	const math::vec2 normal{-direction.y, direction.x};
	const math::vec2 offset = normal * shape.radius;
	draw::push_line(renderer, begin + offset, end + offset, style);
	draw::push_line(renderer, begin - offset, end - offset, style);

	const float axis_angle = direction.angle_rad();
	const float normal_angle = axis_angle + math::pi_half;
	const float inverse_two_pi = 1.f / math::pi_2;
	const auto radius = stroke_radius_range(shape.radius, style);
	const auto color = make_color_section(style);
	const auto generic = make_generic(style);
	const auto segments = std::max<std::uint32_t>(6u, circle_segment_count(shape.radius) / 2u);

	renderer.push(instr::poly_partial{
		.generic = generic,
		.pos = end,
		.segments = segments,
		.range = {normal_angle * inverse_two_pi, -0.5f},
		.radius = radius,
		.color = {color.from, color.from, color.to, color.to}
	});

	renderer.push(instr::poly_partial{
		.generic = generic,
		.pos = begin,
		.segments = segments,
		.range = {(normal_angle - math::pi) * inverse_two_pi, -0.5f},
		.radius = radius,
		.color = {color.from, color.from, color.to, color.to}
	});
}

void fill_shape(
	gui::renderer_frontend& renderer,
	const physics::capsule_shape& shape,
	const math::trans2 transform,
	const collision_shape_draw_style& style){
	if(shape.radius <= 0.f){
		return;
	}

	const math::vec2 begin = shape.begin >> transform;
	const math::vec2 end = shape.end >> transform;
	const math::vec2 axis = end - begin;
	const float axis_length2 = axis.length2();
	if(axis_length2 <= collision_draw_epsilon * collision_draw_epsilon){
		draw::fill_shape(renderer, physics::circle_shape{shape.radius}, math::trans2{begin, transform.rot}, style);
		return;
	}

	const float axis_length = std::sqrt(axis_length2);
	const float axis_angle = axis.angle_rad();
	renderer.push(instr::rectangle{
		.generic = make_generic(style),
		.pos = (begin + end) * 0.5f,
		.angle = axis_angle,
		.scale = 2.f,
		.vert_color = {style.color},
		.extent = {axis_length * 0.5f, shape.radius}
	});
	draw::fill_shape(renderer, physics::circle_shape{shape.radius}, math::trans2{begin, transform.rot}, style);
	draw::fill_shape(renderer, physics::circle_shape{shape.radius}, math::trans2{end, transform.rot}, style);
}

void draw_shape(
	gui::renderer_frontend& renderer,
	const physics::box_shape& shape,
	const math::trans2 transform,
	const collision_shape_draw_style& style){
	if(shape.half_extent.x <= 0.f || shape.half_extent.y <= 0.f){
		return;
	}

	const auto half = shape.half_extent;
	const std::array vertices{
		math::vec2{-half.x, -half.y} >> transform,
		math::vec2{half.x, -half.y} >> transform,
		math::vec2{half.x, half.y} >> transform,
		math::vec2{-half.x, half.y} >> transform
	};
	draw::push_closed_line(renderer, vertices, style);
}

void fill_shape(
	gui::renderer_frontend& renderer,
	const physics::box_shape& shape,
	const math::trans2 transform,
	const collision_shape_draw_style& style){
	if(shape.half_extent.x <= 0.f || shape.half_extent.y <= 0.f){
		return;
	}

	const auto half = shape.half_extent;
	const std::array vertices{
		math::vec2{-half.x, -half.y} >> transform,
		math::vec2{half.x, -half.y} >> transform,
		math::vec2{-half.x, half.y} >> transform,
		math::vec2{half.x, half.y} >> transform
	};
	renderer.push(instr::quad{
		.generic = make_generic(style),
		.vert = vertices,
		.uv = {},
		.vert_color = {style.color}
	});
}

void draw_shape(
	gui::renderer_frontend& renderer,
	const physics::convex_polygon_shape& shape,
	const math::trans2 transform,
	const collision_shape_draw_style& style){
	if(!shape.valid()){
		return;
	}

	std::vector<math::vec2> vertices{};
	vertices.reserve(shape.vertices.size());
	for(const auto vertex : shape.vertices){
		vertices.push_back(vertex >> transform);
	}
	draw::push_closed_line(renderer, vertices, style);
}

void fill_shape(
	gui::renderer_frontend& renderer,
	const physics::convex_polygon_shape& shape,
	const math::trans2 transform,
	const collision_shape_draw_style& style){
	if(!shape.valid()){
		return;
	}

	std::vector<math::vec2> vertices{};
	vertices.reserve(shape.vertices.size());
	for(const auto vertex : shape.vertices){
		vertices.push_back(vertex >> transform);
	}
	draw::push_filled_polygon(renderer, vertices, style);
}

void draw_shape_part(
	gui::renderer_frontend& renderer,
	const physics::collision_shape_record_part& part,
	const std::span<const math::vec2> polygon_vertices,
	const math::trans2 transform,
	const collision_shape_draw_style& style){
	const math::trans2 part_transform = part.local_transform >> transform;
	switch(part.type){
	case physics::shape_type::circle:
		draw::draw_shape(renderer, part.payload.circle, part_transform, style);
		return;
	case physics::shape_type::capsule:
		draw::draw_shape(renderer, part.payload.capsule, part_transform, style);
		return;
	case physics::shape_type::box:
		draw::draw_shape(renderer, part.payload.box, part_transform, style);
		return;
	case physics::shape_type::convex_polygon:{
		const auto payload = part.payload.convex_polygon;
		const auto offset = static_cast<std::size_t>(payload.vertex_offset);
		const auto count = static_cast<std::size_t>(payload.vertex_count);
		if(count < 3 || offset > polygon_vertices.size() || count > polygon_vertices.size() - offset){
			return;
		}

		std::vector<math::vec2> vertices{};
		vertices.reserve(count);
		for(const auto vertex : polygon_vertices.subspan(offset, count)){
			vertices.push_back(vertex >> part_transform);
		}
		draw::push_closed_line(renderer, vertices, style);
		return;
	}
	}
	std::unreachable();
}

void fill_shape_part(
	gui::renderer_frontend& renderer,
	const physics::collision_shape_record_part& part,
	const std::span<const math::vec2> polygon_vertices,
	const math::trans2 transform,
	const collision_shape_draw_style& style){
	const math::trans2 part_transform = part.local_transform >> transform;
	switch(part.type){
	case physics::shape_type::circle:
		draw::fill_shape(renderer, part.payload.circle, part_transform, style);
		return;
	case physics::shape_type::capsule:
		draw::fill_shape(renderer, part.payload.capsule, part_transform, style);
		return;
	case physics::shape_type::box:
		draw::fill_shape(renderer, part.payload.box, part_transform, style);
		return;
	case physics::shape_type::convex_polygon:{
		const auto payload = part.payload.convex_polygon;
		const auto offset = static_cast<std::size_t>(payload.vertex_offset);
		const auto count = static_cast<std::size_t>(payload.vertex_count);
		if(count < 3 || offset > polygon_vertices.size() || count > polygon_vertices.size() - offset){
			return;
		}

		std::vector<math::vec2> vertices{};
		vertices.reserve(count);
		for(const auto vertex : polygon_vertices.subspan(offset, count)){
			vertices.push_back(vertex >> part_transform);
		}
		draw::push_filled_polygon(renderer, vertices, style);
		return;
	}
	}
	std::unreachable();
}

void draw_shape(
	gui::renderer_frontend& renderer,
	const physics::collision_shape_record& shape,
	const math::trans2 transform,
	const collision_shape_draw_style& style){
	for(const auto& part : shape.records()){
		draw::draw_shape_part(renderer, part, shape.polygon_vertices(), transform, style);
	}
}

void fill_shape(
	gui::renderer_frontend& renderer,
	const physics::collision_shape_record& shape,
	const math::trans2 transform,
	const collision_shape_draw_style& style){
	for(const auto& part : shape.records()){
		draw::fill_shape_part(renderer, part, shape.polygon_vertices(), transform, style);
	}
}

void draw_shape(
	gui::renderer_frontend& renderer,
	const physics::collision_shape& shape,
	const math::trans2 transform,
	const collision_shape_draw_style& style){
	shape.visit_parts([&](std::size_t, const auto& component){
		draw::draw_shape(renderer, component.shape, component.local_transform >> transform, style);
	});
}

void fill_shape(
	gui::renderer_frontend& renderer,
	const physics::collision_shape& shape,
	const math::trans2 transform,
	const collision_shape_draw_style& style){
	shape.visit_parts([&](std::size_t, const auto& component){
		draw::fill_shape(renderer, component.shape, component.local_transform >> transform, style);
	});
}

void draw_render_shape(
	gui::renderer_frontend& renderer,
	const physics::collision_shape_record& shape,
	const math::trans2 transform,
	const collision_shape_draw_style& style,
	const game_render_shape_surface surface){
	if(game::game_render_shape_has_fill(surface)){
		draw::fill_shape(renderer, shape, transform, style);
	}
	if(game::game_render_shape_has_outline(surface)){
		draw::draw_shape(renderer, shape, transform, style);
	}
}

void draw_render_shape(
	gui::renderer_frontend& renderer,
	const physics::box_shape& shape,
	const math::trans2 transform,
	const collision_shape_draw_style& style,
	const game_render_shape_surface surface){
	if(game::game_render_shape_has_fill(surface)){
		draw::fill_shape(renderer, shape, transform, style);
	}
	if(game::game_render_shape_has_outline(surface)){
		draw::draw_shape(renderer, shape, transform, style);
	}
}

void draw_render_shape(
	gui::renderer_frontend& renderer,
	const physics::collision_shape& shape,
	const math::trans2 transform,
	const collision_shape_draw_style& style,
	const game_render_shape_surface surface){
	if(game::game_render_shape_has_fill(surface)){
		draw::fill_shape(renderer, shape, transform, style);
	}
	if(game::game_render_shape_has_outline(surface)){
		draw::draw_shape(renderer, shape, transform, style);
	}
}
}
