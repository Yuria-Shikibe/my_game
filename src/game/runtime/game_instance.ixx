export module mo_yanxi.game.instance;

export import mo_yanxi.game.runtime.game_render_device;
export import mo_yanxi.game.runtime.game_renderer;
export import mo_yanxi.game.runtime.world;
export import mo_yanxi.input_handle.input_event_queue;

import mo_yanxi.graphic.camera2;
import mo_yanxi.game.profile.runtime;
import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.input_handle;
import mo_yanxi.math.vector2;
import mo_yanxi.log;
import std;

namespace mo_yanxi::game{
export
struct game_instance_config{
	float fixed_step_seconds{1.f / 60.f};
	float max_frame_delta_seconds{0.1f};
	unsigned max_steps_per_frame{4};
	float camera_min_scale{0.1f};
	float camera_max_scale{5.f};
	std::uint32_t debug_random_seed{0x4d59474du};
};

export
class debug_camera2_binding{
public:
	using vec2_t = math::vec2;

private:
	bool middle_dragging_{};
	bool cursor_seen_{};
	vec2_t last_cursor_{};

	void apply_drag(graphic::camera2& camera, const vec2_t delta) const noexcept{
		camera.move(delta * (-1.f / camera.get_scale()));
	}

public:
	debug_camera2_binding() = default;

	void reset_state() noexcept{
		middle_dragging_ = false;
		cursor_seen_ = false;
		last_cursor_ = {};
	}

	void on_mouse_button(input_handle::mouse button, input_handle::act action) noexcept{
		if(button != input_handle::mouse::CMB) return;

		if(action == input_handle::act::press){
			middle_dragging_ = true;
			return;
		}

		if(action == input_handle::act::release){
			middle_dragging_ = false;
		}
	}

	void on_cursor_move(graphic::camera2& camera, const vec2_t cursor_pos) noexcept{
		if(middle_dragging_){
			apply_drag(camera, cursor_pos - last_cursor_);
		}

		last_cursor_ = cursor_pos;
		cursor_seen_ = true;
	}

	[[nodiscard]] std::optional<vec2_t> cursor_world(const graphic::camera2& camera) const noexcept{
		if(!cursor_seen_){
			return std::nullopt;
		}

		return camera.get_screen_to_world(last_cursor_);
	}

	void on_scroll(graphic::camera2& camera, vec2_t delta) noexcept{
		delta.flip_y();
		camera.set_scale_by_delta(delta.y * -0.05f);
	}

	void reset_camera(graphic::camera2& camera) noexcept{
		camera.set_center({});
		camera.set_scale(camera.get_target_scale_def());
		camera.set_target_scale_def();
	}

	void on_key_press(graphic::camera2& camera, const input_handle::key key) noexcept{
		if(key == input_handle::key::home){
			reset_camera(camera);
		}
	}

	void on_event(graphic::camera2& camera, const input_handle::input_event_variant event) noexcept{
		switch(event.type){
		case input_handle::input_event_type::input_key:
			if(event.input_key.action == input_handle::act::press){
				on_key_press(camera, event.input_key.as_key());
			}
			break;
		case input_handle::input_event_type::input_mouse:
			on_mouse_button(event.input_key.as_mouse(), event.input_key.action);
			break;
		case input_handle::input_event_type::input_scroll:
			on_scroll(camera, event.cursor);
			break;
		case input_handle::input_event_type::cursor_move:
			on_cursor_move(camera, event.cursor);
			break;
		default:
			break;
		}
	}
};

export
class game_instance{
private:
	game_instance_config config_{};
	std::mt19937 debug_shot_random_engine_{config_.debug_random_seed};
	std::optional<game_world> world_{std::in_place};
	std::mutex game_state_mutex_{};
	std::mutex command_mutex_{};
	std::vector<game_world_command> pending_commands_{};
	std::mutex event_mutex_{};
	std::vector<input_handle::input_event_variant> pending_events_{};
	std::mutex render_extent_mutex_{};
	math::vec2 pending_render_extent_{};
	float accumulated_seconds_{};
	std::uint64_t simulation_tick_{};
	graphic::camera2 world_camera_{};
	debug_camera2_binding camera_binding_{};
	math::vec2 render_extent_{};
	unsigned pending_debug_shape_shots_{};
	std::uint64_t render_frame_index_{};
	game_render_effect_config effect_config_{};
	std::optional<profile::profile_scenario_config> profile_scenario_{};
	std::shared_ptr<profile::profile_session> profile_session_{};
	std::atomic_bool initialized_{};

	using debug_shape_entity_desc = std::tuple<
		ecs::chunk_meta,
		ecs::mech_motion,
		ecs::collider,
		ecs::physics_body,
		ecs::collision_shape_drawer,
		ecs::projectile_state
	>;

	using debug_scene_entity_desc = std::tuple<
		ecs::chunk_meta,
		ecs::mech_motion,
		ecs::collider,
		ecs::physics_body,
		ecs::collision_shape_drawer
	>;

	enum class debug_shot_shape_kind : std::uint8_t{
		circle,
		capsule,
		box,
		convex_polygon,
		compound_convex_polygons
	};

