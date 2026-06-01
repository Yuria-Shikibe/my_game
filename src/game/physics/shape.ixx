export module mo_yanxi.game.physics.shape;

export import mo_yanxi.math.vector2;
export import mo_yanxi.math.rect_ortho;
export import mo_yanxi.math.trans2;

import mo_yanxi.aligned_allocator;
import std;

namespace mo_yanxi::game::physics{
export
template<typename Range>
concept polygon_vertex_range =
	std::ranges::sized_range<Range>
	&& std::ranges::bidirectional_range<Range>
	&& std::convertible_to<std::ranges::range_reference_t<Range>, math::vec2>;

namespace detail{
constexpr float geometry_epsilon = 1.0e-5f;

[[nodiscard]] constexpr std::uint32_t checked_u32(const std::size_t value){
	if(value > std::numeric_limits<std::uint32_t>::max()){
		throw std::length_error{"collision shape exceeds 32-bit storage limits"};
	}
	return static_cast<std::uint32_t>(value);
}

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

template<typename Function, typename Iterator>
concept cyclic_triplet_callback = std::invocable<Function&, Iterator, Iterator, Iterator>;

template<typename Range, typename Function>
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
enum class shape_type : std::uint8_t{
	circle,
	capsule,
	box,
	convex_polygon
};

export
struct convex_polygon_storage{
	std::uint32_t vertex_offset{};
	std::uint32_t vertex_count{};

	[[nodiscard]] constexpr std::size_t size() const noexcept{
		return vertex_count;
	}

	[[nodiscard]] constexpr bool valid() const noexcept{
		return vertex_count >= 3;
	}
};

export
template<typename Shape>
struct shape_of{
	using payload_type = Shape;

	math::trans2 local_transform{};
	Shape shape{};
};

export
template<typename Shape>
struct shape_type_of;

template<>
struct shape_type_of<circle_shape> : std::integral_constant<shape_type, shape_type::circle>{};

template<>
struct shape_type_of<capsule_shape> : std::integral_constant<shape_type, shape_type::capsule>{};

template<>
struct shape_type_of<box_shape> : std::integral_constant<shape_type, shape_type::box>{};

template<>
struct shape_type_of<convex_polygon_shape> : std::integral_constant<shape_type, shape_type::convex_polygon>{};

export
template<typename Shape>
inline constexpr shape_type shape_type_of_v = shape_type_of<std::remove_cvref_t<Shape>>::value;

export
template<shape_type Type>
struct shape_type_payload;

template<>
struct shape_type_payload<shape_type::circle>{
	using type = circle_shape;
};

template<>
struct shape_type_payload<shape_type::capsule>{
	using type = capsule_shape;
};

template<>
struct shape_type_payload<shape_type::box>{
	using type = box_shape;
};

template<>
struct shape_type_payload<shape_type::convex_polygon>{
	using type = convex_polygon_shape;
};

export
template<shape_type Type>
using shape_type_payload_t = typename shape_type_payload<Type>::type;

export
using collision_shape_record_circle_payload = circle_shape;

export
using collision_shape_record_capsule_payload = capsule_shape;

export
using collision_shape_record_box_payload = box_shape;

export
using collision_shape_record_convex_polygon_payload = convex_polygon_storage;

export
union collision_shape_record_payload{
	collision_shape_record_circle_payload circle;
	collision_shape_record_capsule_payload capsule;
	collision_shape_record_box_payload box;
	collision_shape_record_convex_polygon_payload convex_polygon;

	constexpr collision_shape_record_payload() noexcept : circle{} {}
};

export
struct collision_shape_record_part{
	shape_type type{shape_type::circle};
	math::trans2 local_transform{};
	collision_shape_record_payload payload{};

