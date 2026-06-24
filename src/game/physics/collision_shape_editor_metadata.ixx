export module mo_yanxi.game.physics.collision_shape_editor_metadata;

export import mo_yanxi.game.physics.shape;

import std;
import mo_yanxi.math;

namespace mo_yanxi::game::physics{
namespace editor_detail{
inline constexpr float polygon_epsilon = 1.0e-5f;
inline constexpr float mirror_axis_merge_epsilon = 0.5f;

[[nodiscard]] constexpr bool finite_vec2(const math::vec2 value) noexcept{
	return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] constexpr float signed_area(const std::span<const math::vec2> vertices) noexcept{
	if(vertices.size() < 3u){
		return 0.f;
	}

	float area{};
	for(std::size_t index = 0u; index != vertices.size(); ++index){
		const math::vec2 current = vertices[index];
		const math::vec2 next = vertices[(index + 1u) % vertices.size()];
		area += current.cross(next);
	}
	return area * 0.5f;
}

[[nodiscard]] constexpr bool strict_ccw_convex_polygon(const std::span<const math::vec2> vertices) noexcept{
	if(vertices.size() < 3u){
		return false;
	}
	if(!std::ranges::all_of(vertices, editor_detail::finite_vec2)){
		return false;
	}
	if(editor_detail::signed_area(vertices) <= editor_detail::polygon_epsilon){
		return false;
	}

	for(std::size_t index = 0u; index != vertices.size(); ++index){
		const math::vec2 previous = vertices[(index + vertices.size() - 1u) % vertices.size()];
		const math::vec2 current = vertices[index];
		const math::vec2 next = vertices[(index + 1u) % vertices.size()];
		if((current - previous).cross(next - current) <= editor_detail::polygon_epsilon){
			return false;
		}
	}
	return true;
}

[[nodiscard]] constexpr bool same_polygon_point(const math::vec2 lhs, const math::vec2 rhs) noexcept{
	return (lhs - rhs).length2() <= editor_detail::polygon_epsilon * editor_detail::polygon_epsilon;
}

[[nodiscard]] constexpr bool polygon_point_on_segment(
	const math::vec2 point,
	const math::vec2 begin,
	const math::vec2 end) noexcept{
	if(std::abs((end - begin).cross(point - begin)) > editor_detail::polygon_epsilon){
		return false;
	}
	return point.x >= std::min(begin.x, end.x) - editor_detail::polygon_epsilon
		&& point.x <= std::max(begin.x, end.x) + editor_detail::polygon_epsilon
		&& point.y >= std::min(begin.y, end.y) - editor_detail::polygon_epsilon
		&& point.y <= std::max(begin.y, end.y) + editor_detail::polygon_epsilon;
}

[[nodiscard]] constexpr bool polygon_segments_intersect(
	const math::vec2 a,
	const math::vec2 b,
	const math::vec2 c,
	const math::vec2 d) noexcept{
	const float abc = (b - a).cross(c - a);
	const float abd = (b - a).cross(d - a);
	const float cda = (d - c).cross(a - c);
	const float cdb = (d - c).cross(b - c);

	if(std::abs(abc) <= editor_detail::polygon_epsilon && editor_detail::polygon_point_on_segment(c, a, b)){
		return true;
	}
	if(std::abs(abd) <= editor_detail::polygon_epsilon && editor_detail::polygon_point_on_segment(d, a, b)){
		return true;
	}
	if(std::abs(cda) <= editor_detail::polygon_epsilon && editor_detail::polygon_point_on_segment(a, c, d)){
		return true;
	}
	if(std::abs(cdb) <= editor_detail::polygon_epsilon && editor_detail::polygon_point_on_segment(b, c, d)){
		return true;
	}

	return ((abc > editor_detail::polygon_epsilon && abd < -editor_detail::polygon_epsilon)
			|| (abc < -editor_detail::polygon_epsilon && abd > editor_detail::polygon_epsilon))
		&& ((cda > editor_detail::polygon_epsilon && cdb < -editor_detail::polygon_epsilon)
			|| (cda < -editor_detail::polygon_epsilon && cdb > editor_detail::polygon_epsilon));
}

[[nodiscard]] constexpr bool polygon_edges_adjacent(
	const std::size_t lhs,
	const std::size_t rhs,
	const std::size_t vertex_count) noexcept{
	return lhs == rhs
		|| (lhs + 1u) % vertex_count == rhs
		|| (rhs + 1u) % vertex_count == lhs;
}

[[nodiscard]] constexpr bool polygon_self_intersects(const std::span<const math::vec2> vertices) noexcept{
	if(vertices.size() < 4u){
		return false;
	}

	for(std::size_t lhs = 0u; lhs != vertices.size(); ++lhs){
		const math::vec2 lhs_begin = vertices[lhs];
		const math::vec2 lhs_end = vertices[(lhs + 1u) % vertices.size()];
		for(std::size_t rhs = lhs + 1u; rhs != vertices.size(); ++rhs){
			if(editor_detail::polygon_edges_adjacent(lhs, rhs, vertices.size())){
				continue;
			}
			const math::vec2 rhs_begin = vertices[rhs];
			const math::vec2 rhs_end = vertices[(rhs + 1u) % vertices.size()];
			if(editor_detail::polygon_segments_intersect(lhs_begin, lhs_end, rhs_begin, rhs_end)){
				return true;
			}
		}
	}
	return false;
}

[[nodiscard]] constexpr bool editable_polygon(const std::span<const math::vec2> vertices) noexcept{
	if(vertices.size() < 3u){
		return false;
	}
	if(!std::ranges::all_of(vertices, editor_detail::finite_vec2)){
		return false;
	}
	for(std::size_t lhs = 0u; lhs != vertices.size(); ++lhs){
		for(std::size_t rhs = lhs + 1u; rhs != vertices.size(); ++rhs){
			if(editor_detail::same_polygon_point(vertices[lhs], vertices[rhs])){
				return false;
			}
		}
	}
	for(std::size_t first = 0u; first != vertices.size(); ++first){
		for(std::size_t second = first + 1u; second != vertices.size(); ++second){
			for(std::size_t third = second + 1u; third != vertices.size(); ++third){
				if(std::abs((vertices[second] - vertices[first]).cross(vertices[third] - vertices[first]))
					> editor_detail::polygon_epsilon){
					return true;
				}
			}
		}
	}
	return false;
}

[[nodiscard]] constexpr bool editable_open_polyline(const std::span<const math::vec2> vertices) noexcept{
	if(vertices.size() < 2u){
		return false;
	}
	if(!std::ranges::all_of(vertices, editor_detail::finite_vec2)){
		return false;
	}
	for(std::size_t lhs = 0u; lhs != vertices.size(); ++lhs){
		for(std::size_t rhs = lhs + 1u; rhs != vertices.size(); ++rhs){
			if(editor_detail::same_polygon_point(vertices[lhs], vertices[rhs])){
				return false;
			}
		}
	}
	return true;
}

void append_unique_polygon_point(std::vector<math::vec2>& points, const math::vec2 point){
	if(!editor_detail::finite_vec2(point)){
		return;
	}
	if(std::ranges::any_of(points, [point](const math::vec2 existing) noexcept{
		return editor_detail::same_polygon_point(existing, point);
	})){
		return;
	}
	points.push_back(point);
}

[[nodiscard]] std::vector<math::vec2> convex_hull(std::span<const math::vec2> vertices){
	std::vector<math::vec2> points{};
	points.reserve(vertices.size());
	for(const math::vec2 vertex : vertices){
		editor_detail::append_unique_polygon_point(points, vertex);
	}

	if(points.size() < 3u){
		throw std::invalid_argument{"convex hull requires at least three unique finite vertices"};
	}

	std::ranges::sort(points, [](const math::vec2 lhs, const math::vec2 rhs) noexcept{
		if(lhs.x == rhs.x){
			return lhs.y < rhs.y;
		}
		return lhs.x < rhs.x;
	});

	std::vector<math::vec2> hull{};
	hull.reserve(points.size() * 2u);
	const auto append_half = [&hull](const std::span<const math::vec2> source){
		for(const math::vec2 point : source){
			while(hull.size() >= 2u){
				const math::vec2 a = hull[hull.size() - 2u];
				const math::vec2 b = hull[hull.size() - 1u];
				if((b - a).cross(point - b) > editor_detail::polygon_epsilon){
					break;
				}
				hull.pop_back();
			}
			hull.push_back(point);
		}
	};

	append_half(points);
	const std::size_t lower_size = hull.size();
	for(auto cursor = points.rbegin() + 1; cursor != points.rend(); ++cursor){
		while(hull.size() > lower_size){
			const math::vec2 a = hull[hull.size() - 2u];
			const math::vec2 b = hull[hull.size() - 1u];
			if((b - a).cross(*cursor - b) > editor_detail::polygon_epsilon){
				break;
			}
			hull.pop_back();
		}
		hull.push_back(*cursor);
	}

	if(!hull.empty()){
		hull.pop_back();
	}
	if(hull.size() < 3u || std::abs(editor_detail::signed_area(hull)) <= editor_detail::polygon_epsilon){
		throw std::invalid_argument{"convex hull result is degenerate"};
	}
	if(editor_detail::signed_area(hull) < 0.f){
		std::ranges::reverse(hull);
	}
	return hull;
}

[[nodiscard]] std::vector<std::size_t> normalize_part_indices(std::span<const std::size_t> part_indices){
	std::vector<std::size_t> indices{part_indices.begin(), part_indices.end()};
	std::ranges::sort(indices);
	indices.erase(std::ranges::unique(indices).begin(), indices.end());
	return indices;
}

struct polygon_segment_intersection{
	std::size_t edge_index{};
	float segment_t{};
	float edge_t{};
	math::vec2 point{};
};

[[nodiscard]] std::optional<polygon_segment_intersection> intersect_segment_with_polygon_edge(
	const math::vec2 segment_begin,
	const math::vec2 segment_end,
	const math::vec2 edge_begin,
	const math::vec2 edge_end,
	const std::size_t edge_index) noexcept{
	const math::vec2 segment = segment_end - segment_begin;
	const math::vec2 edge = edge_end - edge_begin;
	const float denominator = segment.cross(edge);
	if(std::abs(denominator) <= editor_detail::polygon_epsilon){
		return std::nullopt;
	}

	const math::vec2 offset = edge_begin - segment_begin;
	const float segment_t = offset.cross(edge) / denominator;
	const float edge_t = offset.cross(segment) / denominator;
	if(segment_t < -editor_detail::polygon_epsilon
		|| segment_t > 1.f + editor_detail::polygon_epsilon
		|| edge_t < -editor_detail::polygon_epsilon
		|| edge_t > 1.f + editor_detail::polygon_epsilon){
		return std::nullopt;
	}

	const float clamped_segment_t = std::clamp(segment_t, 0.f, 1.f);
	const float clamped_edge_t = std::clamp(edge_t, 0.f, 1.f);
	return polygon_segment_intersection{
		.edge_index = edge_index,
		.segment_t = clamped_segment_t,
		.edge_t = clamped_edge_t,
		.point = segment_begin + segment * clamped_segment_t
	};
}

[[nodiscard]] std::vector<polygon_segment_intersection> polygon_segment_intersections(
	const std::span<const math::vec2> vertices,
	const math::vec2 segment_begin,
	const math::vec2 segment_end){
	std::vector<polygon_segment_intersection> intersections{};
	if(vertices.size() < 3u){
		return intersections;
	}

	for(std::size_t edge_index = 0u; edge_index != vertices.size(); ++edge_index){
		const math::vec2 edge_begin = vertices[edge_index];
		const math::vec2 edge_end = vertices[(edge_index + 1u) % vertices.size()];
		if(auto intersection = editor_detail::intersect_segment_with_polygon_edge(
			segment_begin,
			segment_end,
			edge_begin,
			edge_end,
			edge_index)){
			intersections.push_back(*intersection);
		}
	}

	std::ranges::sort(intersections, [](const auto& lhs, const auto& rhs) noexcept{
		return lhs.segment_t < rhs.segment_t;
	});
	intersections.erase(
		std::ranges::unique(intersections, [](const auto& lhs, const auto& rhs) noexcept{
			return editor_detail::same_polygon_point(lhs.point, rhs.point);
		}).begin(),
		intersections.end());
	return intersections;
}

[[nodiscard]] std::optional<std::array<std::vector<math::vec2>, 2u>> split_polygon_vertices_by_segment(
	const std::span<const math::vec2> vertices,
	const math::vec2 segment_begin,
	const math::vec2 segment_end){
	const auto intersections = editor_detail::polygon_segment_intersections(vertices, segment_begin, segment_end);
	if(intersections.size() != 2u){
		return std::nullopt;
	}

	const polygon_segment_intersection first = intersections[0u];
	const polygon_segment_intersection second = intersections[1u];
	if(first.edge_index == second.edge_index){
		return std::nullopt;
	}

	const auto make_path = [vertices](const polygon_segment_intersection& begin, const polygon_segment_intersection& end){
		std::vector<math::vec2> path{};
		path.reserve(vertices.size() + 2u);
		path.push_back(begin.point);
		std::size_t current = (begin.edge_index + 1u) % vertices.size();
		const std::size_t stop = (end.edge_index + 1u) % vertices.size();
		while(current != stop){
			if(!editor_detail::same_polygon_point(path.back(), vertices[current])){
				path.push_back(vertices[current]);
			}
			current = (current + 1u) % vertices.size();
		}
		if(!editor_detail::same_polygon_point(path.back(), end.point)){
			path.push_back(end.point);
		}
		return path;
	};

	std::array<std::vector<math::vec2>, 2u> result{
		make_path(first, second),
		make_path(second, first)
	};
	if(!editor_detail::editable_polygon(result[0u]) || !editor_detail::editable_polygon(result[1u])){
		return std::nullopt;
	}
	return result;
}

template <typename Shape>
void append_runtime_part(
	collision_shape& out,
	const math::trans2 local_transform,
	const math::trans2 total_transform,
	Shape shape){
	out.add(shape_of<Shape>{
		.local_transform = local_transform >> total_transform,
		.shape = std::move(shape)
	});
}
}

export
enum class collision_shape_editor_polygon_export_mode : std::uint8_t{
	convex_hull,
	decompose
};

export
[[nodiscard]] constexpr std::string_view collision_shape_editor_polygon_export_mode_name(
	const collision_shape_editor_polygon_export_mode mode) noexcept{
	switch(mode){
	case collision_shape_editor_polygon_export_mode::convex_hull:
		return "convex hull";
	case collision_shape_editor_polygon_export_mode::decompose:
		return "decompose";
	default:
		return "unknown";
	}
}

export
struct collision_shape_editor_mirror_modifier{
	bool mirror_x{};
	bool mirror_y{};
	math::trans2 origin{};

