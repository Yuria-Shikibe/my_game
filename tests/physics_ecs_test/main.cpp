#include <cstdlib>
#include <cstdio>

import std;
import mo_yanxi.game.ecs.component.chamber.damage_grid;
import mo_yanxi.game.ecs.component.damage;
import mo_yanxi.game.ecs.component.faction;
import mo_yanxi.game.ecs.component.manage;
import mo_yanxi.game.ecs.component.physics;
import mo_yanxi.game.ecs.component.projectile.manifold;
import mo_yanxi.game.ecs.system.physics;
import mo_yanxi.game.ecs.system.projectile;
import mo_yanxi.game.aiming;

namespace{
	using namespace mo_yanxi::game;
	using namespace mo_yanxi::game::ecs;
	namespace math = mo_yanxi::math;

	using body_desc = std::tuple<chunk_meta, mech_motion, collider, physics_body>;
	using projectile_desc = std::tuple<chunk_meta, mech_motion, collider, physics_body, projectile_manifold>;
	using damageable_desc = std::tuple<chunk_meta, mech_motion, collider, physics_body, hit_point, faction_data>;

	[[nodiscard]] bool near(const float lhs, const float rhs, const float margin = 0.05f) noexcept{
		return std::abs(lhs - rhs) <= margin;
	}

	[[nodiscard]] physics_body make_stable_dynamic_body(
		const float mass,
		const float rotational_inertia,
		const float friction = 0.8f) noexcept{
		auto body = physics_body::make_dynamic(mass, rotational_inertia);
		body.body.friction = friction;
		body.body.restitution = 0.f;
		body.body.linear_drag = 0.1f;
		body.body.angular_drag = 0.1f;
		return body;
	}

	template <typename Desc = body_desc>
	entity_id spawn(
		component_manager& manager,
		const math::vec2 position,
		const physics::collision_shape& shape,
		physics_body body,
		physics::collision_filter filter = {},
		const math::vec2 velocity = {},
		const bool sensor = false,
		const physics::ccd_mode ccd = physics::ccd_mode::disabled){
		tuple_to_comp_t<Desc> components{};
		components.template get<mech_motion>().trans.vec = position;
		components.template get<mech_motion>().vel.vec = velocity;
		components.template get<collider>().shape = shape.to_record();
		components.template get<collider>().filter = filter;
		components.template get<collider>().filter.sensor = sensor;
		components.template get<collider>().ccd = ccd;
		components.template get<collider>().ccd_threshold = 0.1f;
		components.template get<physics_body>() = body;
		components.template get<physics_body>().body.ccd = ccd;
		components.template get<physics_body>().body.ccd_threshold = 0.1f;
		return manager.spawn<Desc>(std::move(components));
	}

	[[nodiscard]] bool test_channel_group_and_sensor_filter(){
		component_manager manager{};
		manager.update_update_delta(0.016f);
		system::physics_system physics_sys{};
		const auto shape = physics::make_box_collision_shape({0.5f, 0.5f});

		const auto player = spawn(
			manager,
			{-0.25f, 0.f},
			shape,
			physics_body::make_dynamic(1.f),
			{.category = 1u << 0, .mask = 1u << 1});
		const auto enemy = spawn(
			manager,
			{0.25f, 0.f},
			shape,
			physics_body::make_static(),
			{.category = 1u << 1, .mask = 1u << 0});
		const auto ignored = spawn(
			manager,
			{0.25f, 2.f},
			shape,
			physics_body::make_static(),
			{.category = 1u << 2, .mask = 0});
		manager.commit();

		physics_sys.step(manager);
		const auto events = physics_sys.contact_events();
		if(events.size() != 1 || events.front().key != physics_contact_key::ordered(player, enemy)){
			return false;
		}
		if(ignored.is_expired()){
			return false;
		}

		component_manager sensor_manager{};
		sensor_manager.update_update_delta(0.016f);
		system::physics_system sensor_physics_sys{};
		const auto dynamic = spawn(sensor_manager, {-0.25f, 0.f}, shape, physics_body::make_dynamic(1.f), {}, {2.f, 0.f});
		const auto sensor = spawn(sensor_manager, {0.25f, 0.f}, shape, physics_body::make_static(), {}, {}, true);
		sensor_manager.commit();
		const auto before = dynamic.at<mech_motion>().vel.vec.x;
		sensor_physics_sys.step(sensor_manager);
		if(sensor_physics_sys.contact_events().size() != 1
			|| !sensor_physics_sys.contact_events().front().sensor
			|| sensor_physics_sys.contact_events().front().key != physics_contact_key::ordered(dynamic, sensor)){
			return false;
		}
		return near(dynamic.at<mech_motion>().vel.vec.x, before, 0.001f);
	}