	template<typename Self, typename Function>
	decltype(auto) visit_payload(this Self&& self, Function&& function){
		switch(self.type){
			case shape_type::circle:
				return std::invoke(
					std::forward<Function>(function),
					std::forward_like<Self>(self.payload.circle));
			case shape_type::capsule:
				return std::invoke(
					std::forward<Function>(function),
					std::forward_like<Self>(self.payload.capsule));
			case shape_type::box:
				return std::invoke(
					std::forward<Function>(function),
					std::forward_like<Self>(self.payload.box));
			case shape_type::convex_polygon:
				return std::invoke(
					std::forward<Function>(function),
					std::forward_like<Self>(self.payload.convex_polygon));
		default:
			std::unreachable();
		}
	}
};

static_assert(std::is_trivially_destructible_v<collision_shape_record_payload>);
static_assert(std::is_trivially_copyable_v<collision_shape_record_payload>);
static_assert(std::is_trivially_destructible_v<collision_shape_record_part>);
static_assert(std::is_trivially_copyable_v<collision_shape_record_part>);
static_assert(std::is_trivially_destructible_v<math::vec2>);

export
struct collision_shape_record;

export
struct collision_shape_query_transform{
	math::trans2 transform{};
	float cos{1.f};
	float sin{};

	[[nodiscard]] static constexpr bool is_identity(const math::trans2 transform) noexcept{
		return transform.vec.is_zero() && transform.rot == 0.f;
	}

	[[nodiscard]] static constexpr collision_shape_query_transform make(const math::trans2 transform) noexcept{
		const auto [cos_value, sin_value] = math::cos_sin(transform.rot);
		return {
			.transform = transform,
			.cos = cos_value,
			.sin = sin_value
		};
	}

	[[nodiscard]] static constexpr collision_shape_query_transform combine(
		const math::trans2 local,
		const collision_shape_query_transform world) noexcept{
		if(collision_shape_query_transform::is_identity(local)){
			return world;
		}

		const math::trans2 transform{
			world.apply_to(local.vec),
			local.rot + world.transform.rot
		};
		if(local.rot == 0.f){
			return {
				.transform = transform,
				.cos = world.cos,
				.sin = world.sin
			};
		}
		return collision_shape_query_transform::make(transform);
	}

	[[nodiscard]] constexpr math::vec2 rotate_to_local(math::vec2 direction) const noexcept{
		direction.rotate(this->cos, -this->sin);
		return direction;
	}

	[[nodiscard]] constexpr math::vec2 rotate_to_world(math::vec2 point) const noexcept{
		point.rotate(this->cos, this->sin);
		return point;
	}

	[[nodiscard]] constexpr math::vec2 apply_to(const math::vec2 point) const noexcept{
		return this->rotate_to_world(point) + this->transform.vec;
	}
};

export
struct collision_shape_record_query{
	const collision_shape_record* shape{};
	collision_shape_query_transform transform{};

