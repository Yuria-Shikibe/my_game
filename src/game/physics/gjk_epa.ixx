export module mo_yanxi.game.physics.gjk_epa;

export import mo_yanxi.game.physics.shape;

import std;

namespace mo_yanxi::game::physics{
using iteration_size_t = unsigned;

namespace detail{
constexpr inline float gjk_epsilon = 1.0e-5f;
constexpr inline float epa_tolerance = 1.0e-4f;
constexpr inline iteration_size_t gjk_max_iterations = 32;
constexpr inline iteration_size_t epa_max_iterations = 64;

[[nodiscard]] constexpr math::vec2 triple_product(
	const math::vec2 a,
	const math::vec2 b,
	const math::vec2 c) noexcept{
	return b * a.dot(c) - a * b.dot(c);
}

[[nodiscard]] math::vec2 perpendicular_toward_origin(const math::vec2 edge, const math::vec2 point) noexcept{
	auto direction = triple_product(edge, -point, edge);
	if(direction.length2() <= gjk_epsilon * gjk_epsilon){
		direction = {-edge.y, edge.x};
		if(direction.dot(-point) < 0.f){
			direction = -direction;
		}
	}
	return direction;
}
}

export
struct support_vertex{
	math::vec2 point{};
	math::vec2 point_a{};
	math::vec2 point_b{};
};

export
struct simplex{
	std::array<support_vertex, 3> vertices{};
	std::uint8_t count{};

	constexpr void push_front(const support_vertex vertex) noexcept{
		for(std::uint8_t i = count; i > 0; --i){
			vertices[i] = vertices[i - 1];
		}
		vertices[0] = vertex;
		if(count < vertices.size()){
			++count;
		}
	}

	[[nodiscard]] constexpr support_vertex& operator[](const std::size_t index) noexcept{
		return vertices[index];
	}

	[[nodiscard]] constexpr const support_vertex& operator[](const std::size_t index) const noexcept{
		return vertices[index];
	}
};

export
struct gjk_result{
	bool intersect{};
	simplex final_simplex{};
	math::vec2 last_direction{1.f, 0.f};
	unsigned iterations{};
};

export
struct contact_result{
	bool hit{};
	math::vec2 normal{1.f, 0.f};
	float depth{};
	math::vec2 point{};
	unsigned gjk_iterations{};
	unsigned epa_iterations{};
	bool epa_converged{};
};

export
struct contact_point{
	math::vec2 point{};
	float depth{};
};

export
struct contact_manifold{
	bool hit{};
	math::vec2 normal{1.f, 0.f};
	std::array<contact_point, 2> points{};
	std::uint8_t point_count{};
	unsigned gjk_iterations{};
	unsigned epa_iterations{};
	bool epa_converged{};

