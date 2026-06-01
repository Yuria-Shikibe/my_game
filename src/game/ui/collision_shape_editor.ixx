export module mo_yanxi.game.ui.collision_shape_editor;

import std;
import mo_yanxi.gui.elem.button;
import mo_yanxi.gui.elem.label;
import mo_yanxi.gui.elem.sequence;
import mo_yanxi.gui.elem.table;
import mo_yanxi.gui.elem.viewport;
import mo_yanxi.gui.fx;
import mo_yanxi.graphic.color;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.input_handle;
import mo_yanxi.math;
import mo_yanxi.game.physics;
import mo_yanxi.game.runtime.draw.collision_shape;
import align;

namespace mo_yanxi::game::ui{
namespace editor_detail{
inline constexpr float hit_epsilon = 1.0e-5f;

enum class operation_kind{
	none,
	move,
	rotate
};

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
	default:
		return "unknown";
	}
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
}

struct collision_shape_editor_history_entry{
	physics::collision_shape_editor_document document{};
	std::optional<std::size_t> selected_part{};
	std::optional<std::size_t> selected_vertex{};
};

struct collision_shape_editor_operation{
	editor_detail::operation_kind kind{editor_detail::operation_kind::none};
	math::vec2 initial_cursor{};
	math::vec2 pivot{};
	std::size_t part_index{};
	std::optional<std::size_t> vertex_index{};
	physics::collision_shape_editor_part source_part{};
	bool invalid{};

	[[nodiscard]] bool active() const noexcept{
		return kind != editor_detail::operation_kind::none;
	}

	void reset() noexcept{
		kind = editor_detail::operation_kind::none;
		invalid = false;
		vertex_index.reset();
	}
};

export
struct collision_shape_editor_state{
	static constexpr std::size_t history_limit = 64u;

	physics::collision_shape_editor_document document{};
	std::optional<std::size_t> selected_part{};
	std::optional<std::size_t> selected_vertex{};
	collision_shape_editor_operation operation{};
	std::vector<collision_shape_editor_history_entry> history{};
	std::size_t history_index{};
	std::string last_error{};

	[[nodiscard]] collision_shape_editor_state(){
		static_cast<void>(this->add_shape(physics::shape_type::box, {}, false));
		this->push_history();
	}

	[[nodiscard]] std::size_t part_count() const noexcept{
		return document.parts.size();
	}

	[[nodiscard]] physics::collision_shape_editor_part* selected() noexcept{
		if(!selected_part || *selected_part >= document.parts.size()){
			return nullptr;
		}
		return std::addressof(document.parts[*selected_part]);
	}

	[[nodiscard]] const physics::collision_shape_editor_part* selected() const noexcept{
		if(!selected_part || *selected_part >= document.parts.size()){
			return nullptr;
		}
		return std::addressof(document.parts[*selected_part]);
	}

	[[nodiscard]] bool operation_active() const noexcept{
		return operation.active();
	}

	[[nodiscard]] std::string selected_text() const{
		const auto* part = this->selected();
		if(part == nullptr){
			return "none";
		}
		if(selected_vertex){
			return std::format(
				"{} {} vertex {}",
				editor_detail::shape_type_name(part->type),
				*selected_part,
				*selected_vertex);
		}
		return std::format("{} {}", editor_detail::shape_type_name(part->type), *selected_part);
	}

	[[nodiscard]] std::string operation_text() const{
		if(operation.active()){
			return std::string{editor_detail::operation_name(operation.kind)};
		}
		return "idle";
	}

	[[nodiscard]] std::size_t add_shape(
		const physics::shape_type type,
		const math::vec2 position = {},
		const bool record_history = true){
		const std::size_t index = document.add_shape(type, position);
		selected_part = index;
		selected_vertex.reset();
		last_error.clear();
		if(record_history){
			this->push_history();
		}
		return index;
	}

	void select_at(const math::vec2 world_point, const float vertex_radius) noexcept{
		selected_part.reset();
		selected_vertex.reset();

		for(std::size_t index = document.parts.size(); index != 0u; --index){
			const std::size_t part_index = index - 1u;
			const auto& part = document.parts[part_index];
			if(const auto vertex = editor_detail::polygon_vertex_at(part, world_point, vertex_radius)){
				selected_part = part_index;
				selected_vertex = vertex;
				return;
			}
			if(editor_detail::part_contains(part, world_point)){
				selected_part = part_index;
				return;
			}
		}
	}

