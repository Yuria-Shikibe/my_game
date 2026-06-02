module mo_yanxi.game.ui.collision_shape_editor;

import std;
import align;
import mo_yanxi.game.physics.collision_shape_editor_metadata;
import mo_yanxi.game.runtime.draw.collision_shape;
import mo_yanxi.graphic.color;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.graphic.image_atlas;
import mo_yanxi.gui.compound.file_selector;
import mo_yanxi.gui.compound.numeric_input_area;
import mo_yanxi.gui.elem.button;
import mo_yanxi.gui.elem.check_box;
import mo_yanxi.gui.elem.head_body_elem;
import mo_yanxi.gui.elem.label;
import mo_yanxi.gui.elem.scroll_pane;
import mo_yanxi.gui.elem.scaling_stack;
import mo_yanxi.gui.elem.sequence;
import mo_yanxi.gui.elem.table;
import mo_yanxi.gui.elem.viewport;
import mo_yanxi.gui.fx;
import mo_yanxi.history_stack;
import mo_yanxi.input_handle;
import mo_yanxi.math.trans2;
import mo_yanxi.react_flow.common;

namespace mo_yanxi::game::ui{
namespace{
struct collision_shape_editor_reference_image_resources{
	graphic::image_atlas* atlas{};
	graphic::image_page* page{};
};

collision_shape_editor_reference_image_resources collision_shape_editor_reference_images{};
}

void configure_collision_shape_editor_reference_images(graphic::image_atlas& image_atlas){
	collision_shape_editor_reference_images.atlas = std::addressof(image_atlas);
	collision_shape_editor_reference_images.page = std::addressof(
		image_atlas.create_image_page("collision_shape_editor.reference", {
			.extent = {4096u, 4096u},
			.margin = 2u
		}));
}

void clear_collision_shape_editor_reference_images() noexcept{
	collision_shape_editor_reference_images = {};
}

namespace editor_detail{
enum class operation_kind{
	none,
	move,
	rotate,
	resize
};

enum class editor_mode{
	object,
	edit,
	reference_image,
	origin
};

inline constexpr float hit_epsilon = 1.0e-5f;

[[nodiscard]] constexpr std::string_view shape_type_name(const physics::shape_type type) noexcept{
	switch(type){
	case physics::shape_type::circle:
		return "circle";
	case physics::shape_type::capsule:
		return "capsule";
	case physics::shape_type::box:
		return "box";
	case physics::shape_type::convex_polygon:
		return "polygon";
	default:
		return "unknown";
	}
}

[[nodiscard]] constexpr std::string_view operation_name(const operation_kind kind) noexcept{
	switch(kind){
	case operation_kind::none:
		return "none";
	case operation_kind::move:
		return "move";
	case operation_kind::rotate:
		return "rotate";
	case operation_kind::resize:
		return "resize";
	default:
		return "unknown";
	}
}

[[nodiscard]] constexpr std::string_view mode_name(const editor_mode mode) noexcept{
	switch(mode){
	case editor_mode::object:
		return "object";
	case editor_mode::edit:
		return "edit";
	case editor_mode::reference_image:
		return "reference image";
	case editor_mode::origin:
		return "origin";
	default:
		return "unknown";
	}
}

struct circle_shape_properties{
	float radius{};
};

struct capsule_shape_properties{
	math::vec2 begin{};
	math::vec2 end{};
	float radius{};
	float length{};
};

struct box_shape_properties{
	math::vec2 size{};
};

struct polygon_shape_properties{
	std::size_t vertex_count{};
};

using shape_properties = std::variant<
	circle_shape_properties,
	capsule_shape_properties,
	box_shape_properties,
	polygon_shape_properties>;

[[nodiscard]] shape_properties make_shape_properties(
	const physics::collision_shape_editor_part& part) noexcept{
	switch(part.type){
	case physics::shape_type::circle:
		return circle_shape_properties{.radius = part.circle.radius};
	case physics::shape_type::capsule:
		return capsule_shape_properties{
			.begin = part.capsule.begin,
			.end = part.capsule.end,
			.radius = part.capsule.radius,
			.length = (part.capsule.end - part.capsule.begin).length()
		};
	case physics::shape_type::box:
		return box_shape_properties{.size = part.box.half_extent * 2.f};
	case physics::shape_type::convex_polygon:
		return polygon_shape_properties{.vertex_count = part.convex_polygon.vertices.size()};
	default:
		std::unreachable();
	}
}

[[nodiscard]] constexpr bool operation_command_char_allowed(const char value) noexcept{
	return value == '.'
		|| value == '+'
		|| value == '-'
		|| (value >= '0' && value <= '9');
}

[[nodiscard]] std::optional<float> operation_command_value(const std::string_view command) noexcept{
	float value{};
	const char* cursor = command.data();
	const char* const end = command.data() + command.size();

	while(cursor != end){
		const auto result = std::from_chars(cursor, end, value);
		if(result.ec == std::errc{}){
			return value;
		}
		if(result.ptr != cursor){
			cursor = result.ptr;
		}else{
			++cursor;
		}
	}

	return std::nullopt;
}

[[nodiscard]] float point_segment_distance2(
	const math::vec2 point,
	const math::vec2 begin,
	const math::vec2 end) noexcept{
	const math::vec2 segment = end - begin;
	const float length2 = segment.length2();
	if(length2 <= hit_epsilon * hit_epsilon){
		return point.dst2(begin);
	}

	const float t = std::clamp((point - begin).dot(segment) / length2, 0.f, 1.f);
	return point.dst2(begin + segment * t);
}

[[nodiscard]] bool polygon_contains(
	const physics::convex_polygon_shape& polygon,
	const math::vec2 local_point) noexcept{
	if(polygon.vertices.size() < 3u){
		return false;
	}

	std::optional<bool> positive{};
	for(std::size_t index = 0u; index != polygon.vertices.size(); ++index){
		const math::vec2 a = polygon.vertices[index];
		const math::vec2 b = polygon.vertices[(index + 1u) % polygon.vertices.size()];
		const float cross = (b - a).cross(local_point - a);
		if(std::abs(cross) <= hit_epsilon){
			continue;
		}
		const bool current_positive = cross > 0.f;
		if(!positive){
			positive = current_positive;
		}else if(*positive != current_positive){
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool part_contains(
	const physics::collision_shape_editor_part& part,
	const math::vec2 world_point) noexcept{
	const math::vec2 local = part.local_transform.apply_inv_to(world_point);
	switch(part.type){
	case physics::shape_type::circle:
		return local.length2() <= part.circle.radius * part.circle.radius;
	case physics::shape_type::capsule:
		return editor_detail::point_segment_distance2(local, part.capsule.begin, part.capsule.end)
			<= part.capsule.radius * part.capsule.radius;
	case physics::shape_type::box:
		return std::abs(local.x) <= part.box.half_extent.x
			&& std::abs(local.y) <= part.box.half_extent.y;
	case physics::shape_type::convex_polygon:
		return editor_detail::polygon_contains(part.convex_polygon, local);
	default:
		return false;
	}
}

[[nodiscard]] math::trans2 display_transform(
	const physics::collision_shape_editor_document& document,
	const physics::collision_shape_editor_part& part) noexcept{
	static_cast<void>(document);
	return part.local_transform;
}

[[nodiscard]] std::optional<std::size_t> polygon_vertex_at(
	const physics::collision_shape_editor_part& part,
	const math::vec2 world_point,
	const float radius) noexcept{
	if(part.type != physics::shape_type::convex_polygon){
		return std::nullopt;
	}

	const float radius2 = radius * radius;
	for(std::size_t index = part.convex_polygon.vertices.size(); index != 0u; --index){
		const std::size_t vertex_index = index - 1u;
		const math::vec2 vertex_world = part.convex_polygon.vertices[vertex_index] >> part.local_transform;
		if(vertex_world.dst2(world_point) <= radius2){
			return vertex_index;
		}
	}
	return std::nullopt;
}

[[nodiscard]] float rotation_delta(
	const math::vec2 initial_cursor,
	const math::vec2 current_cursor,
	const math::vec2 pivot) noexcept{
	const math::vec2 initial = initial_cursor - pivot;
	const math::vec2 current = current_cursor - pivot;
	if(initial.length2() <= hit_epsilon * hit_epsilon || current.length2() <= hit_epsilon * hit_epsilon){
		return 0.f;
	}
	return initial.angle_between_rad(current);
}

[[nodiscard]] bool contains_inbound_elem(
	const gui::elem& root,
	const std::span<gui::elem* const> inbounds) noexcept{
	for(const gui::elem* inbound : inbounds){
		for(const gui::elem* current = inbound; current != nullptr; current = current->parent()){
			if(current == std::addressof(root)){
				return true;
			}
		}
	}
	return false;
}
}

struct collision_shape_editor_history_entry{
	physics::collision_shape_editor_document document{};
	editor_detail::editor_mode mode{editor_detail::editor_mode::object};
	std::optional<std::size_t> object_selected_part{};
	std::optional<std::size_t> edit_selected_part{};
	std::optional<std::size_t> edit_selected_vertex{};
};

struct collision_shape_editor_operation{
	editor_detail::operation_kind kind{editor_detail::operation_kind::none};
	math::vec2 initial_cursor{};
	math::vec2 pivot{};
	math::bool2 constrain{true, true};
	std::size_t part_index{};
	std::optional<std::size_t> vertex_index{};
	physics::collision_shape_editor_part source_part{};
	bool invalid{};
	bool precision_mode{};
	std::string command{};

	[[nodiscard]] bool active() const noexcept;

	[[nodiscard]] float precision_scale() const noexcept;

	[[nodiscard]] bool has_axis_constraint() const noexcept;

	[[nodiscard]] std::optional<float> command_value() const noexcept;

	[[nodiscard]] math::vec2 move_delta(math::vec2 cursor) const noexcept;

	[[nodiscard]] float rotation_delta(math::vec2 cursor) const noexcept;

	void reset() noexcept;
};

struct collision_shape_editor_transform_operation{
	editor_detail::operation_kind kind{editor_detail::operation_kind::none};
	math::vec2 initial_cursor{};
	math::vec2 pivot{};
	math::bool2 constrain{true, true};
	math::trans2 source_transform{};
	math::vec2 source_half_extent{};
	bool precision_mode{};
	std::string command{};

	[[nodiscard]] bool active() const noexcept;

	[[nodiscard]] float precision_scale() const noexcept;

	[[nodiscard]] bool has_axis_constraint() const noexcept;

	[[nodiscard]] std::optional<float> command_value() const noexcept;

	[[nodiscard]] math::vec2 move_delta(math::vec2 cursor) const noexcept;

	[[nodiscard]] float rotation_delta(math::vec2 cursor) const noexcept;

	void reset() noexcept;
};

struct collision_shape_editor_object_mode_state{
	std::optional<std::size_t> selected_part{};
	collision_shape_editor_operation operation{};
};

struct collision_shape_editor_edit_mode_state{
	std::optional<std::size_t> selected_part{};
	std::optional<std::size_t> selected_vertex{};
	collision_shape_editor_operation operation{};
};

struct collision_shape_editor_reference_image_mode_state{
	collision_shape_editor_transform_operation operation{};
	graphic::allocated_image_region image_region{};
	std::string loaded_path{};
};

struct collision_shape_editor_origin_mode_state{
	collision_shape_editor_transform_operation operation{};
};

struct collision_shape_editor_state{
	static constexpr std::size_t history_limit = 64u;

	physics::collision_shape_editor_document document{};
	editor_detail::editor_mode mode{editor_detail::editor_mode::object};
	collision_shape_editor_object_mode_state object_mode{};
	collision_shape_editor_edit_mode_state edit_mode{};
	collision_shape_editor_reference_image_mode_state reference_image_mode{};
	collision_shape_editor_origin_mode_state origin_mode{};
	mo_yanxi::history_stack<collision_shape_editor_history_entry> history{history_limit};
	std::string last_error{};

	[[nodiscard]] collision_shape_editor_state();

	[[nodiscard]] std::size_t part_count() const noexcept;

	[[nodiscard]] physics::collision_shape_editor_part* selected() noexcept;

	[[nodiscard]] const physics::collision_shape_editor_part* selected() const noexcept;

	[[nodiscard]] bool operation_active() const noexcept;

	[[nodiscard]] bool reference_image_loaded() const noexcept;

	[[nodiscard]] std::optional<std::size_t> current_selected_part_index() const noexcept;

	[[nodiscard]] std::string selected_text() const;

	[[nodiscard]] std::string operation_text() const;

	void set_mode(editor_detail::editor_mode new_mode);

	[[nodiscard]] std::size_t add_shape(
		physics::shape_type type,
		math::vec2 position = {},
		bool record_history = true);

	void select_at(math::vec2 world_point, float vertex_radius) noexcept;

	void erase_selected();

	void duplicate_selected();

	void toggle_mirror_x();

	void toggle_mirror_y();

	[[nodiscard]] bool reset_position_for_current_mode();

	[[nodiscard]] bool reset_rotation_for_current_mode();

	[[nodiscard]] bool start_operation(editor_detail::operation_kind kind, math::vec2 cursor);

	[[nodiscard]] bool preview_operation(math::vec2 cursor);

	[[nodiscard]] bool commit_operation();

	void cancel_operation();

	[[nodiscard]] bool set_operation_precision(bool precision_mode, math::vec2 cursor);

	[[nodiscard]] bool toggle_operation_constraint(math::bool2 constrain) noexcept;

	[[nodiscard]] bool input_operation_character(char32_t value);

	[[nodiscard]] bool erase_operation_character();

	[[nodiscard]] bool clear_operation_command() noexcept;

	void push_history();

	void undo();

	void redo();

	void set_reference_image(std::filesystem::path path, graphic::allocated_image_region&& region, math::vec2 position);

	void clear_reference_image();

private:
	void apply_history_entry(const collision_shape_editor_history_entry& entry);

	void validate_selection() noexcept;

	[[nodiscard]] bool save_operation_mid_data(math::vec2 cursor);

	[[nodiscard]] collision_shape_editor_operation* current_part_operation() noexcept;

	[[nodiscard]] collision_shape_editor_transform_operation* current_transform_operation() noexcept;

	[[nodiscard]] const collision_shape_editor_operation* current_part_operation() const noexcept;

	[[nodiscard]] const collision_shape_editor_transform_operation* current_transform_operation() const noexcept;

	[[nodiscard]] bool preview_part_operation(collision_shape_editor_operation& target, math::vec2 cursor);

	[[nodiscard]] bool preview_transform_operation(collision_shape_editor_transform_operation& target, math::vec2 cursor);
};

struct collision_shape_editor_viewport : gui::viewport{
	collision_shape_editor_state state{};

private:
	struct reference_image_path_listener : react_flow::terminal<std::span<const std::filesystem::path>>{
		collision_shape_editor_viewport* viewport{};

		[[nodiscard]] explicit reference_image_path_listener(collision_shape_editor_viewport* target)
			: viewport(target){
		}

	protected:
		void on_update(react_flow::data_carrier<std::span<const std::filesystem::path>>& data) override;
	};

	gui::elem* add_menu_overlay_{};
	gui::elem* reference_image_file_overlay_{};
	math::vec2 pending_add_position_{};
	math::vec2 last_cursor_scene_pos_{};
	math::vec2 last_cursor_world_pos_{};
	react_flow::node_holder_pinned<reference_image_path_listener> reference_image_path_node_;

public:
	[[nodiscard]] collision_shape_editor_viewport(gui::scene& scene, gui::elem* parent);

	bool update(float delta_in_ticks) override;

	gui::events::op_afterwards on_click(
		gui::events::click event,
		std::span<gui::elem* const> aboves) override;

	gui::events::op_afterwards on_drag(gui::events::drag event) override;

	gui::events::op_afterwards on_cursor_moved(gui::events::cursor_move event) override;

	gui::events::op_afterwards on_key_input(input_handle::key_set key) override;

	gui::events::op_afterwards on_unicode_input(char32_t value) override;

	gui::events::op_afterwards on_esc() override;

	void record_draw_layer(gui::draw_recorder& call_stack_builder) const override;

	[[nodiscard]] float selection_radius() const noexcept;

	[[nodiscard]] math::vec2 cursor_world_pos() const noexcept;

	[[nodiscard]] math::vec2 local_world_pos(math::vec2 local_pos) const noexcept;

	void open_reference_image_file_selector();

private:
	void show_add_menu();

	void close_add_menu();

	void show_reference_image_file_selector();

	void close_reference_image_file_selector();

	void load_reference_image(const std::filesystem::path& path);

	void refresh_cursor_cache_from_local(math::vec2 local_pos) noexcept;

	void draw_editor_content() const;
};

struct collision_shape_editor_float_input : gui::cpd::numeric_input_area<float>{
	std::function<void(float)> on_value_changed{};

	[[nodiscard]] collision_shape_editor_float_input(gui::scene& scene, gui::elem* parent)
		: gui::cpd::numeric_input_area<float>(scene, parent){
	}

	void on_changed(const float value) override{
		if(on_value_changed){
			on_value_changed(value);
		}
	}
};

struct collision_shape_editor_check_box : gui::check_box{
	std::function<void(bool)> on_value_changed{};
	bool suppress_callback{};

	[[nodiscard]] collision_shape_editor_check_box(gui::scene& scene, gui::elem* parent)
		: gui::check_box(scene, parent, std::in_place){
	}

	void set_checked_no_propagate(const bool value){
		suppress_callback = true;
		this->set_current_value(value ? 1u : 0u);
		suppress_callback = false;
	}

protected:
	void on_selected_val_updated(const unsigned value) override{
		gui::check_box::on_selected_val_updated(value);
		if(!suppress_callback && on_value_changed){
			on_value_changed(value != 0u);
		}
	}
};

namespace{
void set_collision_shape_editor_label_text(gui::direct_label* label, const std::string_view text){
	if(label != nullptr){
		label->set_tokenized_text(typesetting::tokenized_text{
			std::string{text},
			typesetting::tokenize_tag::raw
		});
	}
}

template <typename Function>
void setup_collision_shape_editor_button(
	gui::button<gui::direct_label>& button,
	const std::string_view text,
	Function function){
	button.set_style(gui::style::family_variant::base_only);
	button.set_fit_type(gui::label_fit_type::scl);
	button.text_entire_align = align::pos::center;
	button.set_tokenized_text({text});
	button.set_button_callback(function);
}
}

struct collision_shape_editor_numeric_property : gui::head_body_no_invariant{
	gui::direct_label* label_{};
	collision_shape_editor_float_input* input_{};
	std::function<void(float)> on_value_changed{};

	[[nodiscard]] collision_shape_editor_numeric_property(gui::scene& scene, gui::elem* parent)
		: gui::head_body_no_invariant(scene, parent, gui::layout::layout_policy::vert_major){
		this->set_style();
		this->set_expand_policy(gui::layout::expand_policy::passive);
		this->set_head_size(128.f);
		this->set_body_size({gui::layout::size_category::passive, 1.f});
		this->set_pad(4.f);

		this->create_head([this](gui::direct_label& label){
			label.set_style(gui::style::family_variant::base_only);
			label.set_fit_type(gui::label_fit_type::scl);
			label.text_entire_align = align::pos::center_left;
			label_ = std::addressof(label);
		});
		this->create_body([this](collision_shape_editor_float_input& input){
			input.set_value_no_propagate(0.f);
			input.on_value_changed = [this](const float value){
				if(on_value_changed){
					on_value_changed(value);
				}
			};
			input_ = std::addressof(input);
		});
	}

	void set_label_text(const std::string_view text) const{
		set_collision_shape_editor_label_text(label_, text);
	}

	void set_value(const float value) const{
		if(input_ != nullptr){
			input_->set_value_no_propagate(value);
		}
	}

	void set_property_active(const bool active){
		this->invisible = !active;
		if(input_ != nullptr){
			input_->set_disabled(!active);
		}
	}
};

struct collision_shape_editor_check_property : gui::head_body_no_invariant{
	gui::direct_label* label_{};
	collision_shape_editor_check_box* check_{};
	std::function<void(bool)> on_value_changed{};

	[[nodiscard]] collision_shape_editor_check_property(gui::scene& scene, gui::elem* parent)
		: gui::head_body_no_invariant(scene, parent, gui::layout::layout_policy::vert_major){
		this->set_style();
		this->set_expand_policy(gui::layout::expand_policy::passive);
		this->set_head_size(128.f);
		this->set_body_size(34.f);
		this->set_pad(4.f);

		this->create_head([this](gui::direct_label& label){
			label.set_style(gui::style::family_variant::base_only);
			label.set_fit_type(gui::label_fit_type::scl);
			label.text_entire_align = align::pos::center_left;
			label_ = std::addressof(label);
		});
		this->create_body([this](collision_shape_editor_check_box& check){
			check.on_value_changed = [this](const bool value){
				if(on_value_changed){
					on_value_changed(value);
				}
			};
			check_ = std::addressof(check);
		});
	}

	void set_label_text(const std::string_view text) const{
		set_collision_shape_editor_label_text(label_, text);
	}

	void set_checked(const bool value) const{
		if(check_ != nullptr){
			check_->set_checked_no_propagate(value);
		}
	}

	void set_property_active(const bool active){
		this->invisible = !active;
		if(check_ != nullptr){
			check_->set_disabled(!active);
		}
	}
};

struct collision_shape_editor_text_property : gui::head_body_no_invariant{
	gui::direct_label* label_{};
	gui::direct_label* value_{};

	[[nodiscard]] collision_shape_editor_text_property(gui::scene& scene, gui::elem* parent)
		: gui::head_body_no_invariant(scene, parent, gui::layout::layout_policy::vert_major){
		this->set_style();
		this->set_expand_policy(gui::layout::expand_policy::passive);
		this->set_head_size(128.f);
		this->set_body_size({gui::layout::size_category::passive, 1.f});
		this->set_pad(4.f);

		this->create_head([this](gui::direct_label& label){
			label.set_style(gui::style::family_variant::base_only);
			label.set_fit_type(gui::label_fit_type::scl);
			label.text_entire_align = align::pos::center_left;
			label_ = std::addressof(label);
		});
		this->create_body([this](gui::direct_label& value){
			value.set_style(gui::style::family_variant::base_only);
			value.set_fit_type(gui::label_fit_type::scl);
			value.text_entire_align = align::pos::center_left;
			value_ = std::addressof(value);
		});
	}

	void set_label_text(const std::string_view text) const{
		set_collision_shape_editor_label_text(label_, text);
	}

	void set_value_text(const std::string_view text) const{
		set_collision_shape_editor_label_text(value_, text);
	}

	void set_property_active(const bool active){
		this->invisible = !active;
	}
};

struct collision_shape_editor_transform_properties : gui::sequence{
	collision_shape_editor_numeric_property* x_{};
	collision_shape_editor_numeric_property* y_{};
	collision_shape_editor_numeric_property* rot_{};

	std::function<void(float)> on_x_changed{};
	std::function<void(float)> on_y_changed{};
	std::function<void(float)> on_rot_degrees_changed{};

	[[nodiscard]] collision_shape_editor_transform_properties(gui::scene& scene, gui::elem* parent)
		: gui::sequence(scene, parent, gui::layout::layout_policy::hori_major){
		this->set_style();
		this->set_expand_policy(gui::layout::expand_policy::resize_to_fit);
		this->template_cell.set_size(32.f).set_pad({2.f, 2.f});

		auto x = this->emplace_back<collision_shape_editor_numeric_property>();
		auto y = this->emplace_back<collision_shape_editor_numeric_property>();
		auto rot = this->emplace_back<collision_shape_editor_numeric_property>();
		x_ = std::addressof(x.elem());
		y_ = std::addressof(y.elem());
		rot_ = std::addressof(rot.elem());
		x_->set_label_text("X");
		y_->set_label_text("Y");
		rot_->set_label_text("Rotation");
		x_->on_value_changed = [this](const float value){
			if(on_x_changed){
				on_x_changed(value);
			}
		};
		y_->on_value_changed = [this](const float value){
			if(on_y_changed){
				on_y_changed(value);
			}
		};
		rot_->on_value_changed = [this](const float value){
			if(on_rot_degrees_changed){
				on_rot_degrees_changed(value);
			}
		};
	}

	void set_transform(const math::trans2 transform) const{
		x_->set_value(transform.vec.x);
		y_->set_value(transform.vec.y);
		rot_->set_value(transform.rot / math::deg_to_rad);
	}

	void set_control_active(const bool active){
		this->invisible = !active;
	}
};

struct collision_shape_editor_mirror_properties : gui::sequence{
	collision_shape_editor_text_property* status_{};
	collision_shape_editor_check_property* mirror_x_{};
	collision_shape_editor_check_property* mirror_y_{};
	collision_shape_editor_transform_properties* origin_{};

	std::function<void(bool)> on_mirror_x_changed{};
	std::function<void(bool)> on_mirror_y_changed{};
	std::function<void(float)> on_origin_x_changed{};
	std::function<void(float)> on_origin_y_changed{};
	std::function<void(float)> on_origin_rot_degrees_changed{};

	[[nodiscard]] collision_shape_editor_mirror_properties(gui::scene& scene, gui::elem* parent)
		: gui::sequence(scene, parent, gui::layout::layout_policy::hori_major){
		this->set_style();
		this->set_expand_policy(gui::layout::expand_policy::resize_to_fit);
		this->template_cell.set_size(32.f).set_pad({2.f, 2.f});

		auto status = this->emplace_back<collision_shape_editor_text_property>();
		auto mirror_x = this->emplace_back<collision_shape_editor_check_property>();
		auto mirror_y = this->emplace_back<collision_shape_editor_check_property>();
		auto origin = this->emplace_back<collision_shape_editor_transform_properties>();
		status_ = std::addressof(status.elem());
		mirror_x_ = std::addressof(mirror_x.elem());
		mirror_y_ = std::addressof(mirror_y.elem());
		origin_ = std::addressof(origin.elem());

		status_->set_label_text("Mirror");
		mirror_x_->set_label_text("Mirror X");
		mirror_y_->set_label_text("Mirror Y");
		origin_->x_->set_label_text("Mirror Origin X");
		origin_->y_->set_label_text("Mirror Origin Y");
		origin_->rot_->set_label_text("Mirror Rotation");

		mirror_x_->on_value_changed = [this](const bool value){
			if(on_mirror_x_changed){
				on_mirror_x_changed(value);
			}
		};
		mirror_y_->on_value_changed = [this](const bool value){
			if(on_mirror_y_changed){
				on_mirror_y_changed(value);
			}
		};
		origin_->on_x_changed = [this](const float value){
			if(on_origin_x_changed){
				on_origin_x_changed(value);
			}
		};
		origin_->on_y_changed = [this](const float value){
			if(on_origin_y_changed){
				on_origin_y_changed(value);
			}
		};
		origin_->on_rot_degrees_changed = [this](const float value){
			if(on_origin_rot_degrees_changed){
				on_origin_rot_degrees_changed(value);
			}
		};
	}

	void set_mirror(const physics::collision_shape_editor_mirror_modifier& mirror) const{
		std::string status = "Off";
		if(mirror.active()){
			status = "On";
			if(mirror.mirror_x){
				status += " X";
			}
			if(mirror.mirror_y){
				status += " Y";
			}
		}
		status_->set_value_text(status);
		mirror_x_->set_checked(mirror.mirror_x);
		mirror_y_->set_checked(mirror.mirror_y);
		origin_->set_transform(mirror.origin);
	}

	void set_control_active(const bool active){
		this->invisible = !active;
	}
};

struct collision_shape_editor_shape_properties_control : gui::sequence{
	collision_shape_editor_numeric_property* primary_{};
	collision_shape_editor_numeric_property* secondary_{};
	collision_shape_editor_text_property* info_a_{};
	collision_shape_editor_text_property* info_b_{};
	collision_shape_editor_text_property* info_c_{};

	std::function<void(float)> on_primary_changed{};
	std::function<void(float)> on_secondary_changed{};

	[[nodiscard]] collision_shape_editor_shape_properties_control(gui::scene& scene, gui::elem* parent)
		: gui::sequence(scene, parent, gui::layout::layout_policy::hori_major){
		this->set_style();
		this->set_expand_policy(gui::layout::expand_policy::resize_to_fit);
		this->template_cell.set_size(32.f).set_pad({2.f, 2.f});

		auto primary = this->emplace_back<collision_shape_editor_numeric_property>();
		auto secondary = this->emplace_back<collision_shape_editor_numeric_property>();
		auto info_a = this->emplace_back<collision_shape_editor_text_property>();
		auto info_b = this->emplace_back<collision_shape_editor_text_property>();
		auto info_c = this->emplace_back<collision_shape_editor_text_property>();
		primary_ = std::addressof(primary.elem());
		secondary_ = std::addressof(secondary.elem());
		info_a_ = std::addressof(info_a.elem());
		info_b_ = std::addressof(info_b.elem());
		info_c_ = std::addressof(info_c.elem());

		primary_->on_value_changed = [this](const float value){
			if(on_primary_changed){
				on_primary_changed(value);
			}
		};
		secondary_->on_value_changed = [this](const float value){
			if(on_secondary_changed){
				on_secondary_changed(value);
			}
		};
	}

	void set_shape(
		const editor_detail::shape_properties& shape,
		const std::optional<std::size_t> selected_vertex){
		primary_->set_property_active(false);
		secondary_->set_property_active(false);
		info_a_->set_property_active(false);
		info_b_->set_property_active(false);
		info_c_->set_property_active(false);

		std::visit([this, selected_vertex](const auto& properties){
			using properties_type = std::remove_cvref_t<decltype(properties)>;
			if constexpr(std::same_as<properties_type, editor_detail::circle_shape_properties>){
				primary_->set_label_text("Radius");
				primary_->set_value(properties.radius);
				primary_->set_property_active(true);
			}else if constexpr(std::same_as<properties_type, editor_detail::capsule_shape_properties>){
				primary_->set_label_text("Radius");
				primary_->set_value(properties.radius);
				primary_->set_property_active(true);
				info_a_->set_label_text("Begin");
				info_a_->set_value_text(std::format("{:.3f}, {:.3f}", properties.begin.x, properties.begin.y));
				info_b_->set_label_text("End");
				info_b_->set_value_text(std::format("{:.3f}, {:.3f}", properties.end.x, properties.end.y));
				info_c_->set_label_text("Length");
				info_c_->set_value_text(std::format("{:.3f}", properties.length));
				info_a_->set_property_active(true);
				info_b_->set_property_active(true);
				info_c_->set_property_active(true);
			}else if constexpr(std::same_as<properties_type, editor_detail::box_shape_properties>){
				primary_->set_label_text("Width");
				secondary_->set_label_text("Height");
				primary_->set_value(properties.size.x);
				secondary_->set_value(properties.size.y);
				primary_->set_property_active(true);
				secondary_->set_property_active(true);
			}else if constexpr(std::same_as<properties_type, editor_detail::polygon_shape_properties>){
				info_a_->set_label_text("Vertices");
				info_a_->set_value_text(std::format("{}", properties.vertex_count));
				info_b_->set_label_text("Selected Vertex");
				info_b_->set_value_text(selected_vertex
					? std::format("{}", *selected_vertex)
					: std::string{"None"});
				info_a_->set_property_active(true);
				info_b_->set_property_active(true);
			}
		}, shape);
	}

	void set_control_active(const bool active){
		this->invisible = !active;
	}
};

struct collision_shape_editor_reference_image_actions : gui::sequence{
	std::function<void()> on_choose{};
	std::function<void()> on_clear{};

	[[nodiscard]] collision_shape_editor_reference_image_actions(gui::scene& scene, gui::elem* parent)
		: gui::sequence(scene, parent, gui::layout::layout_policy::vert_major){
		this->set_style();
		this->set_expand_policy(gui::layout::expand_policy::passive);
		this->template_cell.set_pad({2.f, 2.f});

		auto choose = this->create_back([this](gui::button<gui::direct_label>& button){
			setup_collision_shape_editor_button(button, "Choose Image", [this]{
				if(on_choose){
					on_choose();
				}
			});
		});
		choose.cell().set_passive(1.f);
		auto clear = this->create_back([this](gui::button<gui::direct_label>& button){
			setup_collision_shape_editor_button(button, "Clear", [this]{
				if(on_clear){
					on_clear();
				}
			});
		});
		clear.cell().set_size(72.f);
	}
};

struct collision_shape_editor_mode_properties_panel : gui::sequence{
	gui::direct_label* title_{};

	[[nodiscard]] collision_shape_editor_mode_properties_panel(gui::scene& scene, gui::elem* parent)
		: gui::sequence(scene, parent, gui::layout::layout_policy::hori_major){
		this->set_style();
		this->set_expand_policy(gui::layout::expand_policy::resize_to_fit);
		this->template_cell.set_size(32.f).set_pad({2.f, 2.f});

		auto title = this->create_back([this](gui::direct_label& label){
			label.set_style(gui::style::family_variant::base_only);
			label.set_fit_type(gui::label_fit_type::scl);
			label.text_entire_align = align::pos::center_left;
			title_ = std::addressof(label);
		});
		title.cell().set_size(36.f);
	}

	void set_title(const std::string_view text) const{
		set_collision_shape_editor_label_text(title_, text);
	}

	void set_panel_active(const bool active){
		this->invisible = !active;
	}
};

struct collision_shape_editor_object_mode_properties_panel : collision_shape_editor_mode_properties_panel{
	collision_shape_editor_text_property* shape_{};
	collision_shape_editor_transform_properties* transform_{};
	collision_shape_editor_mirror_properties* mirror_{};

	[[nodiscard]] collision_shape_editor_object_mode_properties_panel(gui::scene& scene, gui::elem* parent)
		: collision_shape_editor_mode_properties_panel(scene, parent){
		auto shape = this->emplace_back<collision_shape_editor_text_property>();
		auto transform = this->emplace_back<collision_shape_editor_transform_properties>();
		auto mirror = this->emplace_back<collision_shape_editor_mirror_properties>();
		shape_ = std::addressof(shape.elem());
		transform_ = std::addressof(transform.elem());
		mirror_ = std::addressof(mirror.elem());
		shape_->set_label_text("Shape");
	}

	void refresh(const collision_shape_editor_state& state){
		const auto selected_index = state.object_mode.selected_part;
		const auto* part = state.selected();
		if(part == nullptr || !selected_index.has_value()){
			this->set_title("Object Mode");
			shape_->set_value_text("No selected object");
			transform_->set_control_active(false);
			mirror_->set_control_active(false);
			return;
		}

		this->set_title(std::format(
			"Object {}: {}",
			*selected_index,
			editor_detail::shape_type_name(part->type)));
		shape_->set_value_text(std::format("{}", editor_detail::shape_type_name(part->type)));
		transform_->set_transform(part->local_transform);
		mirror_->set_mirror(part->mirror);
		transform_->set_control_active(true);
		mirror_->set_control_active(true);
	}
};

struct collision_shape_editor_edit_mode_properties_panel : collision_shape_editor_mode_properties_panel{
	collision_shape_editor_text_property* selection_{};
	collision_shape_editor_shape_properties_control* shape_{};

	[[nodiscard]] collision_shape_editor_edit_mode_properties_panel(gui::scene& scene, gui::elem* parent)
		: collision_shape_editor_mode_properties_panel(scene, parent){
		auto selection = this->emplace_back<collision_shape_editor_text_property>();
		auto shape = this->emplace_back<collision_shape_editor_shape_properties_control>();
		selection_ = std::addressof(selection.elem());
		shape_ = std::addressof(shape.elem());
		selection_->set_label_text("Selection");
	}

	void refresh(const collision_shape_editor_state& state){
		const auto selected_index = state.edit_mode.selected_part;
		const auto* part = state.selected();
		if(part == nullptr || !selected_index.has_value()){
			this->set_title("Edit Mode");
			selection_->set_value_text("No selected shape");
			shape_->set_control_active(false);
			return;
		}

		this->set_title(std::format(
			"Edit {}: {}",
			*selected_index,
			editor_detail::shape_type_name(part->type)));
		selection_->set_value_text(state.edit_mode.selected_vertex
			? std::format("Vertex {}", *state.edit_mode.selected_vertex)
			: std::string{"Whole shape"});
		shape_->set_shape(editor_detail::make_shape_properties(*part), state.edit_mode.selected_vertex);
		shape_->set_control_active(true);
	}
};

struct collision_shape_editor_reference_image_properties_panel : collision_shape_editor_mode_properties_panel{
	collision_shape_editor_reference_image_actions* actions_{};
	collision_shape_editor_text_property* path_{};
	collision_shape_editor_check_property* visible_{};
	collision_shape_editor_transform_properties* transform_{};
	collision_shape_editor_numeric_property* width_{};
	collision_shape_editor_numeric_property* height_{};
	collision_shape_editor_numeric_property* opacity_{};

	[[nodiscard]] collision_shape_editor_reference_image_properties_panel(gui::scene& scene, gui::elem* parent)
		: collision_shape_editor_mode_properties_panel(scene, parent){
		auto actions = this->emplace_back<collision_shape_editor_reference_image_actions>();
		auto path = this->emplace_back<collision_shape_editor_text_property>();
		auto visible = this->emplace_back<collision_shape_editor_check_property>();
		auto transform = this->emplace_back<collision_shape_editor_transform_properties>();
		auto width = this->emplace_back<collision_shape_editor_numeric_property>();
		auto height = this->emplace_back<collision_shape_editor_numeric_property>();
		auto opacity = this->emplace_back<collision_shape_editor_numeric_property>();
		actions_ = std::addressof(actions.elem());
		path_ = std::addressof(path.elem());
		visible_ = std::addressof(visible.elem());
		transform_ = std::addressof(transform.elem());
		width_ = std::addressof(width.elem());
		height_ = std::addressof(height.elem());
		opacity_ = std::addressof(opacity.elem());

		path_->set_label_text("Path");
		visible_->set_label_text("Visible");
		width_->set_label_text("Width");
		height_->set_label_text("Height");
		opacity_->set_label_text("Opacity");
	}

	void refresh(const physics::collision_shape_editor_reference_image& reference){
		this->set_title("Reference Image");
		path_->set_value_text(reference.path.empty() ? "No image" : reference.path);
		visible_->set_checked(reference.enabled);
		transform_->set_transform(reference.transform);
		width_->set_value(reference.half_extent.x * 2.f);
		height_->set_value(reference.half_extent.y * 2.f);
		opacity_->set_value(reference.opacity);
	}
};

struct collision_shape_editor_origin_properties_panel : collision_shape_editor_mode_properties_panel{
	collision_shape_editor_transform_properties* transform_{};

	[[nodiscard]] collision_shape_editor_origin_properties_panel(gui::scene& scene, gui::elem* parent)
		: collision_shape_editor_mode_properties_panel(scene, parent){
		auto transform = this->emplace_back<collision_shape_editor_transform_properties>();
		transform_ = std::addressof(transform.elem());
	}

	void refresh(const math::trans2 transform){
		this->set_title("Origin");
		transform_->set_transform(transform);
		transform_->set_control_active(true);
	}
};

struct collision_shape_editor_properties_panel : gui::elem{
private:
	static constexpr std::size_t mode_panel_count = 4u;

	enum class mode_panel_index : std::size_t{
		object,
		edit,
		reference_image,
		origin
	};

	collision_shape_editor_viewport* viewport_{};
	std::array<gui::elem_ptr, mode_panel_count> panels_{};
	std::array<gui::elem*, 1u> exposed_panel_{};
	std::size_t active_panel_{static_cast<std::size_t>(mode_panel_index::object)};
	std::size_t constructing_panel_{mode_panel_count};
	gui::layout::expand_policy expand_policy_{};
	collision_shape_editor_object_mode_properties_panel* object_panel_{};
	collision_shape_editor_edit_mode_properties_panel* edit_panel_{};
	collision_shape_editor_reference_image_properties_panel* reference_panel_{};
	collision_shape_editor_origin_properties_panel* origin_panel_{};

public:
	[[nodiscard]] collision_shape_editor_properties_panel(gui::scene& scene, gui::elem* parent)
		: gui::elem(scene, parent){
		this->interactivity = gui::interactivity_flag::children_only;
		this->set_style();
		this->set_expand_policy(gui::layout::expand_policy::resize_to_fit);
		this->set_self_border(gui::border{}.set(8.f));

		object_panel_ = std::addressof(this->emplace_panel<collision_shape_editor_object_mode_properties_panel>(
			static_cast<std::size_t>(mode_panel_index::object)));
		edit_panel_ = std::addressof(this->emplace_panel<collision_shape_editor_edit_mode_properties_panel>(
			static_cast<std::size_t>(mode_panel_index::edit)));
		reference_panel_ = std::addressof(this->emplace_panel<collision_shape_editor_reference_image_properties_panel>(
			static_cast<std::size_t>(mode_panel_index::reference_image)));
		origin_panel_ = std::addressof(this->emplace_panel<collision_shape_editor_origin_properties_panel>(
			static_cast<std::size_t>(mode_panel_index::origin)));

		this->wire_callbacks();
	}

	void bind(collision_shape_editor_viewport& viewport) noexcept{
		viewport_ = std::addressof(viewport);
	}

	bool update(const float delta_in_ticks) override{
		if(!gui::elem::update(delta_in_ticks)){
			return false;
		}
		this->refresh();
		return true;
	}

	[[nodiscard]] gui::layout::expand_policy get_expand_policy() const noexcept{
		return expand_policy_;
	}

	void set_expand_policy(const gui::layout::expand_policy expand_policy){
		if(gui::util::try_modify(expand_policy_, expand_policy)){
			this->notify_layout_changed(gui::propagate_mask::upper);
			this->layout_state.intercept_lower_to_isolated =
				expand_policy == gui::layout::expand_policy::passive;
		}
	}

	[[nodiscard]] gui::elem_span exposed_children() const noexcept override{
		if(exposed_panel_[0] == nullptr){
			return {};
		}
		return gui::elem_span{exposed_panel_};
	}

	gui::element_collect_buffer collect_children() const override{
		gui::element_collect_buffer result{};
		for(const auto& panel : panels_){
			if(panel){
				result.push_back(*panel);
			}
		}
		return result;
	}

	bool decide_is_children_displayable_on_add(gui::elem&) override{
		return this->is_at_display_stage() && constructing_panel_ == active_panel_;
	}

	void record_draw_layer(gui::draw_recorder& call_stack_builder) const override{
		gui::elem::record_draw_layer(call_stack_builder);
		this->active_panel().record_draw_layer(call_stack_builder);
	}

	void layout_elem() override{
		gui::elem::layout_elem();
		this->active_panel().try_layout();
	}

	bool update_abs_src(math::vec2 parent_content_src) noexcept override{
		if(gui::elem::update_abs_src(parent_content_src)){
			this->active_panel().update_abs_src(this->content_src_pos_abs());
			return true;
		}
		return false;
	}

protected:
	bool resize_impl(const math::vec2 size) override{
		if(gui::elem::resize_impl(size)){
			this->restrict_child(this->active_panel());
			return true;
		}
		return false;
	}

	std::optional<math::vec2> pre_acquire_size_impl(gui::layout::optional_mastering_extent extent) override{
		if(expand_policy_ == gui::layout::expand_policy::passive){
			return std::nullopt;
		}

		auto result = this->active_panel().pre_acquire_size(extent);
		if(!result){
			return result;
		}
		return gui::util::select_prefer_extent(
			expand_policy_ == gui::layout::expand_policy::prefer,
			result.value(),
			this->get_prefer_extent());
	}

private:
	template <std::derived_from<gui::elem> Panel, typename... Args>
		requires std::constructible_from<Panel, gui::scene&, gui::elem*, Args&&...>
	Panel& emplace_panel(const std::size_t index, Args&&... args){
		if(index >= panels_.size()){
			throw std::out_of_range{"index out of properties panel range"};
		}

		constructing_panel_ = index;
		panels_[index] = gui::elem_ptr{
			this->get_scene(),
			this,
			std::in_place_type<Panel>,
			std::forward<Args>(args)...
		};
		constructing_panel_ = mode_panel_count;

		if(index == active_panel_){
			exposed_panel_[0] = panels_[index].get();
		}

		return static_cast<Panel&>(*panels_[index]);
	}

	[[nodiscard]] gui::elem& active_panel() const noexcept{
		return *panels_[active_panel_];
	}

	void switch_to_panel(const std::size_t index){
		if(index >= panels_.size()){
			throw std::out_of_range{"index out of properties panel range"};
		}
		if(active_panel_ == index){
			exposed_panel_[0] = panels_[active_panel_].get();
			return;
		}

		this->active_panel().on_display_state_changed(false, false);
		active_panel_ = index;
		exposed_panel_[0] = panels_[active_panel_].get();
		this->active_panel().on_display_state_changed(this->is_at_display_stage(), false);
		this->restrict_child(this->active_panel());
		this->active_panel().update_abs_src(this->content_src_pos_abs());
		this->notify_isolated_layout_changed();
	}

	void wire_callbacks(){
		object_panel_->transform_->on_x_changed = [this](const float value){
			this->set_transform_x(value);
		};
		object_panel_->transform_->on_y_changed = [this](const float value){
			this->set_transform_y(value);
		};
		object_panel_->transform_->on_rot_degrees_changed = [this](const float value){
			this->set_transform_rot_degrees(value);
		};
		object_panel_->mirror_->on_mirror_x_changed = [this](const bool value){
			this->set_mirror_x(value);
		};
		object_panel_->mirror_->on_mirror_y_changed = [this](const bool value){
			this->set_mirror_y(value);
		};
		object_panel_->mirror_->on_origin_x_changed = [this](const float value){
			this->set_mirror_origin_x(value);
		};
		object_panel_->mirror_->on_origin_y_changed = [this](const float value){
			this->set_mirror_origin_y(value);
		};
		object_panel_->mirror_->on_origin_rot_degrees_changed = [this](const float value){
			this->set_mirror_origin_rot_degrees(value);
		};
		edit_panel_->shape_->on_primary_changed = [this](const float value){
			this->set_secondary_x(value);
		};
		edit_panel_->shape_->on_secondary_changed = [this](const float value){
			this->set_secondary_y(value);
		};
		reference_panel_->actions_->on_choose = [this]{
			if(viewport_ != nullptr){
				viewport_->state.set_mode(editor_detail::editor_mode::reference_image);
				viewport_->open_reference_image_file_selector();
			}
		};
		reference_panel_->actions_->on_clear = [this]{
			if(viewport_ != nullptr){
				viewport_->state.clear_reference_image();
			}
		};
		reference_panel_->visible_->on_value_changed = [this](const bool value){
			this->set_reference_visible(value);
		};
		reference_panel_->transform_->on_x_changed = [this](const float value){
			this->set_transform_x(value);
		};
		reference_panel_->transform_->on_y_changed = [this](const float value){
			this->set_transform_y(value);
		};
		reference_panel_->transform_->on_rot_degrees_changed = [this](const float value){
			this->set_transform_rot_degrees(value);
		};
		reference_panel_->width_->on_value_changed = [this](const float value){
			this->set_secondary_x(value);
		};
		reference_panel_->height_->on_value_changed = [this](const float value){
			this->set_secondary_y(value);
		};
		reference_panel_->opacity_->on_value_changed = [this](const float value){
			this->set_reference_opacity(value);
		};
		origin_panel_->transform_->on_x_changed = [this](const float value){
			this->set_transform_x(value);
		};
		origin_panel_->transform_->on_y_changed = [this](const float value){
			this->set_transform_y(value);
		};
		origin_panel_->transform_->on_rot_degrees_changed = [this](const float value){
			this->set_transform_rot_degrees(value);
		};
	}

	void refresh(){
		if(viewport_ == nullptr){
			this->invisible = true;
			return;
		}

		auto& state = viewport_->state;
		this->invisible = false;

		switch(state.mode){
		case editor_detail::editor_mode::object:
			this->switch_to_panel(static_cast<std::size_t>(mode_panel_index::object));
			object_panel_->refresh(state);
			break;
		case editor_detail::editor_mode::edit:
			this->switch_to_panel(static_cast<std::size_t>(mode_panel_index::edit));
			edit_panel_->refresh(state);
			break;
		case editor_detail::editor_mode::reference_image:
			this->switch_to_panel(static_cast<std::size_t>(mode_panel_index::reference_image));
			reference_panel_->refresh(state.document.reference_image);
			break;
		case editor_detail::editor_mode::origin:
			this->switch_to_panel(static_cast<std::size_t>(mode_panel_index::origin));
			origin_panel_->refresh(state.document.total_transform);
			break;
		default:
			this->invisible = true;
			break;
		}
	}

	template <typename Function>
	void edit_state(Function&& function){
		if(viewport_ == nullptr){
			return;
		}
		auto& state = viewport_->state;
		if(state.operation_active()){
			state.cancel_operation();
		}
		if(std::invoke(std::forward<Function>(function), state)){
			state.last_error.clear();
			state.push_history();
		}
	}

	void set_transform_x(const float value){
		this->edit_state([value](collision_shape_editor_state& state){
			switch(state.mode){
			case editor_detail::editor_mode::object:
				if(auto* part = state.selected(); part != nullptr && part->local_transform.vec.x != value){
					part->local_transform.vec.x = value;
					return true;
				}
				return false;
			case editor_detail::editor_mode::reference_image:
				if(state.document.reference_image.transform.vec.x != value){
					state.document.reference_image.transform.vec.x = value;
					return true;
				}
				return false;
			case editor_detail::editor_mode::origin:
				if(state.document.total_transform.vec.x != value){
					state.document.total_transform.vec.x = value;
					return true;
				}
				return false;
			default:
				return false;
			}
		});
	}

	void set_transform_y(const float value){
		this->edit_state([value](collision_shape_editor_state& state){
			switch(state.mode){
			case editor_detail::editor_mode::object:
				if(auto* part = state.selected(); part != nullptr && part->local_transform.vec.y != value){
					part->local_transform.vec.y = value;
					return true;
				}
				return false;
			case editor_detail::editor_mode::reference_image:
				if(state.document.reference_image.transform.vec.y != value){
					state.document.reference_image.transform.vec.y = value;
					return true;
				}
				return false;
			case editor_detail::editor_mode::origin:
				if(state.document.total_transform.vec.y != value){
					state.document.total_transform.vec.y = value;
					return true;
				}
				return false;
			default:
				return false;
			}
		});
	}

	void set_transform_rot_degrees(const float value){
		const float radians = value * math::deg_to_rad;
		this->edit_state([radians](collision_shape_editor_state& state){
			switch(state.mode){
			case editor_detail::editor_mode::object:
				if(auto* part = state.selected(); part != nullptr && part->local_transform.rot != radians){
					part->local_transform.rot = radians;
					return true;
				}
				return false;
			case editor_detail::editor_mode::reference_image:
				if(state.document.reference_image.transform.rot != radians){
					state.document.reference_image.transform.rot = radians;
					return true;
				}
				return false;
			case editor_detail::editor_mode::origin:
				if(state.document.total_transform.rot != radians){
					state.document.total_transform.rot = radians;
					return true;
				}
				return false;
			default:
				return false;
			}
		});
	}

	void set_secondary_x(const float value){
		this->edit_state([value](collision_shape_editor_state& state){
			if(!std::isfinite(value)){
				state.last_error = "value must be finite";
				return false;
			}

			switch(state.mode){
			case editor_detail::editor_mode::edit:{
				auto* part = state.selected();
				if(part == nullptr){
					return false;
				}
				if(value <= 0.f){
					state.last_error = "shape value must be positive";
					return false;
				}

				physics::collision_shape_editor_part candidate = *part;
				switch(candidate.type){
				case physics::shape_type::circle:
					if(candidate.circle.radius == value){
						return false;
					}
					candidate.circle.radius = value;
					break;
				case physics::shape_type::capsule:
					if(candidate.capsule.radius == value){
						return false;
					}
					candidate.capsule.radius = value;
					break;
				case physics::shape_type::box:{
					const float half_width = value * 0.5f;
					if(candidate.box.half_extent.x == half_width){
						return false;
					}
					candidate.box.half_extent.x = half_width;
					break;
				}
				case physics::shape_type::convex_polygon:
					return false;
				default:
					std::unreachable();
				}

				if(!candidate.payload_valid()){
					state.last_error = "edit would make the shape invalid";
					return false;
				}
				*part = std::move(candidate);
				return true;
			}
			case editor_detail::editor_mode::reference_image:{
				if(value <= 0.f){
					state.last_error = "reference image width must be positive";
					return false;
				}
				const float half_width = value * 0.5f;
				if(state.document.reference_image.half_extent.x == half_width){
					return false;
				}
				state.document.reference_image.half_extent.x = half_width;
				return true;
			}
			default:
				return false;
			}
		});
	}

	void set_secondary_y(const float value){
		this->edit_state([value](collision_shape_editor_state& state){
			if(!std::isfinite(value)){
				state.last_error = "value must be finite";
				return false;
			}

			switch(state.mode){
			case editor_detail::editor_mode::edit:{
				auto* part = state.selected();
				if(part == nullptr || part->type != physics::shape_type::box){
					return false;
				}
				if(value <= 0.f){
					state.last_error = "box height must be positive";
					return false;
				}
				const float half_height = value * 0.5f;
				if(part->box.half_extent.y == half_height){
					return false;
				}

				physics::collision_shape_editor_part candidate = *part;
				candidate.box.half_extent.y = half_height;
				if(!candidate.payload_valid()){
					state.last_error = "edit would make the shape invalid";
					return false;
				}
				*part = std::move(candidate);
				return true;
			}
			case editor_detail::editor_mode::reference_image:{
				if(value <= 0.f){
					state.last_error = "reference image height must be positive";
					return false;
				}
				const float half_height = value * 0.5f;
				if(state.document.reference_image.half_extent.y == half_height){
					return false;
				}
				state.document.reference_image.half_extent.y = half_height;
				return true;
			}
			default:
				return false;
			}
		});
	}

	void set_reference_opacity(const float value){
		this->edit_state([value](collision_shape_editor_state& state){
			if(state.mode != editor_detail::editor_mode::reference_image){
				return false;
			}
			if(value < 0.f){
				state.last_error = "reference image opacity must not be negative";
				return false;
			}
			if(state.document.reference_image.opacity == value){
				return false;
			}
			state.document.reference_image.opacity = value;
			return true;
		});
	}

	void set_reference_visible(const bool value){
		this->edit_state([value](collision_shape_editor_state& state){
			if(state.mode != editor_detail::editor_mode::reference_image
				|| state.document.reference_image.enabled == value){
				return false;
			}
			state.document.reference_image.enabled = value;
			return true;
		});
	}

	void set_mirror_x(const bool value){
		this->edit_state([value](collision_shape_editor_state& state){
			if(state.mode != editor_detail::editor_mode::object){
				return false;
			}
			auto* part = state.selected();
			if(part == nullptr || part->mirror.mirror_x == value){
				return false;
			}
			part->mirror.mirror_x = value;
			return true;
		});
	}

	void set_mirror_y(const bool value){
		this->edit_state([value](collision_shape_editor_state& state){
			if(state.mode != editor_detail::editor_mode::object){
				return false;
			}
			auto* part = state.selected();
			if(part == nullptr || part->mirror.mirror_y == value){
				return false;
			}
			part->mirror.mirror_y = value;
			return true;
		});
	}

	void set_mirror_origin_x(const float value){
		this->edit_state([value](collision_shape_editor_state& state){
			if(state.mode != editor_detail::editor_mode::object){
				return false;
			}
			auto* part = state.selected();
			if(part == nullptr || part->mirror.origin.vec.x == value){
				return false;
			}
			part->mirror.origin.vec.x = value;
			return true;
		});
	}

	void set_mirror_origin_y(const float value){
		this->edit_state([value](collision_shape_editor_state& state){
			if(state.mode != editor_detail::editor_mode::object){
				return false;
			}
			auto* part = state.selected();
			if(part == nullptr || part->mirror.origin.vec.y == value){
				return false;
			}
			part->mirror.origin.vec.y = value;
			return true;
		});
	}

	void set_mirror_origin_rot_degrees(const float value){
		const float radians = value * math::deg_to_rad;
		this->edit_state([radians](collision_shape_editor_state& state){
			if(state.mode != editor_detail::editor_mode::object){
				return false;
			}
			auto* part = state.selected();
			if(part == nullptr || part->mirror.origin.rot == radians){
				return false;
			}
			part->mirror.origin.rot = radians;
			return true;
		});
	}
};

bool collision_shape_editor_operation::active() const noexcept{
	return kind != editor_detail::operation_kind::none;
}

float collision_shape_editor_operation::precision_scale() const noexcept{
	return precision_mode ? 0.2f : 1.f;
}

bool collision_shape_editor_operation::has_axis_constraint() const noexcept{
	return !constrain.area();
}

std::optional<float> collision_shape_editor_operation::command_value() const noexcept{
	return editor_detail::operation_command_value(command);
}

math::vec2 collision_shape_editor_operation::move_delta(const math::vec2 cursor) const noexcept{
	const math::vec2 cursor_delta = cursor - initial_cursor;
	const float scale = this->precision_scale();
	if(const auto distance = this->command_value()){
		if(this->has_axis_constraint()){
			return math::vec2{
				constrain.x ? *distance : 0.f,
				constrain.y ? *distance : 0.f
			} * scale;
		}
		if(cursor_delta.length2() <= editor_detail::hit_epsilon * editor_detail::hit_epsilon){
			return {};
		}
		return cursor_delta.copy().normalize() * (*distance * scale);
	}

	return math::vec2{
		constrain.x ? cursor_delta.x : 0.f,
		constrain.y ? cursor_delta.y : 0.f
	} * scale;
}

float collision_shape_editor_operation::rotation_delta(const math::vec2 cursor) const noexcept{
	if(const auto degrees = this->command_value()){
		return -*degrees * math::deg_to_rad;
	}
	return editor_detail::rotation_delta(initial_cursor, cursor, pivot) * this->precision_scale();
}

void collision_shape_editor_operation::reset() noexcept{
	kind = editor_detail::operation_kind::none;
	invalid = false;
	precision_mode = false;
	constrain = {true, true};
	vertex_index.reset();
	command.clear();
}

bool collision_shape_editor_transform_operation::active() const noexcept{
	return kind != editor_detail::operation_kind::none;
}

float collision_shape_editor_transform_operation::precision_scale() const noexcept{
	return precision_mode ? 0.2f : 1.f;
}

bool collision_shape_editor_transform_operation::has_axis_constraint() const noexcept{
	return !constrain.area();
}

std::optional<float> collision_shape_editor_transform_operation::command_value() const noexcept{
	return editor_detail::operation_command_value(command);
}

math::vec2 collision_shape_editor_transform_operation::move_delta(const math::vec2 cursor) const noexcept{
	const math::vec2 cursor_delta = cursor - initial_cursor;
	const float scale = this->precision_scale();
	if(const auto distance = this->command_value()){
		if(this->has_axis_constraint()){
			return math::vec2{
				constrain.x ? *distance : 0.f,
				constrain.y ? *distance : 0.f
			} * scale;
		}
		if(cursor_delta.length2() <= editor_detail::hit_epsilon * editor_detail::hit_epsilon){
			return {};
		}
		return cursor_delta.copy().normalize() * (*distance * scale);
	}

	return math::vec2{
		constrain.x ? cursor_delta.x : 0.f,
		constrain.y ? cursor_delta.y : 0.f
	} * scale;
}

float collision_shape_editor_transform_operation::rotation_delta(const math::vec2 cursor) const noexcept{
	if(const auto degrees = this->command_value()){
		return -*degrees * math::deg_to_rad;
	}
	return editor_detail::rotation_delta(initial_cursor, cursor, pivot) * this->precision_scale();
}

void collision_shape_editor_transform_operation::reset() noexcept{
	kind = editor_detail::operation_kind::none;
	precision_mode = false;
	constrain = {true, true};
	command.clear();
}

collision_shape_editor_state::collision_shape_editor_state(){
	static_cast<void>(this->add_shape(physics::shape_type::box, {}, false));
	this->push_history();
}

std::size_t collision_shape_editor_state::part_count() const noexcept{
	return document.parts.size();
}

physics::collision_shape_editor_part* collision_shape_editor_state::selected() noexcept{
	const auto selected_part = this->current_selected_part_index();
	if(!selected_part || *selected_part >= document.parts.size()){
		return nullptr;
	}
	return std::addressof(document.parts[*selected_part]);
}

const physics::collision_shape_editor_part* collision_shape_editor_state::selected() const noexcept{
	const auto selected_part = this->current_selected_part_index();
	if(!selected_part || *selected_part >= document.parts.size()){
		return nullptr;
	}
	return std::addressof(document.parts[*selected_part]);
}

bool collision_shape_editor_state::operation_active() const noexcept{
	return object_mode.operation.active()
		|| edit_mode.operation.active()
		|| reference_image_mode.operation.active()
		|| origin_mode.operation.active();
}

bool collision_shape_editor_state::reference_image_loaded() const noexcept{
	return static_cast<bool>(reference_image_mode.image_region)
		&& reference_image_mode.loaded_path == document.reference_image.path;
}

std::optional<std::size_t> collision_shape_editor_state::current_selected_part_index() const noexcept{
	switch(mode){
	case editor_detail::editor_mode::object:
		return object_mode.selected_part;
	case editor_detail::editor_mode::edit:
		return edit_mode.selected_part;
	default:
		return std::nullopt;
	}
}

std::string collision_shape_editor_state::selected_text() const{
	const auto* part = this->selected();
	if(part == nullptr){
		return "none";
	}
	if(mode == editor_detail::editor_mode::edit && edit_mode.selected_vertex){
		return std::format(
			"{} {} vertex {}",
			editor_detail::shape_type_name(part->type),
			*edit_mode.selected_part,
			*edit_mode.selected_vertex);
	}
	return std::format("{} {}", editor_detail::shape_type_name(part->type), *this->current_selected_part_index());
}

std::string collision_shape_editor_state::operation_text() const{
	const auto append_operation_text = [](std::string text, const auto& operation){
		if(operation.has_axis_constraint()){
			text += " ";
			if(operation.constrain.x){
				text += "X";
			}
			if(operation.constrain.y){
				text += "Y";
			}
		}
		if(operation.precision_mode){
			text += " fine";
		}
		if(!operation.command.empty()){
			text += " ";
			text += operation.command;
		}
		return text;
	};

	if(const auto* operation = this->current_part_operation(); operation != nullptr && operation->active()){
		return append_operation_text(std::string{editor_detail::operation_name(operation->kind)}, *operation);
	}
	if(const auto* operation = this->current_transform_operation(); operation != nullptr && operation->active()){
		return append_operation_text(std::string{editor_detail::operation_name(operation->kind)}, *operation);
	}
	return "idle";
}

void collision_shape_editor_state::set_mode(const editor_detail::editor_mode new_mode){
	if(mode == new_mode){
		return;
	}
	if(this->operation_active()){
		this->cancel_operation();
	}
	mode = new_mode;
	last_error.clear();
}

std::size_t collision_shape_editor_state::add_shape(
	const physics::shape_type type,
	const math::vec2 position,
	const bool record_history){
	if(this->operation_active()){
		this->cancel_operation();
	}
	const std::size_t index = document.add_shape(type, position);
	object_mode.selected_part = index;
	edit_mode.selected_part = index;
	edit_mode.selected_vertex.reset();
	last_error.clear();
	if(record_history){
		this->push_history();
	}
	return index;
}

void collision_shape_editor_state::select_at(const math::vec2 world_point, const float vertex_radius) noexcept{
	std::optional<std::size_t>* selected_part{};
	std::optional<std::size_t>* selected_vertex{};
	switch(mode){
	case editor_detail::editor_mode::object:
		selected_part = std::addressof(object_mode.selected_part);
		break;
	case editor_detail::editor_mode::edit:
		selected_part = std::addressof(edit_mode.selected_part);
		selected_vertex = std::addressof(edit_mode.selected_vertex);
		break;
	default:
		return;
	}

	selected_part->reset();
	if(selected_vertex != nullptr){
		selected_vertex->reset();
	}

	for(std::size_t index = document.parts.size(); index != 0u; --index){
		const std::size_t part_index = index - 1u;
		const auto& part = document.parts[part_index];
		if(selected_vertex != nullptr){
			if(const auto vertex = editor_detail::polygon_vertex_at(part, world_point, vertex_radius)){
				*selected_part = part_index;
				*selected_vertex = vertex;
				return;
			}
		}
		if(editor_detail::part_contains(part, world_point)){
			*selected_part = part_index;
			return;
		}
	}
}

void collision_shape_editor_state::erase_selected(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	const auto selected_part = this->current_selected_part_index();
	if(!selected_part || *selected_part >= document.parts.size()){
		return;
	}
	document.parts.erase(document.parts.begin() + static_cast<std::ptrdiff_t>(*selected_part));
	object_mode.selected_part.reset();
	edit_mode.selected_part.reset();
	edit_mode.selected_vertex.reset();
	last_error.clear();
	this->push_history();
}

void collision_shape_editor_state::duplicate_selected(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	const auto* part = this->selected();
	if(part == nullptr){
		return;
	}

	auto copy = *part;
	copy.local_transform.vec += math::vec2{24.f, 0.f};
	document.parts.push_back(std::move(copy));
	object_mode.selected_part = document.parts.size() - 1u;
	edit_mode.selected_part = object_mode.selected_part;
	edit_mode.selected_vertex.reset();
	last_error.clear();
	this->push_history();
}

void collision_shape_editor_state::toggle_mirror_x(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	auto* part = this->selected();
	if(part == nullptr){
		return;
	}
	part->mirror.mirror_x = !part->mirror.mirror_x;
	last_error.clear();
	this->push_history();
}

void collision_shape_editor_state::toggle_mirror_y(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	auto* part = this->selected();
	if(part == nullptr){
		return;
	}
	part->mirror.mirror_y = !part->mirror.mirror_y;
	last_error.clear();
	this->push_history();
}

bool collision_shape_editor_state::reset_position_for_current_mode(){
	if(this->operation_active()){
		this->cancel_operation();
	}

	bool changed{};
	switch(mode){
	case editor_detail::editor_mode::object:{
		auto* part = this->selected();
		if(part == nullptr){
			return false;
		}
		changed = part->local_transform.vec.x != 0.f || part->local_transform.vec.y != 0.f;
		part->local_transform.vec = {};
		break;
	}
	case editor_detail::editor_mode::reference_image:
		if(document.reference_image.path.empty()){
			return false;
		}
		changed = document.reference_image.transform.vec.x != 0.f
			|| document.reference_image.transform.vec.y != 0.f;
		document.reference_image.transform.vec = {};
		break;
	case editor_detail::editor_mode::origin:
		changed = document.total_transform.vec.x != 0.f || document.total_transform.vec.y != 0.f;
		document.total_transform.vec = {};
		break;
	default:
		return false;
	}

	last_error.clear();
	if(changed){
		this->push_history();
	}
	return true;
}

bool collision_shape_editor_state::reset_rotation_for_current_mode(){
	if(this->operation_active()){
		this->cancel_operation();
	}

	bool changed{};
	switch(mode){
	case editor_detail::editor_mode::object:{
		auto* part = this->selected();
		if(part == nullptr){
			return false;
		}
		changed = part->local_transform.rot != 0.f;
		part->local_transform.rot = 0.f;
		break;
	}
	case editor_detail::editor_mode::reference_image:
		if(document.reference_image.path.empty()){
			return false;
		}
		changed = document.reference_image.transform.rot != 0.f;
		document.reference_image.transform.rot = 0.f;
		break;
	case editor_detail::editor_mode::origin:
		changed = document.total_transform.rot != 0.f;
		document.total_transform.rot = 0.f;
		break;
	default:
		return false;
	}

	last_error.clear();
	if(changed){
		this->push_history();
	}
	return true;
}

bool collision_shape_editor_state::start_operation(
	const editor_detail::operation_kind kind,
	const math::vec2 cursor){
	if(this->operation_active() || kind == editor_detail::operation_kind::none){
		return false;
	}

	switch(mode){
	case editor_detail::editor_mode::object:{
		if(!object_mode.selected_part || *object_mode.selected_part >= document.parts.size()){
			return false;
		}
		const auto& part = document.parts[*object_mode.selected_part];
		if(!part.payload_valid()){
			last_error = "selected shape payload is invalid";
			return false;
		}
		if(kind == editor_detail::operation_kind::resize){
			return false;
		}
		object_mode.operation = collision_shape_editor_operation{
			.kind = kind,
			.initial_cursor = cursor,
			.pivot = part.local_transform.vec,
			.part_index = *object_mode.selected_part,
			.source_part = part
		};
		break;
	}
	case editor_detail::editor_mode::edit:{
		if(!edit_mode.selected_part || *edit_mode.selected_part >= document.parts.size()){
			return false;
		}
		const auto& part = document.parts[*edit_mode.selected_part];
		if(!part.payload_valid()){
			last_error = "selected shape payload is invalid";
			return false;
		}
		if(kind == editor_detail::operation_kind::move && !edit_mode.selected_vertex){
			last_error = "edit mode move requires a polygon vertex";
			return false;
		}
		if(kind == editor_detail::operation_kind::rotate){
			return false;
		}
		edit_mode.operation = collision_shape_editor_operation{
			.kind = kind,
			.initial_cursor = cursor,
			.pivot = part.local_transform.vec,
			.part_index = *edit_mode.selected_part,
			.vertex_index = edit_mode.selected_vertex,
			.source_part = part
		};
		break;
	}
	case editor_detail::editor_mode::reference_image:{
		if(!document.reference_image.visible() || !this->reference_image_loaded()){
			last_error = "reference image is not loaded";
			return false;
		}
		reference_image_mode.operation = collision_shape_editor_transform_operation{
			.kind = kind,
			.initial_cursor = cursor,
			.pivot = document.reference_image.transform.vec,
			.source_transform = document.reference_image.transform,
			.source_half_extent = document.reference_image.half_extent
		};
		break;
	}
	case editor_detail::editor_mode::origin:{
		if(kind == editor_detail::operation_kind::resize){
			return false;
		}
		origin_mode.operation = collision_shape_editor_transform_operation{
			.kind = kind,
			.initial_cursor = cursor,
			.pivot = document.total_transform.vec,
			.source_transform = document.total_transform
		};
		break;
	}
	default:
		std::unreachable();
	}
	last_error.clear();
	return true;
}

bool collision_shape_editor_state::preview_operation(const math::vec2 cursor){
	if(auto* operation = this->current_part_operation(); operation != nullptr && operation->active()){
		return this->preview_part_operation(*operation, cursor);
	}
	if(auto* operation = this->current_transform_operation(); operation != nullptr && operation->active()){
		return this->preview_transform_operation(*operation, cursor);
	}
	return true;
}

bool collision_shape_editor_state::commit_operation(){
	if(auto* operation = this->current_part_operation(); operation != nullptr && operation->active()){
		if(operation->part_index >= document.parts.size()){
			operation->reset();
			object_mode.selected_part.reset();
			edit_mode.selected_part.reset();
			edit_mode.selected_vertex.reset();
			last_error = "operation target no longer exists";
			return false;
		}

		if(operation->invalid){
			if(operation->part_index < document.parts.size()){
				document.parts[operation->part_index] = operation->source_part;
			}
			operation->reset();
			return false;
		}

		operation->reset();
		this->push_history();
		return true;
	}
	if(auto* operation = this->current_transform_operation(); operation != nullptr && operation->active()){
		operation->reset();
		this->push_history();
		return true;
	}
	return false;
}

void collision_shape_editor_state::cancel_operation(){
	if(auto* operation = this->current_part_operation(); operation != nullptr && operation->active()){
		if(operation->part_index < document.parts.size()){
			document.parts[operation->part_index] = operation->source_part;
		}
		operation->reset();
		last_error.clear();
		return;
	}
	if(reference_image_mode.operation.active()){
		document.reference_image.transform = reference_image_mode.operation.source_transform;
		document.reference_image.half_extent = reference_image_mode.operation.source_half_extent;
		reference_image_mode.operation.reset();
		last_error.clear();
		return;
	}
	if(origin_mode.operation.active()){
		document.total_transform = origin_mode.operation.source_transform;
		origin_mode.operation.reset();
		last_error.clear();
		return;
	}
}

bool collision_shape_editor_state::set_operation_precision(
	const bool precision_mode,
	const math::vec2 cursor){
	auto* part_operation = this->current_part_operation();
	auto* transform_operation = this->current_transform_operation();
	if(part_operation == nullptr && transform_operation == nullptr){
		return false;
	}
	if(part_operation != nullptr && !part_operation->active()){
		part_operation = nullptr;
	}
	if(transform_operation != nullptr && !transform_operation->active()){
		transform_operation = nullptr;
	}
	if(part_operation == nullptr && transform_operation == nullptr){
		return false;
	}
	if(part_operation != nullptr && part_operation->precision_mode == precision_mode){
		return true;
	}
	if(transform_operation != nullptr && transform_operation->precision_mode == precision_mode){
		return true;
	}
	if(!this->save_operation_mid_data(cursor)){
		return false;
	}
	if(part_operation != nullptr){
		part_operation->precision_mode = precision_mode;
	}else{
		transform_operation->precision_mode = precision_mode;
	}
	return true;
}

bool collision_shape_editor_state::toggle_operation_constraint(const math::bool2 constrain) noexcept{
	if(auto* operation = this->current_part_operation(); operation != nullptr && operation->active()){
		operation->constrain = operation->constrain == constrain ? math::bool2{true, true} : constrain;
		last_error.clear();
		return true;
	}
	if(auto* operation = this->current_transform_operation(); operation != nullptr && operation->active()){
		operation->constrain = operation->constrain == constrain ? math::bool2{true, true} : constrain;
		last_error.clear();
		return true;
	}
	return false;
}

bool collision_shape_editor_state::input_operation_character(const char32_t value){
	if(!this->operation_active() || value > static_cast<char32_t>(std::numeric_limits<unsigned char>::max())){
		return false;
	}

	const char input = static_cast<char>(value);
	if(!editor_detail::operation_command_char_allowed(input)){
		return false;
	}

	if(auto* operation = this->current_part_operation(); operation != nullptr && operation->active()){
		operation->command.push_back(input);
		last_error.clear();
		return true;
	}
	if(auto* operation = this->current_transform_operation(); operation != nullptr && operation->active()){
		operation->command.push_back(input);
		last_error.clear();
		return true;
	}
	return false;
}

bool collision_shape_editor_state::erase_operation_character(){
	if(auto* operation = this->current_part_operation(); operation != nullptr && operation->active()){
		if(!operation->command.empty()){
			operation->command.pop_back();
			last_error.clear();
		}
		return true;
	}
	if(auto* operation = this->current_transform_operation(); operation != nullptr && operation->active()){
		if(!operation->command.empty()){
			operation->command.pop_back();
			last_error.clear();
		}
		return true;
	}
	return false;
}

bool collision_shape_editor_state::clear_operation_command() noexcept{
	if(auto* operation = this->current_part_operation(); operation != nullptr && operation->active()){
		if(operation->command.empty()){
			return false;
		}
		operation->command.clear();
		last_error.clear();
		return true;
	}
	if(auto* operation = this->current_transform_operation(); operation != nullptr && operation->active()){
		if(operation->command.empty()){
			return false;
		}
		operation->command.clear();
		last_error.clear();
		return true;
	}
	return false;
}

void collision_shape_editor_state::push_history(){
	history.push(collision_shape_editor_history_entry{
		.document = document,
		.mode = mode,
		.object_selected_part = object_mode.selected_part,
		.edit_selected_part = edit_mode.selected_part,
		.edit_selected_vertex = edit_mode.selected_vertex
	});
}

void collision_shape_editor_state::undo(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	if(!history.has_prev()){
		return;
	}
	history.to_prev();
	this->apply_history_entry(history.current());
}

void collision_shape_editor_state::redo(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	if(!history.has_next()){
		return;
	}
	history.to_next();
	this->apply_history_entry(history.current());
}

void collision_shape_editor_state::set_reference_image(
	std::filesystem::path path,
	graphic::allocated_image_region&& region,
	const math::vec2 position){
	if(this->operation_active()){
		this->cancel_operation();
	}

	const math::vec2 image_size = region.uv.get_region_size<float>();
	const math::vec2 half_extent = image_size * 0.5f;
	document.reference_image = {
		.enabled = true,
		.path = path.make_preferred().string(),
		.transform = {position, 0.f},
		.half_extent = half_extent,
		.opacity = document.reference_image.opacity > 0.f ? document.reference_image.opacity : 0.35f
	};
	reference_image_mode.image_region = std::move(region);
	reference_image_mode.loaded_path = document.reference_image.path;
	mode = editor_detail::editor_mode::reference_image;
	last_error.clear();
	this->push_history();
}

void collision_shape_editor_state::clear_reference_image(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	document.reference_image = {};
	reference_image_mode.image_region = {};
	reference_image_mode.loaded_path.clear();
	last_error.clear();
	this->push_history();
}

void collision_shape_editor_state::apply_history_entry(const collision_shape_editor_history_entry& entry){
	document = entry.document;
	mode = entry.mode;
	object_mode.selected_part = entry.object_selected_part;
	edit_mode.selected_part = entry.edit_selected_part;
	edit_mode.selected_vertex = entry.edit_selected_vertex;
	this->validate_selection();
	if(!document.reference_image.visible()){
		reference_image_mode.image_region = {};
		reference_image_mode.loaded_path.clear();
	}else if(reference_image_mode.loaded_path != document.reference_image.path){
		reference_image_mode.image_region = {};
		reference_image_mode.loaded_path.clear();
		last_error = "reference image resource is not loaded for this history entry";
		return;
	}
	last_error.clear();
}

bool collision_shape_editor_state::save_operation_mid_data(const math::vec2 cursor){
	if(!this->preview_operation(cursor)){
		return false;
	}

	if(auto* operation = this->current_part_operation(); operation != nullptr && operation->active()){
		if(operation->part_index >= document.parts.size()){
			return false;
		}
		operation->source_part = document.parts[operation->part_index];
		operation->initial_cursor = cursor;
		operation->invalid = false;
	}else if(reference_image_mode.operation.active()){
		reference_image_mode.operation.source_transform = document.reference_image.transform;
		reference_image_mode.operation.source_half_extent = document.reference_image.half_extent;
		reference_image_mode.operation.initial_cursor = cursor;
	}else if(origin_mode.operation.active()){
		origin_mode.operation.source_transform = document.total_transform;
		origin_mode.operation.initial_cursor = cursor;
	}else{
		return false;
	}
	last_error.clear();
	return true;
}

void collision_shape_editor_state::validate_selection() noexcept{
	if(!object_mode.selected_part || *object_mode.selected_part >= document.parts.size()){
		object_mode.selected_part.reset();
	}
	if(!edit_mode.selected_part || *edit_mode.selected_part >= document.parts.size()){
		edit_mode.selected_part.reset();
		edit_mode.selected_vertex.reset();
		return;
	}
	const auto& part = document.parts[*edit_mode.selected_part];
	if(part.type != physics::shape_type::convex_polygon
		|| !edit_mode.selected_vertex
		|| *edit_mode.selected_vertex >= part.convex_polygon.vertices.size()){
		edit_mode.selected_vertex.reset();
	}
}

collision_shape_editor_operation* collision_shape_editor_state::current_part_operation() noexcept{
	switch(mode){
	case editor_detail::editor_mode::object:
		return std::addressof(object_mode.operation);
	case editor_detail::editor_mode::edit:
		return std::addressof(edit_mode.operation);
	default:
		return nullptr;
	}
}

collision_shape_editor_transform_operation* collision_shape_editor_state::current_transform_operation() noexcept{
	switch(mode){
	case editor_detail::editor_mode::reference_image:
		return std::addressof(reference_image_mode.operation);
	case editor_detail::editor_mode::origin:
		return std::addressof(origin_mode.operation);
	default:
		return nullptr;
	}
}

const collision_shape_editor_operation* collision_shape_editor_state::current_part_operation() const noexcept{
	return const_cast<collision_shape_editor_state*>(this)->current_part_operation();
}

const collision_shape_editor_transform_operation*
collision_shape_editor_state::current_transform_operation() const noexcept{
	return const_cast<collision_shape_editor_state*>(this)->current_transform_operation();
}

bool collision_shape_editor_state::preview_part_operation(
	collision_shape_editor_operation& target,
	const math::vec2 cursor){
	if(!target.active()){
		return true;
	}
	if(target.part_index >= document.parts.size()){
		target.reset();
		object_mode.selected_part.reset();
		edit_mode.selected_part.reset();
		edit_mode.selected_vertex.reset();
		last_error = "operation target no longer exists";
		return false;
	}

	auto candidate = target.source_part;
	switch(target.kind){
	case editor_detail::operation_kind::move:
		if(target.vertex_index){
			if(candidate.type != physics::shape_type::convex_polygon
				|| *target.vertex_index >= candidate.convex_polygon.vertices.size()){
				last_error = "selected polygon vertex no longer exists";
				return false;
			}
			const math::vec2 delta = target.move_delta(cursor);
			const math::vec2 source_world =
				candidate.convex_polygon.vertices[*target.vertex_index] >> candidate.local_transform;
			candidate.convex_polygon.vertices[*target.vertex_index] =
				candidate.local_transform.apply_inv_to(source_world + delta);
		}else{
			candidate.local_transform.vec = target.source_part.local_transform.vec + target.move_delta(cursor);
		}
		break;
	case editor_detail::operation_kind::rotate:
		candidate.local_transform.rot = target.source_part.local_transform.rot + target.rotation_delta(cursor);
		break;
	case editor_detail::operation_kind::resize:{
		const math::vec2 local_cursor = target.source_part.local_transform.apply_inv_to(cursor);
		constexpr float minimum_extent = 1.0e-3f;
		switch(candidate.type){
		case physics::shape_type::circle:{
			const float radius = target.command_value().value_or(local_cursor.length());
			candidate.circle.radius = std::max(radius, minimum_extent);
			break;
		}
		case physics::shape_type::capsule:{
			const float radius = target.command_value().value_or(
				std::sqrt(editor_detail::point_segment_distance2(
					local_cursor,
					target.source_part.capsule.begin,
					target.source_part.capsule.end)));
			candidate.capsule.radius = std::max(radius, minimum_extent);
			break;
		}
		case physics::shape_type::box:{
			if(const auto size = target.command_value()){
				if(target.has_axis_constraint()){
					candidate.box.half_extent.x = target.constrain.x
						? std::max(*size, minimum_extent)
						: target.source_part.box.half_extent.x;
					candidate.box.half_extent.y = target.constrain.y
						? std::max(*size, minimum_extent)
						: target.source_part.box.half_extent.y;
				}else{
					candidate.box.half_extent = math::vec2{}.set(std::max(*size, minimum_extent));
				}
			}else{
				candidate.box.half_extent = {
					target.constrain.x ? std::max(std::abs(local_cursor.x), minimum_extent) : target.source_part.box.half_extent.x,
					target.constrain.y ? std::max(std::abs(local_cursor.y), minimum_extent) : target.source_part.box.half_extent.y
				};
			}
			break;
		}
		case physics::shape_type::convex_polygon:
			last_error = "polygon resize is not supported; move vertices instead";
			return false;
		default:
			std::unreachable();
		}
		break;
	}
	case editor_detail::operation_kind::none:
		return true;
	default:
		std::unreachable();
	}

	if(!candidate.payload_valid()){
		document.parts[target.part_index] = target.source_part;
		target.invalid = true;
		last_error = "edit would make the shape invalid";
		return false;
	}

	document.parts[target.part_index] = std::move(candidate);
	target.invalid = false;
	last_error.clear();
	return true;
}

bool collision_shape_editor_state::preview_transform_operation(
	collision_shape_editor_transform_operation& target,
	const math::vec2 cursor){
	if(!target.active()){
		return true;
	}

	switch(mode){
	case editor_detail::editor_mode::reference_image:
		switch(target.kind){
		case editor_detail::operation_kind::move:
			document.reference_image.transform.vec = target.source_transform.vec + target.move_delta(cursor);
			break;
		case editor_detail::operation_kind::rotate:
			document.reference_image.transform.rot = target.source_transform.rot + target.rotation_delta(cursor);
			break;
		case editor_detail::operation_kind::resize:{
			if(const auto size = target.command_value()){
				const math::vec2 scale = target.has_axis_constraint()
					? math::vec2{
						target.constrain.x ? *size : 1.f,
						target.constrain.y ? *size : 1.f
					}
					: math::vec2{}.set(*size);
				document.reference_image.half_extent = {
					std::max(target.source_half_extent.x * scale.x, 1.0e-3f),
					std::max(target.source_half_extent.y * scale.y, 1.0e-3f)
				};
			}else{
				math::vec2 local_cursor = cursor - target.source_transform.vec;
				local_cursor.rotate_rad(-target.source_transform.rot);
				document.reference_image.half_extent = {
					target.constrain.x ? std::max(std::abs(local_cursor.x), 1.0e-3f) : target.source_half_extent.x,
					target.constrain.y ? std::max(std::abs(local_cursor.y), 1.0e-3f) : target.source_half_extent.y
				};
			}
			break;
		}
		case editor_detail::operation_kind::none:
			return true;
		default:
			std::unreachable();
		}
		last_error.clear();
		return true;
	case editor_detail::editor_mode::origin:
		switch(target.kind){
		case editor_detail::operation_kind::move:
			document.total_transform.vec = target.source_transform.vec + target.move_delta(cursor);
			break;
		case editor_detail::operation_kind::rotate:
			document.total_transform.rot = target.source_transform.rot + target.rotation_delta(cursor);
			break;
		case editor_detail::operation_kind::none:
			return true;
		default:
			std::unreachable();
		}
		last_error.clear();
		return true;
	default:
		return true;
	}
}

void collision_shape_editor_viewport::reference_image_path_listener::on_update(
	react_flow::data_carrier<std::span<const std::filesystem::path>>& data){
	const auto paths = data.get();
	if(paths.empty() || viewport == nullptr){
		return;
	}



	viewport->close_reference_image_file_selector();
	viewport->load_reference_image(paths.front());
}

collision_shape_editor_viewport::collision_shape_editor_viewport(gui::scene& scene, gui::elem* parent)
	: gui::viewport(scene, parent),
	reference_image_path_node_{this}{
	this->set_style(gui::style::family_variant::general_static);
	camera.set_scale_range({0.0625f, 4.f});
}

bool collision_shape_editor_viewport::update(const float delta_in_ticks){
	if(!gui::viewport::update(delta_in_ticks)){
		return false;
	}
	if(state.operation_active()){
		static_cast<void>(state.preview_operation(this->cursor_world_pos()));
	}
	return true;
}

gui::events::op_afterwards collision_shape_editor_viewport::on_click(
	const gui::events::click event,
	std::span<gui::elem* const> aboves){
	this->refresh_cursor_cache_from_local(event.pos);
	if(add_menu_overlay_ != nullptr
		&& event.key.action == input_handle::act::press
		&& !editor_detail::contains_inbound_elem(*add_menu_overlay_, this->get_scene().get_inbounds())){
		this->close_add_menu();
		return gui::events::op_afterwards::intercepted;
	}
	if(reference_image_file_overlay_ != nullptr
		&& event.key.action == input_handle::act::press
		&& !editor_detail::contains_inbound_elem(
			*reference_image_file_overlay_,
			this->get_scene().get_inbounds())){
		this->close_reference_image_file_selector();
		return gui::events::op_afterwards::intercepted;
	}

	if(event.key.as_mouse() == input_handle::mouse::LMB && event.within_elem(*this)){
		const math::vec2 world_pos = this->cursor_world_pos();
		if(state.operation_active()){
			if(event.key.action == input_handle::act::release){
				static_cast<void>(state.preview_operation(world_pos));
				static_cast<void>(state.commit_operation());
			}
			return gui::events::op_afterwards::intercepted;
		}
		if(event.key.action == input_handle::act::press){
			state.select_at(world_pos, this->selection_radius());
			return gui::events::op_afterwards::intercepted;
		}
	}
	return gui::viewport::on_click(event, aboves);
}

gui::events::op_afterwards collision_shape_editor_viewport::on_drag(const gui::events::drag event){
	this->refresh_cursor_cache_from_local(event.dst);
	if(event.key.as_mouse() == input_handle::mouse::LMB && state.operation_active()){
		static_cast<void>(state.preview_operation(this->cursor_world_pos()));
		return gui::events::op_afterwards::intercepted;
	}
	return gui::viewport::on_drag(event);
}

gui::events::op_afterwards collision_shape_editor_viewport::on_cursor_moved(const gui::events::cursor_move event){
	this->refresh_cursor_cache_from_local(event.dst);
	return gui::viewport::on_cursor_moved(event);
}

gui::events::op_afterwards collision_shape_editor_viewport::on_key_input(const input_handle::key_set key){
	const math::vec2 cursor = this->cursor_world_pos();

	if(state.operation_active()){
		switch(key.as_key()){
		case input_handle::key::left_shift:
		case input_handle::key::right_shift:
			if(key.action == input_handle::act::press || key.action == input_handle::act::release){
				static_cast<void>(state.set_operation_precision(key.action == input_handle::act::press, cursor));
				static_cast<void>(state.preview_operation(cursor));
				return gui::events::op_afterwards::intercepted;
			}
			break;
		case input_handle::key::x:
			if(key.action == input_handle::act::release){
				static_cast<void>(state.toggle_operation_constraint({true, false}));
				static_cast<void>(state.preview_operation(cursor));
				return gui::events::op_afterwards::intercepted;
			}
			break;
		case input_handle::key::y:
			if(key.action == input_handle::act::release){
				static_cast<void>(state.toggle_operation_constraint({false, true}));
				static_cast<void>(state.preview_operation(cursor));
				return gui::events::op_afterwards::intercepted;
			}
			break;
		case input_handle::key::backspace:
			if(key.action == input_handle::act::press || key.action == input_handle::act::repeat){
				static_cast<void>(state.erase_operation_character());
				static_cast<void>(state.preview_operation(cursor));
				return gui::events::op_afterwards::intercepted;
			}
			break;
		case input_handle::key::del:
			if(key.action == input_handle::act::press){
				return gui::events::op_afterwards::intercepted;
			}
			break;
		case input_handle::key::enter:
			if(key.action == input_handle::act::press){
				static_cast<void>(state.preview_operation(cursor));
				static_cast<void>(state.commit_operation());
				return gui::events::op_afterwards::intercepted;
			}
			break;
		default:
			break;
		}
	}

	if(key.action == input_handle::act::press){
		switch(key.as_key()){
		case input_handle::key::_1:
			state.set_mode(editor_detail::editor_mode::object);
			return gui::events::op_afterwards::intercepted;
		case input_handle::key::_2:
			state.set_mode(editor_detail::editor_mode::edit);
			return gui::events::op_afterwards::intercepted;
		case input_handle::key::_3:
			state.set_mode(editor_detail::editor_mode::reference_image);
			return gui::events::op_afterwards::intercepted;
		case input_handle::key::_4:
			state.set_mode(editor_detail::editor_mode::origin);
			return gui::events::op_afterwards::intercepted;
		case input_handle::key::a:
			if(input_handle::matched(key.mode_bits, input_handle::mode::shift)){
				this->show_add_menu();
				return gui::events::op_afterwards::intercepted;
			}
			break;
		case input_handle::key::f:
			if(state.mode == editor_detail::editor_mode::reference_image){
				this->show_reference_image_file_selector();
				return gui::events::op_afterwards::intercepted;
			}
			break;
		case input_handle::key::d:
			if(input_handle::matched(key.mode_bits, input_handle::mode::ctrl)
				|| input_handle::matched(key.mode_bits, input_handle::mode::shift)){
				state.duplicate_selected();
				return gui::events::op_afterwards::intercepted;
			}
			break;
		case input_handle::key::del:
			state.erase_selected();
			return gui::events::op_afterwards::intercepted;
		case input_handle::key::backspace:
			if(!state.operation_active()){
				state.erase_selected();
				return gui::events::op_afterwards::intercepted;
			}
			break;
		case input_handle::key::z:
			if(input_handle::matched(key.mode_bits, input_handle::mode::ctrl_shift)){
				state.redo();
				return gui::events::op_afterwards::intercepted;
			}
			if(input_handle::matched(key.mode_bits, input_handle::mode::ctrl)){
				state.undo();
				return gui::events::op_afterwards::intercepted;
			}
			break;
		default:
			break;
		}
	}

	if(key.action == input_handle::act::release && !state.operation_active()){
		switch(key.as_key()){
		case input_handle::key::g:
			if(input_handle::matched(key.mode_bits, input_handle::mode::alt)){
				if(state.reset_position_for_current_mode()){
					return gui::events::op_afterwards::intercepted;
				}
				break;
			}
			if(state.start_operation(editor_detail::operation_kind::move, cursor)){
				return gui::events::op_afterwards::intercepted;
			}
			break;
		case input_handle::key::r:
			if(input_handle::matched(key.mode_bits, input_handle::mode::alt)){
				if(state.reset_rotation_for_current_mode()){
					return gui::events::op_afterwards::intercepted;
				}
				break;
			}
			if(state.start_operation(editor_detail::operation_kind::rotate, cursor)){
				return gui::events::op_afterwards::intercepted;
			}
			break;
		case input_handle::key::s:
			if(state.start_operation(editor_detail::operation_kind::resize, cursor)){
				return gui::events::op_afterwards::intercepted;
			}
			break;
		default:
			break;
		}
	}

	return gui::events::op_afterwards::fall_through;
}

gui::events::op_afterwards collision_shape_editor_viewport::on_unicode_input(const char32_t value){
	if(state.input_operation_character(value)){
		static_cast<void>(state.preview_operation(this->cursor_world_pos()));
		return gui::events::op_afterwards::intercepted;
	}
	return gui::events::op_afterwards::fall_through;
}

gui::events::op_afterwards collision_shape_editor_viewport::on_esc(){
	if(state.operation_active()){
		if(!state.clear_operation_command()){
			state.cancel_operation();
		}else{
			static_cast<void>(state.preview_operation(this->cursor_world_pos()));
		}
		return gui::events::op_afterwards::intercepted;
	}
	if(add_menu_overlay_ != nullptr){
		this->close_add_menu();
		return gui::events::op_afterwards::intercepted;
	}
	if(reference_image_file_overlay_ != nullptr){
		this->close_reference_image_file_selector();
		return gui::events::op_afterwards::intercepted;
	}
	if(state.object_mode.selected_part || state.edit_mode.selected_part){
		state.object_mode.selected_part.reset();
		state.edit_mode.selected_part.reset();
		state.edit_mode.selected_vertex.reset();
		return gui::events::op_afterwards::intercepted;
	}
	return gui::viewport::on_esc();
}

void collision_shape_editor_viewport::record_draw_layer(gui::draw_recorder& call_stack_builder) const{
	gui::viewport::record_draw_layer(call_stack_builder);
	call_stack_builder.push_call_noop(
		*this,
		[](const collision_shape_editor_viewport& self, const gui::draw_call_param&, const gui::draw_immut_args& args) static{
			if(!args.layer.is_top()){
				return;
			}

			self.draw_editor_content();
		});
}

float collision_shape_editor_viewport::selection_radius() const noexcept{
	return 9.f / camera.get_scale();
}

math::vec2 collision_shape_editor_viewport::cursor_world_pos() const noexcept{
	return last_cursor_world_pos_;
}

math::vec2 collision_shape_editor_viewport::local_world_pos(const math::vec2 local_pos) const noexcept{
	return this->get_transferred_pos(local_pos - this->content_src_offset());
}

void collision_shape_editor_viewport::open_reference_image_file_selector(){
	this->show_reference_image_file_selector();
}

void collision_shape_editor_viewport::show_add_menu(){
	this->close_add_menu();
	pending_add_position_ = this->cursor_world_pos();
	auto result = this->get_scene().create_overlay(
		{
			.extent = gui::layout::extent_by_external,
			.align = align::pos::top_left,
			.absolute_offset = last_cursor_scene_pos_ + math::vec2{10.f, 10.f}
		},
		[this](gui::table& menu){
			const auto add_menu_button = [this](
				gui::table& target_menu,
				const std::string_view text,
				const physics::shape_type type){
				target_menu.create_back([this, text, type](gui::button<gui::direct_label>& b){
					b.set_style(gui::style::family_variant::base_only);
					b.set_fit_type(gui::label_fit_type::scl);
					b.text_entire_align = align::pos::center;
					b.set_tokenized_text({text});
					b.set_button_callback([this, type]{
						(void)state.add_shape(type, pending_add_position_);
						this->close_add_menu();
					});
				});
			};

			menu.set_style();
			menu.set_layout_spec(gui::layout::layout_policy::vert_major);
			menu.set_expand_policy(gui::layout::expand_policy::resize_to_fit);
			menu.set_entire_align(align::pos::top_left);
			menu.template_cell.set_size({112.f, 44.f}).set_pad(4.f);
			add_menu_button(menu, "Circle", physics::shape_type::circle);
			add_menu_button(menu, "Capsule", physics::shape_type::capsule);
			add_menu_button(menu, "Box", physics::shape_type::box);
			add_menu_button(menu, "Polygon", physics::shape_type::convex_polygon);
		});
	add_menu_overlay_ = std::addressof(result.elem());
}

void collision_shape_editor_viewport::close_add_menu(){
	if(add_menu_overlay_ != nullptr){
		this->get_scene().close_overlay(add_menu_overlay_);
		add_menu_overlay_ = nullptr;
	}
}

void collision_shape_editor_viewport::show_reference_image_file_selector(){
	this->close_reference_image_file_selector();
	auto result = this->get_scene().create_overlay(
		{
			.extent = {
				{gui::layout::size_category::passive, 0.88f},
				{gui::layout::size_category::passive, 0.88f}
			},
			.align = align::pos::center
		},
		[this](gui::cpd::file_selector& selector){
			selector.set_cared_suffix({".png", ".jpg", ".jpeg", ".bmp", ".tga"});
			selector.get_prov().connect_successor(reference_image_path_node_.node);
		});
	reference_image_file_overlay_ = std::addressof(result.elem());
}

void collision_shape_editor_viewport::close_reference_image_file_selector(){
	if(reference_image_file_overlay_ != nullptr){
		this->get_scene().close_overlay(reference_image_file_overlay_);
		reference_image_file_overlay_ = nullptr;
	}
}

void collision_shape_editor_viewport::load_reference_image(const std::filesystem::path& path){
	if(collision_shape_editor_reference_images.page == nullptr || collision_shape_editor_reference_images.atlas == nullptr){
		state.last_error = "reference image page is not configured";
		return;
	}

	try{
		auto bitmap = graphic::load_bitmap(path.string());
		auto region = collision_shape_editor_reference_images.page->async_allocate(
			graphic::image_load_description{graphic::bitmap_load{std::move(bitmap)}});
		collision_shape_editor_reference_images.atlas->wait_load();
		state.set_reference_image(path, std::move(region), this->cursor_world_pos());
	}catch(const std::exception& e){
		state.last_error = std::format("failed to load reference image: {}", e.what());
	}
}

void collision_shape_editor_viewport::refresh_cursor_cache_from_local(const math::vec2 local_pos) noexcept{
	last_cursor_scene_pos_ = gui::util::transform_local2scene(*this, local_pos);
	last_cursor_world_pos_ = this->local_world_pos(local_pos);
}

void collision_shape_editor_viewport::draw_editor_content() const{
	namespace instr = graphic::draw::instruction;

	auto& renderer = this->renderer();
	renderer.update_state(gui::fx::batch_draw_mode::def);

	this->viewport_begin();

	const auto draw_grid = [this]{
		constexpr float grid_step = 100.f;
		constexpr float stroke = 1.f;
		const auto viewport = camera.get_viewport();
		const math::vec2 v00 = viewport.vert_00();
		const math::vec2 v11 = viewport.vert_11();
		const float begin_x = std::floor(v00.x / grid_step) * grid_step;
		const float begin_y = std::floor(v00.y / grid_step) * grid_step;
		const auto color = graphic::colors::dark_gray.copy_set_a(0.35f);

		for(float x = begin_x; x <= v11.x; x += grid_step){
			this->renderer().push(instr::line{
				.src = {x, v00.y},
				.dst = {x, v11.y},
				.color = {color, color},
				.stroke = stroke
			});
		}
		for(float y = begin_y; y <= v11.y; y += grid_step){
			this->renderer().push(instr::line{
				.src = {v00.x, y},
				.dst = {v11.x, y},
				.color = {color, color},
				.stroke = stroke
			});
		}
	};

	const auto draw_part = [this](
		const physics::collision_shape_editor_part& part,
		const math::trans2 transform,
		const draw::collision_shape_draw_style& fill_style,
		const draw::collision_shape_draw_style& outline_style){
		auto& part_renderer = this->renderer();
		switch(part.type){
		case physics::shape_type::circle:
			draw::fill_shape(part_renderer, part.circle, transform, fill_style);
			draw::draw_shape(part_renderer, part.circle, transform, outline_style);
			return;
		case physics::shape_type::capsule:
			draw::fill_shape(part_renderer, part.capsule, transform, fill_style);
			draw::draw_shape(part_renderer, part.capsule, transform, outline_style);
			return;
		case physics::shape_type::box:
			draw::fill_shape(part_renderer, part.box, transform, fill_style);
			draw::draw_shape(part_renderer, part.box, transform, outline_style);
			return;
		case physics::shape_type::convex_polygon:
			draw::fill_shape(part_renderer, part.convex_polygon, transform, fill_style);
			draw::draw_shape(part_renderer, part.convex_polygon, transform, outline_style);
			return;
		default:
			return;
		}
	};

	const auto draw_selected_handles = [this](const physics::collision_shape_editor_part& part, const math::trans2 transform){
		if(part.type != physics::shape_type::convex_polygon){
			return;
		}

		const float radius = this->selection_radius() * 0.7f;
		const draw::collision_shape_draw_style handle_style{
			.color = graphic::colors::light_gray.copy_set_a(0.95f),
			.stroke = 2.f,
			.depth = 1.f
		};
		const draw::collision_shape_draw_style selected_handle_style{
			.color = graphic::colors::ORANGE.copy_set_a(0.98f),
			.stroke = 2.5f,
			.depth = 2.f
		};

		for(std::size_t index = 0u; index != part.convex_polygon.vertices.size(); ++index){
			const math::vec2 vertex = part.convex_polygon.vertices[index] >> transform;
			const auto& style = state.edit_mode.selected_vertex == index ? selected_handle_style : handle_style;
			draw::fill_shape(this->renderer(), physics::circle_shape{radius}, math::trans2{vertex, 0.f}, style);
			draw::draw_shape(this->renderer(), physics::circle_shape{radius}, math::trans2{vertex, 0.f}, style);
		}
	};

	const auto draw_mirror_axes = [this](const physics::collision_shape_editor_mirror_modifier& mirror){
		const math::trans2 mirror_transform = mirror.origin;
		const math::vec2 origin = mirror_transform.vec;
		const auto push_axis = [this, origin, mirror_transform](math::vec2 direction, const graphic::color color){
			direction.rotate_rad(mirror_transform.rot);
			this->renderer().push(instr::line{
				.src = origin - direction * 10000.f,
				.dst = origin + direction * 10000.f,
				.color = {color, color},
				.stroke = 2.f
			});
		};

		if(mirror.mirror_x){
			push_axis({0.f, 1.f}, graphic::colors::CRIMSON.copy_set_a(0.75f));
		}
		if(mirror.mirror_y){
			push_axis({1.f, 0.f}, graphic::colors::LIME.copy_set_a(0.75f));
		}
	};

	const auto draw_transform_axes = [this](const math::trans2 transform, const float stroke){
		math::vec2 x_axis{1.f, 0.f};
		math::vec2 y_axis{0.f, 1.f};
		x_axis.rotate_rad(transform.rot);
		y_axis.rotate_rad(transform.rot);
		this->renderer().push(instr::line{
			.src = transform.vec - x_axis * 10000.f,
			.dst = transform.vec + x_axis * 10000.f,
			.color = {
				graphic::colors::CRIMSON.copy_set_a(0.45f),
				graphic::colors::CRIMSON.copy_set_a(0.45f)
			},
			.stroke = stroke
		});
		this->renderer().push(instr::line{
			.src = transform.vec - y_axis * 10000.f,
			.dst = transform.vec + y_axis * 10000.f,
			.color = {
				graphic::colors::LIME.copy_set_a(0.45f),
				graphic::colors::LIME.copy_set_a(0.45f)
			},
			.stroke = stroke
		});
	};

	const auto draw_reference_image = [this]{
		if(!state.document.reference_image.visible() || !state.reference_image_loaded()){
			return;
		}

		const auto& reference = state.document.reference_image;
		const auto& region = state.reference_image_mode.image_region;
		const math::vec2 half = reference.half_extent;
		const std::array vertices{
			math::vec2{-half.x, -half.y} >> reference.transform,
			math::vec2{half.x, -half.y} >> reference.transform,
			math::vec2{-half.x, half.y} >> reference.transform,
			math::vec2{half.x, half.y} >> reference.transform
		};
		const auto color = graphic::colors::white.copy_set_a(std::clamp(reference.opacity, 0.f, 1.f));
		this->renderer().push(instr::quad{
			.generic = {
				.image = region.texture_binding(),
				.mode = {},
				.depth = -8.f
			},
			.vert = vertices,
			.uv = {
				region.uv.v00(),
				region.uv.v10(),
				region.uv.v01(),
				region.uv.v11()
			},
			.vert_color = {color}
		});

		if(state.mode == editor_detail::editor_mode::reference_image){
			const graphic::color outline = graphic::colors::ORANGE.copy_set_a(0.75f);
			for(std::size_t index = 0u; index != vertices.size(); ++index){
				this->renderer().push(instr::line{
					.src = vertices[index],
					.dst = vertices[(index + 1u) % vertices.size()],
					.color = {outline, outline},
					.stroke = 3.f
				});
			}
		}
	};

	draw_grid();
	draw_reference_image();

	const draw::collision_shape_draw_style fill_style{
		.color = graphic::colors::gray.copy_set_a(0.22f),
		.stroke = 1.f,
		.depth = -2.f
	};
	const draw::collision_shape_draw_style mirrored_fill_style{
		.color = graphic::colors::ORANGE.copy_set_a(0.16f),
		.stroke = 1.f,
		.depth = -3.f
	};
	const draw::collision_shape_draw_style outline_style{
		.color = graphic::colors::light_gray.copy_set_a(0.85f),
		.stroke = 3.f,
		.depth = -1.f
	};
	const draw::collision_shape_draw_style mirrored_outline_style{
		.color = graphic::colors::ORANGE.copy_set_a(0.65f),
		.stroke = 2.5f,
		.depth = -1.f
	};
	const draw::collision_shape_draw_style selected_style{
		.color = graphic::colors::aqua.copy_set_a(0.95f),
		.stroke = 5.f,
		.depth = 0.f
	};

	for(const physics::collision_shape_editor_part& part : state.document.parts){
		if(part.mirror.active()){
			const auto mirrored = physics::mirror_collision_shape_editor_part(part, part.mirror);
			draw_part(
				mirrored,
				editor_detail::display_transform(state.document, mirrored),
				mirrored_fill_style,
				mirrored_outline_style);
			draw_mirror_axes(part.mirror);
		}
	}

	for(std::size_t index = 0u; index != state.document.parts.size(); ++index){
		const auto& part = state.document.parts[index];
		const bool selected = state.mode == editor_detail::editor_mode::object
			? state.object_mode.selected_part == index
			: state.mode == editor_detail::editor_mode::edit && state.edit_mode.selected_part == index;
		draw_part(
			part,
			editor_detail::display_transform(state.document, part),
			fill_style,
			selected ? selected_style : outline_style);
	}

	if(state.mode == editor_detail::editor_mode::edit){
		if(const auto* selected = state.selected(); selected != nullptr){
			draw_selected_handles(*selected, editor_detail::display_transform(state.document, *selected));
		}
	}

	draw_transform_axes(state.document.total_transform, state.mode == editor_detail::editor_mode::origin ? 3.f : 1.5f);

	this->viewport_end();
}

collision_shape_editor::collision_shape_editor(gui::scene& scene, gui::elem* parent)
	: gui::head_body(scene, parent, gui::layout::layout_policy::vert_major){
	this->set_style();
	this->set_expand_policy(gui::layout::expand_policy::passive);
	this->set_head_size(240.f);
	this->set_body_size({gui::layout::size_category::passive, 1.f});
	this->set_pad(6.f);

	auto& controls_scroll = this->emplace_head<gui::scroll_adaptor<gui::sequence>>();
	controls_scroll.set_layout_spec(gui::layout::layout_policy::hori_major);
	controls_scroll.set_expand_policy(gui::layout::expand_policy::passive);
	auto& controls = controls_scroll.get_elem();
	controls.set_layout_spec(gui::layout::layout_policy::hori_major);
	controls.set_style();

	controls.template_cell.set_size({gui::layout::size_category::mastering, 48.f}).set_pad({4.f, 4.f});

	const auto add_button = [this, &controls]<typename Function>(
		const std::string_view text,
		Function function){
		auto button = controls.create_back([text, function](gui::button<gui::direct_label>& b){
			b.set_style(gui::style::family_variant::base_only);
			b.set_fit_type(gui::label_fit_type::scl);
			b.text_entire_align = align::pos::center;
			b.set_tokenized_text({text});
			b.set_button_callback(function);
		});
		button.cell().set_size(44.f);
		return std::addressof(button.elem());
	};

	object_mode_button_ = add_button("Object", [this]{
			viewport_->state.set_mode(editor_detail::editor_mode::object);
		});
	edit_mode_button_ = add_button("Edit", [this]{
		viewport_->state.set_mode(editor_detail::editor_mode::edit);
	});
	reference_mode_button_ = add_button("Reference", [this]{
		viewport_->state.set_mode(editor_detail::editor_mode::reference_image);
	});
	add_button("Choose Image", [this]{
		viewport_->state.set_mode(editor_detail::editor_mode::reference_image);
		viewport_->open_reference_image_file_selector();
	});
	origin_mode_button_ = add_button("Origin", [this]{
		viewport_->state.set_mode(editor_detail::editor_mode::origin);
	});
	add_button("Add Circle", [this]{
		this->add_shape_at_cursor(physics::shape_type::circle);
	});
	add_button("Add Capsule", [this]{
		this->add_shape_at_cursor(physics::shape_type::capsule);
	});
	add_button("Add Box", [this]{
		this->add_shape_at_cursor(physics::shape_type::box);
	});
	add_button("Add Polygon", [this]{
		this->add_shape_at_cursor(physics::shape_type::convex_polygon);
	});
	add_button("Duplicate", [this]{
		viewport_->state.duplicate_selected();
	});
	add_button("Delete", [this]{
		viewport_->state.erase_selected();
	});
	add_button("G Move", [this]{
		static_cast<void>(viewport_->state.start_operation(
			editor_detail::operation_kind::move,
			viewport_->cursor_world_pos()));
	});
	add_button("R Rotate", [this]{
		static_cast<void>(viewport_->state.start_operation(
			editor_detail::operation_kind::rotate,
			viewport_->cursor_world_pos()));
	});
	add_button("S Resize", [this]{
		static_cast<void>(viewport_->state.start_operation(
			editor_detail::operation_kind::resize,
			viewport_->cursor_world_pos()));
	});
	add_button("Load Ref", [this]{
		viewport_->state.set_mode(editor_detail::editor_mode::reference_image);
		viewport_->open_reference_image_file_selector();
	});
	add_button("Clear Ref", [this]{
		viewport_->state.clear_reference_image();
	});
	add_button("Mirror X", [this]{
		viewport_->state.toggle_mirror_x();
	});
	add_button("Mirror Y", [this]{
		viewport_->state.toggle_mirror_y();
	});
	add_button("Undo", [this]{
		viewport_->state.undo();
	});
	add_button("Redo", [this]{
		viewport_->state.redo();
	});

	gui::scaling_stack& viewport_stack = this->emplace_body<gui::scaling_stack>();
	viewport_stack.template_cell.region_scale = {0.f, 0.f, 1.f, 1.f};

	auto viewport = viewport_stack.emplace_back<collision_shape_editor_viewport>();
	viewport.cell().region_scale = {0.f, 0.f, 1.f, 1.f};
	viewport_ = std::addressof(viewport.elem());

	auto properties_scroll = viewport_stack.emplace_back<gui::scroll_adaptor<gui::sequence>>();
	properties_scroll.elem().set_max_extent({400.f, std::numeric_limits<float>::infinity()});
	properties_scroll.cell().region_scale = {0.f, 0.f, 0.30f, 0.40f};
	properties_scroll.cell().region_align = align::pos::bottom_left;
	properties_scroll.cell().unsaturate_cell_elem_align = align::pos::bottom_left;
	properties_scroll.cell().margin = gui::border{.left = 8.f, .bottom = 8.f};

	auto& properties_sequence = properties_scroll.elem().get_elem();
	properties_sequence.set_style();
	properties_sequence.set_layout_spec(gui::layout::layout_policy::hori_major);
	properties_sequence.set_expand_policy(gui::layout::expand_policy::prefer);
	properties_sequence.set_align_to_tail(true);

	auto properties = properties_sequence.emplace_back<collision_shape_editor_properties_panel>();
	properties.elem().bind(*viewport_);
	properties.cell().set_pending();
	properties_panel_ = std::addressof(properties.elem());

	auto status = viewport_stack.create_back([this](gui::direct_label& label){
		label.set_style(gui::style::family_variant::base_only);
		label.set_fit_type(gui::label_fit_type::scl);
		label.set_self_border(gui::border{}.set(6.f));
		label.max_fit_scale_bound.y = 28.f;
		label.text_entire_align = align::pos::center_left;
		status_ = std::addressof(label);
	});
	status.cell().region_scale = {0.f, 0.f, 1.f, 0.1f};
	status.cell().region_align = align::pos::top_left;
	status.cell().margin = gui::border{.left = 8.f, .right = 8.f, .top = 8.f};

	this->refresh_status_label();
}

void collision_shape_editor::on_display_state_changed(const bool is_shown, const bool is_scene_notified){
	gui::head_body::on_display_state_changed(is_shown, is_scene_notified);
	if(is_shown){
		gui::util::update_insert(*this, gui::update_channel::custom);
	}else{
		gui::util::update_erase(*this, gui::update_channel::custom);
	}
}

bool collision_shape_editor::update(const float delta_in_ticks){
	if(!gui::head_body::update(delta_in_ticks)){
		return false;
	}
	this->refresh_status_label();
	return true;
}

void collision_shape_editor::refresh_status_label() const{
	if(status_ != nullptr && viewport_ != nullptr){
		const auto& state = viewport_->state;
		if(object_mode_button_ != nullptr){
			object_mode_button_->set_toggled(state.mode == editor_detail::editor_mode::object);
		}
		if(edit_mode_button_ != nullptr){
			edit_mode_button_->set_toggled(state.mode == editor_detail::editor_mode::edit);
		}
		if(reference_mode_button_ != nullptr){
			reference_mode_button_->set_toggled(state.mode == editor_detail::editor_mode::reference_image);
		}
		if(origin_mode_button_ != nullptr){
			origin_mode_button_->set_toggled(state.mode == editor_detail::editor_mode::origin);
		}

		const auto* selected_part = state.selected();
		std::string mirror_text{};
		if(selected_part != nullptr){
			mirror_text = selected_part->mirror.active() ? "on" : "off";
			if(selected_part->mirror.mirror_x){
				mirror_text += " X";
			}
			if(selected_part->mirror.mirror_y){
				mirror_text += " Y";
			}
		}else{
			const auto active_mirror_count = std::ranges::count_if(
				state.document.parts,
				[](const physics::collision_shape_editor_part& part){
					return part.mirror.active();
				});
			mirror_text = std::format("{} active", active_mirror_count);
		}

		std::string status_text = std::format(
			"mode: {} / parts: {} / selected: {} / op: {} / mirror: {} / ref: {}",
			editor_detail::mode_name(state.mode),
			state.part_count(),
			state.selected_text(),
			state.operation_text(),
			mirror_text,
			state.reference_image_loaded() && state.document.reference_image.visible() ? "loaded" : "none");
		if(!state.last_error.empty()){
			status_text += std::format(" / {}", state.last_error);
		}
		status_->set_tokenized_text(typesetting::tokenized_text{
			std::move(status_text),
			typesetting::tokenize_tag::raw
		});
	}
}

void collision_shape_editor::add_shape_at_cursor(const physics::shape_type type) const{
	static_cast<void>(viewport_->state.add_shape(type, viewport_->cursor_world_pos()));
}

const physics::collision_shape_editor_document& collision_shape_editor::editor_metadata() const noexcept{
	return viewport_->state.document;
}

physics::collision_shape collision_shape_editor::shape_metadata() const{
	return viewport_->state.document.to_runtime_shape();
}

physics::collision_shape_record collision_shape_editor::packed_shape_record() const{
	return viewport_->state.document.to_packed_record();
}
}
