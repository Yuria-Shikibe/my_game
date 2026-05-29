export module mo_yanxi.game.instance;

export import mo_yanxi.game.runtime.render_gui;
export import mo_yanxi.game.runtime.world;
export import mo_yanxi.input_handle.input_event_queue;

import mo_yanxi.graphic.camera;
import mo_yanxi.input_handle;
import mo_yanxi.math.vector2;
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
	std::mutex command_mutex_{};
	std::vector<game_command> pending_commands_{};
	float accumulated_seconds_{};
	graphic::camera2 world_camera_{};
	debug_camera2_binding camera_binding_{};
	gui_debug_render_system debug_renderer_{};
	math::vec2 render_extent_{};
	unsigned pending_debug_box_shots_{};
	bool initialized_{};

	using debug_box_entity_desc = std::tuple<
		ecs::chunk_meta,
		ecs::mech_motion,
		ecs::collider,
		ecs::physics_body,
		ecs::collision_shape_drawer
	>;

	static constexpr unsigned max_pending_debug_box_shots = 64;
	static constexpr math::vec2 debug_box_half_extent{48.f, 16.f};
	static constexpr float debug_box_spawn_offset{72.f};
	static constexpr float debug_box_initial_speed{900.f};
	static constexpr float debug_box_mass{20.f};
	static constexpr float debug_box_rotational_inertia{18000.f};
	static constexpr float debug_box_drag{1.2f};

	void configure_camera() noexcept{
		world_camera_.set_scale_range({config_.camera_min_scale, config_.camera_max_scale});
	}

	void ensure_initialized(){
		if(!initialized_){
			initialize();
		}
	}

	void resize_camera_if_needed(const math::vec2 extent) noexcept{
		if(render_extent_ == extent){
			return;
		}

		render_extent_ = extent;
		world_camera_.resize_screen(extent.x, extent.y);
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

	[[nodiscard]] static bool is_debug_box_shot_event(const input_handle::input_event_variant event) noexcept{
		return event.type == input_handle::input_event_type::input_key
			&& event.input_key.action == input_handle::act::press
			&& event.input_key.as_key() == input_handle::key::q;
	}

	[[nodiscard]] math::vec2 debug_box_direction() const noexcept{
		const auto origin = world_camera_.get_stable_center();
		auto direction = camera_binding_.cursor_world(world_camera_).value_or(origin + math::vec2{1.f, 0.f}) - origin;
		if(direction.length2() <= 0.0001f){
			return {1.f, 0.f};
		}

		return direction.normalize();
	}

	void spawn_debug_box_shot(game_world& world) const{
		ecs::tuple_to_comp_t<debug_box_entity_desc> components{};
		const auto direction = debug_box_direction();
		const auto spawn_position = world_camera_.get_stable_center()
			+ direction * (debug_box_half_extent.x + debug_box_spawn_offset);
		auto shape = physics::make_box_collision_shape(debug_box_half_extent);

		auto& motion = components.get<ecs::mech_motion>();
		motion.trans = {spawn_position, direction.angle_rad()};
		motion.vel = {direction * debug_box_initial_speed, 0.f};

		auto& collider = components.get<ecs::collider>();
		collider.shape = shape.to_record();
		collider.ccd = physics::ccd_mode::linear_sweep;
		collider.ccd_threshold = 0.5f;

		auto& body = components.get<ecs::physics_body>().body;
		body = physics::rigid_body::make_dynamic(debug_box_mass, debug_box_rotational_inertia);
		body.linear_drag = debug_box_drag;
		body.angular_drag = debug_box_drag;
		body.friction = 0.5f;
		body.restitution = 0.05f;
		body.ccd = physics::ccd_mode::linear_sweep;
		body.ccd_threshold = 0.5f;

		auto& drawer = components.get<ecs::collision_shape_drawer>();
		drawer.style.color = {0.95f, 0.72f, 0.22f, 0.95f};
		drawer.style.stroke = 2.f;

		world.components().create_entity_deferred<debug_box_entity_desc>(std::move(components));
	}

	void spawn_pending_debug_box_shots(){
		const unsigned shot_count = std::exchange(pending_debug_box_shots_, 0u);
		for(unsigned i = 0; i != shot_count; ++i){
			spawn_debug_box_shot(*world_);
		}
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

	void initialize(){
		configure_camera();
		initialized_ = true;
	}

	void shutdown(){
		std::lock_guard lock{command_mutex_};
		pending_commands_.clear();
		pending_debug_box_shots_ = 0;
		initialized_ = false;
	}

	void reset(){
		{
			std::lock_guard lock{command_mutex_};
			pending_commands_.clear();
		}

		world_.emplace();
		accumulated_seconds_ = 0.f;
		render_extent_ = {};
		pending_debug_box_shots_ = 0;
		camera_binding_.reset_state();
		camera_binding_.reset_camera(world_camera_);
		configure_camera();
		initialized_ = true;
	}

	void post_command(game_command command){
		if(!command){
			return;
		}

		std::lock_guard lock{command_mutex_};
		pending_commands_.push_back(std::move(command));
	}

	void handle_event(const input_handle::input_event_variant event) noexcept{
		camera_binding_.on_event(world_camera_, event);
		if(game_instance::is_debug_box_shot_event(event)){
			pending_debug_box_shots_ = std::min(pending_debug_box_shots_ + 1u, max_pending_debug_box_shots);
		}
	}

	void update(const float delta_seconds){
		ensure_initialized();

		const float fixed_step = config_.fixed_step_seconds;
		if(fixed_step <= 0.f || config_.max_steps_per_frame == 0){
			return;
		}

		const float max_accumulated = fixed_step * static_cast<float>(config_.max_steps_per_frame);
		accumulated_seconds_ = std::min(accumulated_seconds_ + clamped_frame_delta(delta_seconds), max_accumulated);

		unsigned steps{};
		while(accumulated_seconds_ + std::numeric_limits<float>::epsilon() >= fixed_step
			&& steps < config_.max_steps_per_frame){
			world_->begin_step(fixed_step);
			drain_commands();
			spawn_pending_debug_box_shots();
			world_->run_systems();
			accumulated_seconds_ -= fixed_step;
			++steps;
		}
	}

	void update(const double delta_seconds){
		update(static_cast<float>(delta_seconds));
	}

	void render(game_render_context& context){
		ensure_initialized();

		const auto extent = context.extent();
		if(extent.x <= 0.f || extent.y <= 0.f){
			return;
		}

		resize_camera_if_needed(extent);
		world_camera_.update(static_cast<float>(context.delta_seconds * 60.0));
		debug_renderer_.draw(context, world_camera_);
		world_->systems.collision_shape_draw.draw(context, world_->components(), world_camera_);
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