	static constexpr unsigned max_pending_debug_shape_shots = 64;
	static constexpr math::vec2 debug_shape_box_half_extent{48.f, 16.f};
	static constexpr float debug_shape_spawn_offset{72.f};
	static constexpr float debug_shape_initial_speed{900.f};
	static constexpr float debug_shape_mass{20.f};
	static constexpr float debug_shape_rotational_inertia{18000.f};
	static constexpr float debug_shape_drag{1.2f};
	static constexpr float debug_shape_projectile_damage{120.f};
	static constexpr float debug_shape_projectile_lifetime{4.f};
	static constexpr int solid_grid_test_width{40};
	static constexpr int solid_grid_test_height{40};
	static constexpr float solid_grid_test_tile_hit_points{100.f};
	static constexpr math::vec2 solid_grid_test_position{240.f, 160.f};
	static constexpr std::uint32_t solid_grid_test_faction_id{3u};

	[[nodiscard]] static constexpr float solid_grid_test_building_hit_points() noexcept{
		return static_cast<float>(solid_grid_test_width * solid_grid_test_height)
			* solid_grid_test_tile_hit_points
			* 0.5f;
	}

	void configure_camera() noexcept{
		world_camera_.set_scale_range({config_.camera_min_scale, config_.camera_max_scale});
	}

	void initialize_state(const bool spawn_demo_scene){
		this->configure_camera();
		initialized_.store(true, std::memory_order_release);
		if(profile_scenario_){
			this->spawn_profile_scene(*profile_scenario_);
			world_->components().commit();
		}else if(spawn_demo_scene){
			this->spawn_default_debug_scene();
			world_->components().commit();
		}
	}

	void ensure_initialized_state(){
		if(initialized_.load(std::memory_order_acquire)){
			return;
		}

		std::lock_guard lock{game_state_mutex_};
		if(!initialized_.load(std::memory_order_acquire)){
			this->initialize_state(false);
		}
	}

	void resize_camera_if_needed(const math::vec2 extent) noexcept{
		if(render_extent_ == extent){
			return;
		}

		render_extent_ = extent;
		world_camera_.resize_screen(extent.x, extent.y);
		world_camera_.update(0.f);
		log::info(
			{"Game"},
			"render extent changed: ({}, {})",
			extent.x,
			extent.y);
	}

	void consume_render_extent_request() noexcept{
		math::vec2 requested_extent{};
		{
			std::lock_guard lock{render_extent_mutex_};
			requested_extent = pending_render_extent_;
		}

		if(requested_extent.x > 0.f && requested_extent.y > 0.f){
			this->resize_camera_if_needed(requested_extent);
		}
	}

	void request_render_extent(const math::vec2 extent){
		std::lock_guard lock{render_extent_mutex_};
		pending_render_extent_ = extent;
	}

	void drain_events(){
		std::vector<input_handle::input_event_variant> events{};
		{
			std::lock_guard lock{event_mutex_};
			events.swap(pending_events_);
		}

		for(const auto event : events){
			this->process_event(event);
		}
	}

	void drain_commands(){
		std::vector<game_world_command> commands{};
		{
			std::lock_guard lock{command_mutex_};
			commands.swap(pending_commands_);
		}

		for(auto& command : commands){
			(void)world_->execute(std::move(command));
		}
	}

	[[nodiscard]] float clamped_frame_delta(const float delta_seconds) const noexcept{
		if(!std::isfinite(delta_seconds) || delta_seconds <= 0.f){
			return 0.f;
		}

		return std::min(delta_seconds, config_.max_frame_delta_seconds);
	}

	[[nodiscard]] static bool is_debug_shape_shot_event(const input_handle::input_event_variant event) noexcept{
		return event.type == input_handle::input_event_type::input_key
			&& event.input_key.action == input_handle::act::press
			&& event.input_key.as_key() == input_handle::key::q;
	}

	[[nodiscard]] math::vec2 debug_shape_direction() const noexcept{
		const auto origin = world_camera_.get_stable_center();
		auto direction = camera_binding_.cursor_world(world_camera_).value_or(origin + math::vec2{1.f, 0.f}) - origin;
		if(direction.length2() <= 0.0001f){
			return {1.f, 0.f};
		}

		return direction.normalize();
	}

	[[nodiscard]] debug_shot_shape_kind random_debug_shot_shape_kind(){
		std::uniform_int_distribution<int> distribution{
			0,
			static_cast<int>(debug_shot_shape_kind::compound_convex_polygons)
		};
		return static_cast<debug_shot_shape_kind>(distribution(debug_shot_random_engine_));
	}

	[[nodiscard]] static physics::collision_shape make_debug_compound_convex_shape(){
		physics::collision_shape shape{};
		const std::array left_vertices{
			math::vec2{-30.f, -18.f},
			math::vec2{8.f, -22.f},
			math::vec2{24.f, 4.f},
			math::vec2{-8.f, 22.f},
			math::vec2{-34.f, 8.f}
		};
		const std::array right_vertices{
			math::vec2{-16.f, -18.f},
			math::vec2{28.f, -14.f},
			math::vec2{34.f, 14.f},
			math::vec2{-12.f, 20.f}
		};
		const std::array bottom_vertices{
			math::vec2{-18.f, -12.f},
			math::vec2{18.f, -10.f},
			math::vec2{10.f, 16.f},
			math::vec2{-16.f, 18.f}
		};

		shape.add(physics::shape_of<physics::convex_polygon_shape>{
			.local_transform = {math::vec2{-24.f, -2.f}, -0.18f},
			.shape = physics::make_convex_polygon(left_vertices)
		});
		shape.add(physics::shape_of<physics::convex_polygon_shape>{
			.local_transform = {math::vec2{26.f, 0.f}, 0.12f},
			.shape = physics::make_convex_polygon(right_vertices)
		});
		shape.add(physics::shape_of<physics::convex_polygon_shape>{
			.local_transform = {math::vec2{0.f, 28.f}, 0.08f},
			.shape = physics::make_convex_polygon(bottom_vertices)
		});
		return shape;
	}