	[[nodiscard]] constexpr bool active() const noexcept{
		return mirror_x || mirror_y;
	}
};

export
[[nodiscard]] inline math::vec2 mirror_collision_shape_editor_point_on_axes(
	const math::vec2 point,
	const collision_shape_editor_mirror_modifier& mirror,
	const bool mirror_x,
	const bool mirror_y) noexcept{
	math::vec2 local = mirror.origin.apply_inv_to(point);
	if(mirror_x){
		local.x = -local.x;
	}
	if(mirror_y){
		local.y = -local.y;
	}
	return mirror.origin.apply_to(local);
}

export
[[nodiscard]] inline math::vec2 mirror_collision_shape_editor_point(
	const math::vec2 point,
	const collision_shape_editor_mirror_modifier& mirror) noexcept{
	return physics::mirror_collision_shape_editor_point_on_axes(
		point,
		mirror,
		mirror.mirror_x,
		mirror.mirror_y);
}

export
[[nodiscard]] inline math::vec2 snap_collision_shape_editor_point_to_mirror_axes(
	const math::vec2 point,
	const collision_shape_editor_mirror_modifier& mirror,
	const float epsilon = editor_detail::polygon_epsilon) noexcept{
	if(!mirror.active()){
		return point;
	}

	math::vec2 local = mirror.origin.apply_inv_to(point);
	if(mirror.mirror_x && std::abs(local.x) <= epsilon){
		local.x = 0.f;
	}
	if(mirror.mirror_y && std::abs(local.y) <= epsilon){
		local.y = 0.f;
	}
	return mirror.origin.apply_to(local);
}

export
[[nodiscard]] constexpr bool collision_shape_editor_polygon_editable(
	const std::span<const math::vec2> vertices) noexcept{
	return editor_detail::editable_polygon(vertices);
}

export
[[nodiscard]] constexpr bool collision_shape_editor_open_polyline_editable(
	const std::span<const math::vec2> vertices) noexcept{
	return editor_detail::editable_open_polyline(vertices);
}

export
[[nodiscard]] constexpr bool collision_shape_editor_polygon_convex(
	const std::span<const math::vec2> vertices) noexcept{
	return editor_detail::strict_ccw_convex_polygon(vertices);
}

export
[[nodiscard]] constexpr bool collision_shape_editor_polygon_self_intersects(
	const std::span<const math::vec2> vertices) noexcept{
	return editor_detail::polygon_self_intersects(vertices);
}

export
[[nodiscard]] inline std::vector<math::vec2> collision_shape_editor_polygon_convex_hull(
	const std::span<const math::vec2> vertices){
	return editor_detail::convex_hull(vertices);
}

export
struct collision_shape_editor_polygon_edge{
	std::size_t first{};
	std::size_t second{};
};

export
enum class collision_shape_editor_polygon_graph_state : std::uint8_t{
	open,
	convex,
	concave,
	self_intersecting
};

export
[[nodiscard]] constexpr std::string_view collision_shape_editor_polygon_graph_state_name(
	const collision_shape_editor_polygon_graph_state state) noexcept{
	switch(state){
	case collision_shape_editor_polygon_graph_state::open:
		return "open";
	case collision_shape_editor_polygon_graph_state::convex:
		return "convex";
	case collision_shape_editor_polygon_graph_state::concave:
		return "concave";
	case collision_shape_editor_polygon_graph_state::self_intersecting:
		return "self-intersecting";
	default:
		return "unknown";
	}
}

export
struct collision_shape_editor_polygon_graph{
	std::vector<math::vec2> vertices{};
	std::vector<collision_shape_editor_polygon_edge> edges{};
};

export
struct collision_shape_editor_polygon_graph_analysis{
	collision_shape_editor_polygon_graph_state state{collision_shape_editor_polygon_graph_state::open};
	std::vector<math::vec2> ordered_vertices{};
	std::string error{"polygon edge graph must form one closed loop"};

