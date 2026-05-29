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

export
template <collision_support_shape ShapeA, collision_support_shape ShapeB>
[[nodiscard]] gjk_result gjk_intersect(
	const ShapeA& a,
	const math::trans2 transform_a,
	const ShapeB& b,
	const math::trans2 transform_b) noexcept{
	math::vec2 direction = transform_b.vec - transform_a.vec;
	if(direction.length2() <= detail::gjk_epsilon * detail::gjk_epsilon){
		direction = {1.f, 0.f};
	}

	simplex simplex{};
	simplex.push_front(physics::support(a, transform_a, b, transform_b, direction));
	direction = -simplex[0].point;

	for(unsigned iteration = 0; iteration != detail::gjk_max_iterations; ++iteration){
		if(direction.length2() <= detail::gjk_epsilon * detail::gjk_epsilon){
			direction = {1.f, 0.f};
		}

		const support_vertex vertex = physics::support(a, transform_a, b, transform_b, direction);
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

	std::vector<support_vertex> polytope{};
	polytope.reserve(detail::epa_max_iterations + 3);
	for(std::uint8_t i = 0; i != gjk.final_simplex.count; ++i){
		polytope.push_back(gjk.final_simplex[i]);
	}

	if(detail::polygon_area(polytope) < 0.f){
		std::ranges::reverse(polytope);
	}

	contact_result result{
			.hit = true,
			.gjk_iterations = gjk.iterations
		};

	for(iteration_size_t iteration = 0; iteration != detail::epa_max_iterations; ++iteration){
		const auto edge = detail::closest_edge(polytope);
		const auto vertex = physics::support(a, transform_a, b, transform_b, edge.normal);
		const auto distance = vertex.point.dot(edge.normal);

		result.normal = edge.normal;
		result.depth = edge.distance;
		result.point = detail::interpolate_contact(
			polytope[edge.index],
			polytope[(edge.index + 1) % polytope.size()]);
		result.epa_iterations = iteration + 1;

		if(distance - edge.distance <= detail::epa_tolerance){
			result.depth = distance;
			result.epa_converged = true;
			return result;
		}

		polytope.insert(polytope.begin() + static_cast<std::ptrdiff_t>(edge.index + 1), vertex);
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