	[[nodiscard]] physics::collision_shape make_random_debug_shot_shape(){
		switch(this->random_debug_shot_shape_kind()){
		case debug_shot_shape_kind::circle:
			return physics::make_circle_collision_shape(34.f);
		case debug_shot_shape_kind::capsule:
			return physics::make_capsule_collision_shape({-42.f, 0.f}, {42.f, 0.f}, 17.f);
		case debug_shot_shape_kind::box:
			return physics::make_box_collision_shape(debug_shape_box_half_extent);
		case debug_shot_shape_kind::convex_polygon:{
			const std::array vertices{
				math::vec2{-46.f, -14.f},
				math::vec2{-10.f, -32.f},
				math::vec2{42.f, -18.f},
				math::vec2{34.f, 16.f},
				math::vec2{-22.f, 30.f}
			};
			return physics::make_convex_polygon_collision_shape(vertices);
		}
		case debug_shot_shape_kind::compound_convex_polygons:
			return game_instance::make_debug_compound_convex_shape();
		}

		std::unreachable();
	}

	[[nodiscard]] static std::vector<ecs::chamber::chamber_command> make_solid_grid_test_commands(){
		std::vector<ecs::chamber::chamber_command> commands{};
		commands.reserve(1u);
		commands.push_back(ecs::chamber::place_basic_building_command{
			.region = {
				.src = {0, 0},
				.extent = {solid_grid_test_width, solid_grid_test_height}
			},
			.hit_points = game_instance::solid_grid_test_building_hit_points(),
			.structural = true,
			.structural_support_radius = static_cast<std::uint32_t>(
				std::max(solid_grid_test_width, solid_grid_test_height))
		});
		return commands;
	}

	void spawn_debug_shape_shot(game_world& world){
		ecs::tuple_to_comp_t<debug_shape_entity_desc> components{};
		auto shape = this->make_random_debug_shot_shape();
		const auto direction = debug_shape_direction();
		const auto spawn_position = world_camera_.get_stable_center()
			+ direction * (shape.radius_bound() + debug_shape_spawn_offset);

		auto& motion = components.get<ecs::mech_motion>();
		motion.trans = {spawn_position, direction.angle_rad()};
		motion.vel = {direction * debug_shape_initial_speed, 0.f};

		auto& collider = components.get<ecs::collider>();
		collider.shape = shape.to_record();
		collider.filter.sensor = true;
		collider.ccd = physics::ccd_mode::linear_sweep;
		collider.ccd_threshold = 0.5f;

		auto& body = components.get<ecs::physics_body>().body;
		body = physics::rigid_body::make_dynamic(debug_shape_mass, debug_shape_rotational_inertia);
		body.linear_drag = debug_shape_drag;
		body.angular_drag = debug_shape_drag;
		body.friction = 0.5f;
		body.restitution = 0.05f;
		body.ccd = physics::ccd_mode::linear_sweep;
		body.ccd_threshold = 0.5f;

		auto& drawer = components.get<ecs::collision_shape_drawer>();
		drawer.style.color = {0.95f, 0.72f, 0.22f, 0.95f};
		drawer.style.stroke = 2.f;

		auto& projectile = components.get<ecs::projectile_state>();
		projectile.remaining_lifetime = debug_shape_projectile_lifetime;
		projectile.set_damage({
			.material_damage = {.direct = debug_shape_projectile_damage}
		});

		(void)world.components().spawn<debug_shape_entity_desc>(std::move(components));
		log::debug({"Game"}, "spawn debug shape shot at ({}, {})", spawn_position.x, spawn_position.y);
	}

	void spawn_pending_debug_shape_shots(){
		const unsigned shot_count = std::exchange(pending_debug_shape_shots_, 0u);
		for(unsigned i = 0; i != shot_count; ++i){
			this->spawn_debug_shape_shot(*world_);
		}
	}

	void spawn_debug_scene_shape(
		game_world& world,
		physics::collision_shape shape,
		const math::trans2 transform,
		draw::collision_shape_draw_style style){
		ecs::tuple_to_comp_t<debug_scene_entity_desc> components{};
		auto& motion = components.get<ecs::mech_motion>();
		motion.trans = transform;

		auto& collider = components.get<ecs::collider>();
		collider.shape = shape.to_record();

		components.get<ecs::physics_body>() = ecs::physics_body::make_static();
		auto& drawer = components.get<ecs::collision_shape_drawer>();
		drawer.style = style;
		drawer.screen_clip_margin = 32.f;

		(void)world.components().spawn<debug_scene_entity_desc>(std::move(components));
	}

	void spawn_solid_grid_test_chamber(){
		(void)world_->execute(spawn_chamber_command{
			.extent = {solid_grid_test_width, solid_grid_test_height},
			.position = solid_grid_test_position,
			.faction_id = solid_grid_test_faction_id,
			.initial_commands = game_instance::make_solid_grid_test_commands()
		});
	}