	[[nodiscard]] math::vec2 support(math::vec2 direction, math::trans2 parent_transform = {}) const noexcept;
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
template<polygon_vertex_range Vertices, typename Function>
	requires std::invocable<Function&, convex_polygon_shape>
constexpr void decompose_polygon(Vertices&& vertices, Function&& function){
	auto cleaned = detail::clean_polygon(std::forward<Vertices>(vertices));
	if(cleaned.size() < 3){
		throw std::invalid_argument{"polygon requires at least three vertices"};
	}

	auto emit = [&](convex_polygon_shape polygon){
		std::invoke(function, std::move(polygon));
	};

	if(detail::is_convex_cleaned_polygon(cleaned)){
		emit(convex_polygon_shape{std::move(cleaned)});
		return;
	}

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
			emit(physics::make_convex_polygon(triangle));
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
	emit(make_convex_polygon(triangle));
}

export
template<polygon_vertex_range Vertices>
[[nodiscard]] constexpr std::vector<convex_polygon_shape> decompose_polygon(Vertices&& vertices){
	std::vector<convex_polygon_shape> result{};
	const auto source_size = std::ranges::size(vertices);
	if(source_size > 2){
		result.reserve(source_size - 2);
	}
	physics::decompose_polygon(std::forward<Vertices>(vertices), [&](convex_polygon_shape polygon){
		result.push_back(std::move(polygon));
	});
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

template<std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
[[nodiscard]] constexpr math::vec2 support_vertices(Iterator first, const Sentinel last, const math::vec2 direction) noexcept{
	if(first == last){
		return {};
	}

	auto best = static_cast<math::vec2>(*first);
	auto best_dot = best.dot(direction);
	for(++first; first != last; ++first){
		const math::vec2 vertex = *first;
		if(const auto value = vertex.dot(direction); value > best_dot){
			best = vertex;
			best_dot = value;
		}
	}
	return best;
}

[[nodiscard]] constexpr math::vec2 support_local(const convex_polygon_shape& polygon, const math::vec2 direction) noexcept{
	return support_vertices(polygon.vertices.begin(), polygon.vertices.end(), direction);
}

[[nodiscard]] constexpr float local_radius_bound(const circle_shape& circle) noexcept{
	return circle.radius;
}

[[nodiscard]] constexpr float local_radius_bound(const capsule_shape& capsule) noexcept{
	return std::sqrt(std::max(capsule.begin.length2(), capsule.end.length2())) + capsule.radius;
}

[[nodiscard]] constexpr float local_radius_bound(const box_shape& box) noexcept{
	return box.half_extent.length();
}

[[nodiscard]] constexpr float local_radius_bound(const convex_polygon_shape& polygon) noexcept{
	float best{};
	for(const auto vertex : polygon.vertices){
		best = std::max(best, vertex.length());
	}
	return best;
}

struct bounds_accumulator{
	bool initialized{};
	math::vec2 min{};
	math::vec2 max{};

	constexpr void include(const math::vec2 point) noexcept{
		if(!initialized){
			initialized = true;
			min = point;
			max = point;
			return;
		}

		min.x = std::min(min.x, point.x);
		min.y = std::min(min.y, point.y);
		max.x = std::max(max.x, point.x);
		max.y = std::max(max.y, point.y);
	}

	constexpr void include_expanded(const math::vec2 center, const float radius) noexcept{
		const math::vec2 expansion{radius, radius};
		include(center - expansion);
		include(center + expansion);
	}

	[[nodiscard]] constexpr math::frect rect() const noexcept{
		return math::frect{tags::from_vertex, min, max};
	}
};

constexpr void include_shape_bounds(
	bounds_accumulator& bounds,
	const circle_shape& circle,
	const math::trans2 transform) noexcept{
	bounds.include_expanded(transform.vec, circle.radius);
}

constexpr void include_shape_bounds(
	bounds_accumulator& bounds,
	const capsule_shape& capsule,
	const math::trans2 transform) noexcept{
	bounds.include_expanded(capsule.begin >> transform, capsule.radius);
	bounds.include_expanded(capsule.end >> transform, capsule.radius);
}

constexpr void include_shape_bounds(
	bounds_accumulator& bounds,
	const box_shape& box,
	const math::trans2 transform) noexcept{
	const auto half = box.half_extent;
	bounds.include(math::vec2{-half.x, -half.y} >> transform);
	bounds.include(math::vec2{half.x, -half.y} >> transform);
	bounds.include(math::vec2{-half.x, half.y} >> transform);
	bounds.include(math::vec2{half.x, half.y} >> transform);
}

constexpr void include_shape_bounds(
	bounds_accumulator& bounds,
	const convex_polygon_shape& polygon,
	const math::trans2 transform) noexcept{
	for(const auto vertex : polygon.vertices){
		bounds.include(vertex >> transform);
	}
}

inline void include_record_bounds(
	bounds_accumulator& bounds,
	const collision_shape_record_part& part,
	const std::span<const math::vec2> polygon_vertices,
	const math::trans2 transform) noexcept{
	switch(part.type){
		case shape_type::circle:
			include_shape_bounds(bounds, part.payload.circle, transform);
			return;
		case shape_type::capsule:
			include_shape_bounds(bounds, part.payload.capsule, transform);
			return;
		case shape_type::box:
			include_shape_bounds(bounds, part.payload.box, transform);
			return;
		case shape_type::convex_polygon:{
			const auto first = polygon_vertices.begin() + part.payload.convex_polygon.vertex_offset;
			const auto last = first + part.payload.convex_polygon.vertex_count;
			for(auto current = first; current != last; ++current){
				bounds.include(*current >> transform);
			}
			return;
		}
	}
	std::unreachable();
}

}

export
struct collision_shape{
	std::vector<shape_of<circle_shape>> circles{};
	std::vector<shape_of<capsule_shape>> capsules{};
	std::vector<shape_of<box_shape>> boxes{};
	std::vector<shape_of<convex_polygon_shape>> convex_polygons{};

