#include <gtest/gtest.h>

import std;
import mo_yanxi.game.physics;

namespace mo_yanxi::game::physics{
	struct bvh_test_item{
		int id{};
		math::frect box{};
		math::vec2 displacement{};
	};

	template <>
	struct bvh_trait<bvh_test_item>{
		using id_type = int;

		[[nodiscard]] static constexpr id_type id(const bvh_test_item& item) noexcept{
			return item.id;
		}

		[[nodiscard]] static constexpr math::frect aabb(const bvh_test_item& item) noexcept{
			return item.box;
		}

		[[nodiscard]] static math::frect fat_aabb(const bvh_test_item& item, const float margin) noexcept{
			auto result = item.box;
			if(!item.displacement.is_zero()){
				auto swept = item.box;
				swept.move(item.displacement);
				result.expand_by(swept);
			}
			result.expand(margin);
			return result;
		}
	};
}

namespace{
	using namespace mo_yanxi::game::physics;
	namespace math = mo_yanxi::math;
	namespace tags = mo_yanxi::tags;

	[[nodiscard]] bool near(const float lhs, const float rhs, const float margin = 0.01f) noexcept{
		return std::abs(lhs - rhs) <= margin;
	}

	[[nodiscard]] bool near_vec(const math::vec2 lhs, const math::vec2 rhs, const float margin = 0.01f) noexcept{
		return near(lhs.x, rhs.x, margin) && near(lhs.y, rhs.y, margin);
	}

	[[nodiscard]] constexpr math::frect make_rect(
		const float src_x,
		const float src_y,
		const float end_x,
		const float end_y) noexcept{
		return math::frect{tags::from_vertex, {src_x, src_y}, {end_x, end_y}};
	}

	static_assert(shape_type_of_v<circle_shape> == shape_type::circle);
	static_assert(shape_type_of_v<capsule_shape> == shape_type::capsule);
	static_assert(shape_type_of_v<box_shape> == shape_type::box);
	static_assert(shape_type_of_v<convex_polygon_shape> == shape_type::convex_polygon);
	static_assert(std::same_as<shape_type_payload_t<shape_type::circle>, circle_shape>);
	static_assert(std::same_as<shape_type_payload_t<shape_type::capsule>, capsule_shape>);
	static_assert(std::same_as<shape_type_payload_t<shape_type::box>, box_shape>);
	static_assert(std::same_as<shape_type_payload_t<shape_type::convex_polygon>, convex_polygon_shape>);

	static_assert([]{
		const std::array concave{
			math::vec2{0.f, 0.f},
			math::vec2{2.f, 0.f},
			math::vec2{2.f, 1.f},
			math::vec2{1.f, 1.f},
			math::vec2{1.f, 2.f},
			math::vec2{0.f, 2.f}
		};
		const std::array cleaned_convex{
			math::vec2{0.f, 0.f},
			math::vec2{1.f, 0.f},
			math::vec2{2.f, 0.f},
			math::vec2{2.f, 1.f},
			math::vec2{2.f, 1.f},
			math::vec2{0.f, 1.f},
			math::vec2{0.f, 0.f}
		};

		const auto pieces = decompose_polygon(concave);
		const auto compound = make_polygon_collision_shape(concave);
		const auto polygon = make_convex_polygon(cleaned_convex);
		const auto box = make_box_collision_shape({2.f, 1.f});
		const auto support = box.support({1.f, 1.f});
		return pieces.size() >= 2
			&& compound.part_count() == pieces.size()
			&& compound.convex_polygons.size() == pieces.size()
			&& compound.convex_polygons.front().shape.vertices.size() >= 3
			&& polygon.size() == 4
			&& support.x == 2.f
			&& support.y == 1.f;
	}());