	void erase_selected(){
		if(operation.active()){
			this->cancel_operation();
		}
		if(!selected_part || *selected_part >= document.parts.size()){
			return;
		}
		document.parts.erase(document.parts.begin() + static_cast<std::ptrdiff_t>(*selected_part));
		selected_part.reset();
		selected_vertex.reset();
		last_error.clear();
		this->push_history();
	}

	void duplicate_selected(){
		if(operation.active()){
			this->cancel_operation();
		}
		const auto* part = this->selected();
		if(part == nullptr){
			return;
		}

		auto copy = *part;
		copy.local_transform.vec += math::vec2{24.f, 0.f};
		document.parts.push_back(std::move(copy));
		selected_part = document.parts.size() - 1u;
		selected_vertex.reset();
		last_error.clear();
		this->push_history();
	}

	void toggle_mirror_enabled(){
		document.mirror.enabled = !document.mirror.enabled;
		last_error.clear();
		this->push_history();
	}

	void toggle_mirror_x(){
		document.mirror.mirror_x = !document.mirror.mirror_x;
		last_error.clear();
		this->push_history();
	}

	void toggle_mirror_y(){
		document.mirror.mirror_y = !document.mirror.mirror_y;
		last_error.clear();
		this->push_history();
	}

	[[nodiscard]] bool start_operation(const editor_detail::operation_kind kind, const math::vec2 cursor){
		if(operation.active() || kind == editor_detail::operation_kind::none){
			return false;
		}
		const auto* part = this->selected();
		if(part == nullptr){
			return false;
		}
		if(!part->payload_valid()){
			last_error = "selected shape payload is invalid";
			return false;
		}

		operation = collision_shape_editor_operation{
			.kind = kind,
			.initial_cursor = cursor,
			.pivot = part->local_transform.vec,
			.part_index = *selected_part,
			.vertex_index = selected_vertex,
			.source_part = *part
		};
		last_error.clear();
		return true;
	}

	[[nodiscard]] bool preview_operation(const math::vec2 cursor){
		if(!operation.active()){
			return true;
		}
		if(operation.part_index >= document.parts.size()){
			operation.reset();
			selected_part.reset();
			selected_vertex.reset();
			last_error = "operation target no longer exists";
			return false;
		}

		auto candidate = operation.source_part;
		const math::vec2 delta = cursor - operation.initial_cursor;
		switch(operation.kind){
		case editor_detail::operation_kind::move:
			if(operation.vertex_index){
				if(candidate.type != physics::shape_type::convex_polygon
					|| *operation.vertex_index >= candidate.convex_polygon.vertices.size()){
					last_error = "selected polygon vertex no longer exists";
					return false;
				}
				const math::vec2 source_world =
					candidate.convex_polygon.vertices[*operation.vertex_index] >> candidate.local_transform;
				candidate.convex_polygon.vertices[*operation.vertex_index] =
					candidate.local_transform.apply_inv_to(source_world + delta);
			}else{
				candidate.local_transform.vec = operation.source_part.local_transform.vec + delta;
			}
			break;
		case editor_detail::operation_kind::rotate:{
			const float angle = editor_detail::rotation_delta(operation.initial_cursor, cursor, operation.pivot);
			if(operation.vertex_index){
				if(candidate.type != physics::shape_type::convex_polygon
					|| *operation.vertex_index >= candidate.convex_polygon.vertices.size()){
					last_error = "selected polygon vertex no longer exists";
					return false;
				}
				math::vec2 rotated =
					candidate.convex_polygon.vertices[*operation.vertex_index] >> candidate.local_transform;
				rotated = operation.pivot + (rotated - operation.pivot).rotate_rad(angle);
				candidate.convex_polygon.vertices[*operation.vertex_index] =
					candidate.local_transform.apply_inv_to(rotated);
			}else{
				candidate.local_transform.rot = operation.source_part.local_transform.rot + angle;
			}
			break;
		}
		case editor_detail::operation_kind::none:
			return true;
		default:
			std::unreachable();
		}

		if(!candidate.payload_valid()){
			document.parts[operation.part_index] = operation.source_part;
			operation.invalid = true;
			last_error = "polygon edit would make the shape invalid";
			return false;
		}

		document.parts[operation.part_index] = std::move(candidate);
		operation.invalid = false;
		last_error.clear();
		return true;
	}