	template<shape_type Type, typename S>
	[[nodiscard]] constexpr auto& components(this S&& self) noexcept{
		if constexpr(Type == shape_type::circle){
			return std::forward_like<S>(self.circles);
		}else if constexpr(Type == shape_type::capsule){
			return std::forward_like<S>(self.capsules);
		}else if constexpr(Type == shape_type::box){
			return std::forward_like<S>(self.boxes);
		}else if constexpr (Type == shape_type::convex_polygon){
			return std::forward_like<S>(self.convex_polygons);
		}else{
			static_assert(false, "Unsupported shape_type");
		}
	}

	template<typename Shape>
	constexpr auto& add(shape_of<Shape> component) & {
		using payload_type = std::remove_cvref_t<Shape>;
		auto& storage = components<shape_type_of_v<payload_type>>();
		return storage.emplace_back(std::move(component));
	}


	template<polygon_vertex_range Vertices>
	constexpr void add_convex_polygon(Vertices&& vertices){
		auto polygon = physics::make_convex_polygon(std::forward<Vertices>(vertices));
		add(shape_of<convex_polygon_shape>{.shape = std::move(polygon)});
	}

	template<polygon_vertex_range Vertices>
	constexpr void add_polygon(Vertices&& vertices){
		const auto source_size = std::ranges::size(vertices);
		if(source_size > 2){
			convex_polygons.reserve(convex_polygons.size() + source_size - 2);
		}
		physics::decompose_polygon(std::forward<Vertices>(vertices), [&](convex_polygon_shape polygon){
			add(shape_of<convex_polygon_shape>{.shape = std::move(polygon)});
		});
	}

	constexpr collision_shape& splice(collision_shape&& other){
		circles.append_range(other.circles | std::views::as_rvalue);
		capsules.append_range(other.capsules | std::views::as_rvalue);
		boxes.append_range(other.boxes | std::views::as_rvalue);
		convex_polygons.append_range(other.convex_polygons | std::views::as_rvalue);
		return *this;
	}

	constexpr collision_shape& splice(const collision_shape& other){
		circles.append_range(other.circles);
		capsules.append_range(other.capsules);
		boxes.append_range(other.boxes);
		convex_polygons.append_range(other.convex_polygons);
		return *this;
	}

	[[nodiscard]] constexpr std::size_t part_count() const noexcept{
		return circles.size() + capsules.size() + boxes.size() + convex_polygons.size();
	}

	[[nodiscard]] constexpr bool empty() const noexcept{
		return circles.empty() && capsules.empty() && boxes.empty() && convex_polygons.empty();
	}

	[[nodiscard]] collision_shape_record to_record() const;

	template<typename Self, typename Function>
	constexpr void visit_parts(this Self&& self, Function&& function){
		auto foreach_range = [&]<typename T>(T&& range){
			auto sz = std::ranges::size(range);
			for(decltype(sz) i = 0; i < sz; ++i){
				std::invoke(
					function,
					i,
					std::forward_like<Self>(range[i]));
			}
		};

		foreach_range(std::forward_like<Self>(self.circles));
		foreach_range(std::forward_like<Self>(self.capsules));
		foreach_range(std::forward_like<Self>(self.boxes));
		foreach_range(std::forward_like<Self>(self.convex_polygons));
	}

	[[nodiscard]] constexpr math::vec2 support(const math::vec2 direction, const math::trans2 transform = {}) const noexcept{
		if(empty()){
			return transform.vec;
		}

		bool initialized{};
		math::vec2 best{};
		float best_dot = -std::numeric_limits<float>::infinity();

		visit_parts([&](std::size_t, const auto& component){
			const auto part_transform = detail::combine_transform(component.local_transform, transform);
			const auto local_direction = detail::to_local_direction(direction, part_transform);
			const auto local_support = detail::support_local(component.shape, local_direction);
			const auto world_support = local_support >> part_transform;
			const auto projected = world_support.dot(direction);
			if(!initialized || projected > best_dot){
				initialized = true;
				best = world_support;
				best_dot = projected;
			}
		});

		return best;
	}