	[[nodiscard]] bool test_filter(){
		collision_filter player{.category = 1u << 0, .mask = 1u << 1};
		collision_filter enemy{.category = 1u << 1, .mask = 1u << 0};
		collision_filter scenery{.category = 1u << 2, .mask = 1u << 2};

		if(!player.can_collide_with(enemy) || player.can_collide_with(scenery)){
			return false;
		}

		player.group = enemy.group = -7;
		if(player.can_collide_with(enemy)){
			return false;
		}

		player.group = enemy.group = 3;
		enemy.sensor = true;
		return player.can_collide_with(enemy) && !player.should_solve_with(enemy);
	}

	[[nodiscard]] bool test_shape_support_and_decomposition(){
		const auto box = make_box_collision_shape({2.f, 1.f});
		const auto support = box.support({1.f, 1.f});
		const auto box_record = box.to_record();
		const auto record_support = box_record.support({1.f, 1.f});
		if(!near(support.x, 2.f) || !near(support.y, 1.f)){
			return false;
		}
		if(!near(record_support.x, support.x) || !near(record_support.y, support.y)){
			return false;
		}

		const std::array concave{
			math::vec2{0.f, 0.f},
			math::vec2{2.f, 0.f},
			math::vec2{2.f, 1.f},
			math::vec2{1.f, 1.f},
			math::vec2{1.f, 2.f},
			math::vec2{0.f, 2.f}
		};

		const auto decomposed = decompose_polygon(concave);
		if(decomposed.size() < 2){
			return false;
		}

		const auto compound = make_polygon_collision_shape(concave);
		if(compound.part_count() != decomposed.size() || compound.empty()){
			return false;
		}
		if(compound.part_count() != decomposed.size()
			|| compound.convex_polygons.size() != decomposed.size()){
			return false;
		}
		std::size_t compound_vertex_count{};
		for(const auto& polygon : compound.convex_polygons){
			compound_vertex_count += polygon.shape.vertices.size();
			if(polygon.shape.vertices.size() < 3){
				return false;
			}
		}
		const auto compound_record = compound.to_record();
		if(compound_record.part_count() != compound.part_count()
			|| compound_record.records().size() != compound.part_count()
			|| compound_record.polygon_vertices().size() != compound_vertex_count){
			return false;
		}

		const std::list<math::vec2> concave_list{concave.begin(), concave.end()};
		const auto list_decomposed = decompose_polygon(concave_list);
		const auto list_compound = make_polygon_collision_shape(concave_list);
		if(list_decomposed.size() < 2 || list_compound.part_count() != list_decomposed.size()){
			return false;
		}
		if(list_compound.part_count() != list_decomposed.size()
			|| list_compound.convex_polygons.size() != list_decomposed.size()){
			return false;
		}
		for(const auto& polygon : list_compound.convex_polygons){
			if(polygon.shape.vertices.size() < 3){
				return false;
			}
		}

		const std::array cleaned_convex{
			math::vec2{0.f, 0.f},
			math::vec2{1.f, 0.f},
			math::vec2{2.f, 0.f},
			math::vec2{2.f, 1.f},
			math::vec2{2.f, 1.f},
			math::vec2{0.f, 1.f},
			math::vec2{0.f, 0.f}
		};

		const auto convex = make_convex_polygon(cleaned_convex);
		const std::list<math::vec2> convex_list{cleaned_convex.begin(), cleaned_convex.end()};
		const auto list_convex = make_convex_polygon_collision_shape(convex_list);
		const auto list_convex_record = list_convex.to_record();
		return convex.size() == 4
			&& !list_convex.empty()
			&& list_convex.part_count() == 1
			&& list_convex.convex_polygons.size() == 1
			&& list_convex.convex_polygons[0].shape.vertices.size() == 4
			&& list_convex_record.records().size() == 1
			&& list_convex_record.polygon_vertices().size() == 4;
	}