	void spawn_default_debug_scene(){
		if(!world_){
			return;
		}

		draw::collision_shape_draw_style base_style{
			.color = {0.20f, 0.95f, 0.45f, 0.58f},
			.stroke = 2.5f
		};
		draw::collision_shape_draw_style glow_style{
			.color = {3.50f, 1.65f, 0.25f, 0.82f},
			.stroke = 3.5f
		};
		draw::collision_shape_draw_style cool_style{
			.color = {0.25f, 0.55f, 2.80f, 0.50f},
			.stroke = 2.25f
		};

		this->spawn_debug_scene_shape(
			*world_,
			physics::make_box_collision_shape({180.f, 72.f}),
			{{-150.f, -36.f}, 0.18f},
			base_style);
		this->spawn_debug_scene_shape(
			*world_,
			physics::make_capsule_collision_shape({-120.f, 0.f}, {120.f, 0.f}, 34.f),
			{{92.f, 38.f}, -0.34f},
			cool_style);
		this->spawn_debug_scene_shape(
			*world_,
			physics::make_circle_collision_shape(58.f),
			{{-28.f, -18.f}, 0.f},
			glow_style);

		const std::array diamond{
			math::vec2{0.f, -72.f},
			math::vec2{84.f, -8.f},
			math::vec2{36.f, 72.f},
			math::vec2{-76.f, 48.f},
			math::vec2{-98.f, -32.f}
		};
		this->spawn_debug_scene_shape(
			*world_,
			physics::make_convex_polygon_collision_shape(diamond),
			{{170.f, -92.f}, 0.08f},
			draw::collision_shape_draw_style{
				.color = {2.10f, 0.35f, 1.35f, 0.62f},
				.stroke = 3.f
		});

		this->spawn_solid_grid_test_chamber();

		(void)world_->execute(spawn_chamber_command{
			.extent = {180, 80},
			.position = {-260.f, 108.f},
			.rotation = 0.08f,
			.faction_id = 1u,
			.initial_commands = {
				ecs::chamber::place_standard_building_command{
					.type = ecs::chamber::standard_building_type::structural_joint,
					.region = {.src = {0, 30}, .extent = {14, 14}},
					.hit_points = 900.f,
					.structural_support_radius = 180
				},
				ecs::chamber::place_standard_building_command{
					.type = ecs::chamber::standard_building_type::armor,
					.region = {.src = {18, 20}, .extent = {48, 30}},
					.hit_points = 1800.f
				},
				ecs::chamber::place_standard_building_command{
					.type = ecs::chamber::standard_building_type::energy_generator,
					.region = {.src = {74, 30}, .extent = {14, 14}},
					.hit_points = 700.f,
					.energy = {.power = 2}
				},
				ecs::chamber::place_standard_building_command{
					.type = ecs::chamber::standard_building_type::radar,
					.region = {.src = {102, 30}, .extent = {14, 14}},
					.hit_points = 650.f,
					.radar = {.sensor = {.range = ecs::chamber::tiles_to_world_units(180.f)}}
				},
				ecs::chamber::place_standard_building_command{
					.type = ecs::chamber::standard_building_type::turret,
					.region = {.src = {134, 30}, .extent = {16, 16}},
					.hit_points = 850.f,
					.turret = {
						.projectile_speed = ecs::chamber::tiles_to_world_units(180.f),
						.projectile_damage = {.material_damage = {.direct = 120.f}}
					}
				}
			}
		});

		(void)world_->execute(spawn_chamber_command{
			.extent = {150, 70},
			.position = {118.f, -190.f},
			.rotation = -0.22f,
			.faction_id = 2u,
			.initial_commands = {
				ecs::chamber::place_standard_building_command{
					.type = ecs::chamber::standard_building_type::structural_joint,
					.region = {.src = {0, 24}, .extent = {12, 12}},
					.hit_points = 800.f,
					.structural_support_radius = 150
				},
				ecs::chamber::place_standard_building_command{
					.type = ecs::chamber::standard_building_type::armor,
					.region = {.src = {16, 16}, .extent = {38, 28}},
					.hit_points = 1400.f
				},
				ecs::chamber::place_standard_building_command{
					.type = ecs::chamber::standard_building_type::energy_generator,
					.region = {.src = {62, 24}, .extent = {12, 12}},
					.hit_points = 600.f,
					.energy = {.power = 1}
				},
				ecs::chamber::place_standard_building_command{
					.type = ecs::chamber::standard_building_type::radar,
					.region = {.src = {88, 24}, .extent = {12, 12}},
					.hit_points = 560.f,
					.radar = {.sensor = {.range = ecs::chamber::tiles_to_world_units(150.f)}}
				},
				ecs::chamber::place_standard_building_command{
					.type = ecs::chamber::standard_building_type::turret,
					.region = {.src = {118, 24}, .extent = {14, 14}},
					.hit_points = 760.f,
					.turret = {
						.projectile_speed = ecs::chamber::tiles_to_world_units(170.f),
						.projectile_damage = {.material_damage = {.direct = 110.f}}
					}
				}
			}
		});
	}

	[[nodiscard]] static draw::collision_shape_draw_style make_profile_draw_style(
		const std::uint32_t index,
		const float alpha,
		const float stroke,
		const float depth) noexcept{
		const float lane = static_cast<float>(index % 5u);
		return draw::collision_shape_draw_style{
			.color = {
				0.20f + 0.28f * static_cast<float>((index + 1u) % 3u),
				0.45f + 0.16f * lane,
				1.20f + 0.24f * static_cast<float>((index + 2u) % 4u),
				alpha
			},
			.stroke = stroke,
			.depth = depth
		};
	}