	[[nodiscard]] constexpr math::frect aabb(const math::trans2 transform = {}) const noexcept{
		if(empty()){
			return math::frect{transform.vec, 0.f};
		}

		detail::bounds_accumulator bounds{};
		visit_parts([&](std::size_t, const auto& component){
			const auto part_transform = detail::combine_transform(component.local_transform, transform);
			detail::include_shape_bounds(bounds, component.shape, part_transform);
		});
		return bounds.rect();
	}

	[[nodiscard]] constexpr float radius_bound() const noexcept{
		float result{};
		visit_parts([&](std::size_t, const auto& component){
			const auto local_origin = component.local_transform.vec;
			const auto radius = detail::local_radius_bound(component.shape);
			result = std::max(result, local_origin.length() + radius);
		});
		return result;
	}

	[[nodiscard]] constexpr friend collision_shape operator|(collision_shape lhs, collision_shape&& rhs){
		lhs.splice(std::move(rhs));
		return lhs;
	}

	[[nodiscard]] constexpr friend collision_shape operator|(collision_shape lhs, const collision_shape& rhs){
		lhs.splice(rhs);
		return lhs;
	}

	constexpr friend collision_shape& operator|=(collision_shape& lhs, collision_shape&& rhs){
		return lhs.splice(std::move(rhs));
	}

	constexpr friend collision_shape& operator|=(collision_shape& lhs, const collision_shape& rhs){
		return lhs.splice(rhs);
	}

};

export
[[nodiscard]] constexpr collision_shape make_circle_collision_shape(
	const float radius,
	const math::trans2 local_transform = {}){
	collision_shape shape{};
	shape.add(shape_of<circle_shape>{
		.local_transform = local_transform,
		.shape = {radius}
	});
	return shape;
}

export
[[nodiscard]] constexpr collision_shape make_capsule_collision_shape(
	const math::vec2 begin,
	const math::vec2 end,
	const float radius,
	const math::trans2 local_transform = {}){
	collision_shape shape{};
	shape.add(shape_of<capsule_shape>{
		.local_transform = local_transform,
		.shape = {begin, end, radius}
	});
	return shape;
}

export
[[nodiscard]] constexpr collision_shape make_box_collision_shape(
	const math::vec2 half_extent,
	const math::trans2 local_transform = {}){
	collision_shape shape{};
	shape.add(shape_of<box_shape>{
		.local_transform = local_transform,
		.shape = {half_extent}
	});
	return shape;
}

export
template<polygon_vertex_range Vertices>
[[nodiscard]] constexpr collision_shape make_convex_polygon_collision_shape(
	Vertices&& vertices,
	const math::trans2 local_transform = {}){
	collision_shape shape{};
	shape.add(shape_of<convex_polygon_shape>{
		.local_transform = local_transform,
		.shape = make_convex_polygon(std::forward<Vertices>(vertices))
	});
	return shape;
}

export
template<polygon_vertex_range Vertices>
[[nodiscard]] constexpr collision_shape make_polygon_collision_shape(
	Vertices&& vertices,
	const math::trans2 local_transform = {}){
	collision_shape shape{};
	const auto source_size = std::ranges::size(vertices);
	if(source_size > 2){
		shape.convex_polygons.reserve(source_size - 2);
	}
	physics::decompose_polygon(std::forward<Vertices>(vertices), [&](convex_polygon_shape polygon){
		shape.add(shape_of<convex_polygon_shape>{
			.local_transform = local_transform,
			.shape = std::move(polygon)
		});
	});
	return shape;
}

export
struct collision_shape_record{
	collision_shape_record() = default;

	explicit(false) collision_shape_record(const collision_shape& shape){
		assign(shape);
	}

	collision_shape_record(const collision_shape_record& other){
		copy_from(other);
	}

	collision_shape_record& operator=(const collision_shape_record& other){
		if(this != &other){
			copy_from(other);
		}
		return *this;
	}

	collision_shape_record(collision_shape_record&& other) noexcept{
		move_from(std::move(other));
	}

