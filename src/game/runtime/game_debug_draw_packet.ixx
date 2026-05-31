export module mo_yanxi.game.runtime.game_debug_draw_packet;

export import mo_yanxi.game.physics.shape;
export import mo_yanxi.game.runtime.draw.collision_shape_style;

import mo_yanxi.graphic.draw.instruction;
import std;

namespace mo_yanxi::game{
export
struct game_debug_draw_line{
	math::vec2 src{};
	math::vec2 dst{};
	graphic::color color{};
	float stroke{1.f};
	float depth{};
};

export
struct game_debug_draw_ring{
	math::vec2 center{};
	math::range radius{};
	graphic::color color{};
	float initial_angle{};
	float turn_range{1.f};
	std::uint32_t segments{12u};
	float depth{};
};

export
struct game_debug_draw_closed_polyline{
	std::uint32_t vertex_offset{};
	std::uint32_t vertex_count{};
	graphic::color color{};
	float stroke{1.f};
	float depth{};
};

export
struct game_debug_draw_packet{
	std::vector<game_debug_draw_line> lines{};
	std::vector<game_debug_draw_ring> rings{};
	std::vector<game_debug_draw_closed_polyline> closed_polylines{};
	std::vector<math::vec2> vertices{};

	[[nodiscard]] bool empty() const noexcept{
		return lines.empty() && rings.empty() && closed_polylines.empty();
	}

	void clear() noexcept{
		lines.clear();
		rings.clear();
		closed_polylines.clear();
		vertices.clear();
	}
};

export
struct alignas(16) game_debug_draw_line_gpu{
	math::vec2 src{};
	math::vec2 dst{};
	std::array<float, 4> color{};
	float stroke{1.f};
	float depth{};
	float reserved0{};
	float reserved1{};

	friend bool operator==(const game_debug_draw_line_gpu&, const game_debug_draw_line_gpu&) noexcept = default;
};

export
struct alignas(16) game_debug_draw_ring_gpu{
	math::vec2 center{};
	math::vec2 radius{};
	std::array<float, 4> color{};
	float initial_angle{};
	float turn_range{1.f};
	std::uint32_t segments{12u};
	float depth{};

	friend bool operator==(const game_debug_draw_ring_gpu&, const game_debug_draw_ring_gpu&) noexcept = default;
};

export
struct alignas(16) game_debug_draw_closed_polyline_gpu{
	std::uint32_t vertex_offset{};
	std::uint32_t vertex_count{};
	float stroke{1.f};
	float depth{};
	std::array<float, 4> color{};

	friend bool operator==(const game_debug_draw_closed_polyline_gpu&, const game_debug_draw_closed_polyline_gpu&) noexcept = default;
};

export
struct alignas(16) game_debug_draw_vertex_gpu{
	math::vec2 position{};
	float reserved0{};
	float reserved1{};

	friend bool operator==(const game_debug_draw_vertex_gpu&, const game_debug_draw_vertex_gpu&) noexcept = default;
};

export
struct alignas(16) game_debug_draw_params_gpu{
	static constexpr std::uint32_t max_lines = 4096;
	static constexpr std::uint32_t max_rings = 4096;
	static constexpr std::uint32_t max_closed_polylines = 2048;
	static constexpr std::uint32_t max_vertices = 16384;

	std::uint32_t line_count{};
	std::uint32_t ring_count{};
	std::uint32_t closed_polyline_count{};
	std::uint32_t vertex_count{};

	friend bool operator==(const game_debug_draw_params_gpu&, const game_debug_draw_params_gpu&) noexcept = default;
};

export
struct game_debug_draw_gpu_packet{
	game_debug_draw_params_gpu params{};
	std::vector<game_debug_draw_line_gpu> lines{};
	std::vector<game_debug_draw_ring_gpu> rings{};
	std::vector<game_debug_draw_closed_polyline_gpu> closed_polylines{};
	std::vector<game_debug_draw_vertex_gpu> vertices{};