	void spawn_profile_render_shapes_scene(const profile::profile_scenario_config& scenario){
		const std::uint32_t count = scenario.shape_count;
		if(count == 0u){
			return;
		}

		const auto columns = static_cast<std::uint32_t>(
			std::ceil(std::sqrt(static_cast<double>(count))));
		constexpr float spacing = 52.f;
		const float half_columns = (static_cast<float>(columns) - 1.f) * 0.5f;
		const std::array polygon_vertices{
			math::vec2{-17.f, -13.f},
			math::vec2{11.f, -18.f},
			math::vec2{22.f, 6.f},
			math::vec2{4.f, 21.f},
			math::vec2{-20.f, 10.f}
		};

		for(std::uint32_t index = 0; index != count; ++index){
			const auto column = static_cast<float>(index % columns);
			const auto row = static_cast<float>(index / columns);
			const math::vec2 position{
				(column - half_columns) * spacing,
				(row - half_columns) * spacing
			};
			const float rotation = static_cast<float>((index + scenario.seed) % 19u) * 0.047f;
			auto style = game_instance::make_profile_draw_style(index, 0.68f, 1.75f, static_cast<float>(index % 16u));
			physics::collision_shape shape{};
			switch(index % 4u){
			case 0u:
				shape = physics::make_box_collision_shape({18.f, 10.f + static_cast<float>(index % 7u)});
				break;
			case 1u:
				shape = physics::make_circle_collision_shape(10.f + static_cast<float>(index % 11u));
				break;
			case 2u:
				shape = physics::make_capsule_collision_shape({-18.f, 0.f}, {18.f, 0.f}, 6.f + static_cast<float>(index % 5u));
				break;
			default:
				shape = physics::make_convex_polygon_collision_shape(polygon_vertices);
				style.stroke = 2.25f;
				break;
			}
			this->spawn_debug_scene_shape(*world_, std::move(shape), {position, rotation}, style);
		}
	}

	void spawn_profile_dynamic_body(
		const std::uint32_t index,
		const math::vec2 position,
		const math::vec2 velocity){
		ecs::tuple_to_comp_t<debug_scene_entity_desc> components{};
		auto& motion = components.get<ecs::mech_motion>();
		motion.trans = {position, static_cast<float>(index % 13u) * 0.08f};
		motion.vel.vec = velocity;

		auto& collider = components.get<ecs::collider>();
		collider.shape = (index % 2u == 0u
			? physics::make_circle_collision_shape(13.f)
			: physics::make_box_collision_shape({14.f, 11.f})).to_record();
		collider.ccd = physics::ccd_mode::linear_sweep;
		collider.ccd_threshold = 0.5f;

		auto& body = components.get<ecs::physics_body>();
		body = ecs::physics_body::make_dynamic(18.f, 2200.f);
		body.body.linear_drag = 0.08f;
		body.body.angular_drag = 0.08f;
		body.body.friction = 0.35f;
		body.body.restitution = 0.15f;
		body.body.ccd = physics::ccd_mode::linear_sweep;
		body.body.ccd_threshold = 0.5f;

		auto& drawer = components.get<ecs::collision_shape_drawer>();
		drawer.style = game_instance::make_profile_draw_style(index, 0.58f, 1.6f, 2.f);
		drawer.screen_clip_margin = 48.f;

		(void)world_->components().spawn<debug_scene_entity_desc>(std::move(components));
	}

	void spawn_profile_projectile(
		const std::uint32_t index,
		const math::vec2 position,
		const math::vec2 velocity,
		const float direct_damage = 8.f,
		const physics::collision_filter filter = {}){
		ecs::tuple_to_comp_t<debug_shape_entity_desc> components{};
		auto& motion = components.get<ecs::mech_motion>();
		motion.trans = {position, velocity.angle_rad()};
		motion.vel.vec = velocity;

		auto& collider = components.get<ecs::collider>();
		collider.shape = physics::make_capsule_collision_shape({-10.f, 0.f}, {10.f, 0.f}, 5.f).to_record();
		collider.filter = filter;
		collider.ccd = physics::ccd_mode::linear_sweep;
		collider.ccd_threshold = 0.5f;

		auto& body = components.get<ecs::physics_body>();
		body = ecs::physics_body::make_dynamic(5.f, 850.f);
		body.body.linear_drag = 0.02f;
		body.body.angular_drag = 0.02f;
		body.body.friction = 0.20f;
		body.body.restitution = 0.05f;
		body.body.ccd = physics::ccd_mode::linear_sweep;
		body.body.ccd_threshold = 0.5f;

		auto& drawer = components.get<ecs::collision_shape_drawer>();
		drawer.style = game_instance::make_profile_draw_style(index + 1000u, 0.86f, 1.35f, 4.f);
		drawer.screen_clip_margin = 48.f;

		auto& projectile = components.get<ecs::projectile_state>();
		projectile.remaining_lifetime = std::numeric_limits<float>::infinity();
		projectile.expired_on_hit = false;
		projectile.set_damage({
			.material_damage = {.direct = direct_damage}
		});

		(void)world_->components().spawn<debug_shape_entity_desc>(std::move(components));
	}

	[[nodiscard]] static std::vector<ecs::chamber::chamber_command> make_profile_damage_grid_commands(
		const math::point2 extent,
		const float tile_hit_points){
		std::vector<ecs::chamber::chamber_command> commands{};
		commands.reserve(1u);
		commands.push_back(ecs::chamber::place_basic_building_command{
			.region = {
				.src = {0, 0},
				.extent = extent
			},
			.hit_points = static_cast<float>(extent.x * extent.y) * tile_hit_points * 0.5f,
			.structural = true,
			.structural_support_radius = static_cast<std::uint32_t>(std::max(extent.x, extent.y))
		});
		return commands;
	}