	collision_shape_record& operator=(collision_shape_record&& other) noexcept{
		if(this != &other){
			move_from(std::move(other));
		}
		return *this;
	}

	void assign(const collision_shape& shape){
		std::size_t vertex_count{};
		for(const auto& polygon : shape.convex_polygons){
			vertex_count += polygon.shape.vertices.size();
		}

		allocate(shape.part_count(), vertex_count);

		std::uint32_t next_vertex{};
		std::size_t record_index{};
		shape.visit_parts([&](std::size_t, const auto& component){
			auto& record = parts_[record_index++];
			record.local_transform = component.local_transform;

			using component_t = std::remove_cvref_t<decltype(component)>;
			using payload_t = typename component_t::payload_type;
			record.type = shape_type_of_v<payload_t>;
			if constexpr(std::same_as<payload_t, circle_shape>){
				std::construct_at(std::addressof(record.payload.circle), component.shape);
			}else if constexpr(std::same_as<payload_t, capsule_shape>){
				std::construct_at(std::addressof(record.payload.capsule), component.shape);
			}else if constexpr(std::same_as<payload_t, box_shape>){
				std::construct_at(std::addressof(record.payload.box), component.shape);
			}else{
				const auto vertex_count_u32 = detail::checked_u32(component.shape.vertices.size());
				std::construct_at(
					std::addressof(record.payload.convex_polygon),
					collision_shape_record_convex_polygon_payload{
						.vertex_offset = next_vertex,
						.vertex_count = vertex_count_u32
					});
				std::ranges::copy(
					component.shape.vertices,
					polygon_vertices_.begin() + static_cast<std::ptrdiff_t>(next_vertex));
				next_vertex += vertex_count_u32;
			}
		});
	}

	[[nodiscard]] static bool validate_packed_data(
		const std::span<const collision_shape_record_part> parts,
		const std::span<const math::vec2> polygon_vertices) noexcept{
		if(parts.empty()){
			return polygon_vertices.empty();
		}
		if(parts.size() > std::numeric_limits<std::uint32_t>::max()
			|| polygon_vertices.size() > std::numeric_limits<std::uint32_t>::max()){
			return false;
		}

		for(const collision_shape_record_part& part : parts){
			switch(part.type){
			case shape_type::circle:
			case shape_type::capsule:
			case shape_type::box:
				break;
			case shape_type::convex_polygon:{
				const auto offset = static_cast<std::size_t>(part.payload.convex_polygon.vertex_offset);
				const auto count = static_cast<std::size_t>(part.payload.convex_polygon.vertex_count);
				if(count < 3u || offset > polygon_vertices.size() || count > polygon_vertices.size() - offset){
					return false;
				}
				break;
			}
			default:
				return false;
			}
		}
		return true;
	}

	void assign_packed_data(
		const std::span<const collision_shape_record_part> parts,
		const std::span<const math::vec2> polygon_vertices){
		if(!collision_shape_record::validate_packed_data(parts, polygon_vertices)){
			throw std::invalid_argument{"invalid collision shape record packed data"};
		}

		allocate(parts.size(), polygon_vertices.size());
		std::ranges::copy(parts, parts_.begin());
		std::ranges::copy(polygon_vertices, polygon_vertices_.begin());
	}

	[[nodiscard]] std::span<const collision_shape_record_part> records() const noexcept{
		return parts_;
	}

	[[nodiscard]] std::span<const math::vec2> polygon_vertices() const noexcept{
		return polygon_vertices_;
	}

	[[nodiscard]] std::size_t storage_size() const noexcept{
		return heap_storage_.size();
	}

	[[nodiscard]] std::size_t part_count() const noexcept{
		return parts_.size();
	}

	[[nodiscard]] bool empty() const noexcept{
		return parts_.empty();
	}

