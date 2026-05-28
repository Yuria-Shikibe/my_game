export module mo_yanxi.game.physics.shape;

export import mo_yanxi.math.vector2;
export import mo_yanxi.math.rect_ortho;
export import mo_yanxi.math.trans2;

import std;

namespace mo_yanxi::game::physics{
export
template<class Range>
concept polygon_vertex_range =
	std::ranges::sized_range<Range>
	&& std::ranges::bidirectional_range<Range>
	&& std::convertible_to<std::ranges::range_reference_t<Range>, math::vec2>;

namespace detail{
constexpr float geometry_epsilon = 1.0e-5f;

[[nodiscard]] constexpr float cross(const math::vec2 a, const math::vec2 b, const math::vec2 c) noexcept{
	return (b - a).cross(c - a);
}

[[nodiscard]] constexpr math::vec2 safe_normalized(math::vec2 direction) noexcept{
	if(direction.length2() <= geometry_epsilon * geometry_epsilon){
		return {1.f, 0.f};
	}
	return direction.normalize();
}

[[nodiscard]] constexpr bool same_point(const math::vec2 a, const math::vec2 b) noexcept{
	return (a - b).length2() <= geometry_epsilon * geometry_epsilon;
}

template<class Function, class Iterator>
concept cyclic_triplet_callback = std::invocable<Function&, Iterator, Iterator, Iterator>;

template<class Range, class Function>
	requires std::ranges::sized_range<Range>
		&& std::ranges::bidirectional_range<Range>
		&& cyclic_triplet_callback<Function, std::ranges::iterator_t<Range>>
constexpr bool for_each_cyclic_triplet(Range&& range, Function&& function){
	auto remaining = std::ranges::size(range);
	if(remaining < 3){
		return true;
	}

	const auto sentinel = std::ranges::end(range);
	const auto first = std::ranges::begin(range);
	auto last = std::ranges::next(first, sentinel);
	--last;

	auto previous = last;
	auto current = first;
	auto next = current;
	++next;

	for(; remaining != 0; --remaining){
		if(next == sentinel){
			next = first;
		}

		using iterator = std::ranges::iterator_t<Range>;
		if constexpr(std::predicate<Function&, iterator, iterator, iterator>){
			if(!std::invoke(function, previous, current, next)){
				return false;
			}
		} else{
			std::invoke(function, previous, current, next);
		}

		previous = current;
		current = next;
		++next;
	}

	return true;
}

template<polygon_vertex_range Vertices>
[[nodiscard]] constexpr bool is_convex_cleaned_polygon(Vertices&& vertices) noexcept{
	if(std::ranges::size(vertices) < 3){
		return false;
	}

	bool convex = true;
	using iterator = std::ranges::iterator_t<decltype((vertices))>;
	detail::for_each_cyclic_triplet(vertices, [&](iterator previous, iterator current, iterator next) noexcept{
		if(detail::cross(*previous, *current, *next) >= -geometry_epsilon){
			return true;
		}

		convex = false;
		return false;
	});

	return convex;
}

template<polygon_vertex_range Vertices>
[[nodiscard]] constexpr float signed_area(Vertices&& vertices) noexcept{
	if(std::ranges::size(vertices) < 3){
		return 0.f;
	}

	float area{};
	using iterator = std::ranges::iterator_t<decltype((vertices))>;
	detail::for_each_cyclic_triplet(vertices, [&](iterator, iterator current, iterator next) noexcept{
		const math::vec2 a = *current;
		const math::vec2 b = *next;
		area += a.cross(b);
		return true;
	});
	return area * 0.5f;
}

[[nodiscard]] constexpr bool point_in_triangle(
	const math::vec2 p,
	const math::vec2 a,
	const math::vec2 b,
	const math::vec2 c) noexcept{
	const float ab = cross(a, b, p);
	const float bc = cross(b, c, p);
	const float ca = cross(c, a, p);
	return ab >= -geometry_epsilon && bc >= -geometry_epsilon && ca >= -geometry_epsilon;
}

template<polygon_vertex_range Vertices>
[[nodiscard]] constexpr std::vector<math::vec2> clean_polygon(Vertices&& vertices){
	std::vector<math::vec2> result{};
	result.reserve(std::ranges::size(vertices));

	for(auto&& vertex : vertices){
		const math::vec2 current = vertex;
		if(result.empty() || !same_point(current, result.back())){
			result.push_back(current);
		}
	}

	if(result.size() > 1 && same_point(result.front(), result.back())){
		result.pop_back();
	}

	bool changed = true;
	while(changed && result.size() >= 3){
		changed = false;
		using iterator = std::vector<math::vec2>::iterator;
		auto remove = result.end();
		detail::for_each_cyclic_triplet(result, [&](iterator previous, iterator current, iterator next) noexcept{
			if(math::abs(cross(*previous, *current, *next)) > geometry_epsilon){
				return true;
			}

			remove = current;
			return false;
		});

		if(remove != result.end()){
			result.erase(remove);
			changed = true;
		}
	}

	if(signed_area(result) < 0.f){
		std::ranges::reverse(result);
	}

	return result;
}
}

export
struct circle_shape{
	float radius{0.5f};
};

export
struct capsule_shape{
	math::vec2 begin{-0.5f, 0.f};
	math::vec2 end{0.5f, 0.f};
	float radius{0.25f};
};

export
struct box_shape{
	math::vec2 half_extent{0.5f, 0.5f};
};

export
struct convex_polygon_shape{
	std::vector<math::vec2> vertices{};

