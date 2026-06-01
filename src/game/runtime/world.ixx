export module mo_yanxi.game.runtime.world;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.component.faction;
export import mo_yanxi.game.ecs.component.physics;
export import mo_yanxi.game.ecs.component.targeting;
export import mo_yanxi.game.ecs.system.chamber;
export import mo_yanxi.game.ecs.system.physics;
export import mo_yanxi.game.ecs.system.projectile;
export import mo_yanxi.game.ecs.system.targeting;
export import mo_yanxi.game.runtime.draw.chamber_component;
export import mo_yanxi.game.runtime.draw.collision_shape_component;

import mo_yanxi.game.profile.runtime;
import std;

namespace mo_yanxi::game{
export
	struct motion_system{
	void run(ecs::component_manager& manager) const{
		manager.each([](
			const ecs::component_manager& current_manager,
			const ecs::chunk_meta& meta,
			ecs::mech_motion& motion){
			const ecs::entity_id id = meta.id();
			if(id && id.try_get<ecs::physics_body>() != nullptr){
				return;
			}

			motion.apply_and_reset(current_manager.get_update_delta());
		});
	}
};

export
struct game_systems{
	motion_system motion{};
	ecs::system::chamber_system chamber{};
	ecs::system::projectile_system projectile{};
	ecs::system::physics_system physics{};
	ecs::system::targeting_system targeting{};

	void clear(){
		physics.clear();
		targeting.clear();
	}
};

export
enum class game_command_status{
	applied,
	expired_target,
	missing_chamber
};

export
struct game_command_result{
	game_command_status status{};
	ecs::entity_id target{};

	[[nodiscard]] constexpr bool applied() const noexcept{
		return status == game_command_status::applied;
	}
};

export
struct spawn_chamber_command{
	math::point2 extent{};
	math::vec2 position{};
	float rotation{};
	math::vec2 velocity{};
	float angular_velocity{};
	std::uint32_t faction_id{};
	physics::collision_filter filter{};
	bool sensor{};
	physics::ccd_mode ccd{physics::ccd_mode::disabled};
	ecs::targetable_profile targetable{};
	std::vector<ecs::chamber::chamber_command> initial_commands{};
	ecs::entity_id* out_entity{};
	game_command_result* out_result{};
};

export
struct post_chamber_command{
	ecs::entity_id target{};
	ecs::chamber::chamber_command command{};
	game_command_result* out_result{};
};

export
struct spawn_physics_body_command{
	math::vec2 position{};
	physics::collision_shape_record shape{};
	ecs::physics_body body{};
	math::vec2 velocity{};
	physics::collision_filter filter{};
	bool sensor{};
	physics::ccd_mode ccd{physics::ccd_mode::disabled};
	ecs::targetable_profile targetable{};
	ecs::entity_id* out_entity{};
	game_command_result* out_result{};
};

export
struct spawn_projectile_command{
	math::vec2 position{};
	physics::collision_shape_record shape{};
	ecs::physics_body body{};
	math::vec2 velocity{};
	physics::collision_filter filter{};
	bool sensor{};
	physics::ccd_mode ccd{physics::ccd_mode::disabled};
	ecs::projectile_state projectile{};
	ecs::entity_id* out_entity{};
	game_command_result* out_result{};
};

export
struct spawn_chamber_projectiles_command{
	ecs::entity_id target{};
	physics::collision_shape_record shape{};
	ecs::physics_body body{};
	float lifetime{1.f};
	physics::collision_filter filter{};
	bool sensor{};
	physics::ccd_mode ccd{physics::ccd_mode::disabled};
	std::vector<ecs::entity_id>* out_entities{};
	game_command_result* out_result{};
};

export
using game_world_command = std::variant<
	spawn_chamber_command,
	post_chamber_command,
	spawn_physics_body_command,
	spawn_projectile_command,
	spawn_chamber_projectiles_command
>;

export
struct game_world{
	ecs::component_manager component_manager{};
	game_systems systems{};

	using chamber_entity_desc = std::tuple<
		ecs::chunk_meta,
		ecs::mech_motion,
		ecs::collider,
		ecs::physics_body,
		ecs::faction_data,
		ecs::targetable_profile,
		ecs::chamber_drawer,
		ecs::chamber::chamber_manifold>;
	using physics_entity_desc = std::tuple<
		ecs::chunk_meta,
		ecs::mech_motion,
		ecs::collider,
		ecs::physics_body,
		ecs::targetable_profile>;
	using projectile_entity_desc = std::tuple<
		ecs::chunk_meta,
		ecs::mech_motion,
		ecs::collider,
		ecs::physics_body,
		ecs::projectile_state>;

	[[nodiscard]] ecs::component_manager& components() noexcept{
		return component_manager;
	}