	[[nodiscard]] bool exportable() const noexcept{
		return state == collision_shape_editor_polygon_graph_state::convex
			|| state == collision_shape_editor_polygon_graph_state::concave;
	}
};

export
struct collision_shape_editor_part;

export
[[nodiscard]] collision_shape_editor_polygon_graph collision_shape_editor_part_polygon_graph(
	const collision_shape_editor_part& part);

export
[[nodiscard]] collision_shape_editor_polygon_graph collision_shape_editor_part_effective_polygon_graph(
	const collision_shape_editor_part& part);

export
[[nodiscard]] collision_shape_editor_polygon_graph_analysis analyze_collision_shape_editor_polygon_graph(
	const collision_shape_editor_polygon_graph& graph);

export
[[nodiscard]] collision_shape_editor_polygon_graph_analysis analyze_collision_shape_editor_part_polygon_graph(
	const collision_shape_editor_part& part,
	bool include_mirror = true);

namespace editor_detail{
[[nodiscard]] constexpr bool valid_polygon_point_set(
	const std::span<const math::vec2> vertices,
	const bool closed = true) noexcept{
	return closed
		? editor_detail::editable_polygon(vertices)
		: editor_detail::editable_open_polyline(vertices);
}

[[nodiscard]] constexpr bool valid_polygon_edge_set(
	const std::span<const math::vec2> vertices,
	const std::span<const collision_shape_editor_polygon_edge> edges) noexcept{
	for(std::size_t index = 0u; index != edges.size(); ++index){
		const collision_shape_editor_polygon_edge edge = edges[index];
		if(edge.first >= vertices.size() || edge.second >= vertices.size() || edge.first == edge.second){
			return false;
		}
		for(std::size_t other_index = index + 1u; other_index != edges.size(); ++other_index){
			const collision_shape_editor_polygon_edge other = edges[other_index];
			if((edge.first == other.first && edge.second == other.second)
				|| (edge.first == other.second && edge.second == other.first)){
				return false;
			}
		}
	}
	return true;
}

void add_unique_polygon_graph_edge(
	std::vector<collision_shape_editor_polygon_edge>& edges,
	const collision_shape_editor_polygon_edge edge){
	if(edge.first == edge.second){
		return;
	}
	const bool duplicate = std::ranges::any_of(edges, [edge](const collision_shape_editor_polygon_edge existing) noexcept{
		return (existing.first == edge.first && existing.second == edge.second)
			|| (existing.first == edge.second && existing.second == edge.first);
	});
	if(!duplicate){
		edges.push_back(edge);
	}
}
}

export
struct collision_shape_editor_part{
	shape_type type{shape_type::box};
	math::trans2 local_transform{};
	circle_shape circle{32.f};
	capsule_shape capsule{{-40.f, 0.f}, {40.f, 0.f}, 12.f};
	box_shape box{{40.f, 28.f}};
	convex_polygon_shape convex_polygon{};
	collision_shape_editor_mirror_modifier mirror{};
	std::vector<collision_shape_editor_polygon_edge> polygon_edges{};
	bool polygon_edges_explicit{};
	bool closed{true};

	[[nodiscard]] static collision_shape_editor_part make_default(
		const shape_type type,
		const math::vec2 position = {}){
		collision_shape_editor_part part{};
		part.type = type;
		part.local_transform.vec = position;
		if(type == shape_type::convex_polygon){
			part.convex_polygon.vertices = {
				{-42.f, -30.f},
				{38.f, -28.f},
				{48.f, 24.f},
				{-24.f, 42.f}
			};
		}
		return part;
	}

	[[nodiscard]] bool payload_valid() const noexcept{
		if(!editor_detail::finite_vec2(local_transform.vec) || !std::isfinite(local_transform.rot)){
			return false;
		}

		switch(type){
		case shape_type::circle:
			return std::isfinite(circle.radius) && circle.radius > 0.f;
		case shape_type::capsule:
			return editor_detail::finite_vec2(capsule.begin)
				&& editor_detail::finite_vec2(capsule.end)
				&& std::isfinite(capsule.radius)
				&& capsule.radius > 0.f;
		case shape_type::box:
			return editor_detail::finite_vec2(box.half_extent)
				&& box.half_extent.x > 0.f
				&& box.half_extent.y > 0.f;
		case shape_type::convex_polygon:
			return editor_detail::valid_polygon_point_set(convex_polygon.vertices, closed)
				&& editor_detail::valid_polygon_edge_set(convex_polygon.vertices, polygon_edges);
		default:
			return false;
		}
	}

	[[nodiscard]] bool uses_explicit_polygon_edges() const noexcept{
		return type == shape_type::convex_polygon
			&& (polygon_edges_explicit || !polygon_edges.empty());
	}

	[[nodiscard]] std::expected<std::vector<math::vec2>, std::string> ordered_polygon_vertices_for_export() const{
		if(type != shape_type::convex_polygon){
			return std::unexpected{"ordered polygon export requires a polygon"};
		}
		const auto analysis = physics::analyze_collision_shape_editor_part_polygon_graph(*this, false);
		if(!analysis.exportable()){
			return std::unexpected{analysis.error};
		}
		return analysis.ordered_vertices;
	}

	template <typename Function>
	void export_polygon_vertices(
		const std::span<const math::vec2> vertices,
		Function&& function) const{
		if(editor_detail::strict_ccw_convex_polygon(vertices)){
			std::invoke(
				std::forward<Function>(function),
				physics::make_convex_polygon(vertices));
			return;
		}
		physics::decompose_polygon(vertices, std::forward<Function>(function));
	}

	void append_polygon_to(
		collision_shape& out,
		const std::span<const math::vec2> vertices,
		const math::trans2 total_transform) const{
		this->export_polygon_vertices(vertices, [&](convex_polygon_shape polygon){
			editor_detail::append_runtime_part(
				out,
				local_transform,
				total_transform,
				std::move(polygon));
		});
	}