	[[nodiscard]] constexpr std::size_t size() const noexcept{
		return vertices.size();
	}

	[[nodiscard]] constexpr bool valid() const noexcept{
		return vertices.size() >= 3;
	}
};

export
using primitive_shape = std::variant<circle_shape, capsule_shape, box_shape, convex_polygon_shape>;

export
struct shape_part{
	primitive_shape primitive{};
	math::trans2 local_transform{};
};

export
template<polygon_vertex_range Vertices>
[[nodiscard]] constexpr bool is_convex_polygon(Vertices&& vertices) noexcept{
	if(std::ranges::size(vertices) < 3){
		return false;
	}

	const auto cleaned = detail::clean_polygon(std::forward<Vertices>(vertices));
	if(cleaned.size() < 3){
		return false;
	}

	return detail::is_convex_cleaned_polygon(cleaned);
}

export
template<polygon_vertex_range Vertices>
[[nodiscard]] constexpr convex_polygon_shape make_convex_polygon(Vertices&& vertices){
	auto cleaned = detail::clean_polygon(std::forward<Vertices>(vertices));
	if(!detail::is_convex_cleaned_polygon(cleaned)){
		throw std::invalid_argument{"convex polygon requires at least three convex vertices"};
	}

	return convex_polygon_shape{std::move(cleaned)};
}

export
template<polygon_vertex_range Vertices>
[[nodiscard]] constexpr std::vector<convex_polygon_shape> decompose_polygon(Vertices&& vertices){
	auto cleaned = detail::clean_polygon(std::forward<Vertices>(vertices));
	if(cleaned.size() < 3){
		throw std::invalid_argument{"polygon requires at least three vertices"};
	}

	if(detail::is_convex_cleaned_polygon(cleaned)){
		return {convex_polygon_shape{std::move(cleaned)}};
	}

	std::vector<convex_polygon_shape> result{};
	result.reserve(cleaned.size() - 2);

	std::vector<std::size_t> indices(cleaned.size());
	std::ranges::iota(indices, std::size_t{0});

	while(indices.size() > 3){
		bool clipped{};
		using iterator = std::vector<std::size_t>::iterator;
		auto clipped_index = indices.end();
		detail::for_each_cyclic_triplet(indices, [&](iterator previous, iterator current, iterator next_vertex){
			const auto prev_index = *previous;
			const auto cur_index = *current;
			const auto next_index = *next_vertex;

			const auto prev = cleaned[prev_index];
			const auto cur = cleaned[cur_index];
			const auto next = cleaned[next_index];

			if(detail::cross(prev, cur, next) <= detail::geometry_epsilon){
				return true;
			}

			bool contains_other{};
			for(const auto candidate_index : indices){
				if(candidate_index == prev_index || candidate_index == cur_index || candidate_index == next_index){
					continue;
				}
				if(detail::point_in_triangle(cleaned[candidate_index], prev, cur, next)){
					contains_other = true;
					break;
				}
			}

			if(contains_other){
				return true;
			}

			std::array triangle{prev, cur, next};
			result.push_back(make_convex_polygon(triangle));
			clipped_index = current;
			clipped = true;
			return false;
		});

		if(!clipped){
			throw std::invalid_argument{"polygon decomposition failed; input may be self-intersecting"};
		}

		indices.erase(clipped_index);
	}

	auto index = indices.begin();
	const auto first = cleaned[*index];
	++index;
	const auto second = cleaned[*index];
	++index;
	const auto third = cleaned[*index];
	std::array triangle{first, second, third};
	result.push_back(make_convex_polygon(triangle));
	return result;
}

namespace detail{
[[nodiscard]] constexpr math::trans2 combine_transform(const math::trans2 local, const math::trans2 world) noexcept{
	return local >> world;
}

[[nodiscard]] constexpr math::vec2 to_local_direction(math::vec2 direction, const math::trans2 transform) noexcept{
	direction.rotate_rad(-static_cast<float>(transform.rot));
	return direction;
}

[[nodiscard]] constexpr math::vec2 support_local(const circle_shape& circle, const math::vec2 direction) noexcept{
	return safe_normalized(direction) * circle.radius;
}

[[nodiscard]] constexpr math::vec2 support_local(const capsule_shape& capsule, const math::vec2 direction) noexcept{
	const auto normal = safe_normalized(direction);
	const auto endpoint = capsule.begin.dot(direction) > capsule.end.dot(direction) ? capsule.begin : capsule.end;
	return endpoint + normal * capsule.radius;
}

[[nodiscard]] constexpr math::vec2 support_local(const box_shape& box, const math::vec2 direction) noexcept{
	return {
			direction.x >= 0.f ? box.half_extent.x : -box.half_extent.x,
			direction.y >= 0.f ? box.half_extent.y : -box.half_extent.y
		};
}

[[nodiscard]] constexpr math::vec2 support_local(const convex_polygon_shape& polygon, const math::vec2 direction) noexcept{
	if(polygon.vertices.empty()){
		return {};
	}

	auto best = polygon.vertices.front();
	auto best_dot = best.dot(direction);
	for(const auto vertex : polygon.vertices | std::views::drop(1)){
		if(const auto value = vertex.dot(direction); value > best_dot){
			best = vertex;
			best_dot = value;
		}
	}
	return best;
}
}

export
struct collision_shape{
	std::vector<shape_part> parts{};