	[[nodiscard]] math::vec2 support_local(const collision_shape_record_part& part, const math::vec2 direction) const noexcept{
		switch(part.type){
			case shape_type::circle:
				return detail::safe_normalized(direction) * part.payload.circle.radius;
			case shape_type::capsule:{
				const auto normal = detail::safe_normalized(direction);
				const auto endpoint = part.payload.capsule.begin.dot(direction) > part.payload.capsule.end.dot(direction)
					? part.payload.capsule.begin
					: part.payload.capsule.end;
				return endpoint + normal * part.payload.capsule.radius;
			}
			case shape_type::box:
				return {
					direction.x >= 0.f ? part.payload.box.half_extent.x : -part.payload.box.half_extent.x,
					direction.y >= 0.f ? part.payload.box.half_extent.y : -part.payload.box.half_extent.y
				};
			case shape_type::convex_polygon:{
				const auto first = polygon_vertices_.begin() + part.payload.convex_polygon.vertex_offset;
				const auto last = first + part.payload.convex_polygon.vertex_count;
				return detail::support_vertices(first, last, direction);
			}
		}
		return {};
	}

	[[nodiscard]] float local_radius_bound(const collision_shape_record_part& part) const noexcept{
		switch(part.type){
			case shape_type::circle:
				return part.payload.circle.radius;
			case shape_type::capsule:
				return std::sqrt(std::max(
					part.payload.capsule.begin.length2(),
					part.payload.capsule.end.length2())) + part.payload.capsule.radius;
			case shape_type::box:
				return part.payload.box.half_extent.length();
			case shape_type::convex_polygon:{
				float best{};
				for(std::uint32_t i = 0; i != part.payload.convex_polygon.vertex_count; ++i){
					best = std::max(best, polygon_vertices_[part.payload.convex_polygon.vertex_offset + i].length());
				}
				return best;
			}
		}
		return {};
	}

	[[nodiscard]] math::vec2 support(const math::vec2 direction, const math::trans2 transform = {}) const noexcept{
		return this->support_cached(direction, collision_shape_query_transform::make(transform));
	}

	[[nodiscard]] math::vec2 support_cached(
		const math::vec2 direction,
		const collision_shape_query_transform transform) const noexcept{
		if(parts_.empty()){
			return transform.transform.vec;
		}

		bool initialized{};
		math::vec2 best{};
		float best_dot = -std::numeric_limits<float>::infinity();

		for(const auto& part : parts_){
			const auto part_transform = collision_shape_query_transform::combine(part.local_transform, transform);
			const auto world_support = this->support_world(part, direction, part_transform);
			const auto projected = world_support.dot(direction);
			if(!initialized || projected > best_dot){
				initialized = true;
				best = world_support;
				best_dot = projected;
			}
		}

		return best;
	}

	[[nodiscard]] collision_shape_record_query query(const math::trans2 transform = {}) const noexcept;

	[[nodiscard]] math::frect aabb(const math::trans2 transform = {}) const noexcept{
		if(parts_.empty()){
			return math::frect{transform.vec, 0.f};
		}

		detail::bounds_accumulator bounds{};
		for(const auto& part : parts_){
			const auto part_transform = detail::combine_transform(part.local_transform, transform);
			detail::include_record_bounds(bounds, part, polygon_vertices_, part_transform);
		}
		return bounds.rect();
	}

	[[nodiscard]] float radius_bound() const noexcept{
		float result{};
		for(const auto& part : parts_){
			const auto local_origin = part.local_transform.vec;
			const auto radius = local_radius_bound(part);
			result = std::max(result, local_origin.length() + radius);
		}
		return result;
	}

private:
	using storage_allocator = mo_yanxi::aligned_allocator<std::byte, alignof(std::max_align_t)>;

	[[nodiscard]] math::vec2 support_world(
		const collision_shape_record_part& part,
		const math::vec2 direction,
		const collision_shape_query_transform transform) const noexcept{
		const auto local_direction = transform.rotate_to_local(direction);
		switch(part.type){
			case shape_type::circle:
				return transform.apply_to(detail::safe_normalized(local_direction) * part.payload.circle.radius);
			case shape_type::capsule:{
				const auto normal = detail::safe_normalized(local_direction);
				const auto endpoint = part.payload.capsule.begin.dot(local_direction) > part.payload.capsule.end.dot(local_direction)
					? part.payload.capsule.begin
					: part.payload.capsule.end;
				return transform.apply_to(endpoint + normal * part.payload.capsule.radius);
			}
			case shape_type::box:
				return transform.apply_to({
					local_direction.x >= 0.f ? part.payload.box.half_extent.x : -part.payload.box.half_extent.x,
					local_direction.y >= 0.f ? part.payload.box.half_extent.y : -part.payload.box.half_extent.y
				});
			case shape_type::convex_polygon:{
				const auto first = polygon_vertices_.begin() + part.payload.convex_polygon.vertex_offset;
				const auto last = first + part.payload.convex_polygon.vertex_count;
				return transform.apply_to(detail::support_vertices(first, last, local_direction));
			}
		}
		return {};
	}