	[[nodiscard]] bool test_impulses(){
		const auto shape = physics::make_box_collision_shape({0.5f, 0.5f});

		component_manager static_manager{};
		static_manager.update_update_delta(0.016f);
		system::physics_system static_physics_sys{};
		const auto dynamic = spawn(static_manager, {-0.65f, 0.f}, shape, physics_body::make_dynamic(1.f), {}, {2.f, 0.f});
		(void)spawn(static_manager, {0.f, 0.f}, shape, physics_body::make_static());
		static_manager.commit();
		static_physics_sys.step(static_manager);
		if(dynamic.at<mech_motion>().vel.vec.x >= 2.f){
			return false;
		}

		component_manager dynamic_manager{};
		dynamic_manager.update_update_delta(0.016f);
		system::physics_system dynamic_physics_sys{};
		const auto lhs = spawn(dynamic_manager, {-0.4f, 0.f}, shape, physics_body::make_dynamic(1.f), {}, {1.f, 0.f});
		const auto rhs = spawn(dynamic_manager, {0.4f, 0.f}, shape, physics_body::make_dynamic(1.f), {}, {-1.f, 0.f});
		dynamic_manager.commit();
		dynamic_physics_sys.step(dynamic_manager);
		return lhs.at<mech_motion>().vel.vec.x < 1.f
			&& rhs.at<mech_motion>().vel.vec.x > -1.f;
	}

	[[nodiscard]] bool test_ccd_projectile_event_and_query(){
		component_manager manager{};
		manager.update_update_delta(1.f);
		system::physics_system physics_sys{};
		const auto projectile_shape = physics::make_circle_collision_shape(0.25f);
		const auto wall_shape = physics::make_box_collision_shape({0.5f, 1.f});

		const auto projectile_entity = spawn<projectile_desc>(
			manager,
			{-5.f, 0.f},
			projectile_shape,
			physics_body::make_dynamic(1.f),
			{},
			{10.f, 0.f},
			false,
			physics::ccd_mode::linear_sweep);
		const auto wall = spawn(manager, {0.f, 0.f}, wall_shape, physics_body::make_static());
		manager.commit();

		physics_sys.step(manager);
		const auto events = physics_sys.contact_events();
		if(std::ranges::none_of(events, [&](const physics_contact_event& event){
			return event.phase == physics_contact_phase::begin
				&& event.key == physics_contact_key::ordered(projectile_entity, wall)
				&& event.toi > 0.f
				&& event.toi < 1.f
				&& event.endpoint_for(projectile_entity).id() == projectile_entity
				&& event.endpoint_for(projectile_entity).previous_shape.vec.x < -4.9f
				&& event.endpoint_for(projectile_entity).current_shape.vec.x > 4.9f;
		})){
			return false;
		}

		targeting_queue queue{};
		queue.index_candidates_by_distance(
			physics_sys,
			projectile_entity,
			{0.f, 0.f},
			{0.f, 3.f});
		return queue.get_optimal() == wall;
	}

	[[nodiscard]] bool test_expired_entities_are_skipped_before_destroy(){
		component_manager manager{};
		manager.update_update_delta(0.016f);
		system::physics_system physics_sys{};
		const auto shape = physics::make_box_collision_shape({0.5f, 0.5f});

		const auto active = spawn(manager, {0.f, 0.f}, shape, physics_body::make_static());
		const auto expired = spawn(manager, {0.f, 0.f}, shape, physics_body::make_static());
		manager.commit();
		manager.destroy(expired);

		physics_sys.step(manager);
		if(std::ranges::any_of(physics_sys.contact_events(), [&](const physics_contact_event& event){
			return event.key == physics_contact_key::ordered(active, expired);
		})){
			return false;
		}

		bool found_active{};
		bool found_expired{};
		physics_sys.spatial_query(math::frect{{0.f, 0.f}, 4.f}, [&](const physics_query_result& obj){
			if(obj.id() == active){
				found_active = true;
			}
			if(obj.id() == expired){
				found_expired = true;
			}
		});

		return found_active && !found_expired;
	}

