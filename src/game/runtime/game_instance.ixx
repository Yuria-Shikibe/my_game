export module mo_yanxi.game.instance;

export import mo_yanxi.game.runtime.game_renderer;
export import mo_yanxi.game.runtime.world;
export import mo_yanxi.input_handle.input_event_queue;

import mo_yanxi.graphic.camera;
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
};

export
using game_command = std::move_only_function<void(game_world&)>;

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
	std::optional<game_world> world_{std::in_place};
	std::mutex game_state_mutex_{};
	std::mutex command_mutex_{};
	std::vector<game_command> pending_commands_{};
	std::mutex event_mutex_{};
	std::vector<input_handle::input_event_variant> pending_events_{};
	std::mutex render_extent_mutex_{};
	math::vec2 pending_render_extent_{};
	mutable std::mutex render_snapshot_mutex_{};
	game_render_snapshot render_snapshot_{};
	std::jthread game_thread_{};
	float accumulated_seconds_{};
	graphic::camera2 world_camera_{};
	debug_camera2_binding camera_binding_{};
	math::vec2 render_extent_{};
	unsigned pending_debug_shape_shots_{};
	std::uint64_t render_frame_index_{};
	game_render_effect_config effect_config_{};
	std::atomic_bool initialized_{};

	using debug_shape_entity_desc = std::tuple<
		ecs::chunk_meta,
		ecs::mech_motion,
		ecs::collider,
		ecs::physics_body,
		ecs::collision_shape_drawer
	>;

	using debug_scene_entity_desc = debug_shape_entity_desc;

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

	void configure_camera() noexcept{
		world_camera_.set_scale_range({config_.camera_min_scale, config_.camera_max_scale});
	}

	void initialize_state(const bool spawn_demo_scene){
		this->configure_camera();
		initialized_.store(true, std::memory_order_release);
		if(spawn_demo_scene){
			this->spawn_default_debug_scene();
			world_->components().commit();
		}
		this->publish_render_snapshot(this->make_render_snapshot());
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
		effect_config_.oit_layout = mo_yanxi::game::make_game_oit_buffer_layout(extent);
		effect_config_.ssao_kernel = mo_yanxi::game::make_game_ssao_kernel(extent, effect_config_.ssao_hint_scale);
		log::info(
			{"Game"},
			"render extent changed: ({}, {}) tile_layout={}x{} ssao_samples={}",
			extent.x,
			extent.y,
			effect_config_.oit_layout.width,
			effect_config_.oit_layout.height,
			effect_config_.ssao_kernel.sample_count);
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
		std::vector<game_command> commands{};
		{
			std::lock_guard lock{command_mutex_};
			commands.swap(pending_commands_);
		}

		for(auto& command : commands){
			if(command){
				std::invoke(command, *world_);
			}
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

	[[nodiscard]] static std::mt19937& debug_shot_random_engine(){
		static thread_local std::mt19937 engine{std::random_device{}()};
		return engine;
	}

	[[nodiscard]] static debug_shot_shape_kind random_debug_shot_shape_kind(){
		std::uniform_int_distribution<int> distribution{
			0,
			static_cast<int>(debug_shot_shape_kind::compound_convex_polygons)
		};
		return static_cast<debug_shot_shape_kind>(distribution(game_instance::debug_shot_random_engine()));
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

	[[nodiscard]] static physics::collision_shape make_random_debug_shot_shape(){
		switch(game_instance::random_debug_shot_shape_kind()){
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

	void spawn_debug_shape_shot(game_world& world) const{
		ecs::tuple_to_comp_t<debug_shape_entity_desc> components{};
		auto shape = game_instance::make_random_debug_shot_shape();
		const auto direction = debug_shape_direction();
		const auto spawn_position = world_camera_.get_stable_center()
			+ direction * (shape.radius_bound() + debug_shape_spawn_offset);

		auto& motion = components.get<ecs::mech_motion>();
		motion.trans = {spawn_position, direction.angle_rad()};
		motion.vel = {direction * debug_shape_initial_speed, 0.f};

		auto& collider = components.get<ecs::collider>();
		collider.shape = shape.to_record();
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
	}

	void process_event(const input_handle::input_event_variant event) noexcept{
		camera_binding_.on_event(world_camera_, event);
		if(game_instance::is_debug_shape_shot_event(event)){
			pending_debug_shape_shots_ = std::min(pending_debug_shape_shots_ + 1u, max_pending_debug_shape_shots);
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
			world_->begin_step(fixed_step);
			this->drain_commands();
			this->spawn_pending_debug_shape_shots();
			world_->run_systems();
			accumulated_seconds_ -= fixed_step;
			++steps;
		}

		world_camera_.update(delta_seconds * 60.f);
	}

	[[nodiscard]] game_render_snapshot make_render_snapshot(){
		game_render_snapshot snapshot{};
		snapshot.camera = world_camera_;
		snapshot.effects = effect_config_;
		snapshot.frame_index = ++render_frame_index_;

		world_->components().each([&](
			const ecs::chunk_meta& meta,
			const ecs::collider& collider,
			const ecs::mech_motion& motion,
			const ecs::collision_shape_drawer& drawer){
			if(!meta.id() || !meta.id().is_inserted() || !drawer.enabled || !collider.enabled || collider.shape.empty()){
				return;
			}

			auto style = drawer.style;
			style.depth += motion.depth + collider.depth;
			const auto transform = collider.world_transform(motion);
			snapshot.collision_shapes.push_back(game_collision_shape_render_item{
				.shape = collider.shape,
				.transform = transform,
				.aabb = collider.shape.aabb(transform),
				.style = style,
				.screen_clip_margin = drawer.screen_clip_margin
			});
		});

		log::trace(
			{"Game"},
			"snapshot frame={} collision_shapes={} camera_scale={}",
			snapshot.frame_index,
			snapshot.collision_shapes.size(),
			snapshot.camera.get_scale());
		return snapshot;
	}

	void publish_render_snapshot(game_render_snapshot snapshot){
		std::lock_guard lock{render_snapshot_mutex_};
		render_snapshot_ = std::move(snapshot);
	}

	[[nodiscard]] game_render_snapshot copy_render_snapshot() const{
		std::lock_guard lock{render_snapshot_mutex_};
		return render_snapshot_;
	}

	void game_thread_main(const std::stop_token stop_token){
		using clock = std::chrono::steady_clock;
		auto last_time = clock::now();

		while(!stop_token.stop_requested()){
			const auto now = clock::now();
			const auto delta = std::chrono::duration<float>(now - last_time).count();
			last_time = now;

			game_render_snapshot snapshot{};
			{
				std::lock_guard lock{game_state_mutex_};
				this->advance_game_time(delta);
				snapshot = this->make_render_snapshot();
			}
			this->publish_render_snapshot(std::move(snapshot));

			std::this_thread::sleep_for(std::chrono::milliseconds{1});
		}
	}

	void start_game_thread(){
		if(game_thread_.joinable()){
			return;
		}

		game_thread_ = std::jthread{[this](const std::stop_token stop_token){
			this->game_thread_main(stop_token);
		}};
	}

	void stop_game_thread(){
		if(!game_thread_.joinable()){
			return;
		}

		game_thread_.request_stop();
		game_thread_.join();
	}

public:
	[[nodiscard]] explicit game_instance(game_instance_config config = {})
		: config_(config){
		configure_camera();
	}

	game_instance(const game_instance&) = delete;
	game_instance(game_instance&&) = delete;
	game_instance& operator=(const game_instance&) = delete;
	game_instance& operator=(game_instance&&) = delete;

	~game_instance(){
		this->shutdown();
	}

	void initialize(){
		{
			std::lock_guard lock{game_state_mutex_};
			if(!initialized_.load(std::memory_order_acquire)){
				log::info({"Game"}, "initialize state");
				this->initialize_state(true);
			}
		}
		this->start_game_thread();
	}

	void shutdown(){
		log::info({"Game"}, "shutdown");
		this->stop_game_thread();
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
		const bool should_restart_thread = game_thread_.joinable();
		this->stop_game_thread();
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
			render_extent_ = {};
			pending_debug_shape_shots_ = 0;
			render_frame_index_ = 0;
			camera_binding_.reset_state();
			camera_binding_.reset_camera(world_camera_);
			this->configure_camera();
			initialized_.store(true, std::memory_order_release);
			this->publish_render_snapshot(this->make_render_snapshot());
		}

		{
			std::lock_guard lock{render_extent_mutex_};
			pending_render_extent_ = {};
		}

		if(should_restart_thread){
			this->start_game_thread();
		}
	}

	void post_command(game_command command){
		if(!command){
			return;
		}

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
		this->advance_game_time(delta_seconds);
		this->publish_render_snapshot(this->make_render_snapshot());
	}

	void update(const double delta_seconds){
		update(static_cast<float>(delta_seconds));
	}

	void render(const math::vec2 extent){
		this->initialize();

		if(extent.x <= 0.f || extent.y <= 0.f){
			return;
		}

		this->request_render_extent(extent);
	}

	[[nodiscard]] game_render_snapshot latest_render_snapshot() const{
		return this->copy_render_snapshot();
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
