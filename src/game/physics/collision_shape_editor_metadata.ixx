export module mo_yanxi.game.physics.collision_shape_editor_metadata;

export import mo_yanxi.game.physics.shape;

import std;
import mo_yanxi.math;

namespace mo_yanxi::game::physics{
namespace editor_detail{
inline constexpr float polygon_epsilon = 1.0e-5f;

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
struct collision_shape_editor_mirror_modifier{
	bool mirror_x{};
	bool mirror_y{};
	math::trans2 origin{};

	[[nodiscard]] constexpr bool active() const noexcept{
		return mirror_x || mirror_y;
	}
};

export
struct collision_shape_editor_part{
	shape_type type{shape_type::box};
	math::trans2 local_transform{};
	circle_shape circle{32.f};
	capsule_shape capsule{{-40.f, 0.f}, {40.f, 0.f}, 12.f};
	box_shape box{{40.f, 28.f}};
	convex_polygon_shape convex_polygon{};
	collision_shape_editor_mirror_modifier mirror{};

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
			return editor_detail::strict_ccw_convex_polygon(convex_polygon.vertices);
		default:
			return false;
		}
	}

	void append_to(collision_shape& out, const math::trans2 total_transform = {}) const{
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
			editor_detail::append_runtime_part(
				out,
				local_transform,
				total_transform,
				physics::make_convex_polygon(convex_polygon.vertices));
			return;
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
[[nodiscard]] inline math::vec2 mirror_collision_shape_editor_point(
	const math::vec2 point,
	const collision_shape_editor_mirror_modifier& mirror) noexcept{
	math::vec2 local = mirror.origin.apply_inv_to(point);
	if(mirror.mirror_x){
		local.x = -local.x;
	}
	if(mirror.mirror_y){
		local.y = -local.y;
	}
	return mirror.origin.apply_to(local);
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

		std::vector<math::vec2> candidate = part.convex_polygon.vertices;
		candidate[vertex_index] = local_vertex;
		if(!physics::collision_shape_editor_polygon_valid(candidate)){
			return false;
		}

		part.convex_polygon.vertices = std::move(candidate);
		return true;
	}

	[[nodiscard]] collision_shape to_runtime_shape(const bool include_mirror = true) const{
		collision_shape result{};
		for(const collision_shape_editor_part& part : parts){
			part.append_to(result, total_transform);
		}
		if(include_mirror){
			for(const collision_shape_editor_part& part : parts){
				if(part.mirror.active()){
					physics::mirror_collision_shape_editor_part(part, part.mirror).append_to(result, total_transform);
				}
			}
		}
		return result;
	}

	[[nodiscard]] collision_shape_record to_packed_record(const bool include_mirror = true) const{
		return this->to_runtime_shape(include_mirror).to_record();
	}
};
}