	void append_to(
		collision_shape& out,
		const math::trans2 total_transform = {}) const{
		if(!this->payload_valid()){
			throw std::invalid_argument{"invalid collision shape editor part"};
		}

		switch(type){
		case shape_type::circle:
			editor_detail::append_runtime_part(out, local_transform, total_transform, circle);
			return;
		case shape_type::capsule:
			editor_detail::append_runtime_part(out, local_transform, total_transform, capsule);
			return;
		case shape_type::box:
			editor_detail::append_runtime_part(out, local_transform, total_transform, box);
			return;
		case shape_type::convex_polygon:
			if(auto ordered_vertices = this->ordered_polygon_vertices_for_export()){
				this->append_polygon_to(
					out,
					*ordered_vertices,
					total_transform);
				return;
			}else{
				throw std::invalid_argument{ordered_vertices.error()};
			}
		default:
			throw std::invalid_argument{"unknown collision shape editor part type"};
		}
	}
};

export
[[nodiscard]] constexpr bool collision_shape_editor_polygon_valid(
	const std::span<const math::vec2> vertices) noexcept{
	return editor_detail::strict_ccw_convex_polygon(vertices);
}

export
[[nodiscard]] inline float mirror_collision_shape_editor_angle(
	const float angle,
	const collision_shape_editor_mirror_modifier& mirror) noexcept{
	auto [cos_value, sin_value] = math::cos_sin(angle);
	math::vec2 direction{cos_value, sin_value};
	direction.rotate_rad(-mirror.origin.rot);
	if(mirror.mirror_x){
		direction.x = -direction.x;
	}
	if(mirror.mirror_y){
		direction.y = -direction.y;
	}
	direction.rotate_rad(mirror.origin.rot);
	return direction.angle_rad();
}

export
[[nodiscard]] inline math::vec2 clamp_collision_shape_editor_point_to_mirror_source_axes(
	const math::vec2 point,
	const collision_shape_editor_mirror_modifier& mirror) noexcept{
	if(!mirror.active()){
		return point;
	}
	math::vec2 local = mirror.origin.apply_inv_to(point);
	if(mirror.mirror_x && local.x < 0.f){
		local.x = 0.f;
	}
	if(mirror.mirror_y && local.y < 0.f){
		local.y = 0.f;
	}
	return mirror.origin.apply_to(local);
}

export
[[nodiscard]] inline collision_shape_editor_part mirror_collision_shape_editor_part(
	const collision_shape_editor_part& source,
	const collision_shape_editor_mirror_modifier& mirror){
	collision_shape_editor_part result = source;
	result.local_transform.vec = physics::mirror_collision_shape_editor_point(source.local_transform.vec, mirror);
	result.local_transform.rot = physics::mirror_collision_shape_editor_angle(source.local_transform.rot, mirror);

	switch(source.type){
	case shape_type::circle:
	case shape_type::box:
		break;
	case shape_type::capsule:{
		const math::vec2 mirrored_begin = physics::mirror_collision_shape_editor_point(
			source.capsule.begin >> source.local_transform,
			mirror);
		const math::vec2 mirrored_end = physics::mirror_collision_shape_editor_point(
			source.capsule.end >> source.local_transform,
			mirror);
		result.capsule.begin = result.local_transform.apply_inv_to(mirrored_begin);
		result.capsule.end = result.local_transform.apply_inv_to(mirrored_end);
		break;
	}
	case shape_type::convex_polygon:{
		result.convex_polygon.vertices.clear();
		result.convex_polygon.vertices.reserve(source.convex_polygon.vertices.size());
		for(const math::vec2 vertex : source.convex_polygon.vertices){
			const math::vec2 mirrored_vertex = physics::mirror_collision_shape_editor_point(
				vertex >> source.local_transform,
				mirror);
			result.convex_polygon.vertices.push_back(result.local_transform.apply_inv_to(mirrored_vertex));
		}
		if(mirror.mirror_x != mirror.mirror_y){
			std::ranges::reverse(result.convex_polygon.vertices);
			if(result.uses_explicit_polygon_edges()){
				const std::size_t vertex_count = result.convex_polygon.vertices.size();
				for(collision_shape_editor_polygon_edge& edge : result.polygon_edges){
					edge = {
						.first = vertex_count - 1u - edge.first,
						.second = vertex_count - 1u - edge.second
					};
				}
			}
		}
		break;
	}
	default:
		throw std::invalid_argument{"unknown collision shape editor part type"};
	}

	if(!result.payload_valid()){
		throw std::invalid_argument{"mirrored collision shape editor part is invalid"};
	}
	return result;
}

export
[[nodiscard]] inline std::vector<math::vec2> collision_shape_editor_part_polygon_vertices_with_mirror(
	const collision_shape_editor_part& source){
	return physics::collision_shape_editor_part_effective_polygon_graph(source).vertices;
}

export
void snap_collision_shape_editor_part_vertices_to_mirror_axes(collision_shape_editor_part& part) noexcept{
	if(part.type != shape_type::convex_polygon || !part.mirror.active()){
		return;
	}

	for(math::vec2& vertex : part.convex_polygon.vertices){
		const math::vec2 world = vertex >> part.local_transform;
		vertex = part.local_transform.apply_inv_to(
			physics::snap_collision_shape_editor_point_to_mirror_axes(
				world,
				part.mirror,
				editor_detail::mirror_axis_merge_epsilon));
	}

	std::vector<math::vec2> merged_vertices{};
	std::vector<std::size_t> vertex_remap(part.convex_polygon.vertices.size());
	merged_vertices.reserve(part.convex_polygon.vertices.size());
	for(std::size_t vertex_index = 0u; vertex_index != part.convex_polygon.vertices.size(); ++vertex_index){
		const math::vec2 vertex = part.convex_polygon.vertices[vertex_index];
		if(const auto existing = std::ranges::find_if(merged_vertices, [vertex](const math::vec2 existing) noexcept{
			return editor_detail::same_polygon_point(existing, vertex);
		}); existing != merged_vertices.end()){
			vertex_remap[vertex_index] = static_cast<std::size_t>(std::distance(merged_vertices.begin(), existing));
		}else{
			vertex_remap[vertex_index] = merged_vertices.size();
			merged_vertices.push_back(vertex);
		}
	}

	if(part.uses_explicit_polygon_edges()){
		std::vector<collision_shape_editor_polygon_edge> merged_edges{};
		merged_edges.reserve(part.polygon_edges.size());
		for(const collision_shape_editor_polygon_edge edge : part.polygon_edges){
			if(edge.first >= vertex_remap.size() || edge.second >= vertex_remap.size()){
				continue;
			}
			editor_detail::add_unique_polygon_graph_edge(
				merged_edges,
				collision_shape_editor_polygon_edge{
					.first = vertex_remap[edge.first],
					.second = vertex_remap[edge.second]
				});
		}
		part.polygon_edges = std::move(merged_edges);
	}
	part.convex_polygon.vertices = std::move(merged_vertices);
}

export
[[nodiscard]] inline std::size_t collision_shape_editor_part_polygon_edge_count(
	const collision_shape_editor_part& part) noexcept{
	if(part.type != shape_type::convex_polygon){
		return 0u;
	}
	if(part.uses_explicit_polygon_edges()){
		return part.polygon_edges.size();
	}
	const std::size_t vertex_count = part.convex_polygon.vertices.size();
	if(vertex_count < 2u){
		return 0u;
	}
	return vertex_count - 1u + (part.closed && vertex_count >= 3u ? 1u : 0u);
}

export
[[nodiscard]] inline collision_shape_editor_polygon_edge collision_shape_editor_part_polygon_edge_at(
	const collision_shape_editor_part& part,
	const std::size_t edge_index){
	if(part.uses_explicit_polygon_edges()){
		return part.polygon_edges.at(edge_index);
	}
	const std::size_t vertex_count = part.convex_polygon.vertices.size();
	if(edge_index + 1u < vertex_count){
		return {.first = edge_index, .second = edge_index + 1u};
	}
	if(part.closed && vertex_count >= 3u && edge_index + 1u == vertex_count){
		return {.first = vertex_count - 1u, .second = 0u};
	}
	throw std::out_of_range{"polygon edge index is out of range"};
}

export
inline void materialize_collision_shape_editor_part_polygon_edges(collision_shape_editor_part& part){
	if(part.type != shape_type::convex_polygon || part.uses_explicit_polygon_edges()){
		part.polygon_edges_explicit = part.type == shape_type::convex_polygon;
		return;
	}
	const std::size_t edge_count = physics::collision_shape_editor_part_polygon_edge_count(part);
	std::vector<collision_shape_editor_polygon_edge> materialized_edges{};
	materialized_edges.reserve(edge_count);
	for(std::size_t edge_index = 0u; edge_index != edge_count; ++edge_index){
		materialized_edges.push_back(physics::collision_shape_editor_part_polygon_edge_at(part, edge_index));
	}
	part.polygon_edges = std::move(materialized_edges);
	part.polygon_edges_explicit = true;
}

export
[[nodiscard]] inline collision_shape_editor_polygon_graph collision_shape_editor_part_polygon_graph(
	const collision_shape_editor_part& part){
	collision_shape_editor_polygon_graph result{};
	if(part.type != shape_type::convex_polygon){
		return result;
	}
	result.vertices = part.convex_polygon.vertices;
	const std::size_t edge_count = physics::collision_shape_editor_part_polygon_edge_count(part);
	result.edges.reserve(edge_count);
	for(std::size_t edge_index = 0u; edge_index != edge_count; ++edge_index){
		result.edges.push_back(physics::collision_shape_editor_part_polygon_edge_at(part, edge_index));
	}
	return result;
}

export
[[nodiscard]] inline collision_shape_editor_polygon_graph collision_shape_editor_part_effective_polygon_graph(
	const collision_shape_editor_part& part){
	collision_shape_editor_polygon_graph result = physics::collision_shape_editor_part_polygon_graph(part);
	if(part.type != shape_type::convex_polygon || !part.mirror.active()){
		return result;
	}

	const collision_shape_editor_part mirrored = physics::mirror_collision_shape_editor_part(part, part.mirror);
	const collision_shape_editor_polygon_graph mirrored_graph =
		physics::collision_shape_editor_part_polygon_graph(mirrored);
	std::vector<std::size_t> vertex_remap(mirrored_graph.vertices.size());
	for(std::size_t vertex_index = 0u; vertex_index != mirrored_graph.vertices.size(); ++vertex_index){
		const math::vec2 vertex = mirrored_graph.vertices[vertex_index];
		if(const auto existing = std::ranges::find_if(result.vertices, [vertex](const math::vec2 existing) noexcept{
			return editor_detail::same_polygon_point(existing, vertex);
		}); existing != result.vertices.end()){
			vertex_remap[vertex_index] = static_cast<std::size_t>(std::distance(result.vertices.begin(), existing));
		}else{
			vertex_remap[vertex_index] = result.vertices.size();
			result.vertices.push_back(vertex);
		}
	}

	for(const collision_shape_editor_polygon_edge edge : mirrored_graph.edges){
		if(edge.first >= vertex_remap.size() || edge.second >= vertex_remap.size()){
			continue;
		}
		editor_detail::add_unique_polygon_graph_edge(
			result.edges,
			collision_shape_editor_polygon_edge{
				.first = vertex_remap[edge.first],
				.second = vertex_remap[edge.second]
			});
	}
	return result;
}

export
[[nodiscard]] inline collision_shape_editor_polygon_graph_analysis analyze_collision_shape_editor_polygon_graph(
	const collision_shape_editor_polygon_graph& graph){
	collision_shape_editor_polygon_graph_analysis result{};
	if(graph.vertices.size() < 3u || graph.edges.size() < 3u){
		result.error = "polygon edge graph must form one closed loop";
		return result;
	}
	if(!editor_detail::valid_polygon_point_set(graph.vertices)
		|| !editor_detail::valid_polygon_edge_set(graph.vertices, graph.edges)){
		result.error = "polygon edge graph contains invalid vertices or edges";
		return result;
	}

	std::vector<std::array<std::size_t, 2u>> adjacency(graph.vertices.size());
	std::vector<std::size_t> degree(graph.vertices.size());
	for(const collision_shape_editor_polygon_edge edge : graph.edges){
		if(degree[edge.first] >= 2u || degree[edge.second] >= 2u){
			result.error = "polygon edge graph must not branch";
			return result;
		}
		adjacency[edge.first][degree[edge.first]++] = edge.second;
		adjacency[edge.second][degree[edge.second]++] = edge.first;
	}
	if(std::ranges::any_of(degree, [](const std::size_t value) noexcept{
		return value != 2u;
	})){
		result.error = "every polygon graph vertex must have exactly two edges for export";
		return result;
	}

	std::vector<std::size_t> ordered_indices{};
	ordered_indices.reserve(graph.vertices.size());
	std::vector<bool> visited(graph.vertices.size());
	std::size_t previous = std::numeric_limits<std::size_t>::max();
	std::size_t current{};
	for(std::size_t step = 0u; step != graph.vertices.size(); ++step){
		if(visited[current]){
			result.error = "polygon edge graph must form one closed loop";
			return result;
		}
		visited[current] = true;
		ordered_indices.push_back(current);
		const auto neighbors = adjacency[current];
		const std::size_t next = neighbors[0u] == previous ? neighbors[1u] : neighbors[0u];
		previous = current;
		current = next;
	}
	if(current != ordered_indices.front() || !std::ranges::all_of(visited, std::identity{})){
		result.error = "polygon edge graph must form one closed loop";
		return result;
	}

	result.ordered_vertices.reserve(ordered_indices.size());
	for(const std::size_t vertex_index : ordered_indices){
		result.ordered_vertices.push_back(graph.vertices[vertex_index]);
	}
	if(editor_detail::signed_area(result.ordered_vertices) < 0.f){
		std::ranges::reverse(result.ordered_vertices);
	}
	if(editor_detail::polygon_self_intersects(result.ordered_vertices)){
		result.state = collision_shape_editor_polygon_graph_state::self_intersecting;
		result.error = "polygon edge graph self-intersects";
		return result;
	}
	if(!editor_detail::editable_polygon(result.ordered_vertices)){
		result.ordered_vertices.clear();
		result.error = "polygon edge graph is degenerate";
		return result;
	}
	if(editor_detail::strict_ccw_convex_polygon(result.ordered_vertices)){
		result.state = collision_shape_editor_polygon_graph_state::convex;
		result.error.clear();
		return result;
	}
	result.state = collision_shape_editor_polygon_graph_state::concave;
	result.error.clear();
	return result;
}

export
[[nodiscard]] inline collision_shape_editor_polygon_graph_analysis analyze_collision_shape_editor_part_polygon_graph(
	const collision_shape_editor_part& part,
	const bool include_mirror){
	if(part.type != shape_type::convex_polygon){
		return {};
	}
	return physics::analyze_collision_shape_editor_polygon_graph(
		include_mirror
			? physics::collision_shape_editor_part_effective_polygon_graph(part)
			: physics::collision_shape_editor_part_polygon_graph(part));
}

export
struct collision_shape_editor_reference_image{
	bool enabled{};
	std::string path{};
	math::trans2 transform{};
	math::vec2 half_extent{};
	float opacity{0.35f};