	static constexpr std::size_t align_up(const std::size_t value, const std::size_t alignment) noexcept{
		return (value + alignment - 1) & ~(alignment - 1);
	}

	[[nodiscard]] std::byte* storage_data() noexcept{
		return heap_storage_.data();
	}

	[[nodiscard]] const std::byte* storage_data() const noexcept{
		return heap_storage_.data();
	}

	void allocate(const std::size_t part_count, const std::size_t vertex_count){
		static_cast<void>(detail::checked_u32(part_count));
		static_cast<void>(detail::checked_u32(vertex_count));

		parts_offset_ = align_up(0, alignof(collision_shape_record_part));
		vertices_offset_ = align_up(
			parts_offset_ + sizeof(collision_shape_record_part) * part_count,
			alignof(math::vec2));
		const auto storage_size = vertices_offset_ + sizeof(math::vec2) * vertex_count;
		if(storage_size == 0){
			clear_views();
			return;
		}

		heap_storage_.resize(storage_size);
		bind_views(part_count, vertex_count);
		for(std::size_t i = 0; i != parts_.size(); ++i){
			std::construct_at(parts_.data() + i);
		}
		for(std::size_t i = 0; i != polygon_vertices_.size(); ++i){
			std::construct_at(polygon_vertices_.data() + i);
		}
	}

	void bind_views(const std::size_t part_count, const std::size_t vertex_count) noexcept{
		auto* const base = storage_data();
		parts_ = {
			reinterpret_cast<collision_shape_record_part*>(base + parts_offset_),
			part_count
		};
		polygon_vertices_ = {
			reinterpret_cast<math::vec2*>(base + vertices_offset_),
			vertex_count
		};
	}

	void copy_from(const collision_shape_record& other){
		allocate(other.parts_.size(), other.polygon_vertices_.size());
		std::ranges::copy(other.parts_, parts_.begin());
		std::ranges::copy(other.polygon_vertices_, polygon_vertices_.begin());
	}

	void move_from(collision_shape_record&& other) noexcept{
		heap_storage_ = std::exchange(other.heap_storage_, {});
		parts_offset_ = std::exchange(other.parts_offset_, {});
		vertices_offset_ = std::exchange(other.vertices_offset_, {});
		parts_ = std::exchange(other.parts_, {});
		polygon_vertices_ = std::exchange(other.polygon_vertices_, {});
	}

	void clear_views() noexcept{
		heap_storage_.clear();
		parts_ = {};
		polygon_vertices_ = {};
		parts_offset_ = 0;
		vertices_offset_ = 0;
	}

	std::vector<std::byte, storage_allocator> heap_storage_{};

	std::size_t parts_offset_{};
	std::size_t vertices_offset_{};
	std::span<collision_shape_record_part> parts_{};
	std::span<math::vec2> polygon_vertices_{};
};

[[nodiscard]] inline collision_shape_record collision_shape::to_record() const{
	return collision_shape_record{*this};
}

[[nodiscard]] inline collision_shape_record_query collision_shape_record::query(const math::trans2 transform) const noexcept{
	return {
		.shape = this,
		.transform = collision_shape_query_transform::make(transform)
	};
}

[[nodiscard]] inline math::vec2 collision_shape_record_query::support(
	const math::vec2 direction,
	const math::trans2 parent_transform) const noexcept{
	if(collision_shape_query_transform::is_identity(parent_transform)){
		return this->shape->support_cached(direction, this->transform);
	}

	return this->shape->support_cached(
		direction,
		collision_shape_query_transform::combine(
			this->transform.transform,
			collision_shape_query_transform::make(parent_transform)));
}
}