	void spawn_profile_damage_grid_chamber(
		const std::uint32_t index,
		const math::vec2 center,
		const math::point2 extent){
		(void)world_->execute(spawn_chamber_command{
			.extent = extent,
			.position = center - math::vec2{
				ecs::chamber::tiles_to_world_units(static_cast<float>(extent.x) * 0.5f),
				ecs::chamber::tiles_to_world_units(static_cast<float>(extent.y) * 0.5f)
			},
			.rotation = static_cast<float>(index % 5u) * 0.025f,
			.faction_id = 10u + index,
			.filter = {
				.category = 2u,
				.mask = 4u
			},
			.ccd = physics::ccd_mode::linear_sweep,
			.initial_commands = game_instance::make_profile_damage_grid_commands(extent, 100.f)
		});
	}

	void spawn_profile_physics_projectiles_scene(const profile::profile_scenario_config& scenario){
		const std::uint32_t body_count = scenario.body_count;
		const std::uint32_t projectile_count = scenario.projectile_count;
		const auto body_columns = static_cast<std::uint32_t>(
			std::ceil(std::sqrt(static_cast<double>(std::max(body_count, 1u)))));

		for(std::uint32_t index = 0; index != body_count; ++index){
			const auto column = static_cast<float>(index % body_columns);
			const auto row = static_cast<float>(index / body_columns);
			const math::vec2 position{
				(column - static_cast<float>(body_columns) * 0.5f) * 34.f,
				(row - static_cast<float>(body_columns) * 0.5f) * 34.f
			};
			const float side = index % 2u == 0u ? 1.f : -1.f;
			const math::vec2 velocity{side * (32.f + static_cast<float>(index % 9u) * 4.f), 18.f - static_cast<float>(index % 7u) * 6.f};
			this->spawn_profile_dynamic_body(index, position, velocity);
		}

		constexpr math::point2 chamber_extent{72, 72};
		constexpr std::uint32_t chamber_count = 7u;
		for(std::uint32_t index = 0; index != chamber_count; ++index){
			const float row = static_cast<float>(index) - (static_cast<float>(chamber_count) - 1.f) * 0.5f;
			this->spawn_profile_damage_grid_chamber(
				index,
				{0.f, ecs::chamber::tiles_to_world_units(row * 104.f)},
				chamber_extent);
		}
		if(profile_session_ != nullptr){
			profile_session_->write_counter(0, "scenario.damage_grid_chamber_count", chamber_count);
			profile_session_->write_counter(
				0,
				"scenario.damage_grid_tile_count",
				static_cast<std::uint64_t>(chamber_count * chamber_extent.x * chamber_extent.y));
		}

		const std::uint32_t chamber_projectile_count = projectile_count / 2u;
		for(std::uint32_t index = 0; index != projectile_count; ++index){
			const bool chamber_targeting = index < chamber_projectile_count;
			const auto lane_index = chamber_targeting
				? index % chamber_count
				: index % 35u;
			const float lane = static_cast<float>(lane_index) - (chamber_targeting ? (static_cast<float>(chamber_count) - 1.f) * 0.5f : 17.f);
			const bool from_left = index % 2u == 0u;
			const float chamber_launch_distance = ecs::chamber::tiles_to_world_units(
				900.f + static_cast<float>(index / chamber_count) * 45.f);
			const math::vec2 position = chamber_targeting
				? math::vec2{
					from_left ? -chamber_launch_distance : chamber_launch_distance,
					ecs::chamber::tiles_to_world_units(lane * 104.f)
				}
				: math::vec2{
					from_left ? -520.f : 520.f,
					lane * 20.f
				};
			const math::vec2 velocity = chamber_targeting
				? math::vec2{
					from_left ? ecs::chamber::tiles_to_world_units(420.f) : -ecs::chamber::tiles_to_world_units(420.f),
					0.f
				}
				: math::vec2{
					from_left ? 440.f : -440.f,
					(static_cast<float>((index + scenario.seed) % 11u) - 5.f) * 10.f
				};
			const physics::collision_filter filter{
				.category = 4u,
				.mask = chamber_targeting ? 2u : 1u
			};
			this->spawn_profile_projectile(index, position, velocity, 5000.f, filter);
		}
	}

	void spawn_profile_scene(const profile::profile_scenario_config& scenario){
		switch(scenario.name){
		case profile::profile_scenario_name::baseline:
			this->spawn_default_debug_scene();
			break;
		case profile::profile_scenario_name::render_shapes:
			this->spawn_profile_render_shapes_scene(scenario);
			break;
		case profile::profile_scenario_name::physics_projectiles:
			this->spawn_profile_physics_projectiles_scene(scenario);
			break;
		}

		if(profile_session_ != nullptr){
			profile_session_->write_counter(0, "scenario.shape_count_config", scenario.shape_count);
			profile_session_->write_counter(0, "scenario.projectile_count_config", scenario.projectile_count);
			profile_session_->write_counter(0, "scenario.body_count_config", scenario.body_count);
		}
	}

	void process_event(const input_handle::input_event_variant event) noexcept{
		camera_binding_.on_event(world_camera_, event);
		if(game_instance::is_debug_shape_shot_event(event)){
			pending_debug_shape_shots_ = std::min(pending_debug_shape_shots_ + 1u, max_pending_debug_shape_shots);
		}
	}

	void advance_fixed_game_step(const float fixed_step){
		const std::uint64_t profile_frame = render_frame_index_ + 1u;
		{
			profile::profile_scope_timer timer{profile_session_.get(), profile_frame, "game.fixed_step.begin"};
			world_->begin_step(fixed_step);
			this->drain_commands();
			this->spawn_pending_debug_shape_shots();
		}
		world_->run_systems(profile_session_.get(), profile_frame);
		{
			profile::profile_scope_timer timer{profile_session_.get(), profile_frame, "game.camera.update"};
			world_camera_.update(fixed_step * 60.f);
		}
		++simulation_tick_;
		if(profile_session_ != nullptr){
			profile_session_->write_counter(profile_frame, "game.simulation_tick", simulation_tick_);
		}
	}