	[[nodiscard]] const ecs::component_manager& components() const noexcept{
		return component_manager;
	}

	[[nodiscard]] ecs::system::physics_system& physics() noexcept{
		return systems.physics;
	}

	[[nodiscard]] const ecs::system::physics_system& physics() const noexcept{
		return systems.physics;
	}

	[[nodiscard]] static math::trans2 chamber_world_transform(const ecs::entity_id target) noexcept{
		const auto* motion = target.try_get<ecs::mech_motion>();
		return motion != nullptr
			? static_cast<math::trans2>(motion->trans)
			: math::trans2{};
	}

	[[nodiscard]] static math::vec2 world_to_chamber_point(
		const math::vec2 position,
		const math::trans2 chamber_transform) noexcept{
		return position << chamber_transform;
	}

	[[nodiscard]] static math::vec2 chamber_to_world_point(
		const math::vec2 position,
		const math::trans2 chamber_transform) noexcept{
		return position >> chamber_transform;
	}

	[[nodiscard]] static math::vec2 world_to_chamber_vector(
		math::vec2 vector,
		const math::trans2 chamber_transform) noexcept{
		vector.rotate_rad(-static_cast<float>(chamber_transform.rot));
		return vector;
	}

	[[nodiscard]] static math::vec2 chamber_to_world_vector(
		math::vec2 vector,
		const math::trans2 chamber_transform) noexcept{
		vector.rotate_rad(static_cast<float>(chamber_transform.rot));
		return vector;
	}

	[[nodiscard]] game_command_result execute(spawn_chamber_command&& command){
		ecs::tuple_to_comp_t<chamber_entity_desc> components{};
		ecs::mech_motion& motion = components.template get<ecs::mech_motion>();
		motion.trans.vec = command.position;
		motion.trans.rot = command.rotation;
		motion.vel.vec = command.velocity;
		motion.vel.rot = command.angular_velocity;
		auto& chamber = components.template get<ecs::chamber::chamber_manifold>();
		chamber = ecs::chamber::chamber_manifold{command.extent};
		for(ecs::chamber::chamber_command& chamber_command : command.initial_commands){
			chamber.post_command(std::move(chamber_command));
		}
		(void)chamber.execute_pending_commands();

		ecs::collider& collider = components.template get<ecs::collider>();
		ecs::chamber::synchronize_chamber_collider(collider, chamber);
		chamber.mark_collider_synchronized();
		collider.filter = command.filter;
		collider.filter.sensor = command.sensor;
		collider.ccd = command.ccd;
		components.template get<ecs::physics_body>() = ecs::physics_body::make_kinematic();
		components.template get<ecs::physics_body>().body.ccd = command.ccd;
		components.template get<ecs::faction_data>().id = command.faction_id;
		components.template get<ecs::targetable_profile>() = command.targetable;

		const ecs::entity_id entity = component_manager.spawn<chamber_entity_desc>(std::move(components));
		if(command.out_entity != nullptr){
			*command.out_entity = entity;
		}
		const game_command_result result{.status = game_command_status::applied, .target = entity};
		if(command.out_result != nullptr){
			*command.out_result = result;
		}
		return result;
	}

	[[nodiscard]] game_command_result execute(post_chamber_command&& command){
		if(!command.target || command.target.is_expired()){
			const game_command_result result{
				.status = game_command_status::expired_target,
				.target = command.target
			};
			if(command.out_result != nullptr){
				*command.out_result = result;
			}
			return result;
		}

		auto* chamber = command.target.try_get<ecs::chamber::chamber_manifold>();
		if(chamber == nullptr){
			const game_command_result result{
				.status = game_command_status::missing_chamber,
				.target = command.target
			};
			if(command.out_result != nullptr){
				*command.out_result = result;
			}
			return result;
		}

		chamber->post_command(std::move(command.command));
		const game_command_result result{.status = game_command_status::applied, .target = command.target};
		if(command.out_result != nullptr){
			*command.out_result = result;
		}
		return result;
	}

	[[nodiscard]] game_command_result execute(spawn_physics_body_command&& command){
		ecs::tuple_to_comp_t<physics_entity_desc> components{};
		components.template get<ecs::mech_motion>().trans.vec = command.position;
		components.template get<ecs::mech_motion>().vel.vec = command.velocity;
		components.template get<ecs::collider>().shape = std::move(command.shape);
		components.template get<ecs::collider>().filter = command.filter;
		components.template get<ecs::collider>().filter.sensor = command.sensor;
		components.template get<ecs::collider>().ccd = command.ccd;
		components.template get<ecs::physics_body>() = command.body;
		components.template get<ecs::physics_body>().body.ccd = command.ccd;
		components.template get<ecs::targetable_profile>() = command.targetable;

		const ecs::entity_id entity = component_manager.spawn<physics_entity_desc>(std::move(components));
		if(command.out_entity != nullptr){
			*command.out_entity = entity;
		}
		const game_command_result result{.status = game_command_status::applied, .target = entity};
		if(command.out_result != nullptr){
			*command.out_result = result;
		}
		return result;
	}