	[[nodiscard]] bool commit_operation(const math::vec2 cursor){
		if(!operation.active()){
			return false;
		}

		const bool valid = this->preview_operation(cursor);
		if(!valid){
			if(operation.part_index < document.parts.size()){
				document.parts[operation.part_index] = operation.source_part;
			}
			operation.reset();
			return false;
		}

		operation.reset();
		this->push_history();
		return true;
	}

	void cancel_operation(){
		if(!operation.active()){
			return;
		}
		if(operation.part_index < document.parts.size()){
			document.parts[operation.part_index] = operation.source_part;
		}
		operation.reset();
		last_error.clear();
	}

	void push_history(){
		if(history_index + 1u < history.size()){
			history.erase(history.begin() + static_cast<std::ptrdiff_t>(history_index + 1u), history.end());
		}
		history.push_back(collision_shape_editor_history_entry{
			.document = document,
			.selected_part = selected_part,
			.selected_vertex = selected_vertex
		});
		if(history.size() > history_limit){
			history.erase(history.begin());
		}
		history_index = history.empty() ? 0u : history.size() - 1u;
	}

	void undo(){
		if(operation.active()){
			this->cancel_operation();
		}
		if(history_index == 0u || history.empty()){
			return;
		}
		--history_index;
		this->apply_history_entry(history[history_index]);
	}

	void redo(){
		if(operation.active()){
			this->cancel_operation();
		}
		if(history.empty() || history_index + 1u >= history.size()){
			return;
		}
		++history_index;
		this->apply_history_entry(history[history_index]);
	}

private:
	void apply_history_entry(const collision_shape_editor_history_entry& entry){
		document = entry.document;
		selected_part = entry.selected_part;
		selected_vertex = entry.selected_vertex;
		this->validate_selection();
		last_error.clear();
	}

	void validate_selection() noexcept{
		if(!selected_part || *selected_part >= document.parts.size()){
			selected_part.reset();
			selected_vertex.reset();
			return;
		}
		const auto& part = document.parts[*selected_part];
		if(part.type != physics::shape_type::convex_polygon
			|| !selected_vertex
			|| *selected_vertex >= part.convex_polygon.vertices.size()){
			selected_vertex.reset();
		}
	}
};

export
struct collision_shape_editor_viewport : gui::viewport{
	collision_shape_editor_state state{};

private:
	gui::elem* add_menu_overlay_{};
	math::vec2 pending_add_position_{};

public:
	[[nodiscard]] collision_shape_editor_viewport(gui::scene& scene, gui::elem* parent)
		: gui::viewport(scene, parent){
		this->set_style(gui::style::family_variant::general_static);
		camera.set_scale_range({0.0625f, 4.f});
	}

	bool update(const float delta_in_ticks) override{
		if(!gui::viewport::update(delta_in_ticks)){
			return false;
		}
		if(state.operation_active()){
			static_cast<void>(state.preview_operation(this->get_transferred_cursor_pos()));
		}
		return true;
	}