	[[nodiscard]] contact_result representative() const noexcept{
		contact_result result{
			.hit = hit && point_count != 0,
			.normal = normal,
			.gjk_iterations = gjk_iterations,
			.epa_iterations = epa_iterations,
			.epa_converged = epa_converged
		};
		if(!result.hit){
			return result;
		}

		for(std::uint8_t i = 0; i != point_count; ++i){
			const auto& point = points[i];
			result.point += point.point;
			if(i == 0 || point.depth > result.depth){
				result.depth = point.depth;
			}
		}
		result.point *= 1.f / static_cast<float>(point_count);
		return result;
	}
};

export
struct time_of_impact_result{
	bool hit{};
	float fraction{1.f};
	contact_result contact{};
	iteration_size_t iterations{};
};

export
template <class Shape>
concept collision_support_shape = requires(
	const Shape& shape,
	const math::vec2 direction,
	const math::trans2 transform){
		{ shape.support(direction, transform) } -> std::same_as<math::vec2>;
	};

export
template <collision_support_shape ShapeA, collision_support_shape ShapeB>
[[nodiscard]] support_vertex support(
	const ShapeA& a,
	const math::trans2 transform_a,
	const ShapeB& b,
	const math::trans2 transform_b,
	const math::vec2 direction) noexcept{
	const auto point_a = a.support(direction, transform_a);
	const auto point_b = b.support(-direction, transform_b);
	return {
			.point = point_a - point_b,
			.point_a = point_a,
			.point_b = point_b
		};
}

namespace detail{
[[nodiscard]] bool update_line_simplex(simplex& simplex, math::vec2& direction) noexcept{
	const auto a = simplex[0].point;
	const auto b = simplex[1].point;
	const auto ab = b - a;
	const auto ao = -a;

	if(ab.dot(ao) > 0.f){
		direction = perpendicular_toward_origin(ab, a);
	} else{
		simplex.vertices[0] = simplex[0];
		simplex.count = 1;
		direction = ao;
	}

	return false;
}

[[nodiscard]] bool update_triangle_simplex(simplex& simplex, math::vec2& direction) noexcept{
	const auto a = simplex[0].point;
	const auto b = simplex[1].point;
	const auto c = simplex[2].point;

	const auto ab = b - a;
	const auto ac = c - a;
	const auto ao = -a;

	auto ab_perp = triple_product(ac, ab, ab);
	if(ab_perp.length2() <= gjk_epsilon * gjk_epsilon){
		ab_perp = perpendicular_toward_origin(ab, a);
	}

	if(ab_perp.dot(ao) > 0.f){
		simplex.vertices[1] = simplex[1];
		simplex.count = 2;
		direction = ab_perp;
		return false;
	}

	auto ac_perp = triple_product(ab, ac, ac);
	if(ac_perp.length2() <= gjk_epsilon * gjk_epsilon){
		ac_perp = perpendicular_toward_origin(ac, a);
	}

	if(ac_perp.dot(ao) > 0.f){
		simplex.vertices[1] = simplex[2];
		simplex.count = 2;
		direction = ac_perp;
		return false;
	}

	return true;
}

[[nodiscard]] bool update_simplex(simplex& simplex, math::vec2& direction) noexcept{
	if(simplex.count == 2){
		return update_line_simplex(simplex, direction);
	}
	if(simplex.count == 3){
		return update_triangle_simplex(simplex, direction);
	}

	direction = -simplex[0].point;
	return false;
}
}

namespace detail{
template <collision_support_shape ShapeA, collision_support_shape ShapeB>
[[nodiscard]] gjk_result gjk_intersect(
	const ShapeA& a,
	const math::vec2 origin_a,
	const math::trans2 support_transform_a,
	const ShapeB& b,
	const math::vec2 origin_b,
	const math::trans2 support_transform_b) noexcept{
	math::vec2 direction = origin_b - origin_a;
	if(direction.length2() <= detail::gjk_epsilon * detail::gjk_epsilon){
		direction = {1.f, 0.f};
	}

	simplex simplex{};
	simplex.push_front(physics::support(a, support_transform_a, b, support_transform_b, direction));
	direction = -simplex[0].point;

	for(unsigned iteration = 0; iteration != detail::gjk_max_iterations; ++iteration){
		if(direction.length2() <= detail::gjk_epsilon * detail::gjk_epsilon){
			direction = {1.f, 0.f};
		}

		const support_vertex vertex = physics::support(a, support_transform_a, b, support_transform_b, direction);
		if(vertex.point.dot(direction) < 0.f){
			return {
					.intersect = false,
					.final_simplex = simplex,
					.last_direction = direction,
					.iterations = iteration + 1
				};
		}

		simplex.push_front(vertex);
		if(detail::update_simplex(simplex, direction)){
			return {
					.intersect = true,
					.final_simplex = simplex,
					.last_direction = direction,
					.iterations = iteration + 1
				};
		}
	}

	return {
			.intersect = false,
			.final_simplex = simplex,
			.last_direction = direction,
			.iterations = detail::gjk_max_iterations
		};
}
}

export
template <collision_support_shape ShapeA, collision_support_shape ShapeB>
[[nodiscard]] gjk_result gjk_intersect(
	const ShapeA& a,
	const math::trans2 transform_a,
	const ShapeB& b,
	const math::trans2 transform_b) noexcept{
	return detail::gjk_intersect(
		a,
		transform_a.vec,
		transform_a,
		b,
		transform_b.vec,
		transform_b);
}

namespace detail{
struct epa_edge{
	std::uint32_t index{};
	math::vec2 normal{};
	float distance{};
};

[[nodiscard]] float polygon_area(std::span<const support_vertex> vertices) noexcept{
	float area{};
	for(std::size_t i = 0; i != vertices.size(); ++i){
		area += vertices[i].point.cross(vertices[(i + 1) % vertices.size()].point);
	}
	return area * 0.5f;
}

[[nodiscard]] epa_edge closest_edge(std::span<const support_vertex> vertices) noexcept{
	epa_edge best{
			.index = 0,
			.normal = {1.f, 0.f},
			.distance = std::numeric_limits<float>::infinity()
		};

	for(std::uint32_t i = 0; i != vertices.size(); ++i){
		const auto a = vertices[i].point;
		const auto b = vertices[(i + 1) % vertices.size()].point;
		const auto edge = b - a;
		auto normal = math::vec2{edge.y, -edge.x};
		if(normal.length2() <= gjk_epsilon * gjk_epsilon){
			continue;
		}
		normal.normalize();

		float distance = normal.dot(a);
		if(distance < 0.f){
			distance = -distance;
			normal = -normal;
		}

		if(distance < best.distance){
			best = {
					.index = i,
					.normal = normal,
					.distance = distance
				};
		}
	}

	return best;
}

[[nodiscard]] math::vec2 interpolate_contact(
	const support_vertex& a,
	const support_vertex& b) noexcept{
	const auto edge = b.point - a.point;
	const auto denom = edge.length2();
	const float t = denom <= gjk_epsilon * gjk_epsilon
		                ? 0.f
		                : std::clamp((-a.point).dot(edge) / denom, 0.f, 1.f);
	const auto point_a = a.point_a + (b.point_a - a.point_a) * t;
	const auto point_b = a.point_b + (b.point_b - a.point_b) * t;
	return (point_a + point_b) * 0.5f;
}

static constexpr std::uint8_t support_feature_capacity = 8;

struct support_feature{
	std::array<math::vec2, support_feature_capacity> points{};
	std::uint8_t count{};
	float projection{-std::numeric_limits<float>::infinity()};
};

[[nodiscard]] constexpr math::vec2 safe_normalized(math::vec2 direction) noexcept{
	if(direction.length2() <= gjk_epsilon * gjk_epsilon){
		return {1.f, 0.f};
	}
	return direction.normalize();
}

[[nodiscard]] constexpr math::trans2 combine_transform(const math::trans2 local, const math::trans2 world) noexcept{
	return local >> world;
}

[[nodiscard]] constexpr math::vec2 to_local_direction(math::vec2 direction, const math::trans2 transform) noexcept{
	direction.rotate_rad(-static_cast<float>(transform.rot));
	return direction;
}

[[nodiscard]] bool same_feature_point(const math::vec2 lhs, const math::vec2 rhs, const float tolerance) noexcept{
	return (lhs - rhs).length2() <= tolerance * tolerance;
}

void append_support_point(
	support_feature& feature,
	const math::vec2 point,
	const math::vec2 direction,
	const float tolerance) noexcept{
	const float projection = point.dot(direction);
	if(feature.count == 0 || projection > feature.projection + tolerance){
		feature.count = 1;
		feature.points[0] = point;
		feature.projection = projection;
		return;
	}

	if(std::abs(projection - feature.projection) > tolerance){
		return;
	}

	for(std::uint8_t i = 0; i != feature.count; ++i){
		if(same_feature_point(feature.points[i], point, tolerance)){
			return;
		}
	}

	if(feature.count < support_feature_capacity){
		feature.points[feature.count++] = point;
	}
}

[[nodiscard]] support_feature reduce_feature(
	const support_feature& feature,
	const math::vec2 tangent,
	const float tolerance) noexcept{
	if(feature.count <= 2){
		support_feature result = feature;
		if(result.count == 2 && result.points[1].dot(tangent) < result.points[0].dot(tangent)){
			std::swap(result.points[0], result.points[1]);
		}
		return result;
	}

	std::uint8_t min_index{};
	std::uint8_t max_index{};
	float min_projection = feature.points[0].dot(tangent);
	float max_projection = min_projection;
	for(std::uint8_t i = 1; i != feature.count; ++i){
		const float projection = feature.points[i].dot(tangent);
		if(projection < min_projection){
			min_projection = projection;
			min_index = i;
		}
		if(projection > max_projection){
			max_projection = projection;
			max_index = i;
		}
	}

	support_feature result{
		.points = {feature.points[min_index], feature.points[max_index]},
		.count = static_cast<std::uint8_t>(std::abs(max_projection - min_projection) <= tolerance ? 1u : 2u),
		.projection = feature.projection
	};
	return result;
}

[[nodiscard]] support_feature support_feature_of(
	const circle_shape& circle,
	const math::vec2 direction,
	const math::trans2 transform,
	const float) noexcept{
	const auto local_direction = to_local_direction(direction, transform);
	const auto point = safe_normalized(local_direction) * circle.radius >> transform;
	return {
		.points = {point},
		.count = 1,
		.projection = point.dot(direction)
	};
}

[[nodiscard]] support_feature support_feature_of(
	const capsule_shape& capsule,
	const math::vec2 direction,
	const math::trans2 transform,
	const float tolerance) noexcept{
	const auto local_direction = to_local_direction(direction, transform);
	const auto local_normal = safe_normalized(local_direction);
	const float begin_projection = capsule.begin.dot(local_direction);
	const float end_projection = capsule.end.dot(local_direction);
	support_feature feature{};
	if(begin_projection >= end_projection - tolerance){
		append_support_point(feature, (capsule.begin + local_normal * capsule.radius) >> transform, direction, tolerance);
	}
	if(end_projection >= begin_projection - tolerance){
		append_support_point(feature, (capsule.end + local_normal * capsule.radius) >> transform, direction, tolerance);
	}
	return feature;
}

[[nodiscard]] support_feature support_feature_of(
	const box_shape& box,
	const math::vec2 direction,
	const math::trans2 transform,
	const float tolerance) noexcept{
	const auto half = box.half_extent;
	const std::array vertices{
		math::vec2{-half.x, -half.y},
		math::vec2{half.x, -half.y},
		math::vec2{half.x, half.y},
		math::vec2{-half.x, half.y}
	};
	support_feature feature{};
	for(const auto vertex : vertices){
		append_support_point(feature, vertex >> transform, direction, tolerance);
	}
	return feature;
}

[[nodiscard]] support_feature support_feature_of(
	const convex_polygon_shape& polygon,
	const math::vec2 direction,
	const math::trans2 transform,
	const float tolerance) noexcept{
	support_feature feature{};
	for(const auto vertex : polygon.vertices){
		append_support_point(feature, vertex >> transform, direction, tolerance);
	}
	return feature;
}

[[nodiscard]] support_feature support_feature_of(
	const collision_shape& shape,
	const math::vec2 direction,
	const math::trans2 transform,
	const float tolerance) noexcept{
	support_feature feature{};
	shape.visit_parts([&](std::size_t, const auto& component) noexcept{
		const auto part_transform = combine_transform(component.local_transform, transform);
		const auto part_feature = support_feature_of(component.shape, direction, part_transform, tolerance);
		for(std::uint8_t i = 0; i != part_feature.count; ++i){
			append_support_point(feature, part_feature.points[i], direction, tolerance);
		}
	});
	return feature;
}

[[nodiscard]] support_feature support_feature_of(
	const collision_shape_record& shape,
	const collision_shape_record_part& part,
	const math::vec2 direction,
	const collision_shape_query_transform transform,
	const float tolerance) noexcept{
	const auto local_direction = transform.rotate_to_local(direction);
	switch(part.type){
	case shape_type::circle:{
		const auto point = transform.apply_to(safe_normalized(local_direction) * part.payload.circle.radius);
		return {
			.points = {point},
			.count = 1,
			.projection = point.dot(direction)
		};
	}
	case shape_type::capsule:{
		const auto local_normal = safe_normalized(local_direction);
		const float begin_projection = part.payload.capsule.begin.dot(local_direction);
		const float end_projection = part.payload.capsule.end.dot(local_direction);
		support_feature feature{};
		if(begin_projection >= end_projection - tolerance){
			append_support_point(
				feature,
				transform.apply_to(part.payload.capsule.begin + local_normal * part.payload.capsule.radius),
				direction,
				tolerance);
		}
		if(end_projection >= begin_projection - tolerance){
			append_support_point(
				feature,
				transform.apply_to(part.payload.capsule.end + local_normal * part.payload.capsule.radius),
				direction,
				tolerance);
		}
		return feature;
	}
	case shape_type::box:{
		const auto half = part.payload.box.half_extent;
		const std::array vertices{
			math::vec2{-half.x, -half.y},
			math::vec2{half.x, -half.y},
			math::vec2{half.x, half.y},
			math::vec2{-half.x, half.y}
		};
		support_feature feature{};
		for(const auto vertex : vertices){
			append_support_point(feature, transform.apply_to(vertex), direction, tolerance);
		}
		return feature;
	}
	case shape_type::convex_polygon:{
		const auto first = shape.polygon_vertices().begin() + part.payload.convex_polygon.vertex_offset;
		const auto last = first + part.payload.convex_polygon.vertex_count;
		support_feature feature{};
		for(auto current = first; current != last; ++current){
			append_support_point(feature, transform.apply_to(*current), direction, tolerance);
		}
		return feature;
	}
	}
	return {};
}

[[nodiscard]] support_feature support_feature_of(
	const collision_shape_record& shape,
	const math::vec2 direction,
	const collision_shape_query_transform transform,
	const float tolerance) noexcept{
	support_feature feature{};
	for(const auto& part : shape.records()){
		const auto part_transform = collision_shape_query_transform::combine(part.local_transform, transform);
		const auto part_feature = support_feature_of(shape, part, direction, part_transform, tolerance);
		for(std::uint8_t i = 0; i != part_feature.count; ++i){
			append_support_point(feature, part_feature.points[i], direction, tolerance);
		}
	}
	return feature;
}

[[nodiscard]] support_feature support_feature_of(
	const collision_shape_record& shape,
	const math::vec2 direction,
	const math::trans2 transform,
	const float tolerance) noexcept{
	return support_feature_of(
		shape,
		direction,
		collision_shape_query_transform::make(transform),
		tolerance);
}

[[nodiscard]] support_feature support_feature_of(
	const collision_shape_record_query& shape,
	const math::vec2 direction,
	const math::trans2 transform,
	const float tolerance) noexcept{
	if(collision_shape_query_transform::is_identity(transform)){
		return support_feature_of(*shape.shape, direction, shape.transform, tolerance);
	}

	return support_feature_of(
		*shape.shape,
		direction,
		collision_shape_query_transform::combine(
			shape.transform.transform,
			collision_shape_query_transform::make(transform)),
		tolerance);
}

template <collision_support_shape Shape>
[[nodiscard]] support_feature support_feature_of(
	const Shape& shape,
	const math::vec2 direction,
	const math::trans2 transform,
	const float) noexcept{
	const auto point = shape.support(direction, transform);
	return {
		.points = {point},
		.count = 1,
		.projection = point.dot(direction)
	};
}

[[nodiscard]] math::vec2 point_at_tangent(
	const support_feature& feature,
	const math::vec2 tangent,
	const float tangent_projection) noexcept{
	if(feature.count <= 1){
		return feature.points[0];
	}

	const float begin_projection = feature.points[0].dot(tangent);
	const float end_projection = feature.points[1].dot(tangent);
	const float denom = end_projection - begin_projection;
	if(std::abs(denom) <= gjk_epsilon){
		return (feature.points[0] + feature.points[1]) * 0.5f;
	}

	const float t = std::clamp((tangent_projection - begin_projection) / denom, 0.f, 1.f);
	return feature.points[0] + (feature.points[1] - feature.points[0]) * t;
}

void append_manifold_point(
	contact_manifold& manifold,
	const math::vec2 point_a,
	const math::vec2 point_b,
	const float fallback_depth,
	const float tolerance) noexcept{
	if(manifold.point_count >= manifold.points.size()){
		return;
	}

	const math::vec2 contact = (point_a + point_b) * 0.5f;
	for(std::uint8_t i = 0; i != manifold.point_count; ++i){
		if(same_feature_point(manifold.points[i].point, contact, tolerance)){
			return;
		}
	}

	const float depth = std::max((point_a - point_b).dot(manifold.normal), fallback_depth);
	manifold.points[manifold.point_count++] = {
		.point = contact,
		.depth = depth
	};
}
}

export
template <collision_support_shape ShapeA, collision_support_shape ShapeB>
[[nodiscard]] contact_result epa_penetration(
	const ShapeA& a,
	const math::trans2 transform_a,
	const ShapeB& b,
	const math::trans2 transform_b,
	const gjk_result& gjk) noexcept{
	if(!gjk.intersect || gjk.final_simplex.count < 3){
		return {};
	}

	std::array<support_vertex, detail::epa_max_iterations + 3> polytope{};
	std::size_t polytope_size{};
	for(std::uint8_t i = 0; i != gjk.final_simplex.count; ++i){
		polytope[polytope_size++] = gjk.final_simplex[i];
	}

	const auto polytope_span = [&]() noexcept{
		return std::span<const support_vertex>{polytope.data(), polytope_size};
	};

	if(detail::polygon_area(polytope_span()) < 0.f){
		std::ranges::reverse(polytope.begin(), polytope.begin() + static_cast<std::ptrdiff_t>(polytope_size));
	}

	contact_result result{
			.hit = true,
			.gjk_iterations = gjk.iterations
		};

	for(iteration_size_t iteration = 0; iteration != detail::epa_max_iterations; ++iteration){
		const auto edge = detail::closest_edge(polytope_span());
		const auto vertex = physics::support(a, transform_a, b, transform_b, edge.normal);
		const auto distance = vertex.point.dot(edge.normal);

		result.normal = edge.normal;
		result.depth = edge.distance;
		result.point = detail::interpolate_contact(
			polytope[edge.index],
			polytope[(edge.index + 1) % polytope_size]);
		result.epa_iterations = iteration + 1;

		if(distance - edge.distance <= detail::epa_tolerance){
			result.depth = distance;
			result.epa_converged = true;
			return result;
		}

		const auto insert_index = static_cast<std::size_t>(edge.index + 1);
		if(polytope_size >= polytope.size()){
			return result;
		}
		for(std::size_t i = polytope_size; i != insert_index; --i){
			polytope[i] = polytope[i - 1];
		}
		polytope[insert_index] = vertex;
		++polytope_size;
	}

	return result;
}

export
template <collision_support_shape ShapeA, collision_support_shape ShapeB>
[[nodiscard]] contact_result collide(
	const ShapeA& a,
	const math::trans2 transform_a,
	const ShapeB& b,
	const math::trans2 transform_b) noexcept{
	const gjk_result gjk = physics::gjk_intersect(a, transform_a, b, transform_b);
	if(!gjk.intersect){
		return {.gjk_iterations = gjk.iterations};
	}

	return physics::epa_penetration(a, transform_a, b, transform_b, gjk);
}

export
[[nodiscard]] inline contact_result collide(
	const collision_shape_record_query& a,
	const collision_shape_record_query& b) noexcept{
	const gjk_result gjk = detail::gjk_intersect(
		a,
		a.transform.transform.vec,
		{},
		b,
		b.transform.transform.vec,
		{});
	if(!gjk.intersect){
		return {.gjk_iterations = gjk.iterations};
	}

	return physics::epa_penetration(a, {}, b, {}, gjk);
}

export
[[nodiscard]] inline contact_result collide(
	const collision_shape_record& a,
	const math::trans2 transform_a,
	const collision_shape_record& b,
	const math::trans2 transform_b) noexcept{
	return physics::collide(a.query(transform_a), b.query(transform_b));
}

export
template <collision_support_shape ShapeA, collision_support_shape ShapeB>
[[nodiscard]] contact_manifold build_contact_manifold(
	const ShapeA& a,
	const math::trans2 transform_a,
	const ShapeB& b,
	const math::trans2 transform_b,
	const contact_result& seed) noexcept{
	if(!seed.hit){
		return {
			.gjk_iterations = seed.gjk_iterations,
			.epa_iterations = seed.epa_iterations,
			.epa_converged = seed.epa_converged
		};
	}

	auto normal = seed.normal;
	if(normal.length2() > 0.f){
		normal.normalize();
	}else{
		normal = {1.f, 0.f};
	}

	const math::vec2 tangent{-normal.y, normal.x};
	const float tolerance = std::max(1.0e-4f, std::abs(seed.depth) * 1.0e-3f);
	auto feature_a = detail::support_feature_of(a, normal, transform_a, tolerance);
	auto feature_b = detail::support_feature_of(b, -normal, transform_b, tolerance);

	if(feature_a.count == 0 || feature_b.count == 0){
		return {
			.hit = true,
			.normal = normal,
			.points = {contact_point{.point = seed.point, .depth = seed.depth}},
			.point_count = 1,
			.gjk_iterations = seed.gjk_iterations,
			.epa_iterations = seed.epa_iterations,
			.epa_converged = seed.epa_converged
		};
	}

	feature_a = detail::reduce_feature(feature_a, tangent, tolerance);
	feature_b = detail::reduce_feature(feature_b, tangent, tolerance);

	const float a_begin = feature_a.points[0].dot(tangent);
	const float a_end = feature_a.count == 1 ? a_begin : feature_a.points[1].dot(tangent);
	const float b_begin = feature_b.points[0].dot(tangent);
	const float b_end = feature_b.count == 1 ? b_begin : feature_b.points[1].dot(tangent);
	const float overlap_begin = std::max(std::min(a_begin, a_end), std::min(b_begin, b_end));
	const float overlap_end = std::min(std::max(a_begin, a_end), std::max(b_begin, b_end));

	contact_manifold manifold{
		.hit = true,
		.normal = normal,
		.gjk_iterations = seed.gjk_iterations,
		.epa_iterations = seed.epa_iterations,
		.epa_converged = seed.epa_converged
	};

	if(overlap_begin <= overlap_end + tolerance){
		const auto point_a = detail::point_at_tangent(feature_a, tangent, overlap_begin);
		const auto point_b = detail::point_at_tangent(feature_b, tangent, overlap_begin);
		detail::append_manifold_point(manifold, point_a, point_b, seed.depth, tolerance);

		if(overlap_end - overlap_begin > tolerance){
			const auto end_point_a = detail::point_at_tangent(feature_a, tangent, overlap_end);
			const auto end_point_b = detail::point_at_tangent(feature_b, tangent, overlap_end);
			detail::append_manifold_point(manifold, end_point_a, end_point_b, seed.depth, tolerance);
		}
	}else{
		const float feature_a_mid = (a_begin + a_end) * 0.5f;
		const float feature_b_mid = (b_begin + b_end) * 0.5f;
		const float contact_t = (feature_a_mid + feature_b_mid) * 0.5f;
		const auto point_a = detail::point_at_tangent(feature_a, tangent, contact_t);
		const auto point_b = detail::point_at_tangent(feature_b, tangent, contact_t);
		detail::append_manifold_point(manifold, point_a, point_b, seed.depth, tolerance);
	}

	if(manifold.point_count == 0){
		manifold.points[0] = {.point = seed.point, .depth = seed.depth};
		manifold.point_count = 1;
	}
	return manifold;
}

export
template <collision_support_shape ShapeA, collision_support_shape ShapeB>
[[nodiscard]] contact_manifold collide_manifold(
	const ShapeA& a,
	const math::trans2 transform_a,
	const ShapeB& b,
	const math::trans2 transform_b) noexcept{
	return physics::build_contact_manifold(
		a,
		transform_a,
		b,
		transform_b,
		physics::collide(a, transform_a, b, transform_b));
}

namespace detail{
template <typename ContactAt>
[[nodiscard]] time_of_impact_result linear_time_of_impact_impl(
	ContactAt& contact_at,
	const iteration_size_t coarse_steps,
	const iteration_size_t refine_steps) noexcept{
	if(const contact_result start = contact_at(0.f); start.hit){
		return {
				.hit = true,
				.fraction = 0.f,
				.contact = start,
				.iterations = 1
			};
	}

	float low{};
	float high{};
	contact_result high_contact{};
	iteration_size_t iterations{1};
	const iteration_size_t steps = std::max(1u, coarse_steps);
	for(iteration_size_t i = 1; i <= steps; ++i){
		const float fraction = static_cast<float>(i) / static_cast<float>(steps);
		++iterations;
		if(auto contact = contact_at(fraction); contact.hit){
			low = static_cast<float>(i - 1) / static_cast<float>(steps);
			high = fraction;
			high_contact = contact;
			break;
		}
	}

	if(!high_contact.hit){
		return {.iterations = iterations};
	}

	for(iteration_size_t i = 0; i != refine_steps; ++i){
		const float middle = (low + high) * 0.5f;
		++iterations;
		if(auto contact = contact_at(middle); contact.hit){
			high = middle;
			high_contact = contact;
		} else{
			low = middle;
		}
	}

	return {
			.hit = true,
			.fraction = high,
			.contact = high_contact,
			.iterations = iterations
		};
}
}

export
template <collision_support_shape ShapeA, collision_support_shape ShapeB>
[[nodiscard]] time_of_impact_result linear_time_of_impact(
	const ShapeA& a,
	const math::trans2 start_transform_a,
	const math::trans2 end_transform_a,
	const ShapeB& b,
	const math::trans2 start_transform_b,
	const math::trans2 end_transform_b,
	const iteration_size_t coarse_steps = 32,
	const iteration_size_t refine_steps = 8) noexcept{
	const auto contact_at = [&](const float fraction) noexcept{
		return physics::collide(
			a,
			math::lerp(start_transform_a, end_transform_a, fraction),
			b,
			math::lerp(start_transform_b, end_transform_b, fraction));
	};

	return detail::linear_time_of_impact_impl(contact_at, coarse_steps, refine_steps);
}

export
[[nodiscard]] inline time_of_impact_result linear_time_of_impact(
	const collision_shape_record& a,
	const math::trans2 start_transform_a,
	const math::trans2 end_transform_a,
	const collision_shape_record& b,
	const math::trans2 start_transform_b,
	const math::trans2 end_transform_b,
	const iteration_size_t coarse_steps = 32,
	const iteration_size_t refine_steps = 8) noexcept{
	const auto contact_at = [&](const float fraction) noexcept{
		const auto query_a = a.query(math::lerp(start_transform_a, end_transform_a, fraction));
		const auto query_b = b.query(math::lerp(start_transform_b, end_transform_b, fraction));
		return physics::collide(query_a, query_b);
	};

	return detail::linear_time_of_impact_impl(contact_at, coarse_steps, refine_steps);
}
}