	[[nodiscard]] game_command_result execute(spawn_projectile_command&& command){
		ecs::tuple_to_comp_t<projectile_entity_desc> components{};
		components.template get<ecs::mech_motion>().trans.vec = command.position;
		components.template get<ecs::mech_motion>().vel.vec = command.velocity;
		components.template get<ecs::collider>().shape = std::move(command.shape);
		components.template get<ecs::collider>().filter = command.filter;
		components.template get<ecs::collider>().filter.sensor = command.sensor;
		components.template get<ecs::collider>().ccd = command.ccd;
		components.template get<ecs::physics_body>() = command.body;
		components.template get<ecs::physics_body>().body.ccd = command.ccd;
		components.template get<ecs::projectile_state>() = std::move(command.projectile);

		const ecs::entity_id entity = component_manager.spawn<projectile_entity_desc>(std::move(components));
		if(command.out_entity != nullptr){
			*command.out_entity = entity;
		}
		const game_command_result result{.status = game_command_status::applied, .target = entity};
		if(command.out_result != nullptr){
			*command.out_result = result;
		}
		return result;
	}

	[[nodiscard]] game_command_result execute(spawn_chamber_projectiles_command&& command){
		if(command.out_entities != nullptr){
			command.out_entities->clear();
		}
		if(!command.target || command.target.is_expired()){
			const game_command_result result{
				.status = game_command_status::expired_target,
				.target = command.target
			};
			if(command.out_result != nullptr){
				*command.out_result = result;
			}
			return result;
		}

		auto* chamber = command.target.try_get<ecs::chamber::chamber_manifold>();
		if(chamber == nullptr){
			const game_command_result result{
				.status = game_command_status::missing_chamber,
				.target = command.target
			};
			if(command.out_result != nullptr){
				*command.out_result = result;
			}
			return result;
		}

		for(const ecs::chamber::chamber_fire_request& request : chamber->last_fire_requests()){
			ecs::tuple_to_comp_t<projectile_entity_desc> components{};
			ecs::mech_motion& motion = components.template get<ecs::mech_motion>();
			const math::trans2 chamber_transform = game_world::chamber_world_transform(command.target);
			const auto* chamber_motion = command.target.try_get<ecs::mech_motion>();
			const math::vec2 world_direction = game_world::chamber_to_world_vector(request.direction, chamber_transform);
			motion.trans.vec = game_world::chamber_to_world_point(request.origin, chamber_transform);
			motion.trans.rot = std::atan2(world_direction.y, world_direction.x);
			motion.vel.vec = world_direction * request.projectile_speed;
			if(chamber_motion != nullptr){
				motion.vel.vec += chamber_motion->vel.vec;
			}
			components.template get<ecs::collider>().shape = command.shape;
			components.template get<ecs::collider>().filter = command.filter;
			components.template get<ecs::collider>().filter.sensor = command.sensor;
			components.template get<ecs::collider>().ccd = command.ccd;
			components.template get<ecs::physics_body>() = command.body;
			components.template get<ecs::physics_body>().body.ccd = command.ccd;
			ecs::projectile_state& projectile = components.template get<ecs::projectile_state>();
			projectile.owner = command.target;
			projectile.faction_id = request.faction_id;
			projectile.remaining_lifetime = command.lifetime;
			projectile.set_damage(request.damage);

			const ecs::entity_id entity = component_manager.spawn<projectile_entity_desc>(std::move(components));
			if(command.out_entities != nullptr){
				command.out_entities->push_back(entity);
			}
		}
		(void)chamber->execute(ecs::chamber::clear_fire_requests_command{});

		const game_command_result result{.status = game_command_status::applied, .target = command.target};
		if(command.out_result != nullptr){
			*command.out_result = result;
		}
		return result;
	}

	[[nodiscard]] game_command_result execute(game_world_command&& command){
		return std::visit([this](auto& item){
			return this->execute(std::move(item));
		}, command);
	}

	void begin_step(const float step_seconds) noexcept{
		component_manager.update_update_delta(step_seconds);
	}