	[[nodiscard]] bool empty() const noexcept{
		return params.line_count == 0u
			&& params.ring_count == 0u
			&& params.closed_polyline_count == 0u
			&& params.vertex_count == 0u;
	}
};

static_assert(sizeof(game_debug_draw_line_gpu) == 48u);
static_assert(sizeof(game_debug_draw_ring_gpu) == 48u);
static_assert(sizeof(game_debug_draw_closed_polyline_gpu) == 32u);
static_assert(sizeof(game_debug_draw_vertex_gpu) == 16u);
static_assert(sizeof(game_debug_draw_params_gpu) == 16u);

export
constexpr float game_debug_draw_epsilon = 1.0e-5f;

export
[[nodiscard]] std::array<float, 4> make_game_debug_draw_color_array(const graphic::color color) noexcept{
	return {color.r, color.g, color.b, color.a};
}

export
[[nodiscard]] game_debug_draw_gpu_packet make_game_debug_draw_gpu_packet(
	const game_debug_draw_packet& packet){
	game_debug_draw_gpu_packet gpu_packet{};
	gpu_packet.lines.reserve(std::min<std::size_t>(packet.lines.size(), game_debug_draw_params_gpu::max_lines));
	gpu_packet.rings.reserve(std::min<std::size_t>(packet.rings.size(), game_debug_draw_params_gpu::max_rings));
	gpu_packet.closed_polylines.reserve(
		std::min<std::size_t>(packet.closed_polylines.size(), game_debug_draw_params_gpu::max_closed_polylines));
	gpu_packet.vertices.reserve(std::min<std::size_t>(packet.vertices.size(), game_debug_draw_params_gpu::max_vertices));

	for(const auto& line : packet.lines){
		if(gpu_packet.lines.size() == game_debug_draw_params_gpu::max_lines){
			break;
		}

		gpu_packet.lines.push_back({
			.src = line.src,
			.dst = line.dst,
			.color = game::make_game_debug_draw_color_array(line.color),
			.stroke = line.stroke,
			.depth = line.depth
		});
	}

	for(const auto& ring : packet.rings){
		if(gpu_packet.rings.size() == game_debug_draw_params_gpu::max_rings){
			break;
		}

		gpu_packet.rings.push_back({
			.center = ring.center,
			.radius = {ring.radius.from, ring.radius.to},
			.color = game::make_game_debug_draw_color_array(ring.color),
			.initial_angle = ring.initial_angle,
			.turn_range = ring.turn_range,
			.segments = ring.segments,
			.depth = ring.depth
		});
	}

	for(const auto& polyline : packet.closed_polylines){
		if(gpu_packet.closed_polylines.size() == game_debug_draw_params_gpu::max_closed_polylines
			|| polyline.vertex_count < 3u
			|| polyline.vertex_offset > packet.vertices.size()
			|| polyline.vertex_count > packet.vertices.size() - polyline.vertex_offset){
			continue;
		}

		if(gpu_packet.vertices.size() + polyline.vertex_count > game_debug_draw_params_gpu::max_vertices){
			break;
		}

		const auto vertex_offset = static_cast<std::uint32_t>(gpu_packet.vertices.size());
		for(const auto vertex : std::span{
			packet.vertices.data() + polyline.vertex_offset,
			static_cast<std::size_t>(polyline.vertex_count)
		}){
			gpu_packet.vertices.push_back({
				.position = vertex
			});
		}
		gpu_packet.closed_polylines.push_back({
			.vertex_offset = vertex_offset,
			.vertex_count = polyline.vertex_count,
			.stroke = polyline.stroke,
			.depth = polyline.depth,
			.color = game::make_game_debug_draw_color_array(polyline.color)
		});
	}

	gpu_packet.params = {
		.line_count = static_cast<std::uint32_t>(gpu_packet.lines.size()),
		.ring_count = static_cast<std::uint32_t>(gpu_packet.rings.size()),
		.closed_polyline_count = static_cast<std::uint32_t>(gpu_packet.closed_polylines.size()),
		.vertex_count = static_cast<std::uint32_t>(gpu_packet.vertices.size())
	};
	return gpu_packet;
}

export
[[nodiscard]] float make_game_debug_draw_stroke(const draw::collision_shape_draw_style& style) noexcept{
	return std::max(style.stroke, game_debug_draw_epsilon);
}

export
[[nodiscard]] math::range make_game_debug_draw_stroke_radius_range(
	const float radius,
	const draw::collision_shape_draw_style& style) noexcept{
	const float half_stroke = game::make_game_debug_draw_stroke(style) * 0.5f;
	return {
		std::max(0.f, radius - half_stroke),
		std::max(0.f, radius + half_stroke)
	};
}

export
[[nodiscard]] std::uint32_t make_game_debug_draw_circle_segment_count(const float radius) noexcept{
	return std::max<std::uint32_t>(
		12u,
		graphic::draw::instruction::get_circle_vertices(std::max(radius, 1.f)));
}

export
void append_game_debug_draw_line(
	game_debug_draw_packet& packet,
	const math::vec2 src,
	const math::vec2 dst,
	const draw::collision_shape_draw_style& style){
	if((dst - src).length2() <= game_debug_draw_epsilon * game_debug_draw_epsilon){
		return;
	}

	packet.lines.push_back({
		.src = src,
		.dst = dst,
		.color = style.color,
		.stroke = game::make_game_debug_draw_stroke(style),
		.depth = style.depth
	});
}

export
void append_game_debug_draw_closed_polyline(
	game_debug_draw_packet& packet,
	const std::span<const math::vec2> vertices,
	const draw::collision_shape_draw_style& style){
	if(vertices.size() < 3){
		return;
	}

	const auto vertex_offset = static_cast<std::uint32_t>(packet.vertices.size());
	packet.vertices.append_range(vertices);
	packet.closed_polylines.push_back({
		.vertex_offset = vertex_offset,
		.vertex_count = static_cast<std::uint32_t>(vertices.size()),
		.color = style.color,
		.stroke = game::make_game_debug_draw_stroke(style),
		.depth = style.depth
	});
}

export
void append_game_debug_draw_shape(
	game_debug_draw_packet& packet,
	const physics::circle_shape& shape,
	const math::trans2 transform,
	const draw::collision_shape_draw_style& style = {}){
	if(shape.radius <= 0.f){
		return;
	}

	packet.rings.push_back({
		.center = transform.vec,
		.radius = game::make_game_debug_draw_stroke_radius_range(shape.radius, style),
		.color = style.color,
		.initial_angle = static_cast<float>(transform.rot),
		.turn_range = 1.f,
		.segments = game::make_game_debug_draw_circle_segment_count(shape.radius),
		.depth = style.depth
	});
}

export
void append_game_debug_draw_shape(
	game_debug_draw_packet& packet,
	const physics::capsule_shape& shape,
	const math::trans2 transform,
	const draw::collision_shape_draw_style& style = {}){
	if(shape.radius <= 0.f){
		return;
	}

	const math::vec2 begin = shape.begin >> transform;
	const math::vec2 end = shape.end >> transform;
	const math::vec2 axis = end - begin;
	const float axis_length2 = axis.length2();
	if(axis_length2 <= game_debug_draw_epsilon * game_debug_draw_epsilon){
		game::append_game_debug_draw_shape(packet, physics::circle_shape{shape.radius}, math::trans2{begin, transform.rot}, style);
		return;
	}

	const math::vec2 direction = axis / std::sqrt(axis_length2);
	const math::vec2 normal{-direction.y, direction.x};
	const math::vec2 offset = normal * shape.radius;
	game::append_game_debug_draw_line(packet, begin + offset, end + offset, style);
	game::append_game_debug_draw_line(packet, begin - offset, end - offset, style);

	const float axis_angle = direction.angle_rad();
	const float normal_angle = axis_angle + math::pi_half;
	const float inverse_two_pi = 1.f / math::pi_2;
	const auto radius = game::make_game_debug_draw_stroke_radius_range(shape.radius, style);
	const auto segments = std::max<std::uint32_t>(
		6u,
		game::make_game_debug_draw_circle_segment_count(shape.radius) / 2u);
	packet.rings.push_back({
		.center = end,
		.radius = radius,
		.color = style.color,
		.initial_angle = normal_angle * inverse_two_pi,
		.turn_range = -0.5f,
		.segments = segments,
		.depth = style.depth
	});
	packet.rings.push_back({
		.center = begin,
		.radius = radius,
		.color = style.color,
		.initial_angle = (normal_angle - math::pi) * inverse_two_pi,
		.turn_range = -0.5f,
		.segments = segments,
		.depth = style.depth
	});
}

export
void append_game_debug_draw_shape(
	game_debug_draw_packet& packet,
	const physics::box_shape& shape,
	const math::trans2 transform,
	const draw::collision_shape_draw_style& style = {}){
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
	game::append_game_debug_draw_closed_polyline(packet, vertices, style);
}

export
void append_game_debug_draw_shape(
	game_debug_draw_packet& packet,
	const physics::convex_polygon_shape& shape,
	const math::trans2 transform,
	const draw::collision_shape_draw_style& style = {}){
	if(!shape.valid()){
		return;
	}

	std::vector<math::vec2> vertices{};
	vertices.reserve(shape.vertices.size());
	for(const auto vertex : shape.vertices){
		vertices.push_back(vertex >> transform);
	}
	game::append_game_debug_draw_closed_polyline(packet, vertices, style);
}

export
void append_game_debug_draw_shape_part(
	game_debug_draw_packet& packet,
	const physics::collision_shape_record_part& part,
	const std::span<const math::vec2> polygon_vertices,
	const math::trans2 transform,
	const draw::collision_shape_draw_style& style = {}){
	const math::trans2 part_transform = part.local_transform >> transform;
	switch(part.type){
	case physics::shape_type::circle:
		game::append_game_debug_draw_shape(packet, part.payload.circle, part_transform, style);
		return;
	case physics::shape_type::capsule:
		game::append_game_debug_draw_shape(packet, part.payload.capsule, part_transform, style);
		return;
	case physics::shape_type::box:
		game::append_game_debug_draw_shape(packet, part.payload.box, part_transform, style);
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
		game::append_game_debug_draw_closed_polyline(packet, vertices, style);
		return;
	}
	}
	std::unreachable();
}

export
void append_game_debug_draw_shape(
	game_debug_draw_packet& packet,
	const physics::collision_shape_record& shape,
	const math::trans2 transform,
	const draw::collision_shape_draw_style& style = {}){
	for(const auto& part : shape.records()){
		game::append_game_debug_draw_shape_part(packet, part, shape.polygon_vertices(), transform, style);
	}
}

export
void append_game_debug_draw_shape(
	game_debug_draw_packet& packet,
	const physics::collision_shape& shape,
	const math::trans2 transform,
	const draw::collision_shape_draw_style& style = {}){
	shape.visit_parts([&](std::size_t, const auto& component){
		game::append_game_debug_draw_shape(packet, component.shape, component.local_transform >> transform, style);
	});
}
}
