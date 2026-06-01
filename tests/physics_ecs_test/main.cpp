#include <gtest/gtest.h>

import std;
import mo_yanxi.game.ecs.component.chamber.damage_grid;
import mo_yanxi.game.ecs.component.damage;
import mo_yanxi.game.ecs.component.faction;
import mo_yanxi.game.ecs.component.manage;
import mo_yanxi.game.ecs.component.physics;
import mo_yanxi.game.ecs.component.projectile.manifold;
import mo_yanxi.game.ecs.system.chamber;
import mo_yanxi.game.ecs.system.physics;
import mo_yanxi.game.ecs.system.projectile;
import mo_yanxi.game.ecs.system.targeting;
import mo_yanxi.game.aiming;

namespace{
	using namespace mo_yanxi::game;
	using namespace mo_yanxi::game::ecs;
	namespace math = mo_yanxi::math;

	using body_desc = std::tuple<chunk_meta, mech_motion, collider, physics_body>;
	using projectile_desc = std::tuple<chunk_meta, mech_motion, collider, physics_body, projectile_manifold>;
	using damageable_desc = std::tuple<chunk_meta, mech_motion, collider, physics_body, hit_point, faction_data>;
	using chamber_desc = std::tuple<chunk_meta, chamber::chamber_manifold>;
	using targetable_desc = std::tuple<chunk_meta, mech_motion, collider, physics_body, faction_data, targetable_profile>;
	using targeting_sensor_desc = std::tuple<
		chunk_meta,
		mech_motion,
		collider,
		physics_body,
		faction_data,
		targeting_sensor,
		target_memory>;
	using targeting_chamber_desc = std::tuple<
		chunk_meta,
		mech_motion,
		collider,
		physics_body,
		faction_data,
		targetable_profile,
		chamber::chamber_manifold>;

	struct counting_building{
		int base_charge{};
		int updates{};
	};

	struct counting_building_system{
		void update(chamber::chamber_update_context&, chamber::building_common& common, counting_building& building) const{
			++building.updates;
			building.base_charge += static_cast<int>(common.tile_status_count);
		}
	};

	[[nodiscard]] bool near(const float lhs, const float rhs, const float margin = 0.05f) noexcept{
		return std::abs(lhs - rhs) <= margin;
	}

	[[nodiscard]] constexpr math::vec2 chamber_tile_point(const float x, const float y) noexcept{
		return {
			chamber::tiles_to_world_units(x),
			chamber::tiles_to_world_units(y)
		};
	}

	[[nodiscard]] constexpr math::vec2 chamber_tile_half_extent(const float x, const float y) noexcept{
		return {
			chamber::tiles_to_world_units(x),
			chamber::tiles_to_world_units(y)
		};
	}

	[[nodiscard]] chamber::projectile_hit_context make_chamber_projectile_hit_context(
		const physics::collision_shape_record& shape,
		damage_group& damage,
		const math::vec2 previous,
		const math::vec2 current){
		physics_contact_endpoint_snapshot projectile_endpoint{};
		projectile_endpoint.previous_shape = {chamber_tile_point(previous.x, previous.y), 0.f};
		projectile_endpoint.current_shape = {chamber_tile_point(current.x, current.y), 0.f};
		physics_contact_endpoint_snapshot target_endpoint{};
		return {
			{},
			{},
			shape,
			projectile_endpoint,
			target_endpoint,
			{},
			{1.f, 0.f},
			damage
		};
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

	[[nodiscard]] bool test_targeting_system_entity_sensor_filters_and_limits(){
		component_manager manager{};
		manager.update_update_delta(1.f);
		system::physics_system physics_sys{};
		system::targeting_system targeting_sys{};
		const auto shape = physics::make_box_collision_shape({0.5f, 0.5f});

		const auto sensor = spawn<targeting_sensor_desc>(
			manager,
			{0.f, 0.f},
			shape,
			physics_body::make_static());
		const auto ally = spawn<damageable_desc>(
			manager,
			{1.f, 0.f},
			shape,
			physics_body::make_static());
		const auto channel_hidden = spawn<targetable_desc>(
			manager,
			{2.f, 0.f},
			shape,
			physics_body::make_static());
		const auto stealth_hidden = spawn<targetable_desc>(
			manager,
			{3.f, 0.f},
			shape,
			physics_body::make_static());
		const auto first = spawn<damageable_desc>(
			manager,
			{10.f, 0.f},
			shape,
			physics_body::make_static());
		const auto second = spawn<damageable_desc>(
			manager,
			{20.f, 0.f},
			shape,
			physics_body::make_static());
		const auto third = spawn<damageable_desc>(
			manager,
			{30.f, 0.f},
			shape,
			physics_body::make_static());
		manager.commit();

		sensor.at<faction_data>().id = 1u;
		sensor.at<targeting_sensor>() = {
			.range = 100.f,
			.max_targets = 2u,
			.detect_channels = 0b0001u,
			.detection_strength = 1.f
		};
		ally.at<faction_data>().id = 1u;
		channel_hidden.at<faction_data>().id = 2u;
		channel_hidden.at<targetable_profile>() = {
			.detectable_channels = 0b0010u
		};
		stealth_hidden.at<faction_data>().id = 2u;
		stealth_hidden.at<targetable_profile>() = {
			.detectable_channels = 0b0001u,
			.signature = 0.f,
			.stealth_strength = 2.f
		};
		first.at<faction_data>().id = 2u;
		second.at<faction_data>().id = 2u;
		third.at<faction_data>().id = 2u;

		physics_sys.step(manager);
		targeting_sys.step(manager, physics_sys);

		const target_memory& memory = sensor.at<target_memory>();
		const auto stats = targeting_sys.last_statistics();
		return stats.unique_scan_count == 1u
			&& stats.entity_sensor_count == 1u
			&& stats.selected_target_count == 2u
			&& memory.targets.size() == 2u
			&& memory.targets[0].target.entity == first
			&& memory.targets[1].target.entity == second;
	}

	[[nodiscard]] bool test_targeting_system_groups_chamber_radar_scans(){
		component_manager manager{};
		manager.update_update_delta(1.f);
		system::physics_system physics_sys{};
		system::targeting_system targeting_sys{};
		const auto target_shape = physics::make_box_collision_shape({5.f, 5.f});

		const auto target = spawn<damageable_desc>(
			manager,
			chamber_tile_point(6.f, 0.5f),
			target_shape,
			physics_body::make_static());

		tuple_to_comp_t<targeting_chamber_desc> components{};
		components.template get<mech_motion>().trans.vec = {};
		components.template get<physics_body>() = physics_body::make_kinematic();
		components.template get<faction_data>().id = 1u;
		chamber::chamber_manifold& chamber_component =
			components.template get<chamber::chamber_manifold>();
		chamber_component = chamber::chamber_manifold{{4, 1}};
		const auto structural = chamber_component.execute(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::structural_joint,
			.region = {.src = {0, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.structural_support_radius = 3
		});
		const auto first_radar = chamber_component.execute(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::radar,
			.region = {.src = {1, 0}, .extent = {1, 1}},
			.hit_points = 50.f,
			.radar = {.sensor = {.range = chamber::tiles_to_world_units(10.f)}}
		});
		const auto second_radar = chamber_component.execute(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::radar,
			.region = {.src = {2, 0}, .extent = {1, 1}},
			.hit_points = 50.f,
			.radar = {.sensor = {.range = chamber::tiles_to_world_units(10.f)}}
		});
		if(!structural.applied() || !first_radar.applied() || !second_radar.applied()){
			return false;
		}
		chamber::synchronize_chamber_collider(
			components.template get<collider>(),
			chamber_component);
		chamber_component.mark_collider_synchronized();
		const auto chamber_entity = manager.spawn<targeting_chamber_desc>(std::move(components));
		manager.commit();
		target.at<faction_data>().id = 2u;

		physics_sys.step(manager);
		targeting_sys.step(manager, physics_sys);

		const auto* chamber_state = chamber_entity.try_get<chamber::chamber_manifold>();
		if(chamber_state == nullptr){
			return false;
		}
		const auto* first_state = chamber_state->buildings().try_get<chamber::radar_building>(first_radar.target);
		const auto* second_state = chamber_state->buildings().try_get<chamber::radar_building>(second_radar.target);
		const auto stats = targeting_sys.last_statistics();
		return stats.unique_scan_count == 1u
			&& stats.chamber_sensor_count == 2u
			&& first_state != nullptr
			&& second_state != nullptr
			&& !first_state->memory.empty()
			&& !second_state->memory.empty()
			&& first_state->memory.primary()->target.entity == target
			&& second_state->memory.primary()->target.entity == target;
	}