	[[nodiscard]] bool test_shape_metadata_api(){
		collision_shape shape{};
		const auto first_circle = shape.add(shape_of<circle_shape>{
			.local_transform = {.vec = {1.f, 2.f}, .rot = 0.25f},
			.shape = {1.f}
		});
		const auto box = shape.add(shape_of<box_shape>{
			.local_transform = {.vec = {-1.f, 0.5f}, .rot = 0.5f},
			.shape = {{2.f, 1.f}}
		});
		const auto second_circle = shape.add(shape_of<circle_shape>{
			.local_transform = {.vec = {3.f, -1.f}, .rot = -0.25f},
			.shape = {0.5f}
		});

		if(shape.part_count() != 3
			|| shape.circles.size() != 2
			|| shape.components<shape_type::circle>().size() != 2
			|| shape.boxes.size() != 1
			|| shape.components<shape_type::box>().size() != 1
			|| !near(first_circle.shape.radius, 1.f)
			|| !near(box.shape.half_extent.x, 2.f)
			|| !near(second_circle.shape.radius, 0.5f)){
			return false;
		}

		if(!near(shape.circles[0].shape.radius, 1.f)
			|| !near(shape.circles[1].shape.radius, 0.5f)
			|| !near(shape.boxes[0].shape.half_extent.x, 2.f)
			|| !near(shape.circles[0].local_transform.vec.x, 1.f)
			|| !near(shape.circles[1].local_transform.vec.y, -1.f)){
			return false;
		}

		std::size_t visited{};
		shape.visit_parts([&](std::size_t, const auto& component){
			visited += component.local_transform.vec.x > -10.f ? 1u : 0u;
		});
		if(visited != shape.part_count()){
			return false;
		}

		const auto record = shape.to_record();
		if(record.records().size() != 3
			|| record.records()[0].type != shape_type::circle
			|| record.records()[1].type != shape_type::circle
			|| record.records()[2].type != shape_type::box
			|| !near(record.records()[0].payload.circle.radius, 1.f)
			|| !near(record.records()[1].payload.circle.radius, 0.5f)
			|| !near(record.records()[2].payload.box.half_extent.y, 1.f)
			|| !near(record.records()[0].local_transform.vec.x, 1.f)){
			return false;
		}

		const auto support = shape.support({1.f, 1.f});
		const auto record_support = record.support({1.f, 1.f});
		if(!near(support.x, record_support.x) || !near(support.y, record_support.y)){
			return false;
		}

		const auto bound = shape.aabb({.vec = {0.5f, -0.5f}, .rot = 0.1f});
		const auto record_bound = record.aabb({.vec = {0.5f, -0.5f}, .rot = 0.1f});
		if(!near(bound.vert_00().x, record_bound.vert_00().x)
			|| !near(bound.vert_00().y, record_bound.vert_00().y)
			|| !near(bound.vert_11().x, record_bound.vert_11().x)
			|| !near(bound.vert_11().y, record_bound.vert_11().y)
			|| !near(shape.radius_bound(), record.radius_bound())){
			return false;
		}

		collision_shape direct_polygon{};
		const std::array triangle{
			math::vec2{0.f, 0.f},
			math::vec2{1.f, 0.f},
			math::vec2{0.f, 1.f}
		};
		const auto direct_polygon_part = direct_polygon.add(shape_of<convex_polygon_shape>{
			.local_transform = {.vec = {1.f, 0.f}, .rot = 0.f},
			.shape = make_convex_polygon(triangle)
		});
		if(direct_polygon.part_count() != 1
			|| direct_polygon.convex_polygons.size() != 1
			|| direct_polygon.convex_polygons[0].shape.vertices.size() != triangle.size()
			|| !near(direct_polygon_part.local_transform.vec.x, 1.f)){
			return false;
		}

		collision_shape polygon_shape{};
		const std::array concave{
			math::vec2{0.f, 0.f},
			math::vec2{2.f, 0.f},
			math::vec2{2.f, 1.f},
			math::vec2{1.f, 1.f},
			math::vec2{1.f, 2.f},
			math::vec2{0.f, 2.f}
		};
		auto decomposed = decompose_polygon(concave);
		math::trans2 first_piece_transform{};
		bool saw_piece{};
		for(auto& piece : decomposed){
			const auto part = polygon_shape.add(shape_of<convex_polygon_shape>{
				.local_transform = {.vec = {2.f, 0.f}, .rot = 0.f},
				.shape = std::move(piece)
			});
			if(!saw_piece){
				first_piece_transform = part.local_transform;
				saw_piece = true;
			}
		}
		if(polygon_shape.part_count() < 2
			|| polygon_shape.convex_polygons.size() != polygon_shape.part_count()
			|| !saw_piece
			|| !near(first_piece_transform.vec.x, 2.f)){
			return false;
		}

		collision_shape capsule_meta{};
		capsule_meta.add(shape_of<capsule_shape>{.shape = {{-1.f, 0.f}, {1.f, 0.f}, 0.5f}});
		if(capsule_meta.part_count() != 1
			|| capsule_meta.capsules.size() != 1
			|| !near(capsule_meta.capsules[0].shape.radius, 0.5f)){
			return false;
		}

		auto spliced = make_circle_collision_shape(1.f);
		auto& spliced_ref = spliced.splice(make_box_collision_shape({2.f, 1.f}));
		if(std::addressof(spliced_ref) != std::addressof(spliced)
			|| spliced.part_count() != 2
			|| spliced.circles.size() != 1
			|| spliced.boxes.size() != 1
			|| !near(spliced.boxes[0].shape.half_extent.x, 2.f)){
			return false;
		}

		auto piped = make_circle_collision_shape(0.5f) | make_box_collision_shape({1.f, 1.f});
		if(piped.part_count() != 2 || piped.circles.size() != 1 || piped.boxes.size() != 1){
			return false;
		}

		piped |= make_capsule_collision_shape({-1.f, 0.f}, {1.f, 0.f}, 0.25f);
		if(piped.part_count() != 3
			|| piped.capsules.size() != 1
			|| !near(piped.capsules[0].shape.radius, 0.25f)){
			return false;
		}

		const auto piped_record = piped.to_record();
		const auto piped_bound = piped.aabb({.vec = {0.25f, -0.75f}, .rot = 0.37f});
		const auto piped_record_bound = piped_record.aabb({.vec = {0.25f, -0.75f}, .rot = 0.37f});
		return near(piped_bound.vert_00().x, piped_record_bound.vert_00().x)
			&& near(piped_bound.vert_00().y, piped_record_bound.vert_00().y)
			&& near(piped_bound.vert_11().x, piped_record_bound.vert_11().x)
			&& near(piped_bound.vert_11().y, piped_record_bound.vert_11().y);
	}