	[[nodiscard]] bool test_contact_end_event_survives_deferred_destroy(){
		component_manager manager{};
		manager.update_update_delta(0.016f);
		system::physics_system physics_sys{};
		const auto shape = physics::make_box_collision_shape({0.5f, 0.5f});

		const auto dynamic = spawn(manager, {-0.25f, 0.f}, shape, physics_body::make_dynamic(1.f));
		const auto target = spawn(manager, {0.25f, 0.f}, shape, physics_body::make_static());
		manager.commit();

		physics_sys.step(manager);
		if(std::ranges::none_of(physics_sys.contact_events(), [&](const physics_contact_event& event){
			return event.phase == physics_contact_phase::begin
				&& event.key == physics_contact_key::ordered(dynamic, target);
		})){
			return false;
		}

		manager.destroy(target);
		manager.commit_destroy();
		physics_sys.step(manager);

		bool saw_end{};
		for(const auto& event : physics_sys.contact_events()){
			if(event.phase != physics_contact_phase::end
				|| event.key != physics_contact_key::ordered(dynamic, target)){
				continue;
			}
			saw_end = true;
			if(!event.subject || !event.object){
				return false;
			}
			(void)event.subject.is_expired();
			(void)event.object.is_expired();
		}

		bool found_target{};
		physics_sys.spatial_query(math::frect{{0.25f, 0.f}, 4.f}, [&](const physics_query_result& obj){
			if(obj.id() == target){
				found_target = true;
			}
		});

		return saw_end && !found_target;
	}

	[[nodiscard]] bool test_spatial_query_survives_component_relocation(){
		component_manager manager{};
		manager.update_update_delta(0.016f);
		system::physics_system physics_sys{};
		const auto shape = physics::make_box_collision_shape({0.5f, 0.5f});

		const auto original = spawn(manager, {10.f, 0.f}, shape, physics_body::make_static());
		manager.commit();
		physics_sys.step(manager);

		for(int i = 0; i != 256; ++i){
			(void)spawn(
				manager,
				{100.f + static_cast<float>(i) * 2.f, 0.f},
				shape,
				physics_body::make_static());
		}
		manager.commit();

		bool found_original{};
		physics_sys.spatial_query(math::frect{{10.f, 0.f}, 4.f}, [&](const physics_query_result& obj){
			if(obj.id() == original && near(obj.position().x, 10.f, 0.001f)){
				found_original = true;
			}
		});

		return found_original;
	}

	[[nodiscard]] bool test_low_speed_large_face_contact_stays_stable(){
		component_manager manager{};
		system::physics_system physics_sys{};
		constexpr float dt = 1.f / 60.f;
		const auto shape = physics::make_box_collision_shape({1.f, 4.f});
		const auto dynamic_body = make_stable_dynamic_body(2.f, 8.f);

		const auto dynamic = spawn(manager, {-1.95f, 0.f}, shape, dynamic_body, {}, {0.2f, 0.f});
		const auto wall = spawn(manager, {0.f, 0.f}, shape, physics_body::make_static());
		const auto key = physics_contact_key::ordered(dynamic, wall);
		manager.commit();

		unsigned begin_count{};
		unsigned stay_count{};
		unsigned end_count{};
		float max_abs_angular_velocity{};
		float max_abs_y{};
		for(unsigned i = 0; i != 180; ++i){
			manager.update_update_delta(dt);
			physics_sys.step(manager);
			for(const auto& event : physics_sys.contact_events()){
				if(event.key != key){
					continue;
				}
				switch(event.phase){
				case physics_contact_phase::begin:
					++begin_count;
					break;
				case physics_contact_phase::stay:
					++stay_count;
					break;
				case physics_contact_phase::end:
					++end_count;
					break;
				}
			}
			const auto& motion = dynamic.at<mech_motion>();
			max_abs_angular_velocity = std::max(max_abs_angular_velocity, std::abs(static_cast<float>(motion.vel.rot)));
			max_abs_y = std::max(max_abs_y, std::abs(motion.trans.vec.y));
		}

		const auto& final_motion = dynamic.at<mech_motion>();
		return begin_count == 1
			&& stay_count > 120
			&& end_count == 0
			&& max_abs_angular_velocity < 0.05f
			&& max_abs_y < 0.02f
			&& std::abs(final_motion.vel.vec.x) < 0.05f;
	}