	[[nodiscard]] bool test_targeting_system_chamber_building_targets_expire(){
		component_manager manager{};
		manager.update_update_delta(1.f);
		system::physics_system physics_sys{};
		system::targeting_system targeting_sys{};
		const auto shape = physics::make_box_collision_shape({0.5f, 0.5f});

		const auto sensor = spawn<targeting_sensor_desc>(
			manager,
			{0.f, 0.f},
			shape,
			physics_body::make_static());

		tuple_to_comp_t<targeting_chamber_desc> components{};
		components.template get<mech_motion>().trans.vec = {100.f, 0.f};
		components.template get<mech_motion>().trans.rot = math::pi;
		components.template get<physics_body>() = physics_body::make_kinematic();
		components.template get<faction_data>().id = 2u;
		components.template get<targetable_profile>().priority = -200.f;
		chamber::chamber_manifold& chamber_component =
			components.template get<chamber::chamber_manifold>();
		chamber_component = chamber::chamber_manifold{{1, 1}};
		const auto building = chamber_component.execute(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::structural_joint,
			.region = {.src = {0, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.structural_support_radius = 1
		});
		if(!building.applied()){
			return false;
		}
		chamber::synchronize_chamber_collider(
			components.template get<collider>(),
			chamber_component);
		chamber_component.mark_collider_synchronized();
		const auto target_chamber = manager.spawn<targeting_chamber_desc>(std::move(components));
		manager.commit();

		sensor.at<faction_data>().id = 1u;
		sensor.at<targeting_sensor>() = {
			.range = 200.f,
			.max_targets = 1u
		};

		physics_sys.step(manager);
		targeting_sys.step(manager, physics_sys);

		const target_memory& first_memory = sensor.at<target_memory>();
		if(first_memory.targets.size() != 1u
			|| first_memory.targets.front().target.kind != target_kind::chamber_building
			|| first_memory.targets.front().target.entity != target_chamber
			|| first_memory.targets.front().target.chamber_building != building.target){
			return false;
		}

		auto& target_chamber_state = target_chamber.at<chamber::chamber_manifold>();
		const auto erased = target_chamber_state.execute(chamber::erase_building_command{
			.target = building.target
		});
		if(!erased.applied()){
			return false;
		}
		auto* target_collider = target_chamber.try_get<collider>();
		if(target_collider == nullptr){
			return false;
		}
		chamber::synchronize_chamber_collider(*target_collider, target_chamber_state);
		target_chamber_state.mark_collider_synchronized();

		physics_sys.step(manager);
		targeting_sys.step(manager, physics_sys);

		for(const target_snapshot& target : sensor.at<target_memory>().targets){
			if(target.target.kind == target_kind::chamber_building
				&& target.target.entity == target_chamber
				&& target.target.chamber_building == building.target){
				return false;
			}
		}
		return true;
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
		const auto placed = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {1, 0}, .extent = {4, 1}},
			.hit_points = 40.f,
			.structural = true
		});
		if(!placed.applied()){
			return false;
		}
		const auto* building = chamber.try_building(placed.target);
		if(building == nullptr || chamber.building_handle_at({1, 0}) != placed.target){
			return false;
		}
		auto shape = physics::make_circle_collision_shape(chamber::tiles_to_world_units(0.25f)).to_record();
		damage_group damage{.material_damage = {.direct = 30.f}};
		chamber::projectile_hit_context context =
			make_chamber_projectile_hit_context(shape, damage, {0.f, 0.5f}, {6.f, 0.5f});

		const auto hit = chamber.execute(chamber::projectile_hit_command{context});
		const auto settled = chamber.execute(chamber::settle_pending_damage_command{});

		return hit.hit_any_tile
			&& hit.damage_exhausted
			&& near(hit.actual_damage, 30.f, 0.001f)
			&& building->damage_events.size() == 2
			&& building->damage_events[0].tile_coord == math::point2{1, 0}
			&& near(building->damage_events[0].actual_damage, 20.f, 0.001f)
			&& building->damage_events[1].tile_coord == math::point2{2, 0}
			&& near(building->damage_events[1].actual_damage, 10.f, 0.001f)
			&& near(damage.material_damage.direct, 0.f, 0.001f)
			&& near(building->hit_points.current, 10.f, 0.001f)
			&& near(chamber.structural_hit_points().current, chamber.structural_hit_points().max - 30.f, 0.001f)
			&& near(settled.actual_damage, 30.f, 0.001f);
	}

	[[nodiscard]] bool test_chamber_large_swept_aabb_prunes_without_missing_tiles(){
		chamber::chamber_manifold chamber{{40, 40}};
		const auto first = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {5, 20}, .extent = {1, 1}},
			.hit_points = 100.f,
			.structural = true,
			.structural_support_radius = 40
		});
		const auto second = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {30, 20}, .extent = {1, 1}},
			.hit_points = 100.f,
			.structural = true,
			.structural_support_radius = 40
		});
		if(!first.applied() || !second.applied()){
			return false;
		}

		const auto* first_building = chamber.try_building(first.target);
		const auto* second_building = chamber.try_building(second.target);
		if(first_building == nullptr || second_building == nullptr){
			return false;
		}

		auto shape = physics::make_box_collision_shape(chamber_tile_half_extent(0.25f, 20.f)).to_record();
		damage_group damage{.material_damage = {.direct = 400.f}};
		chamber::projectile_hit_context context =
			make_chamber_projectile_hit_context(shape, damage, {-5.f, 20.5f}, {35.f, 20.5f});

		const auto hit = chamber.execute(chamber::projectile_hit_command{context});
		const auto settled = chamber.execute(chamber::settle_pending_damage_command{});

		return hit.hit_any_tile
			&& hit.damage_exhausted
			&& near(hit.actual_damage, 400.f, 0.001f)
			&& first_building->damage_events.size() == 1
			&& second_building->damage_events.size() == 1
			&& first_building->damage_events[0].tile_coord == math::point2{5, 20}
			&& second_building->damage_events[0].tile_coord == math::point2{30, 20}
			&& near(first_building->damage_events[0].actual_damage, 200.f, 0.001f)
			&& near(second_building->damage_events[0].actual_damage, 200.f, 0.001f)
			&& near(settled.actual_damage, 200.f, 0.001f)
			&& near(chamber.structural_hit_points().current, chamber.structural_hit_points().max - 200.f, 0.001f);
	}

	[[nodiscard]] bool test_chamber_projectile_hit_workspace_reuse(){
		chamber::chamber_manifold chamber{{6, 1}};
		const auto placed = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {1, 0}, .extent = {4, 1}},
			.hit_points = 80.f,
			.structural = true
		});
		if(!placed.applied()){
			return false;
		}
		const auto* building = chamber.try_building(placed.target);
		if(building == nullptr){
			return false;
		}

		auto shape = physics::make_circle_collision_shape(chamber::tiles_to_world_units(0.25f)).to_record();
		damage_group first_damage{.material_damage = {.direct = 40.f}};
		chamber::projectile_hit_context first_context =
			make_chamber_projectile_hit_context(shape, first_damage, {0.f, 0.5f}, {2.f, 0.5f});
		const auto first_hit = chamber.execute(chamber::projectile_hit_command{first_context});
		const auto first_settled = chamber.execute(chamber::settle_pending_damage_command{});

		damage_group second_damage{.material_damage = {.direct = 40.f}};
		chamber::projectile_hit_context second_context =
			make_chamber_projectile_hit_context(shape, second_damage, {5.5f, 0.5f}, {3.5f, 0.5f});
		const auto second_hit = chamber.execute(chamber::projectile_hit_command{second_context});
		const auto second_settled = chamber.execute(chamber::settle_pending_damage_command{});

		return first_hit.hit_any_tile
			&& second_hit.hit_any_tile
			&& near(first_settled.actual_damage, 40.f, 0.001f)
			&& near(second_settled.actual_damage, 40.f, 0.001f)
			&& building->damage_events.size() == 2
			&& building->damage_events[0].tile_coord == math::point2{1, 0}
			&& building->damage_events[1].tile_coord == math::point2{4, 0};
	}

	[[nodiscard]] bool test_chamber_collider_dirty_tracks_killed_buildings(){
		chamber::chamber_manifold chamber{{3, 1}};
		const auto first = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {0, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.structural = true,
			.structural_support_radius = 3
		});
		const auto second = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {2, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.structural = true,
			.structural_support_radius = 3
		});
		if(!first.applied() || !second.applied() || !chamber.collider_needs_synchronization()){
			return false;
		}

		collider chamber_collider{};
		chamber::synchronize_chamber_collider(chamber_collider, chamber);
		chamber.mark_collider_synchronized();
		if(chamber.collider_needs_synchronization() || chamber_collider.shape.part_count() != 2){
			return false;
		}

		auto shape = physics::make_circle_collision_shape(chamber::tiles_to_world_units(0.25f)).to_record();
		damage_group nonfatal_damage{.material_damage = {.direct = 50.f}};
		chamber::projectile_hit_context nonfatal_context =
			make_chamber_projectile_hit_context(shape, nonfatal_damage, {-1.f, 0.5f}, {1.f, 0.5f});
		const auto nonfatal_hit = chamber.execute(chamber::projectile_hit_command{nonfatal_context});
		const auto nonfatal_settled = chamber.execute(chamber::settle_pending_damage_command{});
		if(!nonfatal_hit.hit_any_tile
			|| !near(nonfatal_settled.actual_damage, 50.f, 0.001f)
			|| chamber.collider_needs_synchronization()){
			return false;
		}

		damage_group fatal_damage{.material_damage = {.direct = 500.f}};
		chamber::projectile_hit_context fatal_context =
			make_chamber_projectile_hit_context(shape, fatal_damage, {-1.f, 0.5f}, {1.f, 0.5f});
		const auto fatal_hit = chamber.execute(chamber::projectile_hit_command{fatal_context});
		const auto fatal_settled = chamber.execute(chamber::settle_pending_damage_command{});
		if(!fatal_hit.hit_any_tile
			|| !near(fatal_settled.actual_damage, 50.f, 0.001f)
			|| !chamber.collider_needs_synchronization()){
			return false;
		}

		chamber::synchronize_chamber_collider(chamber_collider, chamber);
		chamber.mark_collider_synchronized();
		return !chamber.collider_needs_synchronization()
			&& chamber_collider.shape.part_count() == 1;
	}

	[[nodiscard]] bool test_chamber_pending_damage_settles_only_hit_buildings(){
		chamber::chamber_manifold chamber{{4, 1}};
		const auto hit_target = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {0, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.structural = true,
			.structural_support_radius = 4
		});
		const auto untouched = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {3, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.structural = true,
			.structural_support_radius = 4
		});
		if(!hit_target.applied() || !untouched.applied()){
			return false;
		}

		const auto* hit_building = chamber.try_building(hit_target.target);
		const auto* untouched_building = chamber.try_building(untouched.target);
		if(hit_building == nullptr || untouched_building == nullptr){
			return false;
		}

		auto shape = physics::make_circle_collision_shape(chamber::tiles_to_world_units(0.25f)).to_record();
		damage_group damage{.material_damage = {.direct = 50.f}};
		chamber::projectile_hit_context context =
			make_chamber_projectile_hit_context(shape, damage, {-1.f, 0.5f}, {1.f, 0.5f});
		const auto hit = chamber.execute(chamber::projectile_hit_command{context});
		const auto settled = chamber.execute(chamber::settle_pending_damage_command{});

		return hit.hit_any_tile
			&& near(hit.actual_damage, 50.f, 0.001f)
			&& near(settled.actual_damage, 50.f, 0.001f)
			&& near(hit_building->hit_points.current, 50.f, 0.001f)
			&& near(untouched_building->hit_points.current, 100.f, 0.001f)
			&& near(chamber.structural_hit_points().current, chamber.structural_hit_points().max - 50.f, 0.001f);
	}

	[[nodiscard]] bool test_chamber_building_commands_and_handle_expiration(){
		chamber::chamber_manifold chamber{{4, 2}};
		const auto blocked_tile = chamber.execute(chamber::set_tile_placeable_command{
			.region = {.src = {2, 1}, .extent = {1, 1}},
			.placeable = false
		});
		if(!blocked_tile.applied() || chamber.tile_state_at({2, 1}).placeable){
			return false;
		}
		const auto blocked_place = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {2, 1}, .extent = {1, 1}},
			.hit_points = 10.f
		});
		if(blocked_place.status != chamber::chamber_command_status::not_placeable){
			return false;
		}
		const auto reopened_tile = chamber.execute(chamber::set_tile_placeable_command{
			.region = {.src = {2, 1}, .extent = {1, 1}},
			.placeable = true
		});
		if(!reopened_tile.applied() || !chamber.tile_state_at({2, 1}).placeable){
			return false;
		}
		chamber.post_command(chamber::set_tile_corridor_command{
			.region = {.src = {2, 1}, .extent = {1, 1}},
			.corridor = true
		});
		chamber.post_command(chamber::place_basic_building_command{
			.region = {.src = {0, 0}, .extent = {2, 1}},
			.hit_points = 20.f,
			.structural = true
		});
		const auto results = chamber.execute_pending_commands();
		if(results.size() != 2 || !results[0].applied() || !results[1].applied() || !chamber.tile_state_at({2, 1}).corridor){
			return false;
		}

		const object_handle handle = results[1].target;
		const auto* first_building = chamber.try_building(handle);
		if(chamber.building_at({0, 0}) == nullptr || chamber.building_handle_at({1, 0}) != handle){
			return false;
		}
		if(first_building == nullptr){
			return false;
		}
		const std::uint32_t first_tile_status_offset = first_building->tile_status_offset;

		const auto occupied = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {1, 0}, .extent = {1, 1}},
			.hit_points = 10.f
		});
		if(occupied.status != chamber::chamber_command_status::occupied){
			return false;
		}

		const auto erased = chamber.execute(chamber::erase_building_command{.target = handle});
		if(!erased.applied()){
			return false;
		}
		if(chamber.try_building(handle) != nullptr
			|| chamber.building_at({0, 0}) != nullptr
			|| chamber.building_at({1, 0}) != nullptr
			|| chamber.execute(chamber::erase_building_command{.target = handle}).status
				!= chamber::chamber_command_status::expired_target){
			return false;
		}

		const auto replacement = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {3, 1}, .extent = {1, 1}},
			.hit_points = 5.f,
			.structural = true
		});
		const auto* replacement_building = chamber.try_building(replacement.target);
		return replacement.applied()
			&& replacement_building != nullptr
			&& replacement_building->tile_status_offset == first_tile_status_offset
			&& near(chamber.tile_status_at(*replacement_building, {3, 1}).hit_point, 10.f, 0.001f);
	}

	[[nodiscard]] bool test_chamber_structural_support_and_cascade(){
		chamber::chamber_manifold chamber{{5, 1}};
		const auto unsupported = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {4, 0}, .extent = {1, 1}},
			.hit_points = 10.f
		});
		if(unsupported.status != chamber::chamber_command_status::unsupported_structure){
			return false;
		}

		const auto structural = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {0, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.structural = true,
			.structural_support_radius = 2
		});
		const auto supported = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {2, 0}, .extent = {1, 1}},
			.hit_points = 10.f
		});
		if(!structural.applied() || !supported.applied()){
			return false;
		}

		const auto outside_support = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {4, 0}, .extent = {1, 1}},
			.hit_points = 10.f
		});
		if(outside_support.status != chamber::chamber_command_status::unsupported_structure){
			return false;
		}

		const auto erased = chamber.execute(chamber::erase_building_command{.target = structural.target});
		return erased.applied()
			&& erased.cascaded_targets.size() == 1
			&& erased.cascaded_targets.front() == supported.target
			&& chamber.try_building(structural.target) == nullptr
			&& chamber.try_building(supported.target) == nullptr
			&& chamber.building_at({2, 0}) == nullptr;
	}

	[[nodiscard]] bool test_chamber_typed_building_channel_updates(){
		chamber::chamber_manifold chamber{{3, 1}};
		chamber.register_building_type<counting_building>(counting_building_system{});

		const auto placed = chamber.execute(chamber::place_building_command<counting_building>{
			.region = {.src = {0, 0}, .extent = {2, 1}},
			.hit_points = 20.f,
			.structural = true,
			.building = {.base_charge = 5}
		});
		if(!placed.applied()){
			return false;
		}

		chamber.execute(chamber::update_buildings_command{.delta_seconds = 1.f});
		const auto* building = chamber.buildings().try_get<counting_building>(placed.target);
		const auto* common = chamber.try_building(placed.target);
		return building != nullptr
			&& common != nullptr
			&& building->updates == 1
			&& building->base_charge == 7
			&& common->handle == placed.target
			&& chamber.building_handle_at({1, 0}) == placed.target;
	}

	[[nodiscard]] bool test_chamber_energy_allocation(){
		chamber::chamber_manifold chamber{{6, 1}};
		const auto generator = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {0, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.structural = true,
			.structural_support_radius = 3,
			.energy = {.power = 3}
		});
		const auto first_consumer = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {1, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.energy = {.power = -3},
			.energy_acquisition = {.maximum_count = 3, .minimum_count = 1, .priority = 10.f}
		});
		const auto second_consumer = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {2, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.energy = {.power = -2},
			.energy_acquisition = {.maximum_count = 2, .minimum_count = 1, .priority = 5.f}
		});
		if(!generator.applied() || !first_consumer.applied() || !second_consumer.applied()){
			return false;
		}

		for(int i = 0; i != 3; ++i){
			chamber.execute(chamber::update_energy_command{.delta_seconds = 1.f});
		}

		const auto* generator_common = chamber.try_building(generator.target);
		const auto* first_common = chamber.try_building(first_consumer.target);
		const auto* second_common = chamber.try_building(second_consumer.target);
		if(generator_common == nullptr || first_common == nullptr || second_common == nullptr){
			return false;
		}
		const auto& summary = chamber.last_energy_summary();
		if(summary.generator_count != 1
			|| summary.consumer_count != 2
			|| summary.generated_energy != 3
			|| summary.minimum_requested_energy != 2
			|| summary.requested_energy != 5
			|| summary.assigned_energy != 3){
			return false;
		}
		if(generator_common->energy_dynamic.power != 3
			|| first_common->valid_energy != 2
			|| second_common->valid_energy != 1
			|| first_common->energy_dynamic.power != -2
			|| second_common->energy_dynamic.power != -1){
			return false;
		}

		const auto reprioritized = chamber.execute(chamber::set_building_energy_command{
			.target = second_consumer.target,
			.energy = {.power = -2},
			.acquisition = {.maximum_count = 2, .minimum_count = 1, .priority = 20.f}
		});
		if(!reprioritized.applied()){
			return false;
		}
		chamber.execute(chamber::update_energy_command{.delta_seconds = 1.f});
		return first_common->valid_energy == 1
			&& second_common->valid_energy == 2
			&& near(first_common->get_efficiency(), 1.f / 3.f, 0.001f)
			&& near(second_common->get_efficiency(), 0.5f, 0.001f);
	}

	[[nodiscard]] bool test_chamber_maneuver_command_and_summary(){
		chamber::chamber_manifold chamber{{4, 1}};
		const auto generator = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {0, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.structural = true,
			.structural_support_radius = 3,
			.energy = {.power = 1}
		});
		const auto thruster = chamber.execute(chamber::place_basic_building_command{
			.region = {.src = {2, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.energy = {.power = -2},
			.energy_acquisition = {.maximum_count = 2, .minimum_count = 1, .priority = 1.f}
		});
		if(!generator.applied() || !thruster.applied()){
			return false;
		}

		const auto invalid_maneuver = chamber.execute(chamber::set_building_maneuver_command{
			.target = thruster.target,
			.maneuver = {.force_longitudinal = std::numeric_limits<float>::quiet_NaN()}
		});
		if(invalid_maneuver.status != chamber::chamber_command_status::invalid_maneuver){
			return false;
		}

		const chamber::maneuver_component maneuver{
			.force_longitudinal = 10.f,
			.force_transverse = 4.f,
			.torque = 2.f,
			.torque_absolute = 1.f,
			.boost = 3.f
		};
		const auto configured = chamber.execute(chamber::set_building_maneuver_command{
			.target = thruster.target,
			.maneuver = maneuver
		});
		if(!configured.applied()){
			return false;
		}

		chamber.execute(chamber::update_energy_command{.delta_seconds = 1.f});
		chamber.execute(chamber::update_maneuver_command{});

		const auto* thruster_common = chamber.try_building(thruster.target);
		if(thruster_common == nullptr || !near(thruster_common->get_efficiency(), 0.5f, 0.001f)){
			return false;
		}

		const math::vec2 center = chamber_tile_point(2.5f, 0.5f);
		const auto& summary = chamber.last_maneuver_summary();
		return near(summary.force_longitudinal, 5.f, 0.001f)
			&& near(summary.force_transverse, 2.f, 0.001f)
			&& near(summary.boost, 1.5f, 0.001f)
			&& near(summary.torque, 0.5f + center.length(), 0.001f)
			&& near(summary.get_force_at(0.f), 5.f, 0.001f);
	}

	[[nodiscard]] bool test_chamber_standard_building_types(){
		chamber::chamber_manifold chamber{{7, 1}};
		chamber.post_command(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::structural_joint,
			.region = {.src = {0, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.structural_support_radius = 6
		});
		auto results = chamber.execute_pending_commands();
		if(results.size() != 1 || !results[0].applied()){
			return false;
		}
		const object_handle structural = results[0].target;

		chamber.post_command(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::armor,
			.region = {.src = {1, 0}, .extent = {1, 1}},
			.hit_points = 20.f
		});
		chamber.post_command(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::energy_generator,
			.region = {.src = {2, 0}, .extent = {1, 1}},
			.hit_points = 30.f,
			.energy = {.power = 1}
		});
		chamber.post_command(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::thruster,
			.region = {.src = {3, 0}, .extent = {1, 1}},
			.hit_points = 40.f,
			.energy = {.power = -1},
			.energy_acquisition = {.maximum_count = 1, .minimum_count = 1, .priority = 1.f},
			.maneuver = {
				.force_longitudinal = 6.f,
				.force_transverse = 2.f,
				.torque = 1.f,
				.torque_absolute = 0.5f,
				.boost = 4.f
			}
		});
		chamber.post_command(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::radar,
			.region = {.src = {4, 0}, .extent = {1, 1}},
			.hit_points = 25.f,
			.radar = {
				.rotation = 0.25f,
				.sensor = {
					.range = chamber::tiles_to_world_units(12.f),
					.scan_interval = 2.f
				}
			}
		});
		chamber.post_command(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::turret,
			.region = {.src = {5, 0}, .extent = {1, 1}},
			.hit_points = 35.f,
			.turret = {.rotation = 0.5f, .reload_seconds = 3.f, .shooting_field_angle = 1.f}
		});
		results = chamber.execute_pending_commands();
		if(results.size() != 5 || std::ranges::any_of(results, [](const chamber::chamber_command_result& result){
			return !result.applied();
		})){
			return false;
		}

		const object_handle armor = results[0].target;
		const object_handle generator = results[1].target;
		const object_handle thruster = results[2].target;
		const object_handle radar = results[3].target;
		const object_handle turret = results[4].target;

		if(chamber.buildings().try_get<chamber::structural_joint_building>(structural) == nullptr
			|| chamber.buildings().try_get<chamber::armor_building>(armor) == nullptr
			|| chamber.buildings().try_get<chamber::energy_generator_building>(generator) == nullptr
			|| chamber.buildings().try_get<chamber::thruster_building>(thruster) == nullptr
			|| chamber.buildings().try_get<chamber::radar_building>(radar) == nullptr
			|| chamber.buildings().try_get<chamber::turret_building>(turret) == nullptr){
			return false;
		}

		chamber.execute(chamber::update_buildings_command{.delta_seconds = 1.f});
		const auto& maneuver = chamber.last_maneuver_summary();
		const math::vec2 thruster_center = chamber_tile_point(3.5f, 0.5f);
		return near(maneuver.force_longitudinal, 6.f, 0.001f)
			&& near(maneuver.force_transverse, 2.f, 0.001f)
			&& near(maneuver.boost, 4.f, 0.001f)
			&& near(maneuver.torque, 0.5f + thruster_center.length(), 0.001f)
			&& chamber.last_energy_summary().generator_count == 1
			&& chamber.last_energy_summary().consumer_count == 1;
	}

	[[nodiscard]] bool test_chamber_corridor_groups(){
		chamber::chamber_manifold chamber{{4, 2}};
		if(chamber.corridor_group_at({0, 0}) != chamber::invalid_corridor_group
			|| chamber.reachable_between({0, 0}, {1, 0})){
			return false;
		}

		const auto first = chamber.execute(chamber::set_tile_corridor_command{
			.region = {.src = {0, 0}, .extent = {3, 1}},
			.corridor = true
		});
		if(!first.applied()
			|| chamber.corridor_group_at({0, 0}) == chamber::invalid_corridor_group
			|| !chamber.reachable_between({0, 0}, {2, 0})
			|| chamber.reachable_between({0, 0}, {3, 0})){
			return false;
		}

		const auto split = chamber.execute(chamber::set_tile_corridor_command{
			.region = {.src = {1, 0}, .extent = {1, 1}},
			.corridor = false
		});
		if(!split.applied()
			|| chamber.reachable_between({0, 0}, {2, 0})
			|| chamber.corridor_group_at({1, 0}) != chamber::invalid_corridor_group){
			return false;
		}

		const auto bridge = chamber.execute(chamber::set_tile_corridor_command{
			.region = {.src = {0, 1}, .extent = {3, 1}},
			.corridor = true
		});
		return bridge.applied()
			&& chamber.reachable_between({0, 0}, {2, 0})
			&& chamber.reachable_between({0, 1}, {2, 1});
	}

	[[nodiscard]] bool test_chamber_radar_turret_targeting_requests(){
		component_manager manager{};
		const auto target = spawn(
			manager,
			chamber_tile_point(3.f, 0.5f),
			physics::make_box_collision_shape(chamber_tile_half_extent(0.5f, 0.5f)),
			physics_body::make_static());
		const auto ally = spawn(
			manager,
			chamber_tile_point(2.f, 0.5f),
			physics::make_box_collision_shape(chamber_tile_half_extent(0.5f, 0.5f)),
			physics_body::make_static());
		manager.commit();

		chamber::chamber_manifold chamber{{4, 1}};
		const auto structural = chamber.execute(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::structural_joint,
			.region = {.src = {0, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.structural_support_radius = 3
		});
		const auto radar = chamber.execute(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::radar,
			.region = {.src = {1, 0}, .extent = {1, 1}},
			.hit_points = 50.f,
			.radar = {.sensor = {.range = chamber::tiles_to_world_units(10.f)}}
		});
		const auto turret = chamber.execute(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::turret,
			.region = {.src = {2, 0}, .extent = {1, 1}},
			.hit_points = 50.f,
			.turret = {
				.reload_seconds = 2.f,
				.projectile_speed = 20.f,
				.projectile_damage = {.material_damage = {.direct = 15.f}}
			}
		});
		if(!structural.applied() || !radar.applied() || !turret.applied()){
			return false;
		}

		chamber::update_targeting_command targeting{
			.candidates = {
				{
					.target = target_ref::entity_target(ally),
					.position = chamber_tile_point(2.f, 0.5f),
					.faction_id = 7
				},
				{
					.target = target_ref::entity_target(target),
					.position = chamber_tile_point(3.f, 0.5f),
					.faction_id = 8
				}
			},
			.faction_id = 7,
			.delta_seconds = 1.f
		};
		chamber.post_command(targeting);
		const auto first_results = chamber.execute_pending_commands();
		const auto* radar_state = chamber.buildings().try_get<chamber::radar_building>(radar.target);
		if(first_results.size() != 1
			|| !first_results[0].applied()
			|| radar_state == nullptr
			|| radar_state->memory.empty()
			|| radar_state->memory.primary()->target.entity != target
			|| chamber.last_fire_requests().size() != 1){
			return false;
		}

		const auto first_fire = chamber.last_fire_requests().front();
		if(first_fire.turret != turret.target
			|| first_fire.target.entity != target
			|| first_fire.faction_id != 7
			|| !near(first_fire.projectile_speed, 20.f, 0.001f)
			|| !near(first_fire.damage.material_damage.direct, 15.f, 0.001f)){
			return false;
		}

		targeting.delta_seconds = 0.5f;
		chamber.post_command(targeting);
		(void)chamber.execute_pending_commands();
		if(!chamber.last_fire_requests().empty()){
			return false;
		}

		targeting.delta_seconds = 2.f;
		chamber.post_command(targeting);
		(void)chamber.execute_pending_commands();
		return chamber.last_fire_requests().size() == 1
			&& chamber.last_fire_requests().front().target.entity == target;
	}

	[[nodiscard]] bool test_chamber_targeting_requires_powered_buildings(){
		component_manager manager{};
		const auto target = spawn(
			manager,
			chamber_tile_point(3.f, 0.5f),
			physics::make_box_collision_shape(chamber_tile_half_extent(0.5f, 0.5f)),
			physics_body::make_static());
		manager.commit();

		chamber::chamber_manifold chamber{{4, 1}};
		const auto structural = chamber.execute(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::structural_joint,
			.region = {.src = {0, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.structural_support_radius = 3
		});
		const auto radar = chamber.execute(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::radar,
			.region = {.src = {1, 0}, .extent = {1, 1}},
			.hit_points = 50.f,
			.energy = {.power = -1},
			.energy_acquisition = {.maximum_count = 1, .minimum_count = 1, .priority = 1.f},
			.radar = {.sensor = {.range = chamber::tiles_to_world_units(10.f)}}
		});
		const auto turret = chamber.execute(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::turret,
			.region = {.src = {2, 0}, .extent = {1, 1}},
			.hit_points = 50.f,
			.energy = {.power = -1},
			.energy_acquisition = {.maximum_count = 1, .minimum_count = 1, .priority = 1.f},
			.turret = {.projectile_speed = 20.f}
		});
		if(!structural.applied() || !radar.applied() || !turret.applied()){
			return false;
		}

		for(int i = 0; i != 3; ++i){
			chamber.execute(chamber::update_energy_command{.delta_seconds = 1.f});
		}
		chamber.post_command(chamber::update_targeting_command{
			.candidates = {{
				.target = target_ref::entity_target(target),
				.position = chamber_tile_point(3.f, 0.5f),
				.faction_id = 8
			}},
			.faction_id = 7,
			.delta_seconds = 1.f
		});
		(void)chamber.execute_pending_commands();
		const auto* radar_state = chamber.buildings().try_get<chamber::radar_building>(radar.target);
		if(radar_state == nullptr || !radar_state->memory.empty() || !chamber.last_fire_requests().empty()){
			return false;
		}

		const auto generator = chamber.execute(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::energy_generator,
			.region = {.src = {3, 0}, .extent = {1, 1}},
			.hit_points = 50.f,
			.energy = {.power = 2}
		});
		if(!generator.applied()){
			return false;
		}

		for(int i = 0; i != 3; ++i){
			chamber.execute(chamber::update_energy_command{.delta_seconds = 1.f});
		}
		chamber.post_command(chamber::update_targeting_command{
			.candidates = {{
				.target = target_ref::entity_target(target),
				.position = chamber_tile_point(3.f, 0.5f),
				.faction_id = 8
			}},
			.faction_id = 7,
			.delta_seconds = 1.f
		});
		(void)chamber.execute_pending_commands();
		return !radar_state->memory.empty()
			&& radar_state->memory.primary()->target.entity == target
			&& chamber.last_fire_requests().size() == 1
			&& chamber.last_fire_requests().front().target.entity == target;
	}

	[[nodiscard]] bool test_chamber_dump_and_load_standard_buildings(){
		chamber::chamber_manifold chamber{{4, 1}};
		(void)chamber.execute(chamber::set_tile_corridor_command{
			.region = {.src = {0, 0}, .extent = {2, 1}},
			.corridor = true
		});
		const auto structural = chamber.execute(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::structural_joint,
			.region = {.src = {0, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.structural_support_radius = 3
		});
		const auto generator = chamber.execute(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::energy_generator,
			.region = {.src = {1, 0}, .extent = {1, 1}},
			.hit_points = 50.f,
			.energy = {.power = 1}
		});
		const auto thruster = chamber.execute(chamber::place_standard_building_command{
			.type = chamber::standard_building_type::thruster,
			.region = {.src = {2, 0}, .extent = {1, 1}},
			.hit_points = 40.f,
			.energy = {.power = -1},
			.energy_acquisition = {.maximum_count = 1, .minimum_count = 1, .priority = 1.f},
			.maneuver = {.force_longitudinal = 8.f}
		});
		if(!structural.applied() || !generator.applied() || !thruster.applied()){
			return false;
		}

		chamber.execute(chamber::update_buildings_command{.delta_seconds = 1.f});

		const chamber::chamber_dump dump = chamber.dump();
		chamber::chamber_manifold restored{};
		restored.load_dump(dump);
		if(restored.extent() != math::point2{4, 1}
			|| !restored.reachable_between({0, 0}, {1, 0})
			|| restored.buildings().try_get<chamber::structural_joint_building>(restored.building_handle_at({0, 0})) == nullptr
			|| restored.buildings().try_get<chamber::energy_generator_building>(restored.building_handle_at({1, 0})) == nullptr
			|| restored.buildings().try_get<chamber::thruster_building>(restored.building_handle_at({2, 0})) == nullptr){
			return false;
		}

		const auto* restored_thruster = restored.try_building(restored.building_handle_at({2, 0}));
		if(restored_thruster == nullptr
			|| !near(restored_thruster->hit_points.current, 40.f, 0.001f)
			|| !near(restored.tile_status_at(*restored_thruster, {2, 0}).hit_point, 80.f, 0.001f)){
			return false;
		}

		restored.execute(chamber::update_buildings_command{.delta_seconds = 1.f});
		return restored.last_energy_summary().generator_count == 1
			&& restored.last_energy_summary().consumer_count == 1
			&& near(restored.last_maneuver_summary().force_longitudinal, 8.f, 0.001f);
	}

	[[nodiscard]] bool test_chamber_system_applies_commands_and_settles_damage(){
		component_manager manager{};
		manager.update_update_delta(1.f);
		system::chamber_system chamber_sys{};

		tuple_to_comp_t<chamber_desc> components{};
		components.get<chamber::chamber_manifold>() = chamber::chamber_manifold{{1, 1}};
		const entity_id entity = manager.spawn<chamber_desc>(std::move(components));
		manager.commit();

		auto& chamber_component = entity.at<chamber::chamber_manifold>();
		chamber_component.post_command(chamber::place_basic_building_command{
			.region = {.src = {0, 0}, .extent = {1, 1}},
			.hit_points = 100.f,
			.structural = true
		});
		chamber_sys.pre_step(manager);

		const auto& command_results = chamber_component.last_command_results();
		if(command_results.size() != 1 || !command_results.front().applied()){
			return false;
		}

		if(chamber_component.building_at({0, 0}) == nullptr){
			return false;
		}

		auto shape = physics::make_circle_collision_shape(chamber::tiles_to_world_units(0.25f)).to_record();
		damage_group damage{.material_damage = {.direct = 120.f}};
		physics_contact_endpoint_snapshot projectile_endpoint{};
		projectile_endpoint.previous_shape = {chamber_tile_point(-1.f, 0.5f), 0.f};
		projectile_endpoint.current_shape = {chamber_tile_point(2.f, 0.5f), 0.f};
		physics_contact_endpoint_snapshot target_endpoint{};
		chamber::projectile_hit_context context{
			{},
			entity,
			shape,
			projectile_endpoint,
			target_endpoint,
			{},
			{1.f, 0.f},
			damage
		};
		const auto hit = chamber_component.execute(chamber::projectile_hit_command{context});
		if(!hit.hit_any_tile || !near(hit.actual_damage, 120.f, 0.001f)){
			return false;
		}

		chamber_sys.post_projectile_step(manager);
		return entity.is_expired()
			&& near(chamber_component.structural_hit_points().current, 0.f, 0.001f);
	}
}