	gui::events::op_afterwards on_click(
		const gui::events::click event,
		std::span<gui::elem* const> aboves) override{
		if(event.key.as_mouse() == input_handle::mouse::LMB && event.within_elem(*this)){
			const math::vec2 world_pos = this->get_transferred_pos(event.pos);
			if(state.operation_active()){
				if(event.key.action == input_handle::act::release){
					static_cast<void>(state.commit_operation(world_pos));
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

	gui::events::op_afterwards on_drag(const gui::events::drag event) override{
		if(event.key.as_mouse() == input_handle::mouse::LMB && state.operation_active()){
			static_cast<void>(state.preview_operation(this->get_transferred_pos(event.dst)));
			return gui::events::op_afterwards::intercepted;
		}
		return gui::viewport::on_drag(event);
	}

	gui::events::op_afterwards on_key_input(const input_handle::key_set key) override{
		if(key.action != input_handle::act::press){
			return gui::events::op_afterwards::fall_through;
		}

		const math::vec2 cursor = this->get_transferred_cursor_pos();
		switch(key.as_key()){
		case input_handle::key::a:
			if(input_handle::matched(key.mode_bits, input_handle::mode::shift)){
				this->show_add_menu();
				return gui::events::op_afterwards::intercepted;
			}
			break;
		case input_handle::key::g:
			if(state.start_operation(editor_detail::operation_kind::move, cursor)){
				return gui::events::op_afterwards::intercepted;
			}
			break;
		case input_handle::key::r:
			if(state.start_operation(editor_detail::operation_kind::rotate, cursor)){
				return gui::events::op_afterwards::intercepted;
			}
			break;
		case input_handle::key::enter:
			if(state.operation_active()){
				static_cast<void>(state.commit_operation(cursor));
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
		case input_handle::key::backspace:
			state.erase_selected();
			return gui::events::op_afterwards::intercepted;
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

		return gui::events::op_afterwards::fall_through;
	}

	gui::events::op_afterwards on_esc() override{
		if(state.operation_active()){
			state.cancel_operation();
			return gui::events::op_afterwards::intercepted;
		}
		if(add_menu_overlay_ != nullptr){
			this->close_add_menu();
			return gui::events::op_afterwards::intercepted;
		}
		if(state.selected_part){
			state.selected_part.reset();
			state.selected_vertex.reset();
			return gui::events::op_afterwards::intercepted;
		}
		return gui::viewport::on_esc();
	}

	void record_draw_layer(gui::draw_recorder& call_stack_builder) const override{
		gui::viewport::record_draw_layer(call_stack_builder);
		call_stack_builder.push_call_noop(
			*this,
			[](const collision_shape_editor_viewport& self, const gui::draw_call_param& param, const gui::draw_immut_args& args) static{
				if(!args.layer.is_top() || !param.draw_bound.overlap_exclusive(self.content_bound_abs())){
					return;
				}
				self.draw_editor_content();
			});
	}

	[[nodiscard]] float selection_radius() const noexcept{
		return 9.f / camera.get_scale();
	}

private:
	void show_add_menu(){
		this->close_add_menu();
		pending_add_position_ = this->get_transferred_cursor_pos();
		const math::vec2 cursor = this->get_scene().get_cursor_pos();
		auto result = this->get_scene().create_overlay(
			{
				.extent = {480.f, 58.f},
				.align = align::pos::top_left,
				.absolute_offset = cursor + math::vec2{10.f, 10.f}
			},
			[this](gui::sequence& menu){
				menu.set_style();
				menu.set_expand_policy(gui::layout::expand_policy::passive);
				menu.template_cell.set_size(116.f).set_pad({4.f, 4.f});
				this->add_add_menu_button(menu, "Circle", physics::shape_type::circle);
				this->add_add_menu_button(menu, "Capsule", physics::shape_type::capsule);
				this->add_add_menu_button(menu, "Box", physics::shape_type::box);
				this->add_add_menu_button(menu, "Polygon", physics::shape_type::convex_polygon);
			},
			gui::layout::layout_policy::hori_major);
		add_menu_overlay_ = std::addressof(result.elem());
	}

	void close_add_menu(){
		if(add_menu_overlay_ != nullptr){
			this->get_scene().close_overlay(add_menu_overlay_);
			add_menu_overlay_ = nullptr;
		}
	}

	void add_add_menu_button(
		gui::sequence& menu,
		const std::string_view text,
		const physics::shape_type type){
		auto button = menu.create_back([this, text, type](gui::button<gui::direct_label>& b){
			b.set_style(gui::style::family_variant::base_only);
			b.set_fit_type(gui::label_fit_type::scl);
			b.text_entire_align = align::pos::center;
			b.set_tokenized_text({text});
			b.set_button_callback([this, type]{
				static_cast<void>(state.add_shape(type, pending_add_position_));
				this->close_add_menu();
			});
		});
		button.cell().set_size(112.f);
	}

	void draw_editor_content() const{
		namespace instr = graphic::draw::instruction;

		auto& renderer = this->renderer();
		renderer.update_state(gui::fx::batch_draw_mode::def);

		this->viewport_begin();
		this->draw_grid();

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

		if(state.document.mirror.active()){
			for(const physics::collision_shape_editor_part& part : state.document.parts){
				this->draw_part(
					physics::mirror_collision_shape_editor_part(part, state.document.mirror),
					mirrored_fill_style,
					mirrored_outline_style);
			}
			this->draw_mirror_axes();
		}

		for(std::size_t index = 0u; index != state.document.parts.size(); ++index){
			const auto& part = state.document.parts[index];
			this->draw_part(
				part,
				fill_style,
				state.selected_part == index ? selected_style : outline_style);
		}

		if(const auto* selected = state.selected(); selected != nullptr){
			this->draw_selected_handles(*selected);
		}

		renderer.push(instr::line{
			.src = {-10000.f, 0.f},
			.dst = {10000.f, 0.f},
			.color = {graphic::colors::CRIMSON.copy_set_a(0.45f), graphic::colors::CRIMSON.copy_set_a(0.45f)},
			.stroke = 1.5f
		});
		renderer.push(instr::line{
			.src = {0.f, -10000.f},
			.dst = {0.f, 10000.f},
			.color = {graphic::colors::LIME.copy_set_a(0.45f), graphic::colors::LIME.copy_set_a(0.45f)},
			.stroke = 1.5f
		});

		this->viewport_end();
	}

	void draw_part(
		const physics::collision_shape_editor_part& part,
		const draw::collision_shape_draw_style& fill_style,
		const draw::collision_shape_draw_style& outline_style) const{
		auto& renderer = this->renderer();
		switch(part.type){
		case physics::shape_type::circle:
			draw::fill_shape(renderer, part.circle, part.local_transform, fill_style);
			draw::draw_shape(renderer, part.circle, part.local_transform, outline_style);
			return;
		case physics::shape_type::capsule:
			draw::fill_shape(renderer, part.capsule, part.local_transform, fill_style);
			draw::draw_shape(renderer, part.capsule, part.local_transform, outline_style);
			return;
		case physics::shape_type::box:
			draw::fill_shape(renderer, part.box, part.local_transform, fill_style);
			draw::draw_shape(renderer, part.box, part.local_transform, outline_style);
			return;
		case physics::shape_type::convex_polygon:
			draw::fill_shape(renderer, part.convex_polygon, part.local_transform, fill_style);
			draw::draw_shape(renderer, part.convex_polygon, part.local_transform, outline_style);
			return;
		default:
			return;
		}
	}

	void draw_selected_handles(const physics::collision_shape_editor_part& part) const{
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
			const math::vec2 vertex = part.convex_polygon.vertices[index] >> part.local_transform;
			const auto& style = state.selected_vertex == index ? selected_handle_style : handle_style;
			draw::fill_shape(this->renderer(), physics::circle_shape{radius}, math::trans2{vertex, 0.f}, style);
			draw::draw_shape(this->renderer(), physics::circle_shape{radius}, math::trans2{vertex, 0.f}, style);
		}
	}

	void draw_mirror_axes() const{
		namespace instr = graphic::draw::instruction;

		const math::vec2 origin = state.document.mirror.origin.vec;
		auto push_axis = [&](math::vec2 direction, const graphic::color color){
			direction.rotate_rad(state.document.mirror.origin.rot);
			this->renderer().push(instr::line{
				.src = origin - direction * 10000.f,
				.dst = origin + direction * 10000.f,
				.color = {color, color},
				.stroke = 2.f
			});
		};

		if(state.document.mirror.mirror_x){
			push_axis({0.f, 1.f}, graphic::colors::CRIMSON.copy_set_a(0.75f));
		}
		if(state.document.mirror.mirror_y){
			push_axis({1.f, 0.f}, graphic::colors::LIME.copy_set_a(0.75f));
		}
	}

	void draw_grid() const{
		namespace instr = graphic::draw::instruction;

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
	}
};

export
struct collision_shape_editor : gui::table{
private:
	collision_shape_editor_viewport* viewport_{};
	gui::direct_label* status_{};

public:
	[[nodiscard]] collision_shape_editor(gui::scene& scene, gui::elem* parent)
		: gui::table(scene, parent){
		this->set_style();
		this->set_expand_policy(gui::layout::expand_policy::passive);
		this->set_layout_spec(gui::layout::layout_specifier::fixed(gui::layout::layout_policy::hori_major));
		this->template_cell.set_pad({6.f, 6.f});

		auto controls = this->emplace_back<gui::sequence>(gui::layout::layout_policy::vert_major);
		controls.cell().set_width(240.f);
		controls->set_style();
		controls->set_expand_policy(gui::layout::expand_policy::passive);
		controls->template_cell.set_size({gui::layout::size_category::mastering, 48.f}).set_pad({4.f, 4.f});

		this->build_controls(controls.elem());

		auto viewport = this->emplace_back<collision_shape_editor_viewport>();
		viewport.cell().set_pending();
		viewport_ = std::addressof(viewport.elem());
	}

	bool update(const float delta_in_ticks) override{
		if(!gui::table::update(delta_in_ticks)){
			return false;
		}
		if(status_ != nullptr && viewport_ != nullptr){
			const auto& state = viewport_->state;
			std::string status_text = std::format(
				"parts: {} / selected: {} / op: {} / mirror: {}{}{}",
				state.part_count(),
				state.selected_text(),
				state.operation_text(),
				state.document.mirror.enabled ? "on" : "off",
				state.document.mirror.mirror_x ? " X" : "",
				state.document.mirror.mirror_y ? " Y" : "");
			if(!state.last_error.empty()){
				status_text += std::format(" / {}", state.last_error);
			}
			status_->set_tokenized_text(typesetting::tokenized_text{
				std::move(status_text),
				typesetting::tokenize_tag::raw
			});
		}
		return true;
	}

	[[nodiscard]] const physics::collision_shape_editor_document& editor_metadata() const noexcept{
		return viewport_->state.document;
	}

	[[nodiscard]] physics::collision_shape shape_metadata() const{
		return viewport_->state.document.to_runtime_shape();
	}

	[[nodiscard]] physics::collision_shape_record packed_shape_record() const{
		return viewport_->state.document.to_packed_record();
	}

private:
	template <typename Function>
	void add_button(gui::sequence& controls, const std::string_view text, Function function){
		auto button = controls.create_back([text, function](gui::button<gui::direct_label>& b){
			b.set_style(gui::style::family_variant::base_only);
			b.set_fit_type(gui::label_fit_type::scl);
			b.text_entire_align = align::pos::center;
			b.set_tokenized_text({text});
			b.set_button_callback(function);
		});
		button.cell().set_size(44.f);
	}

	void add_shape_at_cursor(const physics::shape_type type){
		static_cast<void>(viewport_->state.add_shape(type, viewport_->get_transferred_cursor_pos()));
	}

	void build_controls(gui::sequence& controls){
		this->add_button(controls, "Add Circle", [this]{
			this->add_shape_at_cursor(physics::shape_type::circle);
		});
		this->add_button(controls, "Add Capsule", [this]{
			this->add_shape_at_cursor(physics::shape_type::capsule);
		});
		this->add_button(controls, "Add Box", [this]{
			this->add_shape_at_cursor(physics::shape_type::box);
		});
		this->add_button(controls, "Add Polygon", [this]{
			this->add_shape_at_cursor(physics::shape_type::convex_polygon);
		});
		this->add_button(controls, "Duplicate", [this]{
			viewport_->state.duplicate_selected();
		});
		this->add_button(controls, "Delete", [this]{
			viewport_->state.erase_selected();
		});
		this->add_button(controls, "G Move", [this]{
			static_cast<void>(viewport_->state.start_operation(
				editor_detail::operation_kind::move,
				viewport_->get_transferred_cursor_pos()));
		});
		this->add_button(controls, "R Rotate", [this]{
			static_cast<void>(viewport_->state.start_operation(
				editor_detail::operation_kind::rotate,
				viewport_->get_transferred_cursor_pos()));
		});
		this->add_button(controls, "Mirror", [this]{
			viewport_->state.toggle_mirror_enabled();
		});
		this->add_button(controls, "Mirror X", [this]{
			viewport_->state.toggle_mirror_x();
		});
		this->add_button(controls, "Mirror Y", [this]{
			viewport_->state.toggle_mirror_y();
		});
		this->add_button(controls, "Undo", [this]{
			viewport_->state.undo();
		});
		this->add_button(controls, "Redo", [this]{
			viewport_->state.redo();
		});

		auto status = controls.create_back([this](gui::direct_label& label){
			label.set_style(gui::style::family_variant::base_only);
			label.set_fit_type(gui::label_fit_type::scl);
			label.text_entire_align = align::pos::center;
			label.set_tokenized_text({"parts: 0 / selected: none"});
			status_ = std::addressof(label);
		});
		status.cell().set_size(72.f);
	}
};

export
using collision_box_editor = collision_shape_editor;
}