	[[nodiscard]] bool test_low_speed_stack_settles_without_contact_flicker(){
		component_manager manager{};
		system::physics_system physics_sys{};
		constexpr float dt = 1.f / 60.f;
		const auto box_shape = physics::make_box_collision_shape({0.5f, 0.5f});
		const auto floor_shape = physics::make_box_collision_shape({3.f, 0.5f});
		const auto body = make_stable_dynamic_body(1.f, 1.f);

		const auto floor = spawn(manager, {0.f, -0.55f}, floor_shape, physics_body::make_static());
		const auto first = spawn(manager, {0.f, 0.4f}, box_shape, body, {}, {0.f, -0.2f});
		const auto second = spawn(manager, {0.f, 1.35f}, box_shape, body, {}, {0.f, -0.2f});
		const auto third = spawn(manager, {0.f, 2.3f}, box_shape, body, {}, {0.f, -0.2f});
		const std::array keys{
			physics_contact_key::ordered(floor, first),
			physics_contact_key::ordered(first, second),
			physics_contact_key::ordered(second, third)
		};
		manager.commit();

		unsigned end_count{};
		for(unsigned i = 0; i != 300; ++i){
			manager.update_update_delta(dt);
			physics_sys.step(manager);
			for(const auto& event : physics_sys.contact_events()){
				if(event.phase != physics_contact_phase::end){
					continue;
				}
				if(std::ranges::contains(keys, event.key)){
					++end_count;
				}
			}
		}

		const auto stable = [](const entity_id id) noexcept{
			const auto& motion = id.at<mech_motion>();
			return std::isfinite(motion.trans.vec.x)
				&& std::isfinite(motion.trans.vec.y)
				&& std::abs(motion.trans.vec.x) < 0.1f
				&& motion.vel.vec.length() < 0.05f
				&& std::abs(static_cast<float>(motion.vel.rot)) < 0.05f;
		};

		const bool first_stable = stable(first);
		const bool second_stable = stable(second);
		const bool third_stable = stable(third);
		return end_count <= 1
			&& first_stable
			&& second_stable
			&& third_stable;
	}

	[[nodiscard]] bool test_resting_contact_friction_uses_cached_normal_impulse(){
		component_manager manager{};
		system::physics_system physics_sys{};
		constexpr float dt = 1.f / 60.f;
		const auto box_shape = physics::make_box_collision_shape({0.5f, 0.5f});
		const auto floor_shape = physics::make_box_collision_shape({3.f, 0.5f});
		auto body = make_stable_dynamic_body(1.f, 1.f, 1.f);
		body.body.linear_drag = 0.f;
		body.body.angular_drag = 0.f;

		const auto dynamic = spawn(manager, {0.f, 0.4f}, box_shape, body, {}, {1.f, 0.f});
		(void)spawn(manager, {0.f, -0.55f}, floor_shape, physics_body::make_static());
		manager.commit();

		for(unsigned i = 0; i != 90; ++i){
			dynamic.at<mech_motion>().accel.vec = {0.f, -2.f};
			manager.update_update_delta(dt);
			physics_sys.step(manager);
		}

		const auto& motion = dynamic.at<mech_motion>();
		return std::abs(motion.vel.vec.x) < 0.5f
			&& std::abs(motion.vel.vec.y) < 0.1f
			&& std::abs(static_cast<float>(motion.vel.rot)) < 0.2f;
	}

	[[nodiscard]] bool test_projectile_system_sensor_hit_damage_and_expire(){
		component_manager manager{};
		manager.update_update_delta(1.f);
		system::physics_system physics_sys{};
		system::projectile_system projectile_sys{};
		const auto projectile_shape = physics::make_circle_collision_shape(0.25f);
		const auto target_shape = physics::make_box_collision_shape({0.5f, 1.f});

		const auto projectile_entity = spawn<projectile_desc>(
			manager,
			{-5.f, 0.f},
			projectile_shape,
			physics_body::make_kinematic(),
			{},
			{10.f, 0.f},
			true,
			physics::ccd_mode::linear_sweep);
		const auto target = spawn<damageable_desc>(
			manager,
			{0.f, 0.f},
			target_shape,
			physics_body::make_static());
		manager.commit();

		auto& projectile_component = projectile_entity.at<projectile_state>();
		projectile_component.remaining_lifetime = 10.f;
		projectile_component.set_damage({.material_damage = {.direct = 10.f}});
		target.at<hit_point>().reset_to(10.f);

		projectile_sys.pre_step(manager);
		physics_sys.step(manager);
		projectile_sys.resolve_hits(manager, physics_sys);

		return target.at<hit_point>().is_killed()
			&& projectile_entity.is_expired();
	}