TEST(PhysicsEcsTest, ChannelGroupAndSensorFilter){
	EXPECT_TRUE(test_channel_group_and_sensor_filter());
}

TEST(PhysicsEcsTest, Impulses){
	EXPECT_TRUE(test_impulses());
}

TEST(PhysicsEcsTest, CcdProjectileEventAndQuery){
	EXPECT_TRUE(test_ccd_projectile_event_and_query());
}

TEST(PhysicsEcsTest, ExpiredEntitiesAreSkippedBeforeDestroy){
	EXPECT_TRUE(test_expired_entities_are_skipped_before_destroy());
}

TEST(PhysicsEcsTest, ContactEndEventSurvivesDeferredDestroy){
	EXPECT_TRUE(test_contact_end_event_survives_deferred_destroy());
}

TEST(PhysicsEcsTest, SpatialQuerySurvivesComponentRelocation){
	EXPECT_TRUE(test_spatial_query_survives_component_relocation());
}

TEST(PhysicsEcsTest, TargetingSystemEntitySensorFiltersAndLimits){
	EXPECT_TRUE(test_targeting_system_entity_sensor_filters_and_limits());
}

TEST(PhysicsEcsTest, TargetingSystemGroupsChamberRadarScans){
	EXPECT_TRUE(test_targeting_system_groups_chamber_radar_scans());
}