	[[nodiscard]] bool visible() const noexcept{
		return enabled
			&& !path.empty()
			&& editor_detail::finite_vec2(transform.vec)
			&& std::isfinite(transform.rot)
			&& editor_detail::finite_vec2(half_extent)
			&& half_extent.x > 0.f
			&& half_extent.y > 0.f
			&& std::isfinite(opacity)
			&& opacity > 0.f;
	}
};

export
struct collision_shape_editor_document{
	std::vector<collision_shape_editor_part> parts{};
	math::trans2 total_transform{};
	collision_shape_editor_reference_image reference_image{};
	collision_shape_editor_polygon_export_mode polygon_export_mode{
		collision_shape_editor_polygon_export_mode::convex_hull
	};

	[[nodiscard]] std::size_t add_shape(const shape_type type, const math::vec2 position = {}){
		parts.push_back(collision_shape_editor_part::make_default(type, position));
		return parts.size() - 1u;
	}

	[[nodiscard]] bool set_polygon_vertex(
		const std::size_t part_index,
		const std::size_t vertex_index,
		const math::vec2 local_vertex){
		if(part_index >= parts.size()){
			return false;
		}

		collision_shape_editor_part& part = parts[part_index];
		if(part.type != shape_type::convex_polygon || vertex_index >= part.convex_polygon.vertices.size()){
			return false;
		}

		collision_shape_editor_part candidate = part;
		candidate.convex_polygon.vertices[vertex_index] = local_vertex;
		physics::snap_collision_shape_editor_part_vertices_to_mirror_axes(candidate);
		if(!candidate.payload_valid()){
			return false;
		}

		part = std::move(candidate);
		return true;
	}