	[[nodiscard]] bool test_projectile_system_faction_filter(){
		component_manager manager{};
		manager.update_update_delta(1.f);
		system::physics_system physics_sys{};
		system::projectile_system projectile_sys{};
		const auto projectile_shape = physics::make_circle_collision_shape(0.25f);
		const auto target_shape = physics::make_box_collision_shape({0.5f, 1.f});

		const auto projectile_entity = spawn<projectile_desc>(
			manager,
			{-5.f, 0.f},
			projectile_shape,
			physics_body::make_kinematic(),
			{},
			{10.f, 0.f},
			true,
			physics::ccd_mode::linear_sweep);
		const auto target = spawn<damageable_desc>(
			manager,
			{0.f, 0.f},
			target_shape,
			physics_body::make_static());
		manager.commit();

		auto& projectile_component = projectile_entity.at<projectile_state>();
		projectile_component.remaining_lifetime = 10.f;
		projectile_component.faction_id = 7;
		projectile_component.set_damage({.material_damage = {.direct = 10.f}});
		target.at<hit_point>().reset_to(10.f);
		target.at<faction_data>().id = 7;

		projectile_sys.pre_step(manager);
		physics_sys.step(manager);
		projectile_sys.resolve_hits(manager, physics_sys);

		return near(target.at<hit_point>().current, 10.f, 0.001f)
			&& !projectile_entity.is_expired();
	}

	[[nodiscard]] bool test_chamber_projectile_tile_piercing_damage_order(){
		chamber::chamber_manifold chamber{{6, 1}};
		auto& building = chamber.place_building({.src = {1, 0}, .extent = {4, 1}}, 40.f, true);
		auto shape = physics::make_circle_collision_shape(0.25f).to_record();
		damage_group damage{.material_damage = {.direct = 30.f}};
		physics_contact_endpoint_snapshot projectile_endpoint{};
		projectile_endpoint.previous_shape = {{0.f, 0.5f}, 0.f};
		projectile_endpoint.current_shape = {{6.f, 0.5f}, 0.f};
		physics_contact_endpoint_snapshot target_endpoint{};
		chamber::projectile_hit_context context{
			{},
			{},
			shape,
			projectile_endpoint,
			target_endpoint,
			{},
			{1.f, 0.f},
			damage
		};

		const auto hit = chamber::apply_projectile_hit(chamber, context);
		const auto settled = chamber.settle_pending_damage();

		return hit.hit_any_tile
			&& hit.damage_exhausted
			&& near(hit.actual_damage, 30.f, 0.001f)
			&& building.damage_events.size() == 2
			&& building.damage_events[0].tile_coord == math::point2{1, 0}
			&& near(building.damage_events[0].actual_damage, 20.f, 0.001f)
			&& building.damage_events[1].tile_coord == math::point2{2, 0}
			&& near(building.damage_events[1].actual_damage, 10.f, 0.001f)
			&& near(building.hit_points.current, 10.f, 0.001f)
			&& near(chamber.structural_hit_points.current, chamber.structural_hit_points.max - 30.f, 0.001f)
			&& near(settled.actual_damage, 30.f, 0.001f);
	}
}

int main(){
	if(!test_channel_group_and_sensor_filter()){
		std::println(stderr, "test_channel_group_and_sensor_filter failed");
		return EXIT_FAILURE;
	}
	if(!test_impulses()){
		std::println(stderr, "test_impulses failed");
		return EXIT_FAILURE;
	}
	if(!test_ccd_projectile_event_and_query()){
		std::println(stderr, "test_ccd_projectile_event_and_query failed");
		return EXIT_FAILURE;
	}
	if(!test_expired_entities_are_skipped_before_destroy()){
		std::println(stderr, "test_expired_entities_are_skipped_before_destroy failed");
		return EXIT_FAILURE;
	}
	if(!test_contact_end_event_survives_deferred_destroy()){
		std::println(stderr, "test_contact_end_event_survives_deferred_destroy failed");
		return EXIT_FAILURE;
	}
	if(!test_spatial_query_survives_component_relocation()){
		std::println(stderr, "test_spatial_query_survives_component_relocation failed");
		return EXIT_FAILURE;
	}
	if(!test_low_speed_large_face_contact_stays_stable()){
		std::println(stderr, "test_low_speed_large_face_contact_stays_stable failed");
		return EXIT_FAILURE;
	}
	if(!test_low_speed_stack_settles_without_contact_flicker()){
		std::println(stderr, "test_low_speed_stack_settles_without_contact_flicker failed");
		return EXIT_FAILURE;
	}
	if(!test_resting_contact_friction_uses_cached_normal_impulse()){
		std::println(stderr, "test_resting_contact_friction_uses_cached_normal_impulse failed");
		return EXIT_FAILURE;
	}
	if(!test_projectile_system_sensor_hit_damage_and_expire()){
		std::println(stderr, "test_projectile_system_sensor_hit_damage_and_expire failed");
		return EXIT_FAILURE;
	}
	if(!test_projectile_system_faction_filter()){
		std::println(stderr, "test_projectile_system_faction_filter failed");
		return EXIT_FAILURE;
	}
	if(!test_chamber_projectile_tile_piercing_damage_order()){
		std::println(stderr, "test_chamber_projectile_tile_piercing_damage_order failed");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