	void advance_game_time(const float delta_seconds){
		this->consume_render_extent_request();
		this->drain_events();

		const float fixed_step = config_.fixed_step_seconds;
		if(fixed_step <= 0.f || config_.max_steps_per_frame == 0){
			return;
		}

		const float max_accumulated = fixed_step * static_cast<float>(config_.max_steps_per_frame);
		accumulated_seconds_ = std::min(accumulated_seconds_ + this->clamped_frame_delta(delta_seconds), max_accumulated);

		unsigned steps{};
		while(accumulated_seconds_ + std::numeric_limits<float>::epsilon() >= fixed_step
			&& steps < config_.max_steps_per_frame){
			this->advance_fixed_game_step(fixed_step);
			accumulated_seconds_ -= fixed_step;
			++steps;
		}
	}

	[[nodiscard]] double simulation_time_seconds() const noexcept{
		return static_cast<double>(simulation_tick_) * static_cast<double>(config_.fixed_step_seconds);
	}

	[[nodiscard]] game_render_frame_state make_render_frame_state(const math::vec2 extent){
		game_render_frame_state state{
			.camera = world_camera_,
			.extent = extent,
			.effects = effect_config_,
			.frame_index = ++render_frame_index_,
			.simulation_tick = simulation_tick_,
			.simulation_time_seconds = this->simulation_time_seconds()
		};
		state.camera.resize_screen(extent.x, extent.y);
		state.camera.update(0.f);
		state.viewport = state.camera.get_viewport();
		return state;
	}

	[[nodiscard]] static bool should_draw_bounds(
		const game_draw_cull_bounds& bounds,
		const game_render_frame_state& state) noexcept{
		if(!bounds.enabled){
			return false;
		}

		const float camera_scale = std::max(state.camera.get_scale(), 0.0001f);
		const float world_margin = std::max(bounds.screen_clip_margin, 0.f) / camera_scale;
		return bounds.world_aabb.copy().expand(world_margin, world_margin).overlap_inclusive(state.viewport);
	}

	template <typename Drawer>
	void draw_component_pass(
		gui::renderer_frontend& renderer,
		game_render_frame_stats& stats){
		world_->components().each([&](
			const ecs::chunk_meta& meta,
			const Drawer& drawable){
			const ecs::entity_id entity = meta.id();
			if(!entity || !entity.is_inserted()){
				return;
			}

			++stats.drawable_visited;
			game_draw_context context{
				.manager = world_->components(),
				.entity = entity,
				.frame = stats.frame
			};
			const game_draw_cull_bounds bounds = drawable.cull_bounds(context);
			if(!game_instance::should_draw_bounds(bounds, stats.frame)){
				++stats.drawable_culled;
				return;
			}

			drawable.draw(renderer, context);
			++stats.drawable_drawn;
		});
	}

	[[nodiscard]] game_render_frame_stats draw_render_frame_locked(
		gui::renderer_frontend& renderer,
		game_render_frame_state frame){
		game_render_frame_stats stats{
			.frame = std::move(frame)
		};

		this->draw_component_pass<ecs::collision_shape_drawer>(renderer, stats);
		this->draw_component_pass<ecs::chamber_drawer>(renderer, stats);

		log::trace(
			{"Game"},
			"render frame={} tick={} time={} drawables={} drawn={} culled={} camera_scale={}",
			stats.frame.frame_index,
			stats.frame.simulation_tick,
			stats.frame.simulation_time_seconds,
			stats.drawable_visited,
			stats.drawable_drawn,
			stats.drawable_culled,
			stats.frame.camera.get_scale());
		if(profile_session_ != nullptr){
			profile_session_->write_counter(
				stats.frame.frame_index,
				"renderer.drawable_visited",
				static_cast<std::uint64_t>(stats.drawable_visited));
			profile_session_->write_counter(
				stats.frame.frame_index,
				"renderer.drawable_drawn",
				static_cast<std::uint64_t>(stats.drawable_drawn));
			profile_session_->write_counter(
				stats.frame.frame_index,
				"renderer.drawable_culled",
				static_cast<std::uint64_t>(stats.drawable_culled));
		}
		return stats;
	}

public:
	[[nodiscard]] explicit game_instance(game_instance_config config = {})
		: config_(config),
		  debug_shot_random_engine_(config_.debug_random_seed){
		configure_camera();
	}

	game_instance(const game_instance&) = delete;
	game_instance(game_instance&&) = delete;
	game_instance& operator=(const game_instance&) = delete;
	game_instance& operator=(game_instance&&) = delete;

	~game_instance(){
		this->shutdown();
	}

	void configure_profile(
		profile::profile_scenario_config scenario,
		std::shared_ptr<profile::profile_session> session){
		if(initialized_.load(std::memory_order_acquire)){
			throw std::logic_error{"profile must be configured before game_instance initialization"};
		}
		std::lock_guard lock{game_state_mutex_};
		if(initialized_.load(std::memory_order_acquire)){
			throw std::logic_error{"profile must be configured before game_instance initialization"};
		}
		profile_scenario_ = scenario;
		profile_session_ = std::move(session);
	}

	void initialize(){
		{
			std::lock_guard lock{game_state_mutex_};
			if(!initialized_.load(std::memory_order_acquire)){
				log::info({"Game"}, "initialize state");
				this->initialize_state(true);
			}
		}
	}