TEST(PhysicsEcsTest, TargetingSystemChamberBuildingTargetsExpire){
	EXPECT_TRUE(test_targeting_system_chamber_building_targets_expire());
}

TEST(PhysicsEcsTest, LowSpeedLargeFaceContactStaysStable){
	EXPECT_TRUE(test_low_speed_large_face_contact_stays_stable());
}

TEST(PhysicsEcsTest, LowSpeedStackSettlesWithoutContactFlicker){
	EXPECT_TRUE(test_low_speed_stack_settles_without_contact_flicker());
}

TEST(PhysicsEcsTest, RestingContactFrictionUsesCachedNormalImpulse){
	EXPECT_TRUE(test_resting_contact_friction_uses_cached_normal_impulse());
}

TEST(PhysicsEcsTest, ProjectileSystemSensorHitDamageAndExpire){
	EXPECT_TRUE(test_projectile_system_sensor_hit_damage_and_expire());
}

TEST(PhysicsEcsTest, ProjectileSystemFactionFilter){
	EXPECT_TRUE(test_projectile_system_faction_filter());
}

TEST(PhysicsEcsTest, ChamberProjectileTilePiercingDamageOrder){
	EXPECT_TRUE(test_chamber_projectile_tile_piercing_damage_order());
}

TEST(PhysicsEcsTest, ChamberLargeSweptAabbPrunesWithoutMissingTiles){
	EXPECT_TRUE(test_chamber_large_swept_aabb_prunes_without_missing_tiles());
}