	void run_systems(profile::profile_session* profile_session, const std::uint64_t frame_index){
		if(profile_session == nullptr){
			this->run_systems();
			return;
		}

		{
			profile::profile_scope_timer timer{profile_session, frame_index, "game.systems.commit.begin"};
			component_manager.commit();
		}
		{
			profile::profile_scope_timer timer{profile_session, frame_index, "game.systems.chamber.pre_step"};
			systems.chamber.pre_step(component_manager);
		}
		{
			profile::profile_scope_timer timer{profile_session, frame_index, "game.systems.motion"};
			systems.motion.run(component_manager);
		}
		{
			profile::profile_scope_timer timer{profile_session, frame_index, "game.systems.projectile.pre_step"};
			systems.projectile.pre_step(component_manager);
		}
		{
			profile::profile_scope_timer timer{profile_session, frame_index, "game.systems.physics"};
			systems.physics.step(component_manager);
		}
		const auto physics_stats = systems.physics.last_statistics();
		profile_session->write_counter(frame_index, "physics.proxy_count", static_cast<std::uint64_t>(physics_stats.proxy_count));
		profile_session->write_counter(frame_index, "physics.candidate_pair_count", static_cast<std::uint64_t>(physics_stats.candidate_pair_count));
		profile_session->write_counter(frame_index, "physics.contact_constraint_count", static_cast<std::uint64_t>(physics_stats.contact_constraint_count));
		profile_session->write_counter(frame_index, "physics.contact_event_count", static_cast<std::uint64_t>(physics_stats.contact_event_count));
		{
			profile::profile_scope_timer timer{profile_session, frame_index, "game.systems.targeting"};
			systems.targeting.step(component_manager, systems.physics);
		}
		const auto targeting_stats = systems.targeting.last_statistics();
		profile_session->write_counter(frame_index, "targeting.target_snapshot_count", targeting_stats.target_snapshot_count);
		profile_session->write_counter(
			frame_index,
			"targeting.building_target_snapshot_count",
			targeting_stats.building_target_snapshot_count);
		profile_session->write_counter(frame_index, "targeting.entity_sensor_count", targeting_stats.entity_sensor_count);
		profile_session->write_counter(frame_index, "targeting.chamber_sensor_count", targeting_stats.chamber_sensor_count);
		profile_session->write_counter(frame_index, "targeting.unique_scan_count", targeting_stats.unique_scan_count);
		profile_session->write_counter(frame_index, "targeting.selected_target_count", targeting_stats.selected_target_count);
		{
			profile::profile_scope_timer timer{profile_session, frame_index, "game.systems.projectile.resolve_hits"};
			systems.projectile.resolve_hits(component_manager, systems.physics);
		}
		const auto projectile_stats = systems.projectile.last_statistics();
		profile_session->write_counter(frame_index, "projectile.contact_event_count", projectile_stats.contact_event_count);
		profile_session->write_counter(frame_index, "projectile.projectile_event_count", projectile_stats.projectile_event_count);
		profile_session->write_counter(frame_index, "projectile.ignored_hit_count", projectile_stats.ignored_hit_count);
		profile_session->write_counter(frame_index, "projectile.simple_hit_attempt_count", projectile_stats.simple_hit_attempt_count);
		profile_session->write_counter(frame_index, "projectile.chamber_hit_attempt_count", projectile_stats.chamber_hit_attempt_count);
		profile_session->write_counter(frame_index, "projectile.applied_hit_count", projectile_stats.applied_hit_count);
		profile_session->write_counter(frame_index, "projectile.chamber_tile_hit_count", projectile_stats.chamber_tile_hit_count);
		profile_session->write_counter(frame_index, "projectile.chamber_damaged_tile_count", projectile_stats.chamber_damaged_tile_count);
		profile_session->write_counter(frame_index, "projectile.chamber_hit_time_ns", projectile_stats.chamber_hit_time_ns);
		profile_session->write_region(
			frame_index,
			"game.systems.subgrid.projectile_hit",
			static_cast<double>(projectile_stats.chamber_hit_time_ns) / 1'000'000.0);
		{
			profile::profile_scope_timer timer{profile_session, frame_index, "game.systems.chamber.post_projectile_step"};
			systems.chamber.post_projectile_step(component_manager);
		}
		{
			profile::profile_scope_timer timer{profile_session, frame_index, "game.systems.commit.end"};
			component_manager.commit();
		}
	}

	void run_systems(){
		component_manager.commit();
		systems.chamber.pre_step(component_manager);
		systems.motion.run(component_manager);
		systems.projectile.pre_step(component_manager);
		systems.physics.step(component_manager);
		systems.targeting.step(component_manager, systems.physics);
		systems.projectile.resolve_hits(component_manager, systems.physics);
		systems.chamber.post_projectile_step(component_manager);
		component_manager.commit();
	}

	void step(const float step_seconds){
		begin_step(step_seconds);
		run_systems();
	}

	void clear_runtime_caches(){
		systems.clear();
	}
};
}