	[[nodiscard]] constexpr static collision_shape from_circle(const float radius){
		return collision_shape{{shape_part{circle_shape{radius}}}};
	}

	[[nodiscard]] constexpr static collision_shape from_capsule(
		const math::vec2 begin,
		const math::vec2 end,
		const float radius){
		return collision_shape{{shape_part{capsule_shape{begin, end, radius}}}};
	}

	[[nodiscard]] constexpr static collision_shape from_box_half_extent(const math::vec2 half_extent){
		return collision_shape{{shape_part{box_shape{half_extent}}}};
	}

	template<polygon_vertex_range Vertices>
	[[nodiscard]] constexpr static collision_shape from_convex_polygon(Vertices&& vertices){
		return collision_shape{{shape_part{make_convex_polygon(std::forward<Vertices>(vertices))}}};
	}

	template<polygon_vertex_range Vertices>
	[[nodiscard]] constexpr static collision_shape from_polygon(Vertices&& vertices){
		collision_shape shape{};
		auto pieces = decompose_polygon(std::forward<Vertices>(vertices));
		shape.parts.reserve(pieces.size());
		for(auto& piece : pieces){
			shape.parts.push_back(shape_part{std::move(piece)});
		}
		return shape;
	}

	[[nodiscard]] constexpr std::size_t part_count() const noexcept{
		return parts.size();
	}

	[[nodiscard]] constexpr bool empty() const noexcept{
		return parts.empty();
	}

	[[nodiscard]] constexpr math::vec2 support(const math::vec2 direction, const math::trans2 transform = {}) const noexcept{
		if(parts.empty()){
			return transform.vec;
		}

		bool initialized{};
		math::vec2 best{};
		float best_dot = -std::numeric_limits<float>::infinity();

		for(const auto& part : parts){
			const auto part_transform = detail::combine_transform(part.local_transform, transform);
			const auto local_direction = detail::to_local_direction(direction, part_transform);
			const auto local_support = std::visit([&](const auto& primitive){
				return detail::support_local(primitive, local_direction);
			}, part.primitive);
			const auto world_support = local_support >> part_transform;
			const auto projected = world_support.dot(direction);
			if(!initialized || projected > best_dot){
				initialized = true;
				best = world_support;
				best_dot = projected;
			}
		}

		return best;
	}

	[[nodiscard]] constexpr math::frect aabb(const math::trans2 transform = {}) const noexcept{
		if(parts.empty()){
			return math::frect{transform.vec, 0.f};
		}

		const auto min_x = support({-1.f, 0.f}, transform).x;
		const auto max_x = support({1.f, 0.f}, transform).x;
		const auto min_y = support({0.f, -1.f}, transform).y;
		const auto max_y = support({0.f, 1.f}, transform).y;
		return math::frect{tags::from_vertex, {min_x, min_y}, {max_x, max_y}};
	}

	[[nodiscard]] constexpr float radius_bound() const noexcept{
		float result{};
		for(const auto& part : parts){
			const auto local_origin = part.local_transform.vec;
			const auto radius = std::visit([&](const auto& primitive) -> float{
				using primitive_type = std::decay_t<decltype(primitive)>;
				if constexpr(std::same_as<primitive_type, circle_shape>){
					return primitive.radius;
				} else if constexpr(std::same_as<primitive_type, capsule_shape>){
					return std::sqrt(std::max(primitive.begin.length2(), primitive.end.length2())) + primitive.radius;
				} else if constexpr(std::same_as<primitive_type, box_shape>){
					return primitive.half_extent.length();
				} else{
					float best{};
					for(const auto vertex : primitive.vertices){
						best = std::max(best, vertex.length());
					}
					return best;
				}
			}, part.primitive);
			result = std::max(result, local_origin.length() + radius);
		}
		return result;
	}
};
}