TEST(PhysicsEcsTest, ChamberProjectileHitWorkspaceReuse){
	EXPECT_TRUE(test_chamber_projectile_hit_workspace_reuse());
}

TEST(PhysicsEcsTest, ChamberColliderDirtyTracksKilledBuildings){
	EXPECT_TRUE(test_chamber_collider_dirty_tracks_killed_buildings());
}

TEST(PhysicsEcsTest, ChamberPendingDamageSettlesOnlyHitBuildings){
	EXPECT_TRUE(test_chamber_pending_damage_settles_only_hit_buildings());
}

TEST(PhysicsEcsTest, ChamberBuildingCommandsAndHandleExpiration){
	EXPECT_TRUE(test_chamber_building_commands_and_handle_expiration());
}

TEST(PhysicsEcsTest, ChamberStructuralSupportAndCascade){
	EXPECT_TRUE(test_chamber_structural_support_and_cascade());
}

TEST(PhysicsEcsTest, ChamberTypedBuildingChannelUpdates){
	EXPECT_TRUE(test_chamber_typed_building_channel_updates());
}

TEST(PhysicsEcsTest, ChamberEnergyAllocation){
	EXPECT_TRUE(test_chamber_energy_allocation());
}

TEST(PhysicsEcsTest, ChamberManeuverCommandAndSummary){
	EXPECT_TRUE(test_chamber_maneuver_command_and_summary());
}

TEST(PhysicsEcsTest, ChamberStandardBuildingTypes){
	EXPECT_TRUE(test_chamber_standard_building_types());
}

TEST(PhysicsEcsTest, ChamberCorridorGroups){
	EXPECT_TRUE(test_chamber_corridor_groups());
}

TEST(PhysicsEcsTest, ChamberRadarTurretTargetingRequests){
	EXPECT_TRUE(test_chamber_radar_turret_targeting_requests());
}

TEST(PhysicsEcsTest, ChamberTargetingRequiresPoweredBuildings){
	EXPECT_TRUE(test_chamber_targeting_requires_powered_buildings());
}

TEST(PhysicsEcsTest, ChamberDumpAndLoadStandardBuildings){
	EXPECT_TRUE(test_chamber_dump_and_load_standard_buildings());
}

TEST(PhysicsEcsTest, ChamberSystemAppliesCommandsAndSettlesDamage){
	EXPECT_TRUE(test_chamber_system_applies_commands_and_settles_damage());
}