	[[nodiscard]] bool test_shape_record_cached_query_support(){
		collision_shape shape{};
		shape |= make_circle_collision_shape(0.75f, {.vec = {-0.5f, 0.25f}, .rot = 0.3f});
		shape |= make_capsule_collision_shape(
			{-0.8f, -0.1f},
			{0.9f, 0.25f},
			0.2f,
			{.vec = {0.7f, -0.3f}, .rot = -0.45f});
		shape |= make_box_collision_shape({0.6f, 1.1f}, {.vec = {1.4f, 0.6f}, .rot = 0.65f});

		const std::array triangle{
			math::vec2{-0.3f, -0.4f},
			math::vec2{0.8f, -0.2f},
			math::vec2{0.1f, 0.9f}
		};
		shape |= make_convex_polygon_collision_shape(triangle, {.vec = {-1.1f, 0.45f}, .rot = 0.2f});

		const auto record = shape.to_record();
		const std::array transforms{
			math::trans2{.vec = {0.f, 0.f}, .rot = 0.f},
			math::trans2{.vec = {2.f, -1.f}, .rot = 0.37f},
			math::trans2{.vec = {-1.5f, 2.25f}, .rot = -0.8f}
		};
		const std::array directions{
			math::vec2{1.f, 0.f},
			math::vec2{-0.35f, 0.9f},
			math::vec2{0.2f, -1.f},
			math::vec2{1.f, 1.f}
		};

		for(const auto transform : transforms){
			const auto query = record.query(transform);
			for(const auto direction : directions){
				const auto expected = shape.support(direction, transform);
				if(!near_vec(record.support(direction, transform), expected)
					|| !near_vec(query.support(direction), expected)){
					return false;
				}
			}
		}

		const math::trans2 base{.vec = {0.5f, -0.75f}, .rot = 0.4f};
		const math::trans2 parent{.vec = {-2.f, 1.25f}, .rot = -0.35f};
		const auto composed = base >> parent;
		const auto query = record.query(base);
		for(const auto direction : directions){
			if(!near_vec(query.support(direction, parent), record.support(direction, composed))){
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] bool test_shape_record_storage_and_payload_visit(){
		collision_shape empty{};
		const auto empty_record = empty.to_record();
		if(!empty_record.empty()
			|| empty_record.part_count() != 0
			|| empty_record.storage_size() != 0){
			return false;
		}

		const math::trans2 empty_transform{.vec = {4.f, -3.f}, .rot = 0.f};
		const auto empty_support = empty_record.support({1.f, 0.f}, empty_transform);
		if(!near(empty_support.x, empty_transform.vec.x) || !near(empty_support.y, empty_transform.vec.y)){
			return false;
		}

		const auto circle_record = make_circle_collision_shape(3.f).to_record();
		if(circle_record.records().size() != 1
			|| circle_record.storage_size() == 0){
			return false;
		}
		const auto circle_ok = circle_record.records()[0].visit_payload([&]<class Payload>(const Payload& payload){
			using payload_t = std::remove_cvref_t<Payload>;
			if constexpr(std::same_as<payload_t, collision_shape_record_circle_payload>){
				return near(payload.radius, 3.f);
			} else{
				return false;
			}
		});
		if(!circle_ok){
			return false;
		}

		const auto capsule_record = make_capsule_collision_shape({-1.f, 0.f}, {1.f, 0.f}, 0.5f).to_record();
		const auto capsule_ok = capsule_record.records()[0].visit_payload([&]<class Payload>(const Payload& payload){
			using payload_t = std::remove_cvref_t<Payload>;
			if constexpr(std::same_as<payload_t, collision_shape_record_capsule_payload>){
				return near(payload.begin.x, -1.f)
					&& near(payload.end.x, 1.f)
					&& near(payload.radius, 0.5f);
			} else{
				return false;
			}
		});
		if(!capsule_ok){
			return false;
		}

		const auto box_record = make_box_collision_shape({2.f, 1.f}).to_record();
		const auto box_ok = box_record.records()[0].visit_payload([&]<class Payload>(const Payload& payload){
			using payload_t = std::remove_cvref_t<Payload>;
			if constexpr(std::same_as<payload_t, collision_shape_record_box_payload>){
				return near(payload.half_extent.x, 2.f) && near(payload.half_extent.y, 1.f);
			} else{
				return false;
			}
		});
		if(!box_ok){
			return false;
		}

		const std::array polygon_vertices{
			math::vec2{0.f, 0.f},
			math::vec2{1.f, 0.f},
			math::vec2{0.f, 1.f}
		};
		const auto polygon_record = make_convex_polygon_collision_shape(polygon_vertices).to_record();
		const auto polygon_ok = polygon_record.records()[0].visit_payload([&]<class Payload>(const Payload& payload){
			using payload_t = std::remove_cvref_t<Payload>;
			if constexpr(std::same_as<payload_t, collision_shape_record_convex_polygon_payload>){
				return payload.vertex_offset == 0
					&& payload.vertex_count == static_cast<std::uint32_t>(polygon_vertices.size());
			} else{
				return false;
			}
		});
		if(!polygon_ok || polygon_record.polygon_vertices().size() != polygon_vertices.size()){
			return false;
		}

		auto moved_from = polygon_record;
		auto moved = collision_shape_record{std::move(moved_from)};
		if(!moved_from.empty() || moved_from.storage_size() != 0){
			return false;
		}
		moved = collision_shape_record{polygon_record};
		return moved.records().size() == polygon_record.records().size()
			&& moved.polygon_vertices().size() == polygon_record.polygon_vertices().size()
			&& near(moved.support({1.f, 1.f}).x, polygon_record.support({1.f, 1.f}).x)
			&& near(moved.support({1.f, 1.f}).y, polygon_record.support({1.f, 1.f}).y);
	}

	[[nodiscard]] bool test_gjk_epa(){
		const auto circle = make_circle_collision_shape(1.f);
		const auto circle_record = circle.to_record();

		const auto hit = collide(circle, {}, circle, {.vec = {1.5f, 0.f}, .rot = 0.f});
		if(!hit.hit || !hit.epa_converged || hit.normal.x < 0.9f || !near(hit.depth, 0.5f, 0.04f)){
			return false;
		}
		const auto record_hit = collide(circle_record, {}, circle_record, {.vec = {1.5f, 0.f}, .rot = 0.f});
		if(!record_hit.hit || !record_hit.epa_converged || !near(record_hit.depth, hit.depth, 0.04f)){
			return false;
		}

		const auto miss = collide(circle, {}, circle, {.vec = {3.0f, 0.f}, .rot = 0.f});
		if(miss.hit){
			return false;
		}

		const auto box_a = make_box_collision_shape({1.f, 1.f});
		const auto box_b = make_box_collision_shape({1.f, 1.f});
		const auto box_hit = collide(box_a, {}, box_b, {.vec = {1.25f, 0.f}, .rot = 0.f});
		if(!box_hit.hit || box_hit.depth <= 0.7f || box_hit.normal.x <= 0.9f){
			return false;
		}

		const auto capsule = make_capsule_collision_shape(
			{-0.5f, 0.f},
			{0.7f, 0.1f},
			0.25f,
			{.vec = {0.2f, -0.1f}, .rot = 0.3f});
		const auto box_record = box_a.to_record();
		const auto capsule_record = capsule.to_record();
		const math::trans2 box_transform{.vec = {-0.1f, 0.2f}, .rot = 0.25f};
		const math::trans2 capsule_transform{.vec = {0.95f, 0.15f}, .rot = -0.2f};
		const auto shape_contact = collide(box_a, box_transform, capsule, capsule_transform);
		const auto record_contact = collide(box_record, box_transform, capsule_record, capsule_transform);
		const auto query_contact = collide(box_record.query(box_transform), capsule_record.query(capsule_transform));
		return shape_contact.hit
			&& record_contact.hit
			&& query_contact.hit
			&& near(record_contact.depth, shape_contact.depth, 0.05f)
			&& near(query_contact.depth, record_contact.depth, 0.05f);
	}

	[[nodiscard]] bool test_contact_manifold(){
		const auto box = make_box_collision_shape({1.f, 1.f});
		const auto box_manifold = collide_manifold(box, {}, box, {.vec = {1.25f, 0.f}, .rot = 0.f});
		if(!box_manifold.hit
			|| box_manifold.point_count != 2
			|| box_manifold.normal.x < 0.9f){
			return false;
		}

		const auto first_y = box_manifold.points[0].point.y;
		const auto second_y = box_manifold.points[1].point.y;
		if(!near(std::abs(first_y), 1.f, 0.05f)
			|| !near(std::abs(second_y), 1.f, 0.05f)
			|| near(first_y, second_y, 0.05f)){
			return false;
		}

		const auto box_record = box.to_record();
		const auto query_contact = collide(
			box_record.query({}),
			box_record.query({.vec = {1.25f, 0.f}, .rot = 0.f}));
		const auto query_manifold = build_contact_manifold(
			box_record.query({}),
			{},
			box_record.query({.vec = {1.25f, 0.f}, .rot = 0.f}),
			{},
			query_contact);
		if(!query_manifold.hit
			|| query_manifold.point_count != 2
			|| query_manifold.normal.x < 0.9f){
			return false;
		}

		const auto circle = make_circle_collision_shape(1.f);
		const auto circle_hit = collide(circle, {}, circle, {.vec = {1.5f, 0.f}, .rot = 0.f});
		const auto circle_manifold = collide_manifold(circle, {}, circle, {.vec = {1.5f, 0.f}, .rot = 0.f});
		if(!circle_manifold.hit
			|| circle_manifold.point_count != 1
			|| !near(circle_manifold.representative().depth, circle_hit.depth, 0.05f)){
			return false;
		}

		const auto capsule = make_capsule_collision_shape({-1.f, 0.f}, {1.f, 0.f}, 0.25f);
		const auto capsule_manifold = collide_manifold(capsule, {}, box, {.vec = {0.f, 0.9f}, .rot = 0.f});
		return capsule_manifold.hit && capsule_manifold.point_count >= 1;
	}

	[[nodiscard]] bool test_linear_toi(){
		const auto mover_shape = make_circle_collision_shape(0.5f);
		const auto wall_shape = make_box_collision_shape({1.f, 1.f});
		const auto mover = mover_shape.to_record();
		const auto wall = wall_shape.to_record();
		const auto hit = linear_time_of_impact(
			mover,
			{.vec = {-5.f, 0.f}, .rot = 0.f},
			{.vec = {5.f, 0.f}, .rot = 0.f},
			wall,
			{},
			{},
			32,
			10);
		const auto shape_hit = linear_time_of_impact(
			mover_shape,
			{.vec = {-5.f, 0.f}, .rot = 0.f},
			{.vec = {5.f, 0.f}, .rot = 0.f},
			wall_shape,
			{},
			{},
			32,
			10);
		if(!hit.hit
			|| !shape_hit.hit
			|| hit.fraction < 0.3f
			|| hit.fraction > 0.4f
			|| !hit.contact.hit
			|| !near(hit.fraction, shape_hit.fraction, 0.02f)){
			return false;
		}

		const auto miss = linear_time_of_impact(
			mover,
			{.vec = {-5.f, 4.f}, .rot = 0.f},
			{.vec = {5.f, 4.f}, .rot = 0.f},
			wall,
			{},
			{});
		return !miss.hit;
	}

	[[nodiscard]] bool test_bvh(){
		dynamic_bvh<int> tree{0.05f};
		const auto a = tree.create_proxy(make_rect(0.f, 0.f, 1.f, 1.f), 10);
		const auto b = tree.create_proxy(make_rect(0.8f, 0.f, 1.8f, 1.f), 20);
		const auto c = tree.create_proxy(make_rect(5.f, 5.f, 6.f, 6.f), 30);

		int query_sum{};
		tree.query(make_rect(-0.1f, -0.1f, 2.f, 2.f), [&](bvh_proxy_id, const int value){
			query_sum += value;
		});
		if(query_sum != 30){
			return false;
		}

		int pair_count{};
		tree.collect_pairs([&](bvh_proxy_id, const int lhs, bvh_proxy_id, const int rhs){
			if((lhs == 10 && rhs == 20) || (lhs == 20 && rhs == 10)){
				++pair_count;
			}
		});
		if(pair_count != 1){
			return false;
		}

		const bool reinserted = tree.move_proxy(c, make_rect(1.2f, 0.f, 2.2f, 1.f));
		if(!reinserted){
			return false;
		}
		bool saw_c{};
		tree.query(make_rect(1.f, -0.1f, 2.4f, 1.1f), [&](bvh_proxy_id id, const int value){
			saw_c = saw_c || (id == c && value == 30);
		});

		tree.destroy_proxy(a);
		if(!saw_c || tree.size() != 2){
			return false;
		}

		static_assert(bvh_trait_has_id<bvh_test_item>);
		static_assert(bvh_trait_has_aabb<bvh_test_item>);
		static_assert(bvh_trait_has_fat_aabb<bvh_test_item>);

		dynamic_bvh<bvh_test_item> trait_tree{0.1f};
		const auto trait_a = trait_tree.create_proxy(bvh_test_item{
			.id = 1,
			.box = make_rect(0.f, 0.f, 1.f, 1.f),
			.displacement = {0.5f, 0.f}
		});
		const auto trait_b = trait_tree.create_proxy(bvh_test_item{
			.id = 2,
			.box = make_rect(1.25f, 0.f, 2.25f, 1.f)
		});

		int trait_query_sum{};
		trait_tree.query(make_rect(1.35f, 0.1f, 1.45f, 0.9f), [&](bvh_proxy_id, const bvh_test_item& item){
			trait_query_sum += bvh_trait<bvh_test_item>::id(item);
		});
		if(trait_query_sum != 3){
			return false;
		}

		trait_tree.value(trait_a).box = make_rect(0.2f, 0.f, 1.2f, 1.f);
		trait_tree.value(trait_a).displacement = {0.2f, 0.f};
		if(trait_tree.refresh_proxy(trait_a)){
			return false;
		}

		const bool trait_reinserted = trait_tree.move_proxy(trait_a, bvh_test_item{
			.id = 1,
			.box = make_rect(8.f, 0.f, 9.f, 1.f)
		});
		if(!trait_reinserted || trait_tree.aabb(trait_a) != make_rect(8.f, 0.f, 9.f, 1.f)){
			return false;
		}

		int trait_pair_count{};
		trait_tree.collect_pairs([&](bvh_proxy_id, const bvh_test_item& lhs, bvh_proxy_id, const bvh_test_item& rhs){
			if((lhs.id == 1 && rhs.id == 2) || (lhs.id == 2 && rhs.id == 1)){
				++trait_pair_count;
			}
		});
		if(trait_pair_count != 0){
			return false;
		}

		dynamic_bvh<int> sorted_tree{0.01f};
		for(int i = 0; i != 64; ++i){
			const float x = static_cast<float>(i) * 2.f;
			const auto proxy = sorted_tree.create_proxy(make_rect(x, 0.f, x + 1.f, 1.f), i);
			if(!proxy.valid()){
				return false;
			}
		}
		return sorted_tree.size() == 64 && sorted_tree.height() <= 16u;
	}

	[[nodiscard]] bool test_rigid_body(){
		const auto static_body = rigid_body::make_static();
		if(static_body.inverse_mass != 0.f || static_body.inverse_rotational_inertia != 0.f){
			return false;
		}

		auto dynamic = rigid_body::make_dynamic(2.f, 4.f);
		math::uniform_trans2 velocity{};
		dynamic.apply_impulse(velocity, {4.f, 0.f}, {0.f, 1.f});
		return near(velocity.vec.x, 2.f) && near(static_cast<float>(velocity.rot), -1.f);
	}
}

TEST(PhysicsCoreTest, Filter){
	EXPECT_TRUE(test_filter());
}

TEST(PhysicsCoreTest, ShapeSupportAndDecomposition){
	EXPECT_TRUE(test_shape_support_and_decomposition());
}

TEST(PhysicsCoreTest, ShapeMetadataApi){
	EXPECT_TRUE(test_shape_metadata_api());
}

TEST(PhysicsCoreTest, ShapeRecordCachedQuerySupport){
	EXPECT_TRUE(test_shape_record_cached_query_support());
}

TEST(PhysicsCoreTest, ShapeRecordStorageAndPayloadVisit){
	EXPECT_TRUE(test_shape_record_storage_and_payload_visit());
}

TEST(PhysicsCoreTest, GjkEpa){
	EXPECT_TRUE(test_gjk_epa());
}

TEST(PhysicsCoreTest, ContactManifold){
	EXPECT_TRUE(test_contact_manifold());
}

TEST(PhysicsCoreTest, LinearToi){
	EXPECT_TRUE(test_linear_toi());
}

TEST(PhysicsCoreTest, Bvh){
	EXPECT_TRUE(test_bvh());
}

TEST(PhysicsCoreTest, RigidBody){
	EXPECT_TRUE(test_rigid_body());
}