	void shutdown(){
		log::info({"Game"}, "shutdown");
		{
			std::lock_guard lock{command_mutex_};
			pending_commands_.clear();
		}
		{
			std::lock_guard event_lock{event_mutex_};
			pending_events_.clear();
		}
		{
			std::lock_guard lock{game_state_mutex_};
			pending_debug_shape_shots_ = 0;
			initialized_.store(false, std::memory_order_release);
		}
	}

	void reset(){
		log::info({"Game"}, "reset");
		{
			std::lock_guard lock{command_mutex_};
			pending_commands_.clear();
		}

		{
			std::lock_guard lock{event_mutex_};
			pending_events_.clear();
		}

		{
			std::lock_guard lock{game_state_mutex_};
			world_.emplace();
			accumulated_seconds_ = 0.f;
			simulation_tick_ = 0;
			render_extent_ = {};
			pending_debug_shape_shots_ = 0;
			render_frame_index_ = 0;
			debug_shot_random_engine_.seed(config_.debug_random_seed);
			camera_binding_.reset_state();
			camera_binding_.reset_camera(world_camera_);
			this->configure_camera();
			initialized_.store(true, std::memory_order_release);
		}

		{
			std::lock_guard lock{render_extent_mutex_};
			pending_render_extent_ = {};
		}
	}

	void post_command(game_world_command command){
		std::lock_guard lock{command_mutex_};
		pending_commands_.push_back(std::move(command));
	}

	void handle_event(const input_handle::input_event_variant event) noexcept{
		std::lock_guard lock{event_mutex_};
		pending_events_.push_back(event);
	}

	void update(const float delta_seconds){
		this->ensure_initialized_state();
		std::lock_guard lock{game_state_mutex_};
		const std::uint64_t profile_frame = render_frame_index_ + 1u;
		if(profile_session_ != nullptr){
			profile::profile_scope_timer timer{profile_session_.get(), profile_frame, "game.update.advance"};
			this->advance_game_time(delta_seconds);
		}else{
			this->advance_game_time(delta_seconds);
		}
	}

	void update(const double delta_seconds){
		update(static_cast<float>(delta_seconds));
	}

	void update_for_render(const math::vec2 extent, const float delta_seconds){
		this->ensure_initialized_state();
		if(extent.x > 0.f && extent.y > 0.f){
			this->request_render_extent(extent);
		}

		std::lock_guard lock{game_state_mutex_};
		const std::uint64_t profile_frame = render_frame_index_ + 1u;
		if(profile_session_ != nullptr){
			profile::profile_scope_timer timer{profile_session_.get(), profile_frame, "game.update_for_render.advance"};
			this->advance_game_time(delta_seconds);
		}else{
			this->advance_game_time(delta_seconds);
		}
	}

	void update_for_render(const math::vec2 extent, const double delta_seconds){
		this->update_for_render(extent, static_cast<float>(delta_seconds));
	}

	void render(const math::vec2 extent){
		this->ensure_initialized_state();

		if(extent.x <= 0.f || extent.y <= 0.f){
			return;
		}

		this->request_render_extent(extent);
	}

	[[nodiscard]] game_render_frame_stats render_frame(gui::renderer_frontend& renderer, const math::vec2 extent){
		this->ensure_initialized_state();
		if(extent.x <= 0.f || extent.y <= 0.f){
			return {.frame = {.extent = extent}};
		}

		std::lock_guard lock{game_state_mutex_};
		this->resize_camera_if_needed(extent);
		game_render_frame_state frame = this->make_render_frame_state(extent);
		auto* profile_session = profile_session_.get();
		if(profile_session != nullptr){
			const std::uint64_t profile_frame = frame.frame_index;
			profile::profile_scope_timer timer{profile_session, profile_frame, "renderer.build_draw_list"};
			return this->draw_render_frame_locked(renderer, std::move(frame));
		}
		return this->draw_render_frame_locked(renderer, std::move(frame));
	}

	[[nodiscard]] game_render_frame_stats render_frame(game_2d_renderer& renderer, const math::vec2 extent){
		this->ensure_initialized_state();
		if(extent.x <= 0.f || extent.y <= 0.f){
			return {.frame = {.extent = extent}};
		}

		std::lock_guard lock{game_state_mutex_};
		this->resize_camera_if_needed(extent);
		game_render_frame_state frame = this->make_render_frame_state(extent);
		renderer.begin_frame(frame);
		gui::renderer_frontend frontend = renderer.create_frontend();
		game_render_frame_stats stats{};
		auto* profile_session = profile_session_.get();
		if(profile_session != nullptr){
			profile::profile_scope_timer timer{profile_session, frame.frame_index, "renderer.build_draw_list"};
			stats = this->draw_render_frame_locked(frontend, std::move(frame));
		}else{
			stats = this->draw_render_frame_locked(frontend, std::move(frame));
		}
		renderer.end_frame(stats.frame);
		if(stats.valid_extent()){
			renderer.set_last_frame_stats(stats);
			renderer.commit_prepared_frame();
		}
		return stats;
	}

	[[nodiscard]] game_world& world() noexcept{
		return *world_;
	}

	[[nodiscard]] const game_world& world() const noexcept{
		return *world_;
	}

	[[nodiscard]] ecs::component_manager& components() noexcept{
		return world_->components();
	}

	[[nodiscard]] const ecs::component_manager& components() const noexcept{
		return world_->components();
	}

	[[nodiscard]] graphic::camera2& camera() noexcept{
		return world_camera_;
	}

	[[nodiscard]] const graphic::camera2& camera() const noexcept{
		return world_camera_;
	}
};
}