	[[nodiscard]] bool has_non_convex_polygon(const bool include_mirror = true) const{
		for(const collision_shape_editor_part& part : parts){
			if(part.type != shape_type::convex_polygon){
				continue;
			}
			const auto analysis = physics::analyze_collision_shape_editor_part_polygon_graph(part, include_mirror);
			if(analysis.state == collision_shape_editor_polygon_graph_state::concave){
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool has_self_intersecting_polygon(const bool include_mirror = true) const{
		for(const collision_shape_editor_part& part : parts){
			if(part.type != shape_type::convex_polygon){
				continue;
			}
			const auto analysis = physics::analyze_collision_shape_editor_part_polygon_graph(part, include_mirror);
			if(analysis.state == collision_shape_editor_polygon_graph_state::self_intersecting){
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool has_open_polygon(const bool include_mirror = true) const{
		return std::ranges::any_of(parts, [include_mirror](const collision_shape_editor_part& part){
			return part.type == shape_type::convex_polygon
				&& physics::analyze_collision_shape_editor_part_polygon_graph(part, include_mirror).state
					== collision_shape_editor_polygon_graph_state::open;
		});
	}

	[[nodiscard]] collision_shape to_runtime_shape(const bool include_mirror = true) const{
		collision_shape result{};
		for(const collision_shape_editor_part& part : parts){
			if(include_mirror && part.mirror.active() && part.type == shape_type::convex_polygon){
				const auto analysis = physics::analyze_collision_shape_editor_part_polygon_graph(part, true);
				if(!analysis.exportable()){
					throw std::invalid_argument{analysis.error};
				}
				part.append_polygon_to(result, analysis.ordered_vertices, total_transform);
				continue;
			}
			part.append_to(result, total_transform);
			if(include_mirror && part.mirror.active()){
				physics::mirror_collision_shape_editor_part(part, part.mirror).append_to(
					result,
					total_transform);
			}
		}
		return result;
	}

	[[nodiscard]] collision_shape_record to_packed_record(const bool include_mirror = true) const{
		return this->to_runtime_shape(include_mirror).to_record();
	}
};

export
struct collision_shape_editor_polygon_cut_result{
	std::size_t first_part{};
	std::size_t second_part{};
};

export
struct collision_shape_editor_knife_cut_result{
	std::vector<std::size_t> part_indices{};
};

export
[[nodiscard]] inline std::expected<void, std::string> set_collision_shape_editor_polygon_closed(
	collision_shape_editor_part& part,
	const bool closed){
	if(part.type != shape_type::convex_polygon){
		return std::unexpected{"closed state is only valid for polygons"};
	}

	auto candidate = part;
	candidate.closed = closed;
	if(candidate.closed && candidate.uses_explicit_polygon_edges() && !candidate.ordered_polygon_vertices_for_export()){
		return std::unexpected{"closing requires the polygon edge set to form one closed loop"};
	}
	if(!candidate.payload_valid()){
		return std::unexpected{closed
			? "closing would make the polygon invalid"
			: "opening would make the polygon invalid"};
	}

	part = std::move(candidate);
	physics::snap_collision_shape_editor_part_vertices_to_mirror_axes(part);
	return {};
}

export
[[nodiscard]] inline std::expected<std::size_t, std::string> insert_collision_shape_editor_polygon_vertex(
	collision_shape_editor_part& part,
	const math::vec2 local_vertex,
	const std::optional<std::size_t> insert_after = std::nullopt){
	if(part.type != shape_type::convex_polygon){
		return std::unexpected{"vertex insert requires a polygon"};
	}
	if(!editor_detail::finite_vec2(local_vertex)){
		return std::unexpected{"vertex insert position is invalid"};
	}
	if(insert_after && *insert_after >= part.convex_polygon.vertices.size()){
		return std::unexpected{"vertex insert target no longer exists"};
	}
	if(part.uses_explicit_polygon_edges() && insert_after){
		return std::unexpected{"ordered vertex insert requires an implicit polygon"};
	}

	collision_shape_editor_part candidate = part;
	const std::size_t insert_index = part.uses_explicit_polygon_edges()
		? candidate.convex_polygon.vertices.size()
		: (insert_after
			? *insert_after + 1u
			: candidate.convex_polygon.vertices.size());
	if(part.uses_explicit_polygon_edges()){
		candidate.convex_polygon.vertices.push_back(local_vertex);
	}else{
		candidate.convex_polygon.vertices.insert(
			candidate.convex_polygon.vertices.begin() + static_cast<std::ptrdiff_t>(insert_index),
			local_vertex);
	}
	physics::snap_collision_shape_editor_part_vertices_to_mirror_axes(candidate);
	if(candidate.uses_explicit_polygon_edges()){
		candidate.closed = candidate.ordered_polygon_vertices_for_export().has_value();
	}
	if(!candidate.payload_valid()){
		return std::unexpected{candidate.closed
			? "vertex insert would make the polygon invalid"
			: "vertex insert would make the open polygon invalid"};
	}

	part = std::move(candidate);
	return insert_index;
}

export
[[nodiscard]] inline std::expected<std::size_t, std::string> connect_collision_shape_editor_polygon_vertices_as_edge(
	collision_shape_editor_document& document,
	const std::size_t part_index,
	const std::size_t first_vertex,
	const std::size_t second_vertex){
	if(part_index >= document.parts.size()){
		return std::unexpected{"edge target no longer exists"};
	}

	const collision_shape_editor_part& source = document.parts[part_index];
	if(source.type != shape_type::convex_polygon){
		return std::unexpected{"edge creation requires a polygon"};
	}
	if(first_vertex >= source.convex_polygon.vertices.size()
		|| second_vertex >= source.convex_polygon.vertices.size()
		|| first_vertex == second_vertex){
		return std::unexpected{"edge creation requires two valid polygon vertices"};
	}
	const std::size_t source_edge_count = physics::collision_shape_editor_part_polygon_edge_count(source);
	for(std::size_t edge_index = 0u; edge_index != source_edge_count; ++edge_index){
		const collision_shape_editor_polygon_edge edge =
			physics::collision_shape_editor_part_polygon_edge_at(source, edge_index);
		if((edge.first == first_vertex && edge.second == second_vertex)
			|| (edge.first == second_vertex && edge.second == first_vertex)){
			return std::unexpected{"edge already exists"};
		}
	}

	collision_shape_editor_part candidate = source;
	physics::materialize_collision_shape_editor_part_polygon_edges(candidate);
	candidate.polygon_edges.push_back({
		.first = first_vertex,
		.second = second_vertex
	});
	if(!candidate.payload_valid()){
		return std::unexpected{"edge creation would make the polygon edge set invalid"};
	}

	document.parts[part_index] = std::move(candidate);
	return document.parts[part_index].polygon_edges.size() - 1u;
}

export
[[nodiscard]] inline std::expected<collision_shape_editor_polygon_cut_result, std::string>
cut_collision_shape_editor_polygon(
	collision_shape_editor_document& document,
	const std::size_t part_index,
	const std::size_t first_vertex,
	const std::size_t second_vertex){
	if(part_index >= document.parts.size()){
		return std::unexpected{"cut target no longer exists"};
	}

	const collision_shape_editor_part source = document.parts[part_index];
	if(source.type != shape_type::convex_polygon){
		return std::unexpected{"cut requires a polygon"};
	}
	if(!source.closed){
		return std::unexpected{"cut currently requires a closed polygon"};
	}
	if(source.uses_explicit_polygon_edges()){
		return std::unexpected{"cut currently requires an ordered polygon"};
	}

	const std::size_t vertex_count = source.convex_polygon.vertices.size();
	if(first_vertex >= vertex_count || second_vertex >= vertex_count || first_vertex == second_vertex){
		return std::unexpected{"cut requires two valid polygon vertices"};
	}
	if((first_vertex + 1u) % vertex_count == second_vertex
		|| (second_vertex + 1u) % vertex_count == first_vertex){
		return std::unexpected{"cut vertices must not be adjacent"};
	}

	const auto make_arc = [&source, vertex_count](const std::size_t begin, const std::size_t end){
		std::vector<math::vec2> vertices{};
		std::size_t current = begin;
		for(;;){
			vertices.push_back(source.convex_polygon.vertices[current]);
			if(current == end){
				break;
			}
			current = (current + 1u) % vertex_count;
		}
		return vertices;
	};

	auto first_vertices = make_arc(first_vertex, second_vertex);
	auto second_vertices = make_arc(second_vertex, first_vertex);
	if(!editor_detail::editable_polygon(first_vertices) || !editor_detail::editable_polygon(second_vertices)){
		return std::unexpected{"cut would produce an invalid polygon"};
	}

	collision_shape_editor_part first_part = source;
	first_part.convex_polygon.vertices = std::move(first_vertices);
	collision_shape_editor_part second_part = source;
	second_part.convex_polygon.vertices = std::move(second_vertices);
	physics::snap_collision_shape_editor_part_vertices_to_mirror_axes(first_part);
	physics::snap_collision_shape_editor_part_vertices_to_mirror_axes(second_part);
	if(!first_part.payload_valid() || !second_part.payload_valid()){
		return std::unexpected{"cut would produce an invalid polygon"};
	}

	document.parts[part_index] = std::move(first_part);
	const std::size_t inserted_index = part_index + 1u;
	document.parts.insert(
		document.parts.begin() + static_cast<std::ptrdiff_t>(inserted_index),
		std::move(second_part));
	return collision_shape_editor_polygon_cut_result{
		.first_part = part_index,
		.second_part = inserted_index
	};
}

export
[[nodiscard]] inline std::expected<collision_shape_editor_knife_cut_result, std::string>
knife_cut_collision_shape_editor_polygon(
	collision_shape_editor_document& document,
	const std::size_t part_index,
	const std::span<const math::vec2> world_polyline){
	if(part_index >= document.parts.size()){
		return std::unexpected{"knife cut target no longer exists"};
	}
	if(world_polyline.size() < 2u){
		return std::unexpected{"knife cut requires at least two points"};
	}

	collision_shape_editor_part source = document.parts[part_index];
	if(source.type != shape_type::convex_polygon){
		return std::unexpected{"knife cut requires a polygon"};
	}
	if(!source.closed){
		return std::unexpected{"knife cut requires a closed polygon"};
	}
	if(source.uses_explicit_polygon_edges()){
		auto ordered_vertices = source.ordered_polygon_vertices_for_export();
		if(!ordered_vertices){
			return std::unexpected{ordered_vertices.error()};
		}
		source.convex_polygon.vertices = std::move(*ordered_vertices);
		source.polygon_edges.clear();
		source.polygon_edges_explicit = false;
	}

	std::vector<collision_shape_editor_part> working_parts{source};
	bool cut_any{};
	for(std::size_t segment_index = 0u; segment_index + 1u < world_polyline.size(); ++segment_index){
		const math::vec2 segment_begin = source.local_transform.apply_inv_to(world_polyline[segment_index]);
		const math::vec2 segment_end = source.local_transform.apply_inv_to(world_polyline[segment_index + 1u]);
		if(editor_detail::same_polygon_point(segment_begin, segment_end)){
			continue;
		}

		std::vector<collision_shape_editor_part> next_parts{};
		next_parts.reserve(working_parts.size() + 1u);
		for(const collision_shape_editor_part& part : working_parts){
			auto split = editor_detail::split_polygon_vertices_by_segment(
				part.convex_polygon.vertices,
				segment_begin,
				segment_end);
			if(!split){
				next_parts.push_back(part);
				continue;
			}

			for(std::vector<math::vec2>& vertices : *split){
				collision_shape_editor_part split_part = part;
				split_part.convex_polygon.vertices = std::move(vertices);
				physics::snap_collision_shape_editor_part_vertices_to_mirror_axes(split_part);
				if(!split_part.payload_valid()){
					return std::unexpected{"knife cut would produce an invalid polygon"};
				}
				next_parts.push_back(std::move(split_part));
			}
			cut_any = true;
		}
		working_parts = std::move(next_parts);
	}

	if(!cut_any){
		return std::unexpected{"knife polyline did not cut the selected polygon"};
	}

	document.parts.erase(document.parts.begin() + static_cast<std::ptrdiff_t>(part_index));
	std::vector<std::size_t> inserted_indices{};
	inserted_indices.reserve(working_parts.size());
	for(std::size_t index = 0u; index != working_parts.size(); ++index){
		const std::size_t insert_index = part_index + index;
		document.parts.insert(
			document.parts.begin() + static_cast<std::ptrdiff_t>(insert_index),
			std::move(working_parts[index]));
		inserted_indices.push_back(insert_index);
	}

	return collision_shape_editor_knife_cut_result{
		.part_indices = std::move(inserted_indices)
	};
}

export
[[nodiscard]] inline std::expected<std::size_t, std::string> merge_collision_shape_editor_polygons_as_hull(
	collision_shape_editor_document& document,
	const std::span<const std::size_t> part_indices,
	const std::optional<std::size_t> active_part_index = std::nullopt){
	std::vector<std::size_t> indices = editor_detail::normalize_part_indices(part_indices);
	if(indices.size() < 2u){
		return std::unexpected{"polygon hull merge requires at least two selected polygons"};
	}
	if(indices.back() >= document.parts.size()){
		return std::unexpected{"polygon hull merge selection contains a missing part"};
	}

	const std::size_t target_index = active_part_index && std::ranges::contains(indices, *active_part_index)
		? *active_part_index
		: indices.front();
	const collision_shape_editor_part& target = document.parts[target_index];
	if(target.type != shape_type::convex_polygon){
		return std::unexpected{"polygon hull merge target must be a polygon"};
	}

	std::vector<math::vec2> merged_points{};
	for(const std::size_t index : indices){
		const collision_shape_editor_part& part = document.parts[index];
		if(part.type != shape_type::convex_polygon){
			return std::unexpected{"polygon hull merge only accepts polygons"};
		}
		for(const math::vec2 vertex : part.convex_polygon.vertices){
			const math::vec2 world = vertex >> part.local_transform;
			merged_points.push_back(target.local_transform.apply_inv_to(world));
		}
	}

	collision_shape_editor_part merged = target;
	try{
		merged.convex_polygon.vertices = editor_detail::convex_hull(merged_points);
	}catch(const std::exception& e){
		return std::unexpected{std::format("polygon hull merge failed: {}", e.what())};
	}
	merged.closed = true;
	merged.mirror = {};
	merged.polygon_edges.clear();
	merged.polygon_edges_explicit = false;
	if(!merged.payload_valid()){
		return std::unexpected{"polygon hull merge would produce an invalid polygon"};
	}

	document.parts[target_index] = std::move(merged);
	for(auto cursor = indices.rbegin(); cursor != indices.rend(); ++cursor){
		if(*cursor == target_index){
			continue;
		}
		document.parts.erase(document.parts.begin() + static_cast<std::ptrdiff_t>(*cursor));
	}
	const auto removed_before_target = std::ranges::count_if(indices, [target_index](const std::size_t index) noexcept{
		return index < target_index;
	});
	return target_index - static_cast<std::size_t>(removed_before_target);
}

export
[[nodiscard]] inline std::expected<std::size_t, std::string> join_collision_shape_editor_open_polygons(
	collision_shape_editor_document& document,
	const std::span<const std::size_t> part_indices){
	std::vector<std::size_t> indices = editor_detail::normalize_part_indices(part_indices);
	if(indices.size() < 2u){
		return std::unexpected{"open polygon join requires at least two selected polygons"};
	}
	if(indices.back() >= document.parts.size()){
		return std::unexpected{"open polygon join selection contains a missing part"};
	}

	const std::size_t target_index = indices.front();
	const collision_shape_editor_part& target = document.parts[target_index];
	if(target.type != shape_type::convex_polygon || target.closed){
		return std::unexpected{"open polygon join target must be an open polygon"};
	}
	if(target.uses_explicit_polygon_edges()){
		return std::unexpected{"open polygon join requires ordered open polygon paths"};
	}

	std::vector<std::vector<math::vec2>> paths{};
	paths.reserve(indices.size());
	for(const std::size_t index : indices){
		const collision_shape_editor_part& part = document.parts[index];
		if(part.type != shape_type::convex_polygon || part.closed){
			return std::unexpected{"open polygon join only accepts open polygons"};
		}
		if(part.uses_explicit_polygon_edges()){
			return std::unexpected{"open polygon join requires ordered open polygon paths"};
		}

		std::vector<math::vec2> path{};
		path.reserve(part.convex_polygon.vertices.size());
		for(const math::vec2 vertex : part.convex_polygon.vertices){
			const math::vec2 world = vertex >> part.local_transform;
			path.push_back(target.local_transform.apply_inv_to(world));
		}
		if(!editor_detail::editable_open_polyline(path)){
			return std::unexpected{"open polygon join selection contains an invalid open polygon"};
		}
		paths.push_back(std::move(path));
	}

	std::vector<math::vec2> joined = std::move(paths.front());
	paths.erase(paths.begin());
	while(!paths.empty()){
		struct best_stitch{
			std::size_t path_index{};
			bool reverse{};
			float distance2{std::numeric_limits<float>::infinity()};
		};

		best_stitch best{};
		for(std::size_t index = 0u; index != paths.size(); ++index){
			const auto& path = paths[index];
			const float front_distance = joined.back().dst2(path.front());
			if(front_distance < best.distance2){
				best = {.path_index = index, .reverse = false, .distance2 = front_distance};
			}
			const float back_distance = joined.back().dst2(path.back());
			if(back_distance < best.distance2){
				best = {.path_index = index, .reverse = true, .distance2 = back_distance};
			}
		}

		auto path = std::move(paths[best.path_index]);
		paths.erase(paths.begin() + static_cast<std::ptrdiff_t>(best.path_index));
		if(best.reverse){
			std::ranges::reverse(path);
		}

		for(const math::vec2 vertex : path){
			if(!joined.empty() && editor_detail::same_polygon_point(joined.back(), vertex)){
				continue;
			}
			joined.push_back(vertex);
		}
	}

	if(!editor_detail::editable_open_polyline(joined)){
		return std::unexpected{"open polygon join would produce an invalid open polygon"};
	}

	collision_shape_editor_part merged = target;
	merged.convex_polygon.vertices = std::move(joined);
	merged.closed = false;
	merged.mirror = {};
	merged.polygon_edges.clear();
	merged.polygon_edges_explicit = false;
	document.parts[target_index] = std::move(merged);
	for(auto cursor = indices.rbegin(); cursor != indices.rend(); ++cursor){
		if(*cursor == target_index){
			continue;
		}
		document.parts.erase(document.parts.begin() + static_cast<std::ptrdiff_t>(*cursor));
	}
	return target_index;
}
}

export namespace mo_yanxi::game::collision::editor{
using polygon_export_mode = physics::collision_shape_editor_polygon_export_mode;
using mirror_modifier = physics::collision_shape_editor_mirror_modifier;
using polygon_edge = physics::collision_shape_editor_polygon_edge;
using graph_state = physics::collision_shape_editor_polygon_graph_state;
using polygon_graph = physics::collision_shape_editor_polygon_graph;
using graph_analysis = physics::collision_shape_editor_polygon_graph_analysis;
using part = physics::collision_shape_editor_part;
using reference_image = physics::collision_shape_editor_reference_image;
using document = physics::collision_shape_editor_document;
using polygon_cut_result = physics::collision_shape_editor_polygon_cut_result;
using knife_cut_result = physics::collision_shape_editor_knife_cut_result;

enum class join_origin_mode : std::uint8_t{
	center,
	first,
	last
};

[[nodiscard]] constexpr std::string_view polygon_export_mode_name(
	const polygon_export_mode mode) noexcept{
	return physics::collision_shape_editor_polygon_export_mode_name(mode);
}

[[nodiscard]] constexpr std::string_view graph_state_name(const graph_state state) noexcept{
	return physics::collision_shape_editor_polygon_graph_state_name(state);
}

[[nodiscard]] constexpr bool polygon_editable(const std::span<const math::vec2> vertices) noexcept{
	return physics::collision_shape_editor_polygon_editable(vertices);
}

[[nodiscard]] constexpr bool open_polyline_editable(const std::span<const math::vec2> vertices) noexcept{
	return physics::collision_shape_editor_open_polyline_editable(vertices);
}

[[nodiscard]] constexpr bool polygon_convex(const std::span<const math::vec2> vertices) noexcept{
	return physics::collision_shape_editor_polygon_convex(vertices);
}

[[nodiscard]] constexpr bool polygon_self_intersects(const std::span<const math::vec2> vertices) noexcept{
	return physics::collision_shape_editor_polygon_self_intersects(vertices);
}

[[nodiscard]] inline std::vector<math::vec2> polygon_convex_hull(
	const std::span<const math::vec2> vertices){
	return physics::collision_shape_editor_polygon_convex_hull(vertices);
}

[[nodiscard]] inline math::vec2 mirror_point_on_axes(
	const math::vec2 point,
	const mirror_modifier& mirror,
	const bool mirror_x,
	const bool mirror_y) noexcept{
	return physics::mirror_collision_shape_editor_point_on_axes(point, mirror, mirror_x, mirror_y);
}

[[nodiscard]] inline math::vec2 mirror_point(
	const math::vec2 point,
	const mirror_modifier& mirror) noexcept{
	return physics::mirror_collision_shape_editor_point(point, mirror);
}

[[nodiscard]] inline math::vec2 snap_point_to_mirror_axes(
	const math::vec2 point,
	const mirror_modifier& mirror,
	const float epsilon = physics::editor_detail::polygon_epsilon) noexcept{
	return physics::snap_collision_shape_editor_point_to_mirror_axes(point, mirror, epsilon);
}

[[nodiscard]] inline math::vec2 clamp_point_to_mirror_source_axes(
	const math::vec2 point,
	const mirror_modifier& mirror) noexcept{
	return physics::clamp_collision_shape_editor_point_to_mirror_source_axes(point, mirror);
}

[[nodiscard]] inline float mirror_angle(
	const float angle,
	const mirror_modifier& mirror) noexcept{
	return physics::mirror_collision_shape_editor_angle(angle, mirror);
}

[[nodiscard]] inline part mirror_part(
	const part& source,
	const mirror_modifier& mirror){
	return physics::mirror_collision_shape_editor_part(source, mirror);
}

inline void snap_part_vertices_to_mirror_axes(part& part) noexcept{
	physics::snap_collision_shape_editor_part_vertices_to_mirror_axes(part);
}

[[nodiscard]] inline std::size_t part_polygon_edge_count(const part& part) noexcept{
	return physics::collision_shape_editor_part_polygon_edge_count(part);
}

[[nodiscard]] inline polygon_edge part_polygon_edge_at(const part& part, const std::size_t edge_index){
	return physics::collision_shape_editor_part_polygon_edge_at(part, edge_index);
}

inline void materialize_part_polygon_edges(part& part){
	physics::materialize_collision_shape_editor_part_polygon_edges(part);
}

[[nodiscard]] inline polygon_graph part_polygon_graph(const part& part){
	return physics::collision_shape_editor_part_polygon_graph(part);
}

[[nodiscard]] inline polygon_graph part_effective_polygon_graph(const part& part){
	return physics::collision_shape_editor_part_effective_polygon_graph(part);
}

[[nodiscard]] inline graph_analysis analyze_polygon_graph(const polygon_graph& graph){
	return physics::analyze_collision_shape_editor_polygon_graph(graph);
}

[[nodiscard]] inline graph_analysis analyze_part_polygon_graph(
	const part& part,
	const bool include_mirror = true){
	return physics::analyze_collision_shape_editor_part_polygon_graph(part, include_mirror);
}

[[nodiscard]] inline std::expected<void, std::string> set_polygon_closed(
	part& part,
	const bool closed){
	return physics::set_collision_shape_editor_polygon_closed(part, closed);
}

[[nodiscard]] inline std::expected<std::size_t, std::string> insert_polygon_vertex(
	part& part,
	const math::vec2 local_vertex,
	const std::optional<std::size_t> insert_after = std::nullopt){
	return physics::insert_collision_shape_editor_polygon_vertex(part, local_vertex, insert_after);
}

[[nodiscard]] inline std::expected<std::size_t, std::string> connect_polygon_vertices_as_edge(
	document& document,
	const std::size_t part_index,
	const std::size_t first_vertex,
	const std::size_t second_vertex){
	return physics::connect_collision_shape_editor_polygon_vertices_as_edge(
		document,
		part_index,
		first_vertex,
		second_vertex);
}

[[nodiscard]] inline std::expected<polygon_cut_result, std::string> cut_polygon(
	document& document,
	const std::size_t part_index,
	const std::size_t first_vertex,
	const std::size_t second_vertex){
	return physics::cut_collision_shape_editor_polygon(document, part_index, first_vertex, second_vertex);
}

[[nodiscard]] inline std::expected<knife_cut_result, std::string> knife_cut_polygon(
	document& document,
	const std::size_t part_index,
	const std::span<const math::vec2> world_polyline){
	return physics::knife_cut_collision_shape_editor_polygon(document, part_index, world_polyline);
}

[[nodiscard]] inline std::expected<std::size_t, std::string> merge_polygons_as_hull(
	document& document,
	const std::span<const std::size_t> part_indices,
	const std::optional<std::size_t> active_part_index = std::nullopt){
	return physics::merge_collision_shape_editor_polygons_as_hull(
		document,
		part_indices,
		active_part_index);
}

[[nodiscard]] inline std::expected<std::size_t, std::string> join_polygon_parts(
	document& document,
	const std::span<const std::size_t> part_indices,
	const join_origin_mode origin_mode,
	const std::optional<std::size_t> active_part_index = std::nullopt){
	std::vector<std::size_t> indices = physics::editor_detail::normalize_part_indices(part_indices);
	if(indices.size() < 2u){
		return std::unexpected{"open polygon join requires at least two selected polygons"};
	}
	if(indices.back() >= document.parts.size()){
		return std::unexpected{"open polygon join selection contains a missing part"};
	}

	std::size_t target_index = indices.front();
	if(origin_mode == join_origin_mode::last){
		target_index = indices.back();
	}else if(origin_mode == join_origin_mode::center){
		if(active_part_index && std::ranges::contains(indices, *active_part_index)){
			target_index = *active_part_index;
		}
	}

	const part& target = document.parts[target_index];
	if(target.type != physics::shape_type::convex_polygon || target.closed){
		return std::unexpected{"open polygon join target must be an open polygon"};
	}
	if(target.uses_explicit_polygon_edges()){
		return std::unexpected{"open polygon join requires ordered open polygon paths"};
	}

	std::vector<std::vector<math::vec2>> paths{};
	paths.reserve(indices.size());
	for(const std::size_t index : indices){
		const part& source = document.parts[index];
		if(source.type != physics::shape_type::convex_polygon || source.closed){
			return std::unexpected{"open polygon join only accepts open polygons"};
		}
		if(source.uses_explicit_polygon_edges()){
			return std::unexpected{"open polygon join requires ordered open polygon paths"};
		}

		std::vector<math::vec2> path{};
		path.reserve(source.convex_polygon.vertices.size());
		for(const math::vec2 vertex : source.convex_polygon.vertices){
			const math::vec2 world = vertex >> source.local_transform;
			path.push_back(target.local_transform.apply_inv_to(world));
		}
		if(!physics::editor_detail::editable_open_polyline(path)){
			return std::unexpected{"open polygon join selection contains an invalid open polygon"};
		}
		if(index == target_index){
			paths.insert(paths.begin(), std::move(path));
		}else{
			paths.push_back(std::move(path));
		}
	}

	std::vector<math::vec2> joined = std::move(paths.front());
	paths.erase(paths.begin());
	while(!paths.empty()){
		struct best_stitch{
			std::size_t path_index{};
			bool reverse{};
			float distance2{std::numeric_limits<float>::infinity()};
		};

		best_stitch best{};
		for(std::size_t index = 0u; index != paths.size(); ++index){
			const auto& path = paths[index];
			const float front_distance = joined.back().dst2(path.front());
			if(front_distance < best.distance2){
				best = {.path_index = index, .reverse = false, .distance2 = front_distance};
			}
			const float back_distance = joined.back().dst2(path.back());
			if(back_distance < best.distance2){
				best = {.path_index = index, .reverse = true, .distance2 = back_distance};
			}
		}

		auto path = std::move(paths[best.path_index]);
		paths.erase(paths.begin() + static_cast<std::ptrdiff_t>(best.path_index));
		if(best.reverse){
			std::ranges::reverse(path);
		}

		for(const math::vec2 vertex : path){
			if(!joined.empty() && physics::editor_detail::same_polygon_point(joined.back(), vertex)){
				continue;
			}
			joined.push_back(vertex);
		}
	}

	if(!physics::editor_detail::editable_open_polyline(joined)){
		return std::unexpected{"open polygon join would produce an invalid open polygon"};
	}

	part merged = target;
	if(origin_mode == join_origin_mode::center){
		math::vec2 center{};
		for(const math::vec2 vertex : joined){
			center += vertex;
		}
		center /= static_cast<float>(joined.size());
		math::trans2 centered_transform = target.local_transform;
		centered_transform.vec = target.local_transform.apply_to(center);
		for(math::vec2& vertex : joined){
			vertex = centered_transform.apply_inv_to(vertex >> target.local_transform);
		}
		merged.local_transform = centered_transform;
	}
	merged.convex_polygon.vertices = std::move(joined);
	merged.closed = false;
	merged.mirror = {};
	merged.polygon_edges.clear();
	merged.polygon_edges_explicit = false;
	document.parts[target_index] = std::move(merged);
	for(auto cursor = indices.rbegin(); cursor != indices.rend(); ++cursor){
		if(*cursor == target_index){
			continue;
		}
		document.parts.erase(document.parts.begin() + static_cast<std::ptrdiff_t>(*cursor));
	}
	const auto removed_before_target = std::ranges::count_if(indices, [target_index](const std::size_t index) noexcept{
		return index < target_index;
	});
	return target_index - static_cast<std::size_t>(removed_before_target);
}
}
