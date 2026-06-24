module;

module mo_yanxi.game.ui.collision_shape_editor;

import std;
import magic_enum;
import align;
import mo_yanxi.game.physics.collision_shape_editor_metadata;
import mo_yanxi.game.physics.shape_codec;
import mo_yanxi.game.runtime.draw.collision_shape;
import mo_yanxi.graphic.color;
import mo_yanxi.graphic.g2d;
import mo_yanxi.graphic.bitmap;
import mo_yanxi.graphic.image_atlas;
import mo_yanxi.gui.compound.file_selector;
import mo_yanxi.gui.compound.numeric_input_area;
import mo_yanxi.gui.elem.button;
import mo_yanxi.gui.elem.check_box;
import mo_yanxi.gui.elem.flipper;
import mo_yanxi.gui.elem.head_body_elem;
import mo_yanxi.gui.elem.label;
import mo_yanxi.gui.elem.scroll_pane;
import mo_yanxi.gui.elem.scaling_stack;
import mo_yanxi.gui.elem.sequence;
import mo_yanxi.gui.elem.table;
import mo_yanxi.gui.elem.viewport;
import mo_yanxi.gui.cfg.builtin.constants;
import mo_yanxi.gui.fx;
import mo_yanxi.history_stack;
import mo_yanxi.input_handle;
import mo_yanxi.math.trans2;
import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;


import mo_yanxi.srl.byte_record;
import mo_yanxi.srl.codec;

import mo_yanxi.gui.image_regions;

namespace mo_yanxi::game::ui{
namespace{
struct reference_image_resources{
	graphic::image_atlas* atlas{};
	graphic::image_page* page{};
};

reference_image_resources reference_images{};

inline constexpr std::string_view document_suffix = ".cshape";
inline constexpr std::string_view default_document_name = "collision_shape.cshape";

[[nodiscard]] std::filesystem::path normalized_reference_image_path(std::filesystem::path path){
	return path.make_preferred();
}

[[nodiscard]] std::string reference_image_region_name(const std::filesystem::path& path){
	return normalized_reference_image_path(path).string();
}

[[nodiscard]] gui::constant_image_region_borrow borrow_reference_image_region(
	const std::filesystem::path& path,
	math::vec2* image_size = nullptr){
	if(reference_images.page == nullptr){
		throw std::runtime_error{"reference image page is not configured"};
	}

	const std::string name = reference_image_region_name(path);
	auto registered = reference_images.page->register_named_region(
		name,
		graphic::image_load_description{graphic::bitmap_path_load{name}},
		false);
	if(image_size != nullptr){
		*image_size = registered.region.uv.get_region_size<float>();
	}

	auto borrowed = registered.region.make_cached_borrow();
	if(!borrowed){
		throw std::runtime_error{"reference image region could not be borrowed"};
	}
	return std::move(*borrowed);
}

[[nodiscard]] std::string serialization_error_text(const mo_yanxi::srl::error error){
	return std::format(
		"{} (item {}, offset {})",
		mo_yanxi::srl::message(error.code),
		error.id,
		error.offset);
}

[[nodiscard]] bool finite_editor_transform(const math::trans2 transform) noexcept{
	return std::isfinite(transform.vec.x)
		&& std::isfinite(transform.vec.y)
		&& std::isfinite(transform.rot);
}

[[nodiscard]] std::optional<std::string> document_validation_error(
	const collision::editor::document& document){
	if(!finite_editor_transform(document.total_transform)){
		return "document origin transform is invalid";
	}
	for(std::size_t index = 0u; index != document.parts.size(); ++index){
		if(!document.parts[index].payload_valid()){
			return std::format("shape {} payload is invalid", index);
		}
	}
	if(document.reference_image.enabled && !document.reference_image.visible()){
		return "reference image metadata is invalid";
	}
	return std::nullopt;
}

[[nodiscard]] std::expected<void, std::string> write_document(
	const std::filesystem::path& path,
	const collision::editor::document& document){
	if(const auto validation_error = document_validation_error(document)){
		return std::unexpected{std::format("cannot save invalid document: {}", *validation_error)};
	}

	auto packed = mo_yanxi::srl::pack(document);
	if(!packed){
		return std::unexpected{std::format(
			"serialization failed: {}",
			serialization_error_text(packed.error()))};
	}

	const auto bytes = packed->span();
	if(bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())){
		return std::unexpected{std::format("document is too large to write: {}", bytes.size())};
	}

	std::ofstream file{path, std::ios::binary | std::ios::trunc};
	if(!file.is_open()){
		return std::unexpected{std::format("failed to open {} for writing", path.string())};
	}
	if(!bytes.empty()){
		file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	file.flush();
	if(!file.good()){
		return std::unexpected{std::format("failed to write {}", path.string())};
	}
	return {};
}

[[nodiscard]] std::expected<collision::editor::document, std::string>
read_document(const std::filesystem::path& path){
	std::error_code file_size_error{};
	const std::uintmax_t file_size = std::filesystem::file_size(path, file_size_error);
	if(file_size_error){
		return std::unexpected{std::format(
			"failed to inspect {}: {}",
			path.string(),
			file_size_error.message())};
	}
	if(file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())
		|| file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())){
		return std::unexpected{std::format("document file is too large: {}", path.string())};
	}

	std::ifstream file{path, std::ios::binary};
	if(!file.is_open()){
		return std::unexpected{std::format("failed to open {} for reading", path.string())};
	}

	std::vector<std::byte> bytes(static_cast<std::size_t>(file_size));
	if(!bytes.empty()){
		file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if(!file.good()){
			return std::unexpected{std::format("failed to read {}", path.string())};
		}
	}

	collision::editor::document document{};
	if(const auto unpacked = mo_yanxi::srl::unpack(std::span<const std::byte>{bytes.data(), bytes.size()}, document); !unpacked){
		return std::unexpected{std::format(
			"deserialization failed: {}",
			serialization_error_text(unpacked.error()))};
	}
	if(const auto validation_error = document_validation_error(document)){
		return std::unexpected{std::format("loaded document is invalid: {}", *validation_error)};
	}
	return document;
}

struct alpha_hull_result{
	std::string source_path{};
	std::vector<math::vec2> vertices{};
	bool mirror_x_detected{};
	bool mirror_y_detected{};
};

[[nodiscard]] bool alpha_opaque(
	const graphic::bitmap& bitmap,
	const std::size_t x,
	const std::size_t y) noexcept{
	return bitmap[
		static_cast<graphic::bitmap::size_type>(x),
		static_cast<graphic::bitmap::size_type>(y)].a > 0u;
}

struct alpha_bounds{
	std::size_t min_x{};
	std::size_t min_y{};
	std::size_t max_x{};
	std::size_t max_y{};
	std::size_t opaque_count{};
};

[[nodiscard]] std::optional<alpha_bounds> find_alpha_bounds(
	const graphic::bitmap& bitmap) noexcept{
	const std::size_t width = bitmap.width();
	const std::size_t height = bitmap.height();
	alpha_bounds bounds{
		.min_x = width,
		.min_y = height
	};

	for(std::size_t y = 0u; y != height; ++y){
		for(std::size_t x = 0u; x != width; ++x){
			if(!alpha_opaque(bitmap, x, y)){
				continue;
			}
			bounds.min_x = std::min(bounds.min_x, x);
			bounds.min_y = std::min(bounds.min_y, y);
			bounds.max_x = std::max(bounds.max_x, x);
			bounds.max_y = std::max(bounds.max_y, y);
			++bounds.opaque_count;
		}
	}

	if(bounds.opaque_count == 0u){
		return std::nullopt;
	}
	return bounds;
}

[[nodiscard]] bool alpha_symmetric(
	const graphic::bitmap& bitmap,
	const alpha_bounds bounds,
	const bool test_x_axis) noexcept{
	std::size_t mismatches{};
	for(std::size_t y = bounds.min_y; y <= bounds.max_y; ++y){
		for(std::size_t x = bounds.min_x; x <= bounds.max_x; ++x){
			if(!alpha_opaque(bitmap, x, y)){
				continue;
			}
			const std::size_t mirror_x = test_x_axis ? bounds.min_x + bounds.max_x - x : x;
			const std::size_t mirror_y = test_x_axis ? y : bounds.min_y + bounds.max_y - y;
			if(!alpha_opaque(bitmap, mirror_x, mirror_y)){
				++mismatches;
			}
		}
	}

	const std::size_t tolerated = std::max<std::size_t>(4u, bounds.opaque_count / 50u);
	return mismatches <= tolerated;
}

[[nodiscard]] bool alpha_boundary(
	const graphic::bitmap& bitmap,
	const std::size_t x,
	const std::size_t y) noexcept{
	const std::size_t width = bitmap.width();
	const std::size_t height = bitmap.height();
	if(x == 0u || y == 0u || x + 1u == width || y + 1u == height){
		return true;
	}
	return !alpha_opaque(bitmap, x - 1u, y)
		|| !alpha_opaque(bitmap, x + 1u, y)
		|| !alpha_opaque(bitmap, x, y - 1u)
		|| !alpha_opaque(bitmap, x, y + 1u);
}

[[nodiscard]] alpha_hull_result generate_alpha_hull(
	const std::filesystem::path path){
	const graphic::bitmap bitmap = graphic::load_bitmap(path);
	if(!bitmap || bitmap.width() == 0u || bitmap.height() == 0u){
		throw std::runtime_error{"reference image is empty"};
	}

	const auto maybe_bounds = find_alpha_bounds(bitmap);
	if(!maybe_bounds){
		throw std::runtime_error{"reference image alpha channel is empty"};
	}
	const auto bounds = *maybe_bounds;
	const bool mirror_x = alpha_symmetric(bitmap, bounds, true);
	const bool mirror_y = alpha_symmetric(bitmap, bounds, false);

	const float width = static_cast<float>(bitmap.width());
	const float height = static_cast<float>(bitmap.height());
	const float axis_x_pixel = (static_cast<float>(bounds.min_x) + static_cast<float>(bounds.max_x) + 1.f) * 0.5f;
	const float axis_y_pixel = (static_cast<float>(bounds.min_y) + static_cast<float>(bounds.max_y) + 1.f) * 0.5f;
	const float axis_x_local = axis_x_pixel - width * 0.5f;
	const float axis_y_local = height * 0.5f - axis_y_pixel;

	std::vector<math::vec2> points{};
	points.reserve(bounds.opaque_count / 2u);
	const auto add_symmetric_point = [&](const float pixel_x, const float pixel_y){
		if(mirror_x && pixel_x < axis_x_pixel){
			return;
		}
		if(mirror_y && pixel_y < axis_y_pixel){
			return;
		}

		const math::vec2 base{
			pixel_x - width * 0.5f,
			height * 0.5f - pixel_y
		};
		points.push_back(base);
		if(mirror_x){
			points.push_back({axis_x_local * 2.f - base.x, base.y});
		}
		if(mirror_y){
			points.push_back({base.x, axis_y_local * 2.f - base.y});
		}
		if(mirror_x && mirror_y){
			points.push_back({axis_x_local * 2.f - base.x, axis_y_local * 2.f - base.y});
		}
	};

	for(std::size_t y = bounds.min_y; y <= bounds.max_y; ++y){
		for(std::size_t x = bounds.min_x; x <= bounds.max_x; ++x){
			if(!alpha_opaque(bitmap, x, y)
				|| !alpha_boundary(bitmap, x, y)){
				continue;
			}
			const float fx = static_cast<float>(x);
			const float fy = static_cast<float>(y);
			add_symmetric_point(fx, fy);
			add_symmetric_point(fx + 1.f, fy);
			add_symmetric_point(fx + 1.f, fy + 1.f);
			add_symmetric_point(fx, fy + 1.f);
		}
	}

	if(points.size() < 3u){
		throw std::runtime_error{"reference image alpha boundary has fewer than three points"};
	}

	return alpha_hull_result{
		.source_path = reference_image_region_name(path),
		.vertices = collision::editor::polygon_convex_hull(points),
		.mirror_x_detected = mirror_x,
		.mirror_y_detected = mirror_y
	};
}
}

void configure_collision_shape_editor_reference_images(graphic::image_atlas& image_atlas){
	reference_images.atlas = std::addressof(image_atlas);
	reference_images.page = std::addressof(
		image_atlas.create_image_page("collision_shape_editor.reference", {
			.extent = {4096u, 4096u},
			.margin = 0
		}));
}

void clear_collision_shape_editor_reference_images() noexcept{
	reference_images = {};
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

enum class rotation_reference_mode{
	median,
	cursor,
	active
};

enum class vertex_merge_mode{
	center,
	first,
	last
};

enum class polygon_selection_domain{
	vertex,
	edge
};

enum class operation_constraint_space{
	world,
	local
};

enum class editor_action{
	none,
	operation_precision,
	operation_constrain_x,
	operation_constrain_y,
	operation_backspace,
	operation_consume_delete,
	operation_commit,
	toggle_object_edit_mode,
	set_object_mode,
	set_edit_mode,
	set_reference_image_mode,
	set_origin_mode,
	set_vertex_select_mode,
	set_edge_select_mode,
	select_all,
	show_add_menu,
	add_vertex_between_selected,
	insert_vertex_at_cursor,
	connect_selected_vertices_as_edge,
	show_merge_menu,
	cut_selected_polygon,
	start_knife_cut,
	make_convex_hull,
	split_convex_parts,
	join_polygons_at_center,
	join_polygons_at_first,
	join_polygons_at_last,
	show_reference_image_file_selector,
	generate_alpha_hull,
	duplicate_selected,
	erase_selected,
	undo,
	redo,
	reset_position,
	reset_rotation,
	start_move,
	start_rotate,
	start_resize
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
		std::unreachable();
	}
}

[[nodiscard]] constexpr std::string_view operation_name(const operation_kind kind) noexcept{
	const std::string_view name = ::magic_enum::enum_name(kind);
	if(name.empty()){
		std::unreachable();
	}
	return name;
}

[[nodiscard]] constexpr std::string_view mode_name(const editor_mode mode) noexcept{
	switch(mode){
	case editor_mode::object:
		return "object";
	case editor_mode::edit:
		return "edit";
	case editor_mode::reference_image:
		return "ref";
	case editor_mode::origin:
		return "origin";
	default:
		std::unreachable();
	}
}

[[nodiscard]] constexpr std::string_view rotation_reference_mode_name(
	const rotation_reference_mode mode) noexcept{
	switch(mode){
	case rotation_reference_mode::median:
		return "median";
	case rotation_reference_mode::cursor:
		return "cursor";
	case rotation_reference_mode::active:
		return "active";
	default:
		std::unreachable();
	}
}

[[nodiscard]] constexpr std::string_view rotation_reference_mode_label(
	const rotation_reference_mode mode) noexcept{
	switch(mode){
	case rotation_reference_mode::median:
		return "Median";
	case rotation_reference_mode::cursor:
		return "Cursor";
	case rotation_reference_mode::active:
		return "Active";
	default:
		std::unreachable();
	}
}

[[nodiscard]] constexpr std::string_view rotation_reference_mode_short_label(
	const rotation_reference_mode mode) noexcept{
	switch(mode){
	case rotation_reference_mode::median:
		return "Med";
	case rotation_reference_mode::cursor:
		return "Cur";
	case rotation_reference_mode::active:
		return "Act";
	default:
		std::unreachable();
	}
}

[[nodiscard]] constexpr std::string_view vertex_merge_mode_name(const vertex_merge_mode mode) noexcept{
	const std::string_view name = ::magic_enum::enum_name(mode);
	if(name.empty()){
		std::unreachable();
	}
	return name;
}

[[nodiscard]] constexpr std::string_view polygon_selection_domain_name(const polygon_selection_domain domain) noexcept{
	switch(domain){
	case polygon_selection_domain::vertex:
		return "v";
	case polygon_selection_domain::edge:
		return "e";
	default:
		std::unreachable();
	}
}

[[nodiscard]] constexpr std::string_view graph_state_short_name(const collision::editor::graph_state state) noexcept{
	switch(state){
	case collision::editor::graph_state::convex:
		return "conv";
	case collision::editor::graph_state::concave:
		return "conc";
	case collision::editor::graph_state::self_intersecting:
		return "self";
	case collision::editor::graph_state::open:
		return "open";
	default:
		std::unreachable();
	}
}

[[nodiscard]] constexpr bool key_has_mode(
	const input_handle::key_set key,
	const input_handle::mode mode) noexcept{
	return input_handle::matched(key.mode_bits, mode);
}

[[nodiscard]] constexpr std::optional<editor_action> keymap_action(
	const input_handle::key_set key,
	const bool operation_active,
	const editor_mode current_mode) noexcept{
	using input_key = input_handle::key;
	using input_act = input_handle::act;
	using input_mode = input_handle::mode;

	if(operation_active){
		switch(key.as_key()){
		case input_key::left_shift:
		case input_key::right_shift:
			if(key.action == input_act::press || key.action == input_act::release){
				return editor_action::operation_precision;
			}
			return std::nullopt;
		case input_key::x:
			return key.action == input_act::release
				? std::optional{editor_action::operation_constrain_x}
				: std::nullopt;
		case input_key::y:
			return key.action == input_act::release
				? std::optional{editor_action::operation_constrain_y}
				: std::nullopt;
		case input_key::backspace:
			return key.action == input_act::press || key.action == input_act::repeat
				? std::optional{editor_action::operation_backspace}
				: std::nullopt;
		case input_key::del:
			return key.action == input_act::press
				? std::optional{editor_action::operation_consume_delete}
				: std::nullopt;
		case input_key::enter:
			return key.action == input_act::press
				? std::optional{editor_action::operation_commit}
				: std::nullopt;
		default:
			return std::nullopt;
		}
	}

	if(key.action == input_act::press){
		switch(key.as_key()){
		case input_key::tab:
			return editor_action::toggle_object_edit_mode;
		case input_key::_1:
			return current_mode == editor_mode::edit
				? std::optional{editor_action::set_vertex_select_mode}
				: std::optional{editor_action::set_object_mode};
		case input_key::_2:
			return current_mode == editor_mode::edit
				? std::optional{editor_action::set_edge_select_mode}
				: std::optional{editor_action::set_edit_mode};
		case input_key::_3:
			return editor_action::set_reference_image_mode;
		case input_key::_4:
			return editor_action::set_origin_mode;
		case input_key::a:
			if(editor_detail::key_has_mode(key, input_mode::shift)){
				return current_mode == editor_mode::edit
					? std::optional{editor_action::insert_vertex_at_cursor}
					: std::optional{editor_action::show_add_menu};
			}
			return std::optional{editor_action::select_all};
		case input_key::e:
			return current_mode == editor_mode::edit
				? std::optional{editor_action::insert_vertex_at_cursor}
				: std::nullopt;
		case input_key::f:
			return current_mode == editor_mode::edit
				? std::optional{editor_action::connect_selected_vertices_as_edge}
				: std::optional{editor_action::show_reference_image_file_selector};
		case input_key::m:
			return editor_action::show_merge_menu;
		case input_key::c:
			return editor_action::cut_selected_polygon;
		case input_key::k:
			return editor_action::start_knife_cut;
		case input_key::h:
			return editor_action::make_convex_hull;
		case input_key::j:
			return editor_detail::key_has_mode(key, input_mode::ctrl)
				? std::optional{editor_action::join_polygons_at_last}
				: std::nullopt;
		case input_key::p:
			return editor_action::split_convex_parts;
		case input_key::b:
			return editor_action::generate_alpha_hull;
		case input_key::d:
			return editor_detail::key_has_mode(key, input_mode::ctrl)
					|| editor_detail::key_has_mode(key, input_mode::shift)
				? std::optional{editor_action::duplicate_selected}
				: std::nullopt;
		case input_key::x:
		case input_key::del:
		case input_key::backspace:
			return editor_action::erase_selected;
		case input_key::z:
			if(editor_detail::key_has_mode(key, input_mode::ctrl_shift)){
				return editor_action::redo;
			}
			return editor_detail::key_has_mode(key, input_mode::ctrl)
				? std::optional{editor_action::undo}
				: std::nullopt;
		default:
			return std::nullopt;
		}
	}

	if(key.action == input_act::release){
		switch(key.as_key()){
		case input_key::g:
			return editor_detail::key_has_mode(key, input_mode::alt)
				? std::optional{editor_action::reset_position}
				: std::optional{editor_action::start_move};
		case input_key::r:
			return editor_detail::key_has_mode(key, input_mode::alt)
				? std::optional{editor_action::reset_rotation}
				: std::optional{editor_action::start_rotate};
		case input_key::s:
			return editor_action::start_resize;
		default:
			return std::nullopt;
		}
	}

	return std::nullopt;
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
	std::size_t edge_count{};
	collision::editor::graph_state state{
		collision::editor::graph_state::open
	};
};

using shape_properties = std::variant<
	circle_shape_properties,
	capsule_shape_properties,
	box_shape_properties,
	polygon_shape_properties>;

[[nodiscard]] shape_properties make_shape_properties(
	const collision::editor::part& part) noexcept{
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
	case physics::shape_type::convex_polygon:{
		const auto analysis = collision::editor::analyze_part_polygon_graph(part);
		return polygon_shape_properties{
			.vertex_count = part.convex_polygon.vertices.size(),
			.edge_count = collision::editor::part_polygon_edge_count(part),
			.state = analysis.state
		};
	}
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

[[nodiscard]] bool points_same_side_of_line(
	const math::vec2 line_begin,
	const math::vec2 line_end,
	const math::vec2 lhs,
	const math::vec2 rhs) noexcept{
	const float lhs_side = (line_end - line_begin).cross(lhs - line_begin);
	const float rhs_side = (line_end - line_begin).cross(rhs - line_begin);
	return (lhs_side > hit_epsilon && rhs_side > hit_epsilon)
		|| (lhs_side < -hit_epsilon && rhs_side < -hit_epsilon);
}

[[nodiscard]] bool point_on_segment(
	const math::vec2 point,
	const math::vec2 begin,
	const math::vec2 end) noexcept{
	if(std::abs((end - begin).cross(point - begin)) > hit_epsilon){
		return false;
	}
	return point.x >= std::min(begin.x, end.x) - hit_epsilon
		&& point.x <= std::max(begin.x, end.x) + hit_epsilon
		&& point.y >= std::min(begin.y, end.y) - hit_epsilon
		&& point.y <= std::max(begin.y, end.y) + hit_epsilon;
}

[[nodiscard]] bool segments_intersect(
	const math::vec2 a,
	const math::vec2 b,
	const math::vec2 c,
	const math::vec2 d) noexcept{
	if(editor_detail::point_on_segment(c, a, b)
		|| editor_detail::point_on_segment(d, a, b)
		|| editor_detail::point_on_segment(a, c, d)
		|| editor_detail::point_on_segment(b, c, d)){
		return true;
	}
	return !editor_detail::points_same_side_of_line(a, b, c, d)
		&& !editor_detail::points_same_side_of_line(c, d, a, b);
}

[[nodiscard]] bool segment_intersects_region(
	const math::vec2 begin,
	const math::vec2 end,
	const math::frect region) noexcept{
	if(region.contains_loose(begin) || region.contains_loose(end)){
		return true;
	}
	const std::array vertices{
		region.vert_00(),
		region.vert_10(),
		region.vert_11(),
		region.vert_01()
	};
	for(std::size_t index = 0u; index != vertices.size(); ++index){
		if(editor_detail::segments_intersect(begin, end, vertices[index], vertices[(index + 1u) % vertices.size()])){
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool polygon_contains(
	const physics::convex_polygon_shape& polygon,
	const math::vec2 local_point) noexcept{
	if(polygon.vertices.size() < 3u){
		return false;
	}

	bool inside{};
	for(std::size_t index = 0u; index != polygon.vertices.size(); ++index){
		const math::vec2 a = polygon.vertices[index];
		const math::vec2 b = polygon.vertices[(index + 1u) % polygon.vertices.size()];
		if(editor_detail::point_segment_distance2(local_point, a, b) <= hit_epsilon * hit_epsilon){
			return true;
		}
		if((a.y > local_point.y) != (b.y > local_point.y)){
			const float x = (b.x - a.x) * (local_point.y - a.y) / (b.y - a.y) + a.x;
			if(local_point.x < x){
				inside = !inside;
			}
		}
	}
	return inside;
}

[[nodiscard]] bool polygon_outline_contains(
	const physics::convex_polygon_shape& polygon,
	const math::vec2 local_point,
	const float radius,
	const bool closed) noexcept{
	if(polygon.vertices.size() < 2u){
		return false;
	}

	const float radius2 = radius * radius;
	for(std::size_t index = 0u; index + 1u < polygon.vertices.size(); ++index){
		if(editor_detail::point_segment_distance2(local_point, polygon.vertices[index], polygon.vertices[index + 1u])
			<= radius2){
			return true;
		}
	}
	return closed
		&& editor_detail::point_segment_distance2(local_point, polygon.vertices.back(), polygon.vertices.front())
			<= radius2;
}

[[nodiscard]] bool part_contains(
	const collision::editor::part& part,
	const math::vec2 world_point,
	const float radius = editor_detail::hit_epsilon) noexcept{
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
		if(part.uses_explicit_polygon_edges()){
			const auto analysis = collision::editor::analyze_part_polygon_graph(part, false);
			if(analysis.exportable()){
				physics::convex_polygon_shape ordered_polygon{.vertices = analysis.ordered_vertices};
				if(editor_detail::polygon_contains(ordered_polygon, local)){
					return true;
				}
			}
			const float radius2 = radius * radius;
			const std::size_t edge_count = collision::editor::part_polygon_edge_count(part);
			for(std::size_t edge_index = 0u; edge_index != edge_count; ++edge_index){
				const auto edge = collision::editor::part_polygon_edge_at(part, edge_index);
				if(editor_detail::point_segment_distance2(
					local,
					part.convex_polygon.vertices[edge.first],
					part.convex_polygon.vertices[edge.second]) <= radius2){
					return true;
				}
			}
			return false;
		}
		if(const auto analysis = collision::editor::analyze_part_polygon_graph(part, false); analysis.exportable()){
			physics::convex_polygon_shape ordered_polygon{.vertices = analysis.ordered_vertices};
			return editor_detail::polygon_contains(ordered_polygon, local);
		}
		return editor_detail::polygon_outline_contains(part.convex_polygon, local, radius, false);
	default:
		return false;
	}
}

struct bounds_accumulator{
	bool initialized{};
	math::vec2 min{};
	math::vec2 max{};

	void include(const math::vec2 point) noexcept{
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

	void include_expanded(const math::vec2 point, const float radius) noexcept{
		const math::vec2 expansion{radius, radius};
		this->include(point - expansion);
		this->include(point + expansion);
	}

	[[nodiscard]] math::frect rect() const noexcept{
		return initialized ? math::frect{tags::from_vertex, min, max} : math::frect{};
	}
};

[[nodiscard]] math::frect part_world_aabb(const collision::editor::part& part) noexcept{
	bounds_accumulator bounds{};
	switch(part.type){
	case physics::shape_type::circle:
		bounds.include_expanded(part.local_transform.vec, part.circle.radius);
		break;
	case physics::shape_type::capsule:
		bounds.include_expanded(part.capsule.begin >> part.local_transform, part.capsule.radius);
		bounds.include_expanded(part.capsule.end >> part.local_transform, part.capsule.radius);
		break;
	case physics::shape_type::box:{
		const math::vec2 half = part.box.half_extent;
		bounds.include(math::vec2{-half.x, -half.y} >> part.local_transform);
		bounds.include(math::vec2{half.x, -half.y} >> part.local_transform);
		bounds.include(math::vec2{half.x, half.y} >> part.local_transform);
		bounds.include(math::vec2{-half.x, half.y} >> part.local_transform);
		break;
	}
	case physics::shape_type::convex_polygon:
		for(const math::vec2 vertex : part.convex_polygon.vertices){
			bounds.include(vertex >> part.local_transform);
		}
		break;
	default:
		break;
	}
	return bounds.initialized ? bounds.rect() : math::frect{part.local_transform.vec, 0.f};
}

void normalize_selection_indices(std::vector<std::size_t>& indices) noexcept{
	std::ranges::sort(indices);
	indices.erase(std::ranges::unique(indices).begin(), indices.end());
}

void erase_selection_index(std::vector<std::size_t>& indices, const std::size_t index) noexcept{
	indices.erase(std::ranges::remove(indices, index).begin(), indices.end());
}

void add_selection_index(std::vector<std::size_t>& indices, const std::size_t index){
	if(!std::ranges::contains(indices, index)){
		indices.push_back(index);
	}
}

[[nodiscard]] math::trans2 display_transform(
	const collision::editor::document& document,
	const collision::editor::part& part) noexcept{
	static_cast<void>(document);
	return part.local_transform;
}

[[nodiscard]] std::optional<std::size_t> polygon_vertex_at(
	const collision::editor::part& part,
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

[[nodiscard]] std::optional<std::size_t> polygon_edge_at(
	const collision::editor::part& part,
	const math::vec2 world_point,
	const float radius) noexcept{
	if(part.type != physics::shape_type::convex_polygon || part.convex_polygon.vertices.size() < 2u){
		return std::nullopt;
	}

	const math::vec2 local_point = part.local_transform.apply_inv_to(world_point);
	const float radius2 = radius * radius;
	const std::size_t edge_count = collision::editor::part_polygon_edge_count(part);
	for(std::size_t index = edge_count; index != 0u; --index){
		const std::size_t edge_index = index - 1u;
		const auto edge = collision::editor::part_polygon_edge_at(part, edge_index);
		const math::vec2 begin = part.convex_polygon.vertices[edge.first];
		const math::vec2 end = part.convex_polygon.vertices[edge.second];
		if(editor_detail::point_segment_distance2(local_point, begin, end) <= radius2){
			return edge_index;
		}
	}
	return std::nullopt;
}

[[nodiscard]] std::size_t polygon_edge_count(const collision::editor::part& part) noexcept{
	if(part.type != physics::shape_type::convex_polygon || part.convex_polygon.vertices.size() < 2u){
		return 0u;
	}
	return collision::editor::part_polygon_edge_count(part);
}

[[nodiscard]] math::frect polygon_edge_world_aabb(
	const collision::editor::part& part,
	const std::size_t edge_index) noexcept{
	const auto edge = collision::editor::part_polygon_edge_at(part, edge_index);
	const math::vec2 begin = part.convex_polygon.vertices[edge.first] >> part.local_transform;
	const math::vec2 end = part.convex_polygon.vertices[edge.second] >> part.local_transform;
	return math::frect{tags::from_vertex, begin, end};
}

[[nodiscard]] bool polygon_edge_intersects_region(
	const collision::editor::part& part,
	const std::size_t edge_index,
	const math::frect region) noexcept{
	const auto edge = collision::editor::part_polygon_edge_at(part, edge_index);
	const math::vec2 begin = part.convex_polygon.vertices[edge.first] >> part.local_transform;
	const math::vec2 end = part.convex_polygon.vertices[edge.second] >> part.local_transform;
	return editor_detail::segment_intersects_region(begin, end, region);
}

[[nodiscard]] std::vector<std::size_t> polygon_vertices_from_edges(
	const collision::editor::part& part,
	const std::span<const std::size_t> edges){
	std::vector<std::size_t> vertices{};
	const std::size_t edge_count = editor_detail::polygon_edge_count(part);
	for(const std::size_t edge : edges){
		if(edge >= edge_count){
			continue;
		}
		const auto polygon_edge = collision::editor::part_polygon_edge_at(part, edge);
		editor_detail::add_selection_index(vertices, polygon_edge.first);
		editor_detail::add_selection_index(vertices, polygon_edge.second);
	}
	editor_detail::normalize_selection_indices(vertices);
	return vertices;
}

[[nodiscard]] std::optional<std::size_t> polygon_edge_between(
	const collision::editor::part& part,
	const std::size_t first,
	const std::size_t second) noexcept{
	const std::size_t edge_count = editor_detail::polygon_edge_count(part);
	for(std::size_t edge_index = 0u; edge_index != edge_count; ++edge_index){
		const auto edge = collision::editor::part_polygon_edge_at(part, edge_index);
		if((edge.first == first && edge.second == second)
			|| (edge.first == second && edge.second == first)){
			return edge_index;
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

void clear_overlay_pointer_on_dismiss(gui::overlay& overlay, gui::elem*& overlay_ptr){
	auto on_dismiss = react_flow::node_pointer{react_flow::make_listener(
		[overlay_ptr = std::addressof(overlay_ptr)](const gui::overlay_operation_context& context){
			if(context.operation == gui::overlay_operation::dismiss){
				*overlay_ptr = nullptr;
			}
		})};
	overlay.get_operation_provider().connect_successor(*on_dismiss);
}

struct no_selection{};

struct object_selection_data{
	std::optional<std::size_t> selected_part{};
	std::vector<std::size_t> selected_parts{};
};

struct vertex_selection_data{
	std::optional<std::size_t> selected_part{};
	std::optional<std::size_t> selected_vertex{};
	std::vector<std::size_t> selected_parts{};
	std::vector<std::size_t> selected_vertices{};
};

struct edge_selection_data{
	std::optional<std::size_t> selected_part{};
	std::optional<std::size_t> selected_edge{};
	std::vector<std::size_t> selected_parts{};
	std::vector<std::size_t> selected_edges{};
};

using selection_state = std::variant<
	no_selection,
	object_selection_data,
	vertex_selection_data,
	edge_selection_data>;

struct history_entry{
	collision::editor::document document{};
	editor_detail::editor_mode mode{editor_detail::editor_mode::object};
	selection_state selection{};
};

[[nodiscard]] constexpr float operation_precision_scale(const bool precision_mode) noexcept{
	return precision_mode ? 0.2f : 1.f;
}

[[nodiscard]] bool operation_has_axis_constraint(
	const math::bool2 constrain) noexcept{
	return !constrain.area();
}

[[nodiscard]] math::vec2 operation_constraint_axis(
	const math::bool2 constrain,
	const editor_detail::operation_constraint_space constraint_space,
	const float local_rotation) noexcept{
	math::vec2 axis = constrain.x ? math::vec2{1.f, 0.f} : math::vec2{0.f, 1.f};
	if(constraint_space == editor_detail::operation_constraint_space::local){
		axis.rotate_rad(local_rotation);
	}
	return axis;
}

[[nodiscard]] math::vec2 operation_move_delta(
	const std::string_view command,
	const math::vec2 initial_cursor,
	const math::bool2 constrain,
	const editor_detail::operation_constraint_space constraint_space,
	const float local_rotation,
	const bool precision_mode,
	const math::vec2 cursor) noexcept{
	const math::vec2 cursor_delta = cursor - initial_cursor;
	const float scale = operation_precision_scale(precision_mode);
	if(const auto distance = editor_detail::operation_command_value(command)){
		if(operation_has_axis_constraint(constrain)){
			return operation_constraint_axis(
				constrain,
				constraint_space,
				local_rotation) * (*distance * scale);
		}
		if(cursor_delta.length2() <= editor_detail::hit_epsilon * editor_detail::hit_epsilon){
			return {};
		}
		return cursor_delta.copy().normalize() * (*distance * scale);
	}

	if(operation_has_axis_constraint(constrain)
		&& constraint_space == editor_detail::operation_constraint_space::local){
		const math::vec2 axis = operation_constraint_axis(
			constrain,
			constraint_space,
			local_rotation);
		return axis * (cursor_delta.dot(axis) * scale);
	}
	return math::vec2{
		constrain.x ? cursor_delta.x : 0.f,
		constrain.y ? cursor_delta.y : 0.f
	} * scale;
}

[[nodiscard]] float operation_rotation_delta(
	const std::string_view command,
	const math::vec2 initial_cursor,
	const math::vec2 pivot,
	const bool precision_mode,
	const math::vec2 cursor) noexcept{
	if(const auto degrees = editor_detail::operation_command_value(command)){
		return -*degrees * math::deg_to_rad;
	}
	return editor_detail::rotation_delta(initial_cursor, cursor, pivot)
		* operation_precision_scale(precision_mode);
}

struct operation_part_snapshot{
	std::size_t part_index{};
	collision::editor::part part{};
};

struct part_operation{
	editor_detail::operation_kind kind{editor_detail::operation_kind::move};
	math::vec2 initial_cursor{};
	math::vec2 pivot{};
	math::bool2 constrain{true, true};
	editor_detail::operation_constraint_space constraint_space{editor_detail::operation_constraint_space::world};
	std::size_t part_index{};
	std::optional<std::size_t> vertex_index{};
	std::vector<std::size_t> vertex_indices{};
	collision::editor::part source_part{};
	std::vector<operation_part_snapshot> source_parts{};
	bool invalid{};
	bool precision_mode{};
	std::string command{};

	[[nodiscard]] bool active() const noexcept{
		return true;
	}

	[[nodiscard]] float precision_scale() const noexcept{
		return operation_precision_scale(precision_mode);
	}

	[[nodiscard]] bool has_axis_constraint() const noexcept{
		return operation_has_axis_constraint(constrain);
	}

	[[nodiscard]] std::optional<float> command_value() const noexcept{
		return editor_detail::operation_command_value(command);
	}

	[[nodiscard]] math::vec2 move_delta(const math::vec2 cursor) const noexcept{
		return operation_move_delta(
			command,
			initial_cursor,
			constrain,
			constraint_space,
			source_part.local_transform.rot,
			precision_mode,
			cursor);
	}

	[[nodiscard]] float rotation_delta(const math::vec2 cursor) const noexcept{
		return operation_rotation_delta(
			command,
			initial_cursor,
			pivot,
			precision_mode,
			cursor);
	}

	void reset() noexcept{
		kind = editor_detail::operation_kind::move;
		invalid = false;
		precision_mode = false;
		constrain = {true, true};
		constraint_space = editor_detail::operation_constraint_space::world;
		vertex_index.reset();
		vertex_indices.clear();
		source_parts.clear();
		command.clear();
	}
};

struct transform_operation{
	editor_detail::operation_kind kind{editor_detail::operation_kind::move};
	math::vec2 initial_cursor{};
	math::vec2 pivot{};
	math::bool2 constrain{true, true};
	editor_detail::operation_constraint_space constraint_space{editor_detail::operation_constraint_space::world};
	math::trans2 source_transform{};
	math::vec2 source_half_extent{};
	bool precision_mode{};
	std::string command{};

	[[nodiscard]] bool active() const noexcept{
		return true;
	}

	[[nodiscard]] float precision_scale() const noexcept{
		return operation_precision_scale(precision_mode);
	}

	[[nodiscard]] bool has_axis_constraint() const noexcept{
		return operation_has_axis_constraint(constrain);
	}

	[[nodiscard]] std::optional<float> command_value() const noexcept{
		return editor_detail::operation_command_value(command);
	}

	[[nodiscard]] math::vec2 move_delta(const math::vec2 cursor) const noexcept{
		return operation_move_delta(
			command,
			initial_cursor,
			constrain,
			constraint_space,
			source_transform.rot,
			precision_mode,
			cursor);
	}

	[[nodiscard]] float rotation_delta(const math::vec2 cursor) const noexcept{
		return operation_rotation_delta(
			command,
			initial_cursor,
			pivot,
			precision_mode,
			cursor);
	}

	void reset() noexcept{
		kind = editor_detail::operation_kind::move;
		precision_mode = false;
		constrain = {true, true};
		constraint_space = editor_detail::operation_constraint_space::world;
		command.clear();
	}
};

struct object_mode_state{};
struct edit_mode_state{};
struct ref_mode_state{};
struct origin_mode_state{};

struct ref_image_runtime{
	gui::constant_image_region_borrow image_region{};
	std::string loaded_path{};
};

struct mode_state{
	std::variant<
		object_mode_state,
		edit_mode_state,
		ref_mode_state,
		origin_mode_state> value{object_mode_state{}};

	[[nodiscard]] editor_detail::editor_mode kind() const noexcept{
		if(std::holds_alternative<object_mode_state>(value)){
			return editor_detail::editor_mode::object;
		}
		if(std::holds_alternative<edit_mode_state>(value)){
			return editor_detail::editor_mode::edit;
		}
		if(std::holds_alternative<ref_mode_state>(value)){
			return editor_detail::editor_mode::reference_image;
		}
		return editor_detail::editor_mode::origin;
	}

	[[nodiscard]] operator editor_detail::editor_mode() const noexcept{
		return this->kind();
	}

	mode_state& operator=(const editor_detail::editor_mode mode) noexcept{
		switch(mode){
		case editor_detail::editor_mode::object:
			value = object_mode_state{};
			break;
		case editor_detail::editor_mode::edit:
			value = edit_mode_state{};
			break;
		case editor_detail::editor_mode::reference_image:
			value = ref_mode_state{};
			break;
		case editor_detail::editor_mode::origin:
			value = origin_mode_state{};
			break;
		default:
			std::unreachable();
		}
		return *this;
	}
};

struct knife_cut_state{
	std::size_t part_index{};
	std::vector<math::vec2> points{};
};

struct box_selection_state{
	math::vec2 begin_world{};
	math::vec2 current_world{};

	void update(const math::vec2 world_pos) noexcept{
		current_world = world_pos;
	}

	[[nodiscard]] math::frect region() const noexcept{
		return math::frect{tags::from_vertex, begin_world, current_world};
	}
};

struct idle_interaction{};

struct object_part_interaction{
	part_operation operation{};
};

struct edit_part_interaction{
	part_operation operation{};
};

struct ref_transform_interaction{
	transform_operation operation{};
};

struct origin_transform_interaction{
	transform_operation operation{};
};

using interaction_state = std::variant<
	idle_interaction,
	object_part_interaction,
	edit_part_interaction,
	ref_transform_interaction,
	origin_transform_interaction,
	knife_cut_state,
	box_selection_state>;

struct editor_state{
	static constexpr std::size_t history_limit = 64u;

	collision::editor::document document{};
	mode_state mode{};
	selection_state selection{};
	ref_image_runtime reference_image{};
	interaction_state interaction{};
	mo_yanxi::history_stack<history_entry> history{history_limit};
	editor_detail::rotation_reference_mode rotation_reference{editor_detail::rotation_reference_mode::median};
	math::vec2 rotation_cursor_pivot{};
	std::string last_error{};
	std::future<alpha_hull_result> alpha_hull_future{};

	[[nodiscard]] editor_state(){
		static_cast<void>(this->add_shape(physics::shape_type::box, {}, false));
		this->push_history();
	}

	[[nodiscard]] std::size_t part_count() const noexcept{
		return document.parts.size();
	}

	[[nodiscard]] object_selection_data* object_selection() noexcept{
		return std::get_if<object_selection_data>(std::addressof(selection));
	}

	[[nodiscard]] const object_selection_data* object_selection() const noexcept{
		return std::get_if<object_selection_data>(std::addressof(selection));
	}

	[[nodiscard]] vertex_selection_data* edit_vertex_selection() noexcept{
		return std::get_if<vertex_selection_data>(std::addressof(selection));
	}

	[[nodiscard]] const vertex_selection_data* edit_vertex_selection() const noexcept{
		return std::get_if<vertex_selection_data>(std::addressof(selection));
	}

	[[nodiscard]] edge_selection_data* edit_edge_selection() noexcept{
		return std::get_if<edge_selection_data>(std::addressof(selection));
	}

	[[nodiscard]] const edge_selection_data* edit_edge_selection() const noexcept{
		return std::get_if<edge_selection_data>(std::addressof(selection));
	}

	[[nodiscard]] editor_detail::polygon_selection_domain edit_selection_domain() const noexcept{
		return this->edit_edge_selection() != nullptr
			? editor_detail::polygon_selection_domain::edge
			: editor_detail::polygon_selection_domain::vertex;
	}

	[[nodiscard]] std::optional<std::size_t> object_selected_part_index() const noexcept{
		const auto* object = this->object_selection();
		return object != nullptr ? object->selected_part : std::optional<std::size_t>{};
	}

	[[nodiscard]] std::span<const std::size_t> object_selected_part_indices() const noexcept{
		const auto* object = this->object_selection();
		return object != nullptr
			? std::span<const std::size_t>{object->selected_parts.data(), object->selected_parts.size()}
			: std::span<const std::size_t>{};
	}

	[[nodiscard]] std::optional<std::size_t> edit_selected_part_index() const noexcept{
		if(const auto* vertices = this->edit_vertex_selection(); vertices != nullptr){
			return vertices->selected_part;
		}
		if(const auto* edges = this->edit_edge_selection(); edges != nullptr){
			return edges->selected_part;
		}
		return std::nullopt;
	}

	[[nodiscard]] std::span<const std::size_t> edit_selected_part_indices() const noexcept{
		if(const auto* vertices = this->edit_vertex_selection(); vertices != nullptr){
			return {vertices->selected_parts.data(), vertices->selected_parts.size()};
		}
		if(const auto* edges = this->edit_edge_selection(); edges != nullptr){
			return {edges->selected_parts.data(), edges->selected_parts.size()};
		}
		return {};
	}

	[[nodiscard]] std::optional<std::size_t> edit_selected_vertex_index() const noexcept{
		const auto* vertices = this->edit_vertex_selection();
		return vertices != nullptr ? vertices->selected_vertex : std::optional<std::size_t>{};
	}

	[[nodiscard]] std::span<const std::size_t> edit_selected_vertex_indices() const noexcept{
		const auto* vertices = this->edit_vertex_selection();
		return vertices != nullptr
			? std::span<const std::size_t>{vertices->selected_vertices.data(), vertices->selected_vertices.size()}
			: std::span<const std::size_t>{};
	}

	[[nodiscard]] std::optional<std::size_t> edit_selected_edge_index() const noexcept{
		const auto* edges = this->edit_edge_selection();
		return edges != nullptr ? edges->selected_edge : std::optional<std::size_t>{};
	}

	[[nodiscard]] std::span<const std::size_t> edit_selected_edge_indices() const noexcept{
		const auto* edges = this->edit_edge_selection();
		return edges != nullptr
			? std::span<const std::size_t>{edges->selected_edges.data(), edges->selected_edges.size()}
			: std::span<const std::size_t>{};
	}

	[[nodiscard]] collision::editor::part* selected() noexcept{
		const auto selected_part = this->current_selected_part_index();
		if(!selected_part || *selected_part >= document.parts.size()){
			return nullptr;
		}
		return std::addressof(document.parts[*selected_part]);
	}

	[[nodiscard]] const collision::editor::part* selected() const noexcept{
		const auto selected_part = this->current_selected_part_index();
		if(!selected_part || *selected_part >= document.parts.size()){
			return nullptr;
		}
		return std::addressof(document.parts[*selected_part]);
	}

	[[nodiscard]] bool operation_active() const noexcept{
		return std::holds_alternative<object_part_interaction>(interaction)
			|| std::holds_alternative<edit_part_interaction>(interaction)
			|| std::holds_alternative<ref_transform_interaction>(interaction)
			|| std::holds_alternative<origin_transform_interaction>(interaction);
	}

	[[nodiscard]] bool reference_image_loaded() const noexcept{
		return static_cast<bool>(reference_image.image_region)
			&& reference_image.loaded_path == document.reference_image.path;
	}

	[[nodiscard]] std::optional<std::size_t> current_selected_part_index() const noexcept{
		switch(mode.kind()){
		case editor_detail::editor_mode::object:
			return this->object_selected_part_index();
		case editor_detail::editor_mode::edit:
			return this->edit_selected_part_index();
		default:
			return std::nullopt;
		}
	}

	[[nodiscard]] std::string selected_text() const{
		const auto* part = this->selected();
		if(part == nullptr){
			return "none";
		}
		const auto selected_part = this->current_selected_part_index();
		const std::size_t selected_part_count = mode == editor_detail::editor_mode::edit
			? this->edit_selected_part_indices().size()
			: this->object_selected_part_indices().size();
		const std::string closed_suffix = part->type == physics::shape_type::convex_polygon
			? std::format(
				" {}",
				editor_detail::graph_state_short_name(
					collision::editor::analyze_part_polygon_graph(*part).state))
			: std::string{};
		const std::string suffix = selected_part_count > 1u
			? std::format(" ({} p)", selected_part_count)
			: std::string{};
		if(const auto selected_edge = this->edit_selected_edge_index();
			mode == editor_detail::editor_mode::edit && selected_edge){
			const auto selected_edges = this->edit_selected_edge_indices();
			if(selected_edges.size() > 1u){
				return std::format(
					"{} {}{} e {} ({} sel){}",
					editor_detail::shape_type_name(part->type),
					*selected_part,
					closed_suffix,
					*selected_edge,
					selected_edges.size(),
					suffix);
			}
			return std::format(
				"{} {}{} e {}{}",
				editor_detail::shape_type_name(part->type),
				*selected_part,
				closed_suffix,
				*selected_edge,
				suffix);
		}
		if(const auto selected_vertex = this->edit_selected_vertex_index();
			mode == editor_detail::editor_mode::edit && selected_vertex){
			const auto selected_vertices = this->edit_selected_vertex_indices();
			if(selected_vertices.size() > 1u){
				return std::format(
					"{} {}{} v {} ({} sel){}",
					editor_detail::shape_type_name(part->type),
					*selected_part,
					closed_suffix,
					*selected_vertex,
					selected_vertices.size(),
					suffix);
			}
			return std::format(
				"{} {}{} v {}{}",
				editor_detail::shape_type_name(part->type),
				*selected_part,
				closed_suffix,
				*selected_vertex,
				suffix);
		}
		return std::format(
			"{} {}{}{}",
			editor_detail::shape_type_name(part->type),
			*selected_part,
			closed_suffix,
			suffix);
	}

	[[nodiscard]] std::string operation_text() const{
		const auto append_operation_text = [](std::string text, const auto& operation){
			if(operation.has_axis_constraint()){
				text += " ";
				text += operation.constraint_space == editor_detail::operation_constraint_space::local
					? "local "
					: "global ";
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

	[[nodiscard]] std::string rotation_reference_text() const{
		if(rotation_reference == editor_detail::rotation_reference_mode::cursor){
			return std::format(
				"{} ({:.3f}, {:.3f})",
				editor_detail::rotation_reference_mode_name(rotation_reference),
				rotation_cursor_pivot.x,
				rotation_cursor_pivot.y);
		}
		return std::string{editor_detail::rotation_reference_mode_name(rotation_reference)};
	}

	void set_rotation_reference_mode(
		const editor_detail::rotation_reference_mode mode,
		const math::vec2 cursor) noexcept{
		rotation_reference = mode;
		if(mode == editor_detail::rotation_reference_mode::cursor){
			rotation_cursor_pivot = cursor;
		}
		last_error.clear();
	}

	[[nodiscard]] std::optional<math::vec2> active_rotation_operation_pivot() const noexcept{
		if(const auto* operation = this->current_part_operation();
			operation != nullptr
			&& operation->active()
			&& operation->kind == editor_detail::operation_kind::rotate){
			return operation->pivot;
		}
		if(const auto* operation = this->current_transform_operation();
			operation != nullptr
			&& operation->active()
			&& operation->kind == editor_detail::operation_kind::rotate){
			return operation->pivot;
		}
		return std::nullopt;
	}

	[[nodiscard]] std::string operation_hint_text() const{
		const auto* operation = this->current_part_operation();
		if(operation != nullptr && operation->active() && operation->kind == editor_detail::operation_kind::move){
			return "axis: X/Y g, XX/YY l";
		}
		const auto* transform_operation = this->current_transform_operation();
		if(transform_operation != nullptr
			&& transform_operation->active()
			&& transform_operation->kind == editor_detail::operation_kind::move){
			return "axis: X/Y g, XX/YY l";
		}
		return {};
	}

	void set_object_part_selection(
		std::optional<std::size_t> selected_part,
		std::vector<std::size_t> selected_parts){
		if(!selected_part && selected_parts.empty()){
			selection = no_selection{};
			return;
		}
		selection = object_selection_data{
			.selected_part = selected_part,
			.selected_parts = std::move(selected_parts)
		};
	}

	void set_edit_vertex_selection(
		std::optional<std::size_t> selected_part,
		std::optional<std::size_t> selected_vertex,
		std::vector<std::size_t> selected_parts,
		std::vector<std::size_t> selected_vertices){
		selection = vertex_selection_data{
			.selected_part = selected_part,
			.selected_vertex = selected_vertex,
			.selected_parts = std::move(selected_parts),
			.selected_vertices = std::move(selected_vertices)
		};
	}

	void set_edit_edge_selection(
		std::optional<std::size_t> selected_part,
		std::optional<std::size_t> selected_edge,
		std::vector<std::size_t> selected_parts,
		std::vector<std::size_t> selected_edges){
		selection = edge_selection_data{
			.selected_part = selected_part,
			.selected_edge = selected_edge,
			.selected_parts = std::move(selected_parts),
			.selected_edges = std::move(selected_edges)
		};
	}

	void set_edit_part_selection_for_current_domain(
		std::optional<std::size_t> selected_part,
		std::vector<std::size_t> selected_parts){
		if(this->edit_selection_domain() == editor_detail::polygon_selection_domain::edge){
			this->set_edit_edge_selection(selected_part, std::nullopt, std::move(selected_parts), {});
			return;
		}
		this->set_edit_vertex_selection(selected_part, std::nullopt, std::move(selected_parts), {});
	}

	void set_part_selection_for_current_mode(
		std::optional<std::size_t> selected_part,
		std::vector<std::size_t> selected_parts){
		switch(mode.kind()){
		case editor_detail::editor_mode::object:
			this->set_object_part_selection(selected_part, std::move(selected_parts));
			return;
		case editor_detail::editor_mode::edit:
			this->set_edit_part_selection_for_current_domain(selected_part, std::move(selected_parts));
			return;
		default:
			return;
		}
	}

	[[nodiscard]] std::optional<std::size_t> selected_polygon_part_index_for_operation(
		const std::string_view operation_name){
		if(mode != editor_detail::editor_mode::object
			&& mode != editor_detail::editor_mode::edit){
			last_error = std::format("{} requires object or edit mode", operation_name);
			return std::nullopt;
		}

		const auto selected_part = this->current_selected_part_index();
		if(!selected_part || *selected_part >= document.parts.size()){
			last_error = std::format("{} requires a selected polygon", operation_name);
			return std::nullopt;
		}
		if(document.parts[*selected_part].type != physics::shape_type::convex_polygon){
			last_error = std::format("{} requires a polygon", operation_name);
			return std::nullopt;
		}
		return selected_part;
	}

	void clear_selection_for_current_mode() noexcept{
		switch(mode.kind()){
		case editor_detail::editor_mode::object:
			selection = no_selection{};
			return;
		case editor_detail::editor_mode::edit:
			if(this->edit_selection_domain() == editor_detail::polygon_selection_domain::edge){
				selection = edge_selection_data{};
				return;
			}
			selection = vertex_selection_data{};
			return;
		default:
			return;
		}
	}

	void convert_selection_for_mode(const editor_detail::editor_mode new_mode){
		switch(new_mode){
		case editor_detail::editor_mode::object:
			if(this->object_selection() != nullptr || std::holds_alternative<no_selection>(selection)){
				return;
			}
			if(const auto* vertices = this->edit_vertex_selection(); vertices != nullptr){
				this->set_object_part_selection(vertices->selected_part, vertices->selected_parts);
				return;
			}
			if(const auto* edges = this->edit_edge_selection(); edges != nullptr){
				this->set_object_part_selection(edges->selected_part, edges->selected_parts);
				return;
			}
			return;
		case editor_detail::editor_mode::edit:
			if(this->edit_vertex_selection() != nullptr || this->edit_edge_selection() != nullptr){
				return;
			}
			if(const auto* object = this->object_selection(); object != nullptr){
				this->set_edit_vertex_selection(object->selected_part, std::nullopt, object->selected_parts, {});
				return;
			}
			selection = vertex_selection_data{};
			return;
		case editor_detail::editor_mode::reference_image:
		case editor_detail::editor_mode::origin:
			return;
		default:
			std::unreachable();
		}
	}

	void set_mode(const editor_detail::editor_mode new_mode){
		if(mode == new_mode){
			return;
		}
		if(this->operation_active()){
			this->cancel_operation();
		}
		if(this->knife_cut() != nullptr || this->box_selection() != nullptr){
			this->clear_interaction();
		}
		this->convert_selection_for_mode(new_mode);
		mode = new_mode;
		last_error.clear();
	}

	[[nodiscard]] std::size_t add_shape(
		physics::shape_type type,
		math::vec2 position = {},
		bool record_history = true);

	void select_at(math::vec2 world_point, float vertex_radius, bool extend_selection = false, bool toggle_selection = false) noexcept;

	void select_in_region(math::frect region, float vertex_radius, bool extend_selection = false, bool toggle_selection = false) noexcept;

	void set_edit_selection_domain(editor_detail::polygon_selection_domain domain) noexcept;

	void select_all() noexcept;

	void erase_selected();

	void duplicate_selected();

	void add_vertex_between_selected();

	void insert_vertex_at_cursor(math::vec2 cursor);

	void connect_selected_vertices_as_edge();

	void merge_selected_vertices(editor_detail::vertex_merge_mode merge_mode);

	void make_selected_polygon_convex_hull();

	void split_selected_polygon_to_convex_parts();

	void cut_selected_polygon();

	void start_knife_cut(math::vec2 cursor);

	void add_knife_cut_point(math::vec2 cursor);

	void erase_knife_cut_point();

	void commit_knife_cut();

	void cancel_knife_cut() noexcept;

	void merge_selected_polygons_as_hull();

	void join_selected_polygons(collision::editor::join_origin_mode origin_mode);

	void generate_polygon_from_reference_alpha();

	void poll_alpha_hull_result();

	void toggle_mirror_x();

	void toggle_mirror_y();

	[[nodiscard]] bool reset_position_for_current_mode();

	[[nodiscard]] bool reset_rotation_for_current_mode();

	[[nodiscard]] bool start_operation(editor_detail::operation_kind kind, math::vec2 cursor);

	[[nodiscard]] bool preview_operation(const math::vec2 cursor){
		if(auto* operation = this->current_part_operation(); operation != nullptr && operation->active()){
			return this->preview_part_operation(*operation, cursor);
		}
		if(auto* operation = this->current_transform_operation(); operation != nullptr && operation->active()){
			return this->preview_transform_operation(*operation, cursor);
		}
		return true;
	}

	[[nodiscard]] bool commit_operation();

	void cancel_operation();

	[[nodiscard]] bool set_operation_precision(bool precision_mode, math::vec2 cursor);

	[[nodiscard]] bool toggle_operation_constraint(math::bool2 constrain) noexcept;

	[[nodiscard]] bool input_operation_character(char32_t value);

	[[nodiscard]] bool erase_operation_character();

	[[nodiscard]] bool clear_operation_command() noexcept;

	[[nodiscard]] knife_cut_state* knife_cut() noexcept{
		return std::get_if<knife_cut_state>(std::addressof(interaction));
	}

	[[nodiscard]] const knife_cut_state* knife_cut() const noexcept{
		return std::get_if<knife_cut_state>(std::addressof(interaction));
	}

	[[nodiscard]] box_selection_state* box_selection() noexcept{
		return std::get_if<box_selection_state>(std::addressof(interaction));
	}

	[[nodiscard]] const box_selection_state* box_selection() const noexcept{
		return std::get_if<box_selection_state>(std::addressof(interaction));
	}

	[[nodiscard]] const part_operation* object_part_operation() const noexcept{
		if(const auto* operation = std::get_if<object_part_interaction>(std::addressof(interaction))){
			return std::addressof(operation->operation);
		}
		return nullptr;
	}

	void begin_box_selection(const math::vec2 world_pos) noexcept{
		interaction = box_selection_state{
			.begin_world = world_pos,
			.current_world = world_pos
		};
	}

	void cancel_box_selection() noexcept{
		this->clear_interaction();
	}

	void push_history(){
		history.push(history_entry{
			.document = document,
			.mode = mode,
			.selection = selection
		});
	}

	void undo(){
		if(this->operation_active()){
			this->cancel_operation();
		}
		if(!history.has_prev()){
			return;
		}
		history.to_prev();
		this->apply_history_entry(history.current());
	}

	void redo(){
		if(this->operation_active()){
			this->cancel_operation();
		}
		if(!history.has_next()){
			return;
		}
		history.to_next();
		this->apply_history_entry(history.current());
	}

	void set_reference_image(
		std::filesystem::path path,
		gui::constant_image_region_borrow&& region,
		math::vec2 image_size,
		math::vec2 position);

	void clear_reference_image();

	[[nodiscard]] bool save_document(const std::filesystem::path& path);

	[[nodiscard]] bool load_document(const std::filesystem::path& path);

private:
	void apply_history_entry(const history_entry& entry);

	void validate_selection() noexcept;

	[[nodiscard]] bool save_operation_mid_data(math::vec2 cursor);

	[[nodiscard]] std::optional<math::vec2> rotation_reference_pivot() const;

	void restore_part_operation_sources(const part_operation& operation) noexcept;

	[[nodiscard]] bool part_operation_sources_available(const part_operation& operation) const noexcept;

	void clear_interaction() noexcept{
		interaction = idle_interaction{};
	}

	[[nodiscard]] part_operation* current_part_operation() noexcept{
		if(auto* operation = std::get_if<object_part_interaction>(std::addressof(interaction))){
			return std::addressof(operation->operation);
		}
		if(auto* operation = std::get_if<edit_part_interaction>(std::addressof(interaction))){
			return std::addressof(operation->operation);
		}
		return nullptr;
	}

	[[nodiscard]] transform_operation* current_transform_operation() noexcept{
		if(auto* operation = std::get_if<ref_transform_interaction>(
			std::addressof(interaction))){
			return std::addressof(operation->operation);
		}
		if(auto* operation = std::get_if<origin_transform_interaction>(std::addressof(interaction))){
			return std::addressof(operation->operation);
		}
		return nullptr;
	}

	[[nodiscard]] const part_operation* current_part_operation() const noexcept{
		if(const auto* operation = std::get_if<object_part_interaction>(std::addressof(interaction))){
			return std::addressof(operation->operation);
		}
		if(const auto* operation = std::get_if<edit_part_interaction>(std::addressof(interaction))){
			return std::addressof(operation->operation);
		}
		return nullptr;
	}

	[[nodiscard]] const transform_operation* current_transform_operation() const noexcept{
		if(const auto* operation = std::get_if<ref_transform_interaction>(
			std::addressof(interaction))){
			return std::addressof(operation->operation);
		}
		if(const auto* operation = std::get_if<origin_transform_interaction>(std::addressof(interaction))){
			return std::addressof(operation->operation);
		}
		return nullptr;
	}

	[[nodiscard]] bool preview_part_operation(part_operation& target, math::vec2 cursor);

	[[nodiscard]] bool preview_transform_operation(transform_operation& target, math::vec2 cursor);
};

struct collision_shape_editor_viewport : gui::viewport{
	editor_state state{};

private:
	struct reference_image_path_listener : react_flow::terminal<std::span<const std::filesystem::path>>{
		collision_shape_editor_viewport* viewport{};

		[[nodiscard]] explicit reference_image_path_listener(collision_shape_editor_viewport* target)
			: viewport(target){
		}

	protected:
		void on_update(react_flow::data_carrier<std::span<const std::filesystem::path>>& data) override;
	};

	struct document_path_listener : react_flow::terminal<std::span<const std::filesystem::path>>{
		collision_shape_editor_viewport* viewport{};

		[[nodiscard]] explicit document_path_listener(collision_shape_editor_viewport* target)
			: viewport(target){
		}

	protected:
		void on_update(react_flow::data_carrier<std::span<const std::filesystem::path>>& data) override;
	};

	gui::elem* add_menu_overlay_{};
	gui::elem* merge_menu_overlay_{};
	gui::elem* context_menu_overlay_{};
	gui::elem* reference_image_file_overlay_{};
	gui::elem* document_file_overlay_{};
	gui::cpd::file_selector_mode document_file_selector_mode_{gui::cpd::file_selector_mode::read};
	std::filesystem::path document_path_{};
	math::vec2 pending_add_position_{};
	math::vec2 last_cursor_scene_pos_{};
	math::vec2 last_cursor_world_pos_{};
	react_flow::node_holder_pinned<reference_image_path_listener> reference_image_path_node_;
	react_flow::node_holder_pinned<document_path_listener> document_path_node_;

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

	[[nodiscard]] float selection_radius() const noexcept{
		return 9.f / camera.get_scale();
	}

	[[nodiscard]] math::vec2 cursor_world_pos() const noexcept{
		return last_cursor_world_pos_;
	}

	[[nodiscard]] math::vec2 local_world_pos(const math::vec2 local_pos) const noexcept{
		return this->get_transferred_pos(local_pos - this->content_src_offset());
	}

	void open_reference_image_file_selector(){
		this->show_reference_image_file_selector();
	}

	void open_document_file_selector(const gui::cpd::file_selector_mode mode){
		this->show_document_file_selector(mode);
	}

	void open_merge_menu(){
		this->show_merge_menu();
	}

private:
	void show_add_menu();

	void close_add_menu();

	void show_merge_menu();

	void close_merge_menu();

	void show_context_menu();

	void close_context_menu();

	void show_reference_image_file_selector();

	void close_reference_image_file_selector();

	void load_reference_image(const std::filesystem::path& path);

	void show_document_file_selector(gui::cpd::file_selector_mode mode);

	void close_document_file_selector();

	void save_document(const std::filesystem::path& path);

	void load_document(const std::filesystem::path& path);

	void refresh_cursor_cache_from_local(const math::vec2 local_pos) noexcept{
		last_cursor_scene_pos_ = gui::util::transform_local2scene(*this, local_pos);
		last_cursor_world_pos_ = this->local_world_pos(local_pos);
	}

	void draw_editor_content() const;
};

namespace collision_shape_editor_prop{
namespace{
void assign_label_text(gui::direct_label* label, const std::string_view text){
	if(label != nullptr){
		label->set_tokenized_text(typesetting::tokenized_text{
			std::string{text},
			typesetting::tokenize_tag::raw
		});
	}
}

template <typename Function>
void setup_button(
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

struct numeric_property : gui::head_body_no_invariant{
	struct input : gui::cpd::numeric_input_area<float>{
		std::move_only_function<void(float)> on_value_changed{};

		[[nodiscard]] input(gui::scene& scene, gui::elem* parent)
			: gui::cpd::numeric_input_area<float>(scene, parent){
		}

		void on_changed(const float value) override{
			if(on_value_changed){
				on_value_changed(value);
			}
		}
	};

	gui::direct_label* label_{};
	input* input_{};
	std::function<void(float)> on_value_changed{};

	[[nodiscard]] numeric_property(gui::scene& scene, gui::elem* parent)
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
		this->create_body([this](input& input_widget){
			input_widget.set_value_no_propagate(0.f);
			input_widget.on_value_changed = [this](const float value){
				if(on_value_changed){
					on_value_changed(value);
				}
			};
			input_ = std::addressof(input_widget);
		});
	}

	void set_label_text(const std::string_view text) const{
		assign_label_text(label_, text);
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

struct check_property : gui::head_body_no_invariant{
	struct box : gui::check_box{
		std::function<void(bool)> on_value_changed{};
		bool suppress_callback{};

		[[nodiscard]] box(gui::scene& scene, gui::elem* parent)
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

	gui::direct_label* label_{};
	box* check_{};
	std::function<void(bool)> on_value_changed{};

	[[nodiscard]] check_property(gui::scene& scene, gui::elem* parent)
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
		this->create_body([this](box& check_widget){
			check_widget.on_value_changed = [this](const bool value){
				if(on_value_changed){
					on_value_changed(value);
				}
			};
			check_ = std::addressof(check_widget);
		});
	}

	void set_label_text(const std::string_view text) const{
		assign_label_text(label_, text);
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

struct text_property : gui::head_body_no_invariant{
	gui::direct_label* label_{};
	gui::direct_label* value_{};

	[[nodiscard]] text_property(gui::scene& scene, gui::elem* parent)
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
		assign_label_text(label_, text);
	}

	void set_value_text(const std::string_view text) const{
		assign_label_text(value_, text);
	}

	void set_property_active(const bool active){
		this->invisible = !active;
	}
};

struct transform_properties : gui::sequence{
	numeric_property* x_{};
	numeric_property* y_{};
	numeric_property* rot_{};

	std::move_only_function<void(float)> on_x_changed{};
	std::move_only_function<void(float)> on_y_changed{};
	std::move_only_function<void(float)> on_rot_degrees_changed{};

	[[nodiscard]] transform_properties(gui::scene& scene, gui::elem* parent)
		: gui::sequence(scene, parent, gui::layout::layout_policy::hori_major){
		this->set_style();
		this->set_expand_policy(gui::layout::expand_policy::resize_to_fit);
		this->template_cell.set_size(64.f).set_pad({2.f, 2.f});

		auto x = this->emplace_back<numeric_property>();
		auto y = this->emplace_back<numeric_property>();
		auto rot = this->emplace_back<numeric_property>();
		x_ = std::addressof(x.elem());
		y_ = std::addressof(y.elem());
		rot_ = std::addressof(rot.elem());
		x_->set_label_text("X");
		y_->set_label_text("Y");
		rot_->set_label_text("Rot");

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

struct mirror_properties : gui::sequence{
	text_property* status_{};
	check_property* mirror_x_{};
	check_property* mirror_y_{};
	transform_properties* origin_{};

	std::function<void(bool)> on_mirror_x_changed{};
	std::function<void(bool)> on_mirror_y_changed{};
	std::function<void(float)> on_origin_x_changed{};
	std::function<void(float)> on_origin_y_changed{};
	std::function<void(float)> on_origin_rot_degrees_changed{};

	[[nodiscard]] mirror_properties(gui::scene& scene, gui::elem* parent)
		: gui::sequence(scene, parent, gui::layout::layout_policy::hori_major){
		this->set_style();
		this->set_expand_policy(gui::layout::expand_policy::resize_to_fit);
		this->template_cell.set_size(32.f).set_pad({2.f, 2.f});

		auto status = this->emplace_back<text_property>();
		auto mirror_x = this->emplace_back<check_property>();
		auto mirror_y = this->emplace_back<check_property>();
		auto origin = this->emplace_back<transform_properties>();
		origin.cell().set_pending();
		status_ = std::addressof(status.elem());
		mirror_x_ = std::addressof(mirror_x.elem());
		mirror_y_ = std::addressof(mirror_y.elem());
		origin_ = std::addressof(origin.elem());

		status_->set_label_text("Mir");
		mirror_x_->set_label_text("Mir X");
		mirror_y_->set_label_text("Mir Y");
		origin_->x_->set_label_text("Mir OX");
		origin_->y_->set_label_text("Mir OY");
		origin_->rot_->set_label_text("Mir Rot");

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

	void set_mirror(const collision::editor::mirror_modifier& mirror) const{
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

struct shape_properties_control : gui::sequence{
	numeric_property* primary_{};
	numeric_property* secondary_{};
	text_property* info_a_{};
	text_property* info_b_{};
	text_property* info_c_{};

	std::function<void(float)> on_primary_changed{};
	std::function<void(float)> on_secondary_changed{};

	[[nodiscard]] shape_properties_control(gui::scene& scene, gui::elem* parent)
		: gui::sequence(scene, parent, gui::layout::layout_policy::hori_major){
		this->set_style();
		this->set_expand_policy(gui::layout::expand_policy::resize_to_fit);
		this->template_cell.set_size(32.f).set_pad({2.f, 2.f});

		auto primary = this->emplace_back<numeric_property>();
		auto secondary = this->emplace_back<numeric_property>();
		auto info_a = this->emplace_back<text_property>();
		auto info_b = this->emplace_back<text_property>();
		auto info_c = this->emplace_back<text_property>();
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
		const std::optional<std::size_t> selected_vertex,
		const std::size_t selected_vertex_count){
		primary_->set_property_active(false);
		secondary_->set_property_active(false);
		info_a_->set_property_active(false);
		info_b_->set_property_active(false);
		info_c_->set_property_active(false);

		std::visit([this, selected_vertex, selected_vertex_count](const auto& properties){
			using properties_type = std::remove_cvref_t<decltype(properties)>;
			if constexpr(std::same_as<properties_type, editor_detail::circle_shape_properties>){
				primary_->set_label_text("Rad");
				primary_->set_value(properties.radius);
				primary_->set_property_active(true);
			}else if constexpr(std::same_as<properties_type, editor_detail::capsule_shape_properties>){
				primary_->set_label_text("Rad");
				primary_->set_value(properties.radius);
				primary_->set_property_active(true);
				info_a_->set_label_text("Begin");
				info_a_->set_value_text(std::format("{:.3f}, {:.3f}", properties.begin.x, properties.begin.y));
				info_b_->set_label_text("End");
				info_b_->set_value_text(std::format("{:.3f}, {:.3f}", properties.end.x, properties.end.y));
				info_c_->set_label_text("Len");
				info_c_->set_value_text(std::format("{:.3f}", properties.length));
				info_a_->set_property_active(true);
				info_b_->set_property_active(true);
				info_c_->set_property_active(true);
			}else if constexpr(std::same_as<properties_type, editor_detail::box_shape_properties>){
				primary_->set_label_text("W");
				secondary_->set_label_text("H");
				primary_->set_value(properties.size.x);
				secondary_->set_value(properties.size.y);
				primary_->set_property_active(true);
				secondary_->set_property_active(true);
			}else if constexpr(std::same_as<properties_type, editor_detail::polygon_shape_properties>){
				info_a_->set_label_text("Verts");
				info_a_->set_value_text(std::format("{}", properties.vertex_count));
				info_b_->set_label_text("Sel V");
				info_b_->set_value_text(selected_vertex
					? std::format("{} ({} sel)", *selected_vertex, selected_vertex_count)
					: std::string{"None"});
				info_c_->set_label_text("State");
				info_c_->set_value_text(std::format(
					"{} / {} e",
					editor_detail::graph_state_short_name(properties.state),
					properties.edge_count));
				info_a_->set_property_active(true);
				info_b_->set_property_active(true);
				info_c_->set_property_active(true);
			}
		}, shape);
	}

	void set_control_active(const bool active){
		this->invisible = !active;
	}
};

struct reference_image_actions : gui::sequence{
	std::function<void()> on_choose{};
	std::function<void()> on_clear{};

	[[nodiscard]] reference_image_actions(gui::scene& scene, gui::elem* parent)
		: gui::sequence(scene, parent, gui::layout::layout_policy::vert_major){
		this->set_style();
		this->set_expand_policy(gui::layout::expand_policy::passive);
		this->template_cell.set_pad({2.f, 2.f});

		auto choose = this->create_back([this](gui::button<gui::direct_label>& button){
			setup_button(button, "Pick", [this]{
				if(on_choose){
					on_choose();
				}
			});
		});
		choose.cell().set_passive(1.f);
		auto clear = this->create_back([this](gui::button<gui::direct_label>& button){
			setup_button(button, "Clear", [this]{
				if(on_clear){
					on_clear();
				}
			});
		});
		clear.cell().set_size(72.f);
	}
};

struct mode_panel : gui::sequence{
	gui::direct_label* title_{};

	[[nodiscard]] mode_panel(gui::scene& scene, gui::elem* parent)
		: gui::sequence(scene, parent, gui::layout::layout_policy::hori_major){
		this->set_style();
		this->set_expand_policy(gui::layout::expand_policy::resize_to_fit);
		this->template_cell.set_pending().set_pad({2.f, 2.f});

		auto title = this->create_back([this](gui::direct_label& label){
			label.set_style(gui::style::family_variant::base_only);
			label.set_fit_type(gui::label_fit_type::scl);
			label.text_entire_align = align::pos::center_left;
			title_ = std::addressof(label);
		});
		title.cell().set_size(36.f);
	}

	void set_title(const std::string_view text) const{
		assign_label_text(title_, text);
	}
};

struct object_mode_panel : mode_panel{
	text_property* shape_{};
	transform_properties* transform_{};
	mirror_properties* mirror_{};

	[[nodiscard]] object_mode_panel(gui::scene& scene, gui::elem* parent)
		: mode_panel(scene, parent){
		auto shape = this->emplace_back<text_property>();
		auto transform = this->emplace_back<transform_properties>();
		auto mirror = this->emplace_back<mirror_properties>();
		shape.cell().set_size(60);
		shape_ = std::addressof(shape.elem());
		transform_ = std::addressof(transform.elem());
		mirror_ = std::addressof(mirror.elem());
		shape_->set_label_text("Type");
	}

	void refresh(const editor_state& state){
		const auto selected_index = state.object_selected_part_index();
		const auto* part = state.selected();
		if(part == nullptr || !selected_index.has_value()){
			this->set_title("Obj");
			shape_->set_value_text("None");
			transform_->set_control_active(false);
			mirror_->set_control_active(false);
			return;
		}

		const auto selected_parts = state.object_selected_part_indices();
		this->set_title(selected_parts.size() > 1u
			? std::format(
				"Obj {}: {} ({} sel)",
				*selected_index,
				editor_detail::shape_type_name(part->type),
				selected_parts.size())
			: std::format(
				"Obj {}: {}",
				*selected_index,
				editor_detail::shape_type_name(part->type)));
		shape_->set_value_text(std::format("{}", editor_detail::shape_type_name(part->type)));
		transform_->set_transform(part->local_transform);
		mirror_->set_mirror(part->mirror);
		transform_->set_control_active(true);
		mirror_->set_control_active(true);
	}
};

struct edit_mode_panel : mode_panel{
	text_property* selection_{};
	shape_properties_control* shape_{};

	[[nodiscard]] edit_mode_panel(gui::scene& scene, gui::elem* parent)
		: mode_panel(scene, parent){
		auto selection = this->emplace_back<text_property>();
		auto shape = this->emplace_back<shape_properties_control>();
		selection_ = std::addressof(selection.elem());
		shape_ = std::addressof(shape.elem());
		selection_->set_label_text("Sel");
	}

	void refresh(const editor_state& state){
		const auto selected_index = state.edit_selected_part_index();
		const auto* part = state.selected();
		if(part == nullptr || !selected_index.has_value()){
			this->set_title("Edit");
			selection_->set_value_text("None");
			shape_->set_control_active(false);
			return;
		}

		const auto selected_parts = state.edit_selected_part_indices();
		this->set_title(selected_parts.size() > 1u
			? std::format(
				"Edit {}: {} ({} sel)",
				*selected_index,
				editor_detail::shape_type_name(part->type),
				selected_parts.size())
			: std::format(
				"Edit {}: {}",
				*selected_index,
				editor_detail::shape_type_name(part->type)));
		const auto selected_vertex = state.edit_selected_vertex_index();
		const auto selected_edge = state.edit_selected_edge_index();
		const auto selection_domain = state.edit_selection_domain();
		selection_->set_value_text(selected_vertex
			? std::format("V {} / {}", *selected_vertex, editor_detail::polygon_selection_domain_name(selection_domain))
			: (selected_edge
				? std::format("E {} / {}", *selected_edge, editor_detail::polygon_selection_domain_name(selection_domain))
				: std::format("All / {}", editor_detail::polygon_selection_domain_name(selection_domain))));
		shape_->set_shape(
			editor_detail::make_shape_properties(*part),
			selected_vertex,
			state.edit_selected_vertex_indices().size());
		shape_->set_control_active(true);
	}
};

struct reference_image_panel : mode_panel{
	reference_image_actions* actions_{};
	text_property* path_{};
	check_property* visible_{};
	transform_properties* transform_{};
	numeric_property* width_{};
	numeric_property* height_{};
	numeric_property* opacity_{};

	[[nodiscard]] reference_image_panel(gui::scene& scene, gui::elem* parent)
		: mode_panel(scene, parent){
		auto actions = this->emplace_back<reference_image_actions>();
		auto path = this->emplace_back<text_property>();
		auto visible = this->emplace_back<check_property>();
		auto transform = this->emplace_back<transform_properties>();
		auto width = this->emplace_back<numeric_property>();
		auto height = this->emplace_back<numeric_property>();
		auto opacity = this->emplace_back<numeric_property>();
		actions.cell().set_size(44.f);
		actions_ = std::addressof(actions.elem());
		path_ = std::addressof(path.elem());
		visible_ = std::addressof(visible.elem());
		transform_ = std::addressof(transform.elem());
		width_ = std::addressof(width.elem());
		height_ = std::addressof(height.elem());
		opacity_ = std::addressof(opacity.elem());

		path_->set_label_text("Path");
		visible_->set_label_text("Show");
		width_->set_label_text("W");
		height_->set_label_text("H");
		opacity_->set_label_text("Alpha");
	}

	void refresh(const collision::editor::reference_image& reference){
		this->set_title("Ref");
		path_->set_value_text(reference.path.empty() ? "None" : reference.path);
		visible_->set_checked(reference.enabled);
		transform_->set_transform(reference.transform);
		width_->set_value(reference.half_extent.x * 2.f);
		height_->set_value(reference.half_extent.y * 2.f);
		opacity_->set_value(reference.opacity);
	}
};

struct origin_panel : mode_panel{
	transform_properties* transform_{};

	[[nodiscard]] origin_panel(gui::scene& scene, gui::elem* parent)
		: mode_panel(scene, parent){
		auto transform = this->emplace_back<transform_properties>();
		transform_ = std::addressof(transform.elem());
	}

	void refresh(const math::trans2 transform){
		this->set_title("Origin");
		transform_->set_transform(transform);
		transform_->set_control_active(true);
	}
};

struct panel : gui::flipper<4u>{
private:
	static constexpr std::size_t mode_panel_count = 4u;

	enum class mode_panel_index : std::size_t{
		object,
		edit,
		reference_image,
		origin
	};

	collision_shape_editor_viewport* viewport_{};
	object_mode_panel* object_panel_{};
	edit_mode_panel* edit_panel_{};
	reference_image_panel* reference_panel_{};
	origin_panel* origin_panel_{};

public:
	[[nodiscard]] panel(gui::scene& scene, gui::elem* parent)
		: gui::flipper<mode_panel_count>(scene, parent){
		this->interactivity = gui::interactivity_flag::children_only;
		this->set_style();
		this->set_expand_policy(gui::layout::expand_policy::resize_to_fit);
		this->set_self_border(gui::border_t{}.set(8.f));

		object_panel_ = std::addressof(this->template emplace<object_mode_panel>(
			std::to_underlying(mode_panel_index::object)));
		edit_panel_ = std::addressof(this->template emplace<edit_mode_panel>(
			std::to_underlying(mode_panel_index::edit)));
		reference_panel_ = std::addressof(this->template emplace<reference_image_panel>(
			std::to_underlying(mode_panel_index::reference_image)));
		origin_panel_ = std::addressof(this->template emplace<origin_panel>(
			std::to_underlying(mode_panel_index::origin)));

		this->wire_callbacks();
	}

	void bind(collision_shape_editor_viewport& viewport){
		viewport_ = std::addressof(viewport);
		this->refresh();
	}

	void refresh(){
		if(viewport_ == nullptr){
			this->invisible = true;
			return;
		}

		auto& state = viewport_->state;
		this->invisible = false;

		switch(state.mode.kind()){
		case editor_detail::editor_mode::object:
			this->switch_to(std::to_underlying(mode_panel_index::object));
			object_panel_->refresh(state);
			break;
		case editor_detail::editor_mode::edit:
			this->switch_to(std::to_underlying(mode_panel_index::edit));
			edit_panel_->refresh(state);
			break;
		case editor_detail::editor_mode::reference_image:
			this->switch_to(std::to_underlying(mode_panel_index::reference_image));
			reference_panel_->refresh(state.document.reference_image);
			break;
		case editor_detail::editor_mode::origin:
			this->switch_to(std::to_underlying(mode_panel_index::origin));
			origin_panel_->refresh(state.document.total_transform);
			break;
		default:
			this->invisible = true;
			break;
		}
	}

private:
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

	[[nodiscard]] static math::trans2* active_transform(editor_state& state) noexcept{
		switch(state.mode.kind()){
		case editor_detail::editor_mode::object:
			if(auto* part = state.selected(); part != nullptr){
				return std::addressof(part->local_transform);
			}
			return nullptr;
		case editor_detail::editor_mode::reference_image:
			return std::addressof(state.document.reference_image.transform);
		case editor_detail::editor_mode::origin:
			return std::addressof(state.document.total_transform);
		default:
			return nullptr;
		}
	}

	[[nodiscard]] static collision::editor::part* selected_object_part(
		editor_state& state) noexcept{
		if(state.mode != editor_detail::editor_mode::object){
			return nullptr;
		}
		return state.selected();
	}

	void set_transform_x(const float value){
		this->edit_state([value](editor_state& state){
			auto* transform = panel::active_transform(state);
			if(transform == nullptr || transform->vec.x == value){
				return false;
			}
			transform->vec.x = value;
			return true;
		});
	}

	void set_transform_y(const float value){
		this->edit_state([value](editor_state& state){
			auto* transform = panel::active_transform(state);
			if(transform == nullptr || transform->vec.y == value){
				return false;
			}
			transform->vec.y = value;
			return true;
		});
	}

	void set_transform_rot_degrees(const float value){
		const float radians = value * math::deg_to_rad;
		this->edit_state([radians](editor_state& state){
			auto* transform = panel::active_transform(state);
			if(transform == nullptr || transform->rot == radians){
				return false;
			}
			transform->rot = radians;
			return true;
		});
	}

	void set_secondary_x(const float value){
		this->edit_state([value](editor_state& state){
			if(!std::isfinite(value)){
				state.last_error = "value must be finite";
				return false;
			}

			switch(state.mode.kind()){
			case editor_detail::editor_mode::edit:{
				auto* part = state.selected();
				if(part == nullptr){
					return false;
				}
				if(value <= 0.f){
					state.last_error = "shape value must be positive";
					return false;
				}

				collision::editor::part candidate = *part;
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
		this->edit_state([value](editor_state& state){
			if(!std::isfinite(value)){
				state.last_error = "value must be finite";
				return false;
			}

			switch(state.mode.kind()){
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

				collision::editor::part candidate = *part;
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
		this->edit_state([value](editor_state& state){
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
		this->edit_state([value](editor_state& state){
			if(state.mode != editor_detail::editor_mode::reference_image
				|| state.document.reference_image.enabled == value){
				return false;
			}
			state.document.reference_image.enabled = value;
			return true;
		});
	}

	void set_mirror_x(const bool value){
		this->edit_state([value](editor_state& state){
			auto* part = panel::selected_object_part(state);
			if(part == nullptr || part->mirror.mirror_x == value){
				return false;
			}
			part->mirror.mirror_x = value;
			collision::editor::snap_part_vertices_to_mirror_axes(*part);
			return true;
		});
	}

	void set_mirror_y(const bool value){
		this->edit_state([value](editor_state& state){
			auto* part = panel::selected_object_part(state);
			if(part == nullptr || part->mirror.mirror_y == value){
				return false;
			}
			part->mirror.mirror_y = value;
			collision::editor::snap_part_vertices_to_mirror_axes(*part);
			return true;
		});
	}

	void set_mirror_origin_x(const float value){
		this->edit_state([value](editor_state& state){
			auto* part = panel::selected_object_part(state);
			if(part == nullptr || part->mirror.origin.vec.x == value){
				return false;
			}
			part->mirror.origin.vec.x = value;
			collision::editor::snap_part_vertices_to_mirror_axes(*part);
			return true;
		});
	}

	void set_mirror_origin_y(const float value){
		this->edit_state([value](editor_state& state){
			auto* part = panel::selected_object_part(state);
			if(part == nullptr || part->mirror.origin.vec.y == value){
				return false;
			}
			part->mirror.origin.vec.y = value;
			collision::editor::snap_part_vertices_to_mirror_axes(*part);
			return true;
		});
	}

	void set_mirror_origin_rot_degrees(const float value){
		const float radians = value * math::deg_to_rad;
		this->edit_state([radians](editor_state& state){
			auto* part = panel::selected_object_part(state);
			if(part == nullptr || part->mirror.origin.rot == radians){
				return false;
			}
			part->mirror.origin.rot = radians;
			collision::editor::snap_part_vertices_to_mirror_axes(*part);
			return true;
		});
	}
};

}

std::size_t editor_state::add_shape(
	const physics::shape_type type,
	const math::vec2 position,
	const bool record_history){
	if(this->operation_active()){
		this->cancel_operation();
	}
	const std::size_t index = document.add_shape(type, position);
	if(mode == editor_detail::editor_mode::edit){
		this->set_edit_part_selection_for_current_domain(index, {index});
	}else{
		this->set_object_part_selection(index, {index});
	}
	last_error.clear();
	if(record_history){
		this->push_history();
	}
	return index;
}

void editor_state::select_at(
	const math::vec2 world_point,
	const float vertex_radius,
	const bool extend_selection,
	const bool toggle_selection) noexcept{
	switch(mode.kind()){
	case editor_detail::editor_mode::object:
	case editor_detail::editor_mode::edit:
		break;
	default:
		return;
	}

	const bool replace_selection = !extend_selection && !toggle_selection;
	const std::optional<std::size_t> focused_edit_part =
		!replace_selection &&
		mode == editor_detail::editor_mode::edit
		&& this->edit_selected_part_index()
		&& *this->edit_selected_part_index() < document.parts.size()
			? this->edit_selected_part_index()
			: std::nullopt;

	for(std::size_t index = document.parts.size(); index != 0u; --index){
		const std::size_t part_index = index - 1u;
		if(focused_edit_part && part_index != *focused_edit_part){
			continue;
		}
		const auto& part = document.parts[part_index];
		if(mode == editor_detail::editor_mode::edit){
			const auto previous_edit_part = this->edit_selected_part_index();
			if(this->edit_selection_domain() == editor_detail::polygon_selection_domain::edge){
				if(const auto edge = editor_detail::polygon_edge_at(part, world_point, vertex_radius)){
					const bool same_active_part = previous_edit_part == part_index;
					std::vector<std::size_t> selected_parts{
						this->edit_selected_part_indices().begin(),
						this->edit_selected_part_indices().end()
					};
					std::vector<std::size_t> selected_edges{
						this->edit_selected_edge_indices().begin(),
						this->edit_selected_edge_indices().end()
					};
					if(!extend_selection && !toggle_selection){
						selected_parts = {part_index};
						selected_edges = {*edge};
					}else{
						if(!same_active_part){
							selected_edges.clear();
						}
						if(!std::ranges::contains(selected_parts, part_index)){
							editor_detail::add_selection_index(selected_parts, part_index);
						}
						auto existing = std::ranges::find(selected_edges, *edge);
						if(toggle_selection && same_active_part && existing != selected_edges.end()){
							selected_edges.erase(existing);
						}else if(existing == selected_edges.end()){
							selected_edges.push_back(*edge);
						}
					}
					const std::optional<std::size_t> selected_edge = selected_edges.empty()
						? std::optional<std::size_t>{}
						: std::optional<std::size_t>{selected_edges.back()};
					this->set_edit_edge_selection(part_index, selected_edge, std::move(selected_parts), std::move(selected_edges));
					return;
				}
			}
			if(const auto vertex = editor_detail::polygon_vertex_at(part, world_point, vertex_radius)){
				const bool same_active_part = previous_edit_part == part_index;
				std::vector<std::size_t> selected_parts{
					this->edit_selected_part_indices().begin(),
					this->edit_selected_part_indices().end()
				};
				std::vector<std::size_t> selected_vertices{
					this->edit_selected_vertex_indices().begin(),
					this->edit_selected_vertex_indices().end()
				};
				if(!extend_selection && !toggle_selection){
					selected_parts = {part_index};
					selected_vertices = {*vertex};
				}else{
					if(!same_active_part){
						selected_vertices.clear();
					}
					if(!std::ranges::contains(selected_parts, part_index)){
						editor_detail::add_selection_index(selected_parts, part_index);
					}
					auto existing = std::ranges::find(selected_vertices, *vertex);
					if(toggle_selection && same_active_part && existing != selected_vertices.end()){
						selected_vertices.erase(existing);
					}else if(existing == selected_vertices.end()){
						selected_vertices.push_back(*vertex);
					}
				}
				const std::optional<std::size_t> selected_vertex = selected_vertices.empty()
					? std::optional<std::size_t>{}
					: std::optional<std::size_t>{selected_vertices.back()};
				this->set_edit_vertex_selection(part_index, selected_vertex, std::move(selected_parts), std::move(selected_vertices));
				return;
			}
		}
		if(editor_detail::part_contains(part, world_point, vertex_radius)){
			if(mode == editor_detail::editor_mode::object){
				std::vector<std::size_t> selected_parts{
					this->object_selected_part_indices().begin(),
					this->object_selected_part_indices().end()
				};
				std::optional<std::size_t> selected_part = this->object_selected_part_index();
				if(toggle_selection){
					if(std::ranges::contains(selected_parts, part_index)){
						editor_detail::erase_selection_index(selected_parts, part_index);
						if(selected_part == part_index){
							selected_part = selected_parts.empty()
								? std::optional<std::size_t>{}
								: std::optional<std::size_t>{selected_parts.back()};
						}
					}else{
						editor_detail::add_selection_index(selected_parts, part_index);
						selected_part = part_index;
					}
				}else if(extend_selection){
					editor_detail::add_selection_index(selected_parts, part_index);
					selected_part = part_index;
				}else{
					selected_part = part_index;
					selected_parts = {part_index};
				}
				this->set_object_part_selection(selected_part, std::move(selected_parts));
				return;
			}

			if(mode == editor_detail::editor_mode::edit){
				std::vector<std::size_t> selected_parts{
					this->edit_selected_part_indices().begin(),
					this->edit_selected_part_indices().end()
				};
				std::optional<std::size_t> selected_part = part_index;
				if(focused_edit_part){
					editor_detail::add_selection_index(selected_parts, part_index);
					this->set_edit_part_selection_for_current_domain(selected_part, std::move(selected_parts));
					return;
				}
				if(toggle_selection){
					if(std::ranges::contains(selected_parts, part_index)){
						editor_detail::erase_selection_index(selected_parts, part_index);
						if(selected_part == part_index){
							selected_part = selected_parts.empty()
								? std::optional<std::size_t>{}
								: std::optional<std::size_t>{selected_parts.back()};
						}
					}else{
						editor_detail::add_selection_index(selected_parts, part_index);
						selected_part = part_index;
					}
				}else if(extend_selection){
					editor_detail::add_selection_index(selected_parts, part_index);
					selected_part = part_index;
				}else{
					selected_parts = {part_index};
					selected_part = part_index;
				}
				this->set_edit_part_selection_for_current_domain(selected_part, std::move(selected_parts));
			}
			return;
		}
	}
	if(focused_edit_part){
		return;
	}
	if(!extend_selection && !toggle_selection){
		this->clear_selection_for_current_mode();
	}
}

void editor_state::select_in_region(
	const math::frect region,
	const float vertex_radius,
	const bool extend_selection,
	const bool toggle_selection) noexcept{
	if(region.is_roughly_zero_area(vertex_radius * 0.5f)){
		this->select_at(region.get_center(), vertex_radius, extend_selection, toggle_selection);
		return;
	}

	switch(mode.kind()){
	case editor_detail::editor_mode::object:{
		std::vector<std::size_t> hit_parts{};
		for(std::size_t index = 0u; index != document.parts.size(); ++index){
			if(region.contains_loose(editor_detail::part_world_aabb(document.parts[index]))){
				hit_parts.push_back(index);
			}
		}

		if(hit_parts.empty()){
			if(!extend_selection && !toggle_selection){
				this->clear_selection_for_current_mode();
			}
			return;
		}

		std::vector<std::size_t> selected_parts{
			this->object_selected_part_indices().begin(),
			this->object_selected_part_indices().end()
		};
		if(toggle_selection){
			for(const std::size_t part_index : hit_parts){
				if(std::ranges::contains(selected_parts, part_index)){
					editor_detail::erase_selection_index(selected_parts, part_index);
				}else{
					editor_detail::add_selection_index(selected_parts, part_index);
				}
			}
		}else if(extend_selection){
			for(const std::size_t part_index : hit_parts){
				editor_detail::add_selection_index(selected_parts, part_index);
			}
		}else{
			selected_parts = std::move(hit_parts);
		}
		const std::optional<std::size_t> selected_part = selected_parts.empty()
			? std::optional<std::size_t>{}
			: std::optional<std::size_t>{selected_parts.back()};
		this->set_object_part_selection(selected_part, std::move(selected_parts));
		return;
	}
	case editor_detail::editor_mode::edit:{
		if(const auto selected_part = this->edit_selected_part_index();
			selected_part && *selected_part < document.parts.size()){
			const std::size_t active_part = *selected_part;
			const auto& part = document.parts[active_part];
			if(part.type != physics::shape_type::convex_polygon){
				return;
			}

			std::vector<std::size_t> selected_parts{
				this->edit_selected_part_indices().begin(),
				this->edit_selected_part_indices().end()
			};
			editor_detail::add_selection_index(selected_parts, active_part);
			if(this->edit_selection_domain() == editor_detail::polygon_selection_domain::edge){
				std::vector<std::size_t> selected_edges{
					this->edit_selected_edge_indices().begin(),
					this->edit_selected_edge_indices().end()
				};
				std::vector<std::size_t> hit_edges{};
				const std::size_t edge_count = editor_detail::polygon_edge_count(part);
				for(std::size_t edge_index = 0u; edge_index != edge_count; ++edge_index){
					if(editor_detail::polygon_edge_intersects_region(part, edge_index, region)){
						hit_edges.push_back(edge_index);
					}
				}
				if(hit_edges.empty()){
					return;
				}
				if(!extend_selection && !toggle_selection){
					selected_edges.clear();
				}
				for(const std::size_t edge_index : hit_edges){
					auto existing = std::ranges::find(selected_edges, edge_index);
					if(toggle_selection && existing != selected_edges.end()){
						selected_edges.erase(existing);
					}else if(existing == selected_edges.end()){
						selected_edges.push_back(edge_index);
					}
				}
				const std::optional<std::size_t> selected_edge = selected_edges.empty()
					? std::optional<std::size_t>{}
					: std::optional<std::size_t>{selected_edges.back()};
				this->set_edit_edge_selection(active_part, selected_edge, std::move(selected_parts), std::move(selected_edges));
				return;
			}

			std::vector<std::size_t> selected_vertices{
				this->edit_selected_vertex_indices().begin(),
				this->edit_selected_vertex_indices().end()
			};
			std::vector<std::size_t> hit_vertices{};
			for(std::size_t vertex_index = 0u; vertex_index != part.convex_polygon.vertices.size(); ++vertex_index){
				const math::vec2 vertex_world = part.convex_polygon.vertices[vertex_index] >> part.local_transform;
				if(region.contains_loose(vertex_world)){
					hit_vertices.push_back(vertex_index);
				}
			}
			if(hit_vertices.empty()){
				return;
			}
			if(!extend_selection && !toggle_selection){
				selected_vertices.clear();
			}
			for(const std::size_t vertex_index : hit_vertices){
				auto existing = std::ranges::find(selected_vertices, vertex_index);
				if(toggle_selection && existing != selected_vertices.end()){
					selected_vertices.erase(existing);
				}else if(existing == selected_vertices.end()){
					selected_vertices.push_back(vertex_index);
				}
			}
			const std::optional<std::size_t> selected_vertex = selected_vertices.empty()
				? std::optional<std::size_t>{}
				: std::optional<std::size_t>{selected_vertices.back()};
			this->set_edit_vertex_selection(active_part, selected_vertex, std::move(selected_parts), std::move(selected_vertices));
			return;
		}

		if(this->edit_selection_domain() == editor_detail::polygon_selection_domain::edge){
			struct edge_hit{
				std::size_t part_index{};
				std::size_t edge_index{};
			};

			std::vector<edge_hit> hit_edges{};
			for(std::size_t part_index = 0u; part_index != document.parts.size(); ++part_index){
				const auto& part = document.parts[part_index];
				if(part.type != physics::shape_type::convex_polygon){
					continue;
				}
				const std::size_t edge_count = editor_detail::polygon_edge_count(part);
				for(std::size_t edge_index = 0u; edge_index != edge_count; ++edge_index){
					if(editor_detail::polygon_edge_intersects_region(part, edge_index, region)){
						hit_edges.push_back(edge_hit{
							.part_index = part_index,
							.edge_index = edge_index
						});
					}
				}
			}

			if(hit_edges.empty()){
				if(!extend_selection && !toggle_selection){
					this->clear_selection_for_current_mode();
				}
				return;
			}

			std::vector<std::size_t> hit_parts{};
			for(const edge_hit hit : hit_edges){
				editor_detail::add_selection_index(hit_parts, hit.part_index);
			}

			std::vector<std::size_t> selected_parts{
				this->edit_selected_part_indices().begin(),
				this->edit_selected_part_indices().end()
			};
			const std::size_t active_part = hit_edges.back().part_index;
			if(toggle_selection || extend_selection){
				for(const std::size_t part_index : hit_parts){
					if(!std::ranges::contains(selected_parts, part_index)){
						editor_detail::add_selection_index(selected_parts, part_index);
					}
				}
			}else{
				selected_parts = std::move(hit_parts);
			}

			std::vector<std::size_t> selected_edges{};
			for(const edge_hit hit : hit_edges){
				if(hit.part_index != active_part){
					continue;
				}
				auto existing = std::ranges::find(selected_edges, hit.edge_index);
				if(toggle_selection && existing != selected_edges.end()){
					selected_edges.erase(existing);
				}else if(existing == selected_edges.end()){
					selected_edges.push_back(hit.edge_index);
				}
			}
			const std::optional<std::size_t> selected_edge = selected_edges.empty()
				? std::optional<std::size_t>{}
				: std::optional<std::size_t>{selected_edges.back()};
			this->set_edit_edge_selection(active_part, selected_edge, std::move(selected_parts), std::move(selected_edges));
			return;
		}

		struct vertex_hit{
			std::size_t part_index{};
			std::size_t vertex_index{};
		};

		std::vector<vertex_hit> hit_vertices{};
		for(std::size_t part_index = 0u; part_index != document.parts.size(); ++part_index){
			const auto& part = document.parts[part_index];
			if(part.type != physics::shape_type::convex_polygon){
				continue;
			}
			for(std::size_t vertex_index = 0u; vertex_index != part.convex_polygon.vertices.size(); ++vertex_index){
				const math::vec2 vertex_world = part.convex_polygon.vertices[vertex_index] >> part.local_transform;
				if(region.contains_loose(vertex_world)){
					hit_vertices.push_back(vertex_hit{
						.part_index = part_index,
						.vertex_index = vertex_index
					});
				}
			}
		}

		if(hit_vertices.empty()){
			if(!extend_selection && !toggle_selection){
				this->clear_selection_for_current_mode();
			}
			return;
		}

		std::vector<std::size_t> hit_parts{};
		for(const vertex_hit hit : hit_vertices){
			editor_detail::add_selection_index(hit_parts, hit.part_index);
		}

		std::vector<std::size_t> selected_parts{
			this->edit_selected_part_indices().begin(),
			this->edit_selected_part_indices().end()
		};
		const std::size_t active_part = hit_vertices.back().part_index;
		if(toggle_selection){
			for(const std::size_t part_index : hit_parts){
				if(!std::ranges::contains(selected_parts, part_index)){
					editor_detail::add_selection_index(selected_parts, part_index);
				}
			}
		}else if(extend_selection){
			for(const std::size_t part_index : hit_parts){
				editor_detail::add_selection_index(selected_parts, part_index);
			}
		}else{
			selected_parts = std::move(hit_parts);
		}

		std::vector<std::size_t> selected_vertices{};
		for(const vertex_hit hit : hit_vertices){
			if(hit.part_index != active_part){
				continue;
			}
			auto existing = std::ranges::find(selected_vertices, hit.vertex_index);
			if(toggle_selection && existing != selected_vertices.end()){
				selected_vertices.erase(existing);
			}else if(existing == selected_vertices.end()){
				selected_vertices.push_back(hit.vertex_index);
			}
		}
		const std::optional<std::size_t> selected_vertex = selected_vertices.empty()
			? std::optional<std::size_t>{}
			: std::optional<std::size_t>{selected_vertices.back()};
		this->set_edit_vertex_selection(active_part, selected_vertex, std::move(selected_parts), std::move(selected_vertices));
		return;
	}
	default:
		return;
	}
}

void editor_state::set_edit_selection_domain(
	const editor_detail::polygon_selection_domain domain) noexcept{
	if(this->edit_selection_domain() == domain){
		return;
	}

	const std::optional<std::size_t> selected_part = this->edit_selected_part_index();
	std::vector<std::size_t> selected_parts{
		this->edit_selected_part_indices().begin(),
		this->edit_selected_part_indices().end()
	};
	if(!selected_part || *selected_part >= document.parts.size()){
		if(domain == editor_detail::polygon_selection_domain::edge){
			this->set_edit_edge_selection(std::nullopt, std::nullopt, {}, {});
		}else{
			this->set_edit_vertex_selection(std::nullopt, std::nullopt, {}, {});
		}
		return;
	}

	const auto& part = document.parts[*selected_part];
	if(part.type != physics::shape_type::convex_polygon){
		if(domain == editor_detail::polygon_selection_domain::edge){
			this->set_edit_edge_selection(selected_part, std::nullopt, std::move(selected_parts), {});
		}else{
			this->set_edit_vertex_selection(selected_part, std::nullopt, std::move(selected_parts), {});
		}
		return;
	}

	switch(domain){
	case editor_detail::polygon_selection_domain::vertex:{
		std::vector<std::size_t> selected_edges{
			this->edit_selected_edge_indices().begin(),
			this->edit_selected_edge_indices().end()
		};
		std::vector<std::size_t> selected_vertices = editor_detail::polygon_vertices_from_edges(
			part,
			selected_edges);
		const std::optional<std::size_t> selected_vertex = selected_vertices.empty()
			? std::optional<std::size_t>{}
			: std::optional<std::size_t>{selected_vertices.back()};
		this->set_edit_vertex_selection(
			selected_part,
			selected_vertex,
			std::move(selected_parts),
			std::move(selected_vertices));
		return;
	}
	case editor_detail::polygon_selection_domain::edge:{
		std::vector<std::size_t> selected_vertices{
			this->edit_selected_vertex_indices().begin(),
			this->edit_selected_vertex_indices().end()
		};
		std::vector<std::size_t> mapped_edges{};
		const std::size_t edge_count = editor_detail::polygon_edge_count(part);
		for(std::size_t edge_index = 0u; edge_index != edge_count; ++edge_index){
			const auto edge = collision::editor::part_polygon_edge_at(part, edge_index);
			if(std::ranges::contains(selected_vertices, edge.first)
				&& std::ranges::contains(selected_vertices, edge.second)){
				mapped_edges.push_back(edge_index);
			}
		}
		const std::optional<std::size_t> selected_edge = mapped_edges.empty()
			? std::optional<std::size_t>{}
			: std::optional<std::size_t>{mapped_edges.back()};
		this->set_edit_edge_selection(
			selected_part,
			selected_edge,
			std::move(selected_parts),
			std::move(mapped_edges));
		return;
	}
	default:
		std::unreachable();
	}
}

void editor_state::select_all() noexcept{
	switch(mode.kind()){
	case editor_detail::editor_mode::object:{
		std::vector<std::size_t> selected_parts{};
		selected_parts.reserve(document.parts.size());
		for(std::size_t index = 0u; index != document.parts.size(); ++index){
			selected_parts.push_back(index);
		}
		const std::optional<std::size_t> selected_part = selected_parts.empty()
			? std::optional<std::size_t>{}
			: std::optional<std::size_t>{selected_parts.back()};
		this->set_object_part_selection(selected_part, std::move(selected_parts));
		return;
	}
	case editor_detail::editor_mode::edit:{
		std::optional<std::size_t> selected_part = this->edit_selected_part_index();
		std::vector<std::size_t> selected_parts{
			this->edit_selected_part_indices().begin(),
			this->edit_selected_part_indices().end()
		};
		if(!selected_part || *selected_part >= document.parts.size()){
			selected_parts.clear();
			selected_parts.reserve(document.parts.size());
			for(std::size_t index = 0u; index != document.parts.size(); ++index){
				selected_parts.push_back(index);
			}
			selected_part = selected_parts.empty()
				? std::optional<std::size_t>{}
				: std::optional<std::size_t>{selected_parts.back()};
		}
		if(!selected_part || *selected_part >= document.parts.size()){
			return;
		}
		const auto& part = document.parts[*selected_part];
		editor_detail::add_selection_index(selected_parts, *selected_part);
		if(part.type != physics::shape_type::convex_polygon){
			this->set_edit_part_selection_for_current_domain(selected_part, std::move(selected_parts));
			return;
		}
		if(this->edit_selection_domain() == editor_detail::polygon_selection_domain::edge){
			const std::size_t edge_count = editor_detail::polygon_edge_count(part);
			std::vector<std::size_t> selected_edges{};
			selected_edges.reserve(edge_count);
			for(std::size_t edge_index = 0u; edge_index != edge_count; ++edge_index){
				selected_edges.push_back(edge_index);
			}
			const std::optional<std::size_t> selected_edge = selected_edges.empty()
				? std::optional<std::size_t>{}
				: std::optional<std::size_t>{selected_edges.back()};
			this->set_edit_edge_selection(selected_part, selected_edge, std::move(selected_parts), std::move(selected_edges));
			return;
		}

		std::vector<std::size_t> selected_vertices{};
		selected_vertices.reserve(part.convex_polygon.vertices.size());
		for(std::size_t vertex_index = 0u; vertex_index != part.convex_polygon.vertices.size(); ++vertex_index){
			selected_vertices.push_back(vertex_index);
		}
		const std::optional<std::size_t> selected_vertex = selected_vertices.empty()
			? std::optional<std::size_t>{}
			: std::optional<std::size_t>{selected_vertices.back()};
		this->set_edit_vertex_selection(selected_part, selected_vertex, std::move(selected_parts), std::move(selected_vertices));
		return;
	}
	default:
		return;
	}
}

void editor_state::erase_selected(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	if(const auto* edge_selection = this->edit_edge_selection();
		mode == editor_detail::editor_mode::edit
		&& edge_selection != nullptr
		&& edge_selection->selected_part
		&& *edge_selection->selected_part < document.parts.size()
		&& !edge_selection->selected_edges.empty()){
		auto& part = document.parts[*edge_selection->selected_part];
		if(part.type != physics::shape_type::convex_polygon){
			return;
		}
		auto candidate = part;
		collision::editor::materialize_part_polygon_edges(candidate);
		std::vector<std::size_t> edges = edge_selection->selected_edges;
		std::ranges::sort(edges);
		edges.erase(std::ranges::unique(edges).begin(), edges.end());
		for(auto cursor = edges.rbegin(); cursor != edges.rend(); ++cursor){
			if(*cursor < candidate.polygon_edges.size()){
				candidate.polygon_edges.erase(
					candidate.polygon_edges.begin() + static_cast<std::ptrdiff_t>(*cursor));
			}
		}
		if(candidate.uses_explicit_polygon_edges()){
			candidate.closed = candidate.ordered_polygon_vertices_for_export().has_value();
		}
		if(!candidate.payload_valid()){
			last_error = "delete would make the polygon edge set invalid";
			return;
		}
		part = std::move(candidate);
		this->set_edit_edge_selection(
			edge_selection->selected_part,
			std::nullopt,
			edge_selection->selected_parts,
			{});
		last_error.clear();
		this->push_history();
		return;
	}
	if(const auto* vertex_selection = this->edit_vertex_selection();
		mode == editor_detail::editor_mode::edit
		&& vertex_selection != nullptr
		&& vertex_selection->selected_part
		&& *vertex_selection->selected_part < document.parts.size()
		&& !vertex_selection->selected_vertices.empty()){
		auto& part = document.parts[*vertex_selection->selected_part];
		if(part.type != physics::shape_type::convex_polygon){
			return;
		}
		auto candidate = part;
		std::vector<std::size_t> vertices = vertex_selection->selected_vertices;
		std::ranges::sort(vertices);
		vertices.erase(std::ranges::unique(vertices).begin(), vertices.end());
		if(candidate.uses_explicit_polygon_edges()){
			std::vector<bool> removed(candidate.convex_polygon.vertices.size());
			for(const std::size_t vertex : vertices){
				if(vertex < removed.size()){
					removed[vertex] = true;
				}
			}
			std::vector<std::size_t> remap(candidate.convex_polygon.vertices.size());
			std::vector<math::vec2> remaining_vertices{};
			remaining_vertices.reserve(candidate.convex_polygon.vertices.size());
			for(std::size_t vertex_index = 0u; vertex_index != candidate.convex_polygon.vertices.size(); ++vertex_index){
				if(removed[vertex_index]){
					continue;
				}
				remap[vertex_index] = remaining_vertices.size();
				remaining_vertices.push_back(candidate.convex_polygon.vertices[vertex_index]);
			}
			std::vector<collision::editor::polygon_edge> remaining_edges{};
			remaining_edges.reserve(candidate.polygon_edges.size());
			for(const auto edge : candidate.polygon_edges){
				if(edge.first >= removed.size()
					|| edge.second >= removed.size()
					|| removed[edge.first]
					|| removed[edge.second]){
					continue;
				}
				remaining_edges.push_back({
					.first = remap[edge.first],
					.second = remap[edge.second]
				});
			}
			candidate.convex_polygon.vertices = std::move(remaining_vertices);
			candidate.polygon_edges = std::move(remaining_edges);
			candidate.closed = candidate.ordered_polygon_vertices_for_export().has_value();
		}else{
			for(auto cursor = vertices.rbegin(); cursor != vertices.rend(); ++cursor){
				if(*cursor >= candidate.convex_polygon.vertices.size()){
					continue;
				}
				candidate.convex_polygon.vertices.erase(
					candidate.convex_polygon.vertices.begin() + static_cast<std::ptrdiff_t>(*cursor));
			}
		}
		if(!candidate.payload_valid()){
			last_error = "delete would make the polygon invalid";
			return;
		}
		part = std::move(candidate);
		this->set_edit_vertex_selection(
			vertex_selection->selected_part,
			std::nullopt,
			vertex_selection->selected_parts,
			{});
		last_error.clear();
		this->push_history();
		return;
	}
	std::vector<std::size_t> selected_parts{};
	switch(mode.kind()){
	case editor_detail::editor_mode::object:
		selected_parts.assign(
			this->object_selected_part_indices().begin(),
			this->object_selected_part_indices().end());
		if(selected_parts.empty()){
			if(const auto selected_part = this->object_selected_part_index()){
				selected_parts.push_back(*selected_part);
			}
		}
		break;
	case editor_detail::editor_mode::edit:
		selected_parts.assign(
			this->edit_selected_part_indices().begin(),
			this->edit_selected_part_indices().end());
		if(selected_parts.empty()){
			if(const auto selected_part = this->edit_selected_part_index()){
				selected_parts.push_back(*selected_part);
			}
		}
		break;
	default:
		break;
	}
	selected_parts.erase(std::ranges::remove_if(selected_parts, [this](const std::size_t index) noexcept{
		return index >= document.parts.size();
	}).begin(), selected_parts.end());
	editor_detail::normalize_selection_indices(selected_parts);
	if(selected_parts.empty()){
		return;
	}
	for(auto cursor = selected_parts.rbegin(); cursor != selected_parts.rend(); ++cursor){
		document.parts.erase(document.parts.begin() + static_cast<std::ptrdiff_t>(*cursor));
	}
	this->clear_selection_for_current_mode();
	last_error.clear();
	this->push_history();
}

void editor_state::add_vertex_between_selected(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	const auto selected_part = this->edit_selected_part_index();
	if(mode != editor_detail::editor_mode::edit
		|| !selected_part
		|| *selected_part >= document.parts.size()){
		last_error = "add vertex requires a selected polygon";
		return;
	}

	auto& part = document.parts[*selected_part];
	if(part.type != physics::shape_type::convex_polygon){
		last_error = "add vertex requires a polygon";
		return;
	}

	std::optional<std::size_t> edge_index = this->edit_selected_edge_index();
	std::vector<std::size_t> selected_vertices{
		this->edit_selected_vertex_indices().begin(),
		this->edit_selected_vertex_indices().end()
	};
	if(!edge_index && selected_vertices.size() == 2u){
		edge_index = editor_detail::polygon_edge_between(
			part,
			selected_vertices[0u],
			selected_vertices[1u]);
	}
	if(!edge_index || *edge_index >= editor_detail::polygon_edge_count(part)){
		last_error = "select an existing edge to add a midpoint vertex";
		return;
	}

	auto candidate = part;
	collision::editor::materialize_part_polygon_edges(candidate);
	const auto edge = collision::editor::part_polygon_edge_at(candidate, *edge_index);
	const math::vec2 midpoint =
		(candidate.convex_polygon.vertices[edge.first] + candidate.convex_polygon.vertices[edge.second]) * 0.5f;
	const std::size_t inserted_index = candidate.convex_polygon.vertices.size();
	candidate.convex_polygon.vertices.push_back(midpoint);
	candidate.polygon_edges[*edge_index] = {
		.first = edge.first,
		.second = inserted_index
	};
	candidate.polygon_edges.insert(
		candidate.polygon_edges.begin() + static_cast<std::ptrdiff_t>(*edge_index + 1u),
		collision::editor::polygon_edge{.first = inserted_index, .second = edge.second});
	if(!candidate.payload_valid()){
		last_error = "add vertex would make the polygon edge set invalid";
		return;
	}
	part = std::move(candidate);
	std::vector<std::size_t> selected_parts{
		this->edit_selected_part_indices().begin(),
		this->edit_selected_part_indices().end()
	};
	editor_detail::add_selection_index(selected_parts, *selected_part);
	this->set_edit_vertex_selection(selected_part, inserted_index, std::move(selected_parts), {inserted_index});
	last_error.clear();
	this->push_history();
}

void editor_state::insert_vertex_at_cursor(const math::vec2 cursor){
	if(this->operation_active()){
		this->cancel_operation();
	}
	const auto selected_part = this->edit_selected_part_index();
	if(mode != editor_detail::editor_mode::edit
		|| !selected_part
		|| *selected_part >= document.parts.size()){
		last_error = "point insert requires a selected polygon";
		return;
	}

	auto& part = document.parts[*selected_part];
	if(part.type != physics::shape_type::convex_polygon){
		last_error = "point insert requires a polygon";
		return;
	}

	math::vec2 world_vertex = collision::editor::clamp_point_to_mirror_source_axes(cursor, part.mirror);
	const math::vec2 local_vertex = part.local_transform.apply_inv_to(world_vertex);

	auto candidate = part;
	collision::editor::materialize_part_polygon_edges(candidate);
	candidate.convex_polygon.vertices.push_back(local_vertex);
	collision::editor::snap_part_vertices_to_mirror_axes(candidate);
	if(candidate.uses_explicit_polygon_edges()){
		candidate.closed = candidate.ordered_polygon_vertices_for_export().has_value();
	}
	if(!candidate.payload_valid()){
		last_error = "point insert would make the polygon graph invalid";
		return;
	}
	part = std::move(candidate);
	const auto inserted = std::ranges::find_if(part.convex_polygon.vertices, [local_vertex](const math::vec2 vertex) noexcept{
		return (vertex - local_vertex).length2() <= editor_detail::hit_epsilon * editor_detail::hit_epsilon;
	});
	const std::size_t selected_index = inserted == part.convex_polygon.vertices.end()
		? (part.convex_polygon.vertices.empty() ? 0u : part.convex_polygon.vertices.size() - 1u)
		: static_cast<std::size_t>(std::distance(part.convex_polygon.vertices.begin(), inserted));

	std::vector<std::size_t> selected_parts{
		this->edit_selected_part_indices().begin(),
		this->edit_selected_part_indices().end()
	};
	editor_detail::add_selection_index(selected_parts, *selected_part);
	this->set_edit_vertex_selection(
		selected_part,
		selected_index,
		std::move(selected_parts),
		part.convex_polygon.vertices.empty()
		? std::vector<std::size_t>{}
		: std::vector<std::size_t>{selected_index});
	last_error.clear();
	this->push_history();
}

void editor_state::connect_selected_vertices_as_edge(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	const auto selected_part = this->edit_selected_part_index();
	std::vector<std::size_t> selected_vertices{
		this->edit_selected_vertex_indices().begin(),
		this->edit_selected_vertex_indices().end()
	};
	if(mode != editor_detail::editor_mode::edit
		|| !selected_part
		|| *selected_part >= document.parts.size()){
		last_error = "edge creation requires selected polygon vertices";
		return;
	}
	if(selected_vertices.size() != 2u){
		last_error = "edge creation requires exactly two selected vertices";
		return;
	}

	auto& part = document.parts[*selected_part];
	if(part.type != physics::shape_type::convex_polygon){
		last_error = "edge creation requires a polygon";
		return;
	}
	const std::size_t first = selected_vertices[0u];
	const std::size_t second = selected_vertices[1u];
	if(first >= part.convex_polygon.vertices.size()
		|| second >= part.convex_polygon.vertices.size()
		|| first == second){
		last_error = "edge creation requires two valid polygon vertices";
		return;
	}
	auto candidate = part;
	collision::editor::materialize_part_polygon_edges(candidate);
	candidate.polygon_edges.push_back({.first = first, .second = second});
	if(candidate.uses_explicit_polygon_edges()){
		candidate.closed = candidate.ordered_polygon_vertices_for_export().has_value();
	}
	if(!candidate.payload_valid()){
		last_error = "edge creation would make the polygon edge set invalid";
		return;
	}
	part = std::move(candidate);

	if(this->edit_selection_domain() == editor_detail::polygon_selection_domain::edge){
		const std::size_t new_edge = part.polygon_edges.size() - 1u;
		this->set_edit_edge_selection(selected_part, new_edge, {*selected_part}, {new_edge});
	}else{
		const std::size_t selected_vertex = selected_vertices.back();
		this->set_edit_vertex_selection(selected_part, selected_vertex, {*selected_part}, std::move(selected_vertices));
	}
	last_error.clear();
	this->push_history();
}

#ifdef MO_YANXI_GAME_ENABLE_EDITOR_TESTS
namespace test{
std::size_t make_object_selected_concave_polygon(editor_state& state){
	state.document = {};
	state.set_mode(editor_detail::editor_mode::object);

	const std::size_t polygon = state.document.add_shape(physics::shape_type::convex_polygon);
	auto& part = state.document.parts[polygon];
	part.convex_polygon.vertices = {
		{-2.f, -1.f},
		{2.f, -1.f},
		{0.f, 0.f},
		{2.f, 1.f},
		{-2.f, 1.f}
	};
	state.set_object_part_selection(polygon, {polygon});
	state.last_error.clear();
	return polygon;
}

std::size_t make_edit_selected_square(
	editor_state& state,
	const bool explicit_polygon_edges,
	const bool closed){
	state.document = {};
	state.set_mode(editor_detail::editor_mode::edit);

	const std::size_t polygon = state.document.add_shape(physics::shape_type::convex_polygon);
	auto& part = state.document.parts[polygon];
	part.convex_polygon.vertices = {
		{-2.f, -1.f},
		{2.f, -1.f},
		{2.f, 1.f},
		{-2.f, 1.f}
	};
	if(explicit_polygon_edges){
		part.polygon_edges = closed
			? std::vector<collision::editor::polygon_edge>{
				{.first = 0u, .second = 1u},
				{.first = 1u, .second = 2u},
				{.first = 2u, .second = 3u},
				{.first = 3u, .second = 0u}
			}
			: std::vector<collision::editor::polygon_edge>{
				{.first = 0u, .second = 1u},
				{.first = 1u, .second = 2u},
				{.first = 2u, .second = 3u}
			};
		part.polygon_edges_explicit = true;
	}else{
		part.polygon_edges.clear();
		part.polygon_edges_explicit = false;
	}
	part.closed = closed;

	state.set_edit_vertex_selection(polygon, std::nullopt, {polygon}, {});
	state.last_error.clear();
	return polygon;
}

collision_shape_editor_polygon_operation_result polygon_operation_result_from_state(editor_state& state){
	std::optional<std::size_t> selected_part{};
	std::vector<std::size_t> selected_parts{};
	if(state.mode == editor_detail::editor_mode::object){
		selected_part = state.object_selected_part_index();
		selected_parts.assign(
			state.object_selected_part_indices().begin(),
			state.object_selected_part_indices().end());
	}else if(state.mode == editor_detail::editor_mode::edit){
		selected_part = state.edit_selected_part_index();
		selected_parts.assign(
			state.edit_selected_part_indices().begin(),
			state.edit_selected_part_indices().end());
	}

	return collision_shape_editor_polygon_operation_result{
		.document = std::move(state.document),
		.selected_part = selected_part,
		.selected_parts = std::move(selected_parts),
		.last_error = std::move(state.last_error)
	};
}

collision_shape_editor_edit_operation_result edit_operation_result_from_state(
	editor_state& state,
	const bool probe_runtime_export){
	std::vector<std::size_t> selected_parts{
		state.edit_selected_part_indices().begin(),
		state.edit_selected_part_indices().end()
	};
	std::vector<std::size_t> selected_vertices{
		state.edit_selected_vertex_indices().begin(),
		state.edit_selected_vertex_indices().end()
	};
	std::vector<std::size_t> selected_edges{
		state.edit_selected_edge_indices().begin(),
		state.edit_selected_edge_indices().end()
	};
	std::string runtime_export_error{};
	if(probe_runtime_export){
		try{
			static_cast<void>(state.document.to_runtime_shape());
		}catch(const std::exception& exception){
			runtime_export_error = exception.what();
		}
	}
	const bool has_open_polygon = state.document.has_open_polygon();
	const bool has_non_convex_polygon = state.document.has_non_convex_polygon();
	const bool has_self_intersecting_polygon = state.document.has_self_intersecting_polygon();

	return collision_shape_editor_edit_operation_result{
		.document = std::move(state.document),
		.selected_part = state.edit_selected_part_index(),
		.selected_parts = std::move(selected_parts),
		.selected_vertex = state.edit_selected_vertex_index(),
		.selected_vertices = std::move(selected_vertices),
		.selected_edge = state.edit_selected_edge_index(),
		.selected_edges = std::move(selected_edges),
		.has_open_polygon = has_open_polygon,
		.has_non_convex_polygon = has_non_convex_polygon,
		.has_self_intersecting_polygon = has_self_intersecting_polygon,
		.runtime_export_error = std::move(runtime_export_error),
		.last_error = std::move(state.last_error)
	};
}

collision_shape_editor_insert_point_result insert_point_result_from_state(
	editor_state& state,
	const bool probe_runtime_export){
	std::vector<std::size_t> selected_vertices{
		state.edit_selected_vertex_indices().begin(),
		state.edit_selected_vertex_indices().end()
	};
	std::string runtime_export_error{};
	if(probe_runtime_export){
		try{
			static_cast<void>(state.document.to_runtime_shape());
		}catch(const std::exception& exception){
			runtime_export_error = exception.what();
		}
	}
	const std::string selected_text = state.selected_text();
	const bool has_open_polygon = state.document.has_open_polygon();
	const bool has_non_convex_polygon = state.document.has_non_convex_polygon();
	const bool has_self_intersecting_polygon = state.document.has_self_intersecting_polygon();

	return collision_shape_editor_insert_point_result{
		.document = std::move(state.document),
		.selected_vertex = state.edit_selected_vertex_index(),
		.selected_vertices = std::move(selected_vertices),
		.selected_text = selected_text,
		.has_open_polygon = has_open_polygon,
		.has_non_convex_polygon = has_non_convex_polygon,
		.has_self_intersecting_polygon = has_self_intersecting_polygon,
		.runtime_export_error = std::move(runtime_export_error),
		.last_error = std::move(state.last_error)
	};
}

collision_shape_editor_insert_point_result insert_closed_polygon_point_for_test(){
	editor_state state{};
	state.document = {};
	state.set_mode(editor_detail::editor_mode::edit);

	const std::size_t polygon = state.document.add_shape(physics::shape_type::convex_polygon);
	auto& part = state.document.parts[polygon];
	part.convex_polygon.vertices = {
		{-2.f, -1.f},
		{2.f, -1.f},
		{2.f, 1.f},
		{-2.f, 1.f}
	};
	part.polygon_edges.clear();
	part.polygon_edges_explicit = false;
	part.closed = true;

	state.set_edit_vertex_selection(polygon, std::nullopt, {polygon}, {});
	state.insert_vertex_at_cursor({0.f, -2.f});
	return insert_point_result_from_state(state, false);
}

collision_shape_editor_insert_point_result add_midpoint_to_selected_polygon_edge_for_test(){
	editor_state state{};
	state.document = {};
	state.set_mode(editor_detail::editor_mode::edit);

	const std::size_t polygon = state.document.add_shape(physics::shape_type::convex_polygon);
	auto& part = state.document.parts[polygon];
	part.convex_polygon.vertices = {
		{-2.f, -1.f},
		{2.f, -1.f},
		{2.f, 1.f},
		{-2.f, 1.f}
	};
	part.polygon_edges.clear();
	part.polygon_edges_explicit = false;
	part.closed = true;

	state.set_edit_edge_selection(polygon, 0u, {polygon}, {0u});
	state.add_vertex_between_selected();
	return insert_point_result_from_state(state, true);
}

collision_shape_editor_edge_connection_result connect_inserted_polygon_vertex_for_test(){
	editor_state state{};
	state.document = {};
	state.set_mode(editor_detail::editor_mode::edit);

	const std::size_t polygon = state.document.add_shape(physics::shape_type::convex_polygon);
	auto& part = state.document.parts[polygon];
	part.convex_polygon.vertices = {
		{-2.f, -1.f},
		{2.f, -1.f},
		{2.f, 1.f},
		{-2.f, 1.f}
	};
	part.polygon_edges.clear();
	part.polygon_edges_explicit = true;
	part.closed = false;

	state.set_edit_vertex_selection(polygon, std::nullopt, {polygon}, {});
	state.insert_vertex_at_cursor({0.f, -2.f});
	const std::optional<std::size_t> inserted_vertex = state.edit_selected_vertex_index();
	if(inserted_vertex){
		state.set_edit_vertex_selection(polygon, inserted_vertex, {polygon}, {0u, *inserted_vertex});
	}
	state.connect_selected_vertices_as_edge();

	const auto selected_vertices = state.edit_selected_vertex_indices();
	return collision_shape_editor_edge_connection_result{
		.document = std::move(state.document),
		.selected_vertex = state.edit_selected_vertex_index(),
		.selected_vertices = {selected_vertices.begin(), selected_vertices.end()},
		.last_error = std::move(state.last_error)
	};
}

collision_shape_editor_edit_operation_result connect_open_polygon_closing_edge_for_test(){
	editor_state state{};
	const std::size_t polygon = make_edit_selected_square(state, true, false);

	state.set_edit_vertex_selection(polygon, std::nullopt, {polygon}, {3u, 0u});
	state.connect_selected_vertices_as_edge();
	return edit_operation_result_from_state(state, true);
}

collision_shape_editor_edit_operation_result erase_selected_polygon_vertex_for_test(){
	editor_state state{};
	const std::size_t polygon = make_edit_selected_square(state, true, true);

	state.set_edit_vertex_selection(polygon, 1u, {polygon}, {1u});
	state.erase_selected();
	return edit_operation_result_from_state(state, true);
}

collision_shape_editor_edit_operation_result erase_selected_polygon_edge_for_test(){
	editor_state state{};
	const std::size_t polygon = make_edit_selected_square(state, false, true);

	state.set_edit_edge_selection(polygon, 1u, {polygon}, {1u});
	state.erase_selected();
	return edit_operation_result_from_state(state, true);
}

collision_shape_editor_edit_operation_result merge_selected_polygon_vertices_for_test(){
	editor_state state{};
	const std::size_t polygon = make_edit_selected_square(state, true, true);

	state.set_edit_vertex_selection(polygon, 0u, {polygon}, {0u, 1u});
	state.merge_selected_vertices(editor_detail::vertex_merge_mode::first);
	return edit_operation_result_from_state(state, true);
}

collision_shape_editor_polygon_operation_result hull_object_selected_polygon_for_test(){
	editor_state state{};
	make_object_selected_concave_polygon(state);
	state.make_selected_polygon_convex_hull();
	return polygon_operation_result_from_state(state);
}

collision_shape_editor_polygon_operation_result split_object_selected_polygon_for_test(){
	editor_state state{};
	make_object_selected_concave_polygon(state);
	state.split_selected_polygon_to_convex_parts();
	return polygon_operation_result_from_state(state);
}

collision_shape_editor_polygon_operation_result cut_selected_polygon_between_vertices_for_test(){
	editor_state state{};
	const std::size_t polygon = make_edit_selected_square(state, false, true);
	state.set_edit_vertex_selection(polygon, std::nullopt, {polygon}, {0u, 2u});
	state.cut_selected_polygon();
	return polygon_operation_result_from_state(state);
}
}
#endif

void editor_state::merge_selected_vertices(const editor_detail::vertex_merge_mode merge_mode){
	if(this->operation_active()){
		this->cancel_operation();
	}
	const auto selected_part = this->edit_selected_part_index();
	if(mode != editor_detail::editor_mode::edit
		|| !selected_part
		|| *selected_part >= document.parts.size()){
		last_error = "merge requires selected polygon vertices";
		return;
	}

	auto& part = document.parts[*selected_part];
	if(part.type != physics::shape_type::convex_polygon){
		last_error = "merge requires selected polygon vertices";
		return;
	}

	auto candidate = part;
	std::vector<std::size_t> selected{
		this->edit_selected_vertex_indices().begin(),
		this->edit_selected_vertex_indices().end()
	};
	if(selected.size() < 2u){
		std::vector<std::size_t> selected_edges{
			this->edit_selected_edge_indices().begin(),
			this->edit_selected_edge_indices().end()
		};
		if(!selected_edges.empty()){
			selected = editor_detail::polygon_vertices_from_edges(part, selected_edges);
		}
	}
	selected.erase(std::ranges::remove_if(selected, [&candidate](const std::size_t index) noexcept{
		return index >= candidate.convex_polygon.vertices.size();
	}).begin(), selected.end());
	if(selected.size() < 2u){
		last_error = "merge requires at least two valid selected vertices";
		return;
	}

	const std::size_t keep_source = merge_mode == editor_detail::vertex_merge_mode::last
		? selected.back()
		: selected.front();
	math::vec2 merged{};
	switch(merge_mode){
	case editor_detail::vertex_merge_mode::center:
		for(const std::size_t index : selected){
			merged += candidate.convex_polygon.vertices[index];
		}
		merged /= static_cast<float>(selected.size());
		break;
	case editor_detail::vertex_merge_mode::first:
		merged = candidate.convex_polygon.vertices[selected.front()];
		break;
	case editor_detail::vertex_merge_mode::last:
		merged = candidate.convex_polygon.vertices[selected.back()];
		break;
	default:
		std::unreachable();
	}

	std::vector<std::size_t> erase_indices = selected;
	std::ranges::sort(erase_indices);
	erase_indices.erase(std::ranges::unique(erase_indices).begin(), erase_indices.end());
	const bool uses_explicit_edges = candidate.uses_explicit_polygon_edges();

	std::size_t keep_index = keep_source;
	if(uses_explicit_edges){
		collision::editor::materialize_part_polygon_edges(candidate);
		std::vector<bool> selected_vertices(candidate.convex_polygon.vertices.size());
		for(const std::size_t index : erase_indices){
			selected_vertices[index] = true;
		}

		std::vector<std::size_t> vertex_remap(
			candidate.convex_polygon.vertices.size(),
			std::numeric_limits<std::size_t>::max());
		std::vector<math::vec2> merged_vertices{};
		merged_vertices.reserve(candidate.convex_polygon.vertices.size());
		for(std::size_t vertex_index = 0u; vertex_index != candidate.convex_polygon.vertices.size(); ++vertex_index){
			if(vertex_index == keep_source){
				keep_index = merged_vertices.size();
				vertex_remap[vertex_index] = keep_index;
				merged_vertices.push_back(merged);
				continue;
			}
			if(selected_vertices[vertex_index]){
				continue;
			}
			vertex_remap[vertex_index] = merged_vertices.size();
			merged_vertices.push_back(candidate.convex_polygon.vertices[vertex_index]);
		}
		for(const std::size_t index : erase_indices){
			vertex_remap[index] = keep_index;
		}

		std::vector<collision::editor::polygon_edge> merged_edges{};
		merged_edges.reserve(candidate.polygon_edges.size());
		for(const collision::editor::polygon_edge edge : candidate.polygon_edges){
			if(edge.first >= vertex_remap.size() || edge.second >= vertex_remap.size()){
				continue;
			}
			collision::editor::polygon_edge remapped{
				.first = vertex_remap[edge.first],
				.second = vertex_remap[edge.second]
			};
			if(remapped.first == remapped.second){
				continue;
			}
			const bool duplicate = std::ranges::any_of(
				merged_edges,
				[remapped](const collision::editor::polygon_edge existing) noexcept{
					return (existing.first == remapped.first && existing.second == remapped.second)
						|| (existing.first == remapped.second && existing.second == remapped.first);
				});
			if(duplicate){
				continue;
			}
			merged_edges.push_back(remapped);
		}

		candidate.convex_polygon.vertices = std::move(merged_vertices);
		candidate.polygon_edges = std::move(merged_edges);
	}else{
		candidate.convex_polygon.vertices[keep_index] = merged;
		for(auto cursor = erase_indices.rbegin(); cursor != erase_indices.rend(); ++cursor){
			if(*cursor == keep_source){
				continue;
			}
			candidate.convex_polygon.vertices.erase(
				candidate.convex_polygon.vertices.begin() + static_cast<std::ptrdiff_t>(*cursor));
			if(*cursor < keep_index){
				--keep_index;
			}
		}
	}
	collision::editor::snap_part_vertices_to_mirror_axes(candidate);
	if(candidate.uses_explicit_polygon_edges()){
		candidate.closed = candidate.ordered_polygon_vertices_for_export().has_value();
	}
	if(!candidate.payload_valid()){
		last_error = "merge would make the polygon invalid";
		return;
	}

	part = std::move(candidate);
	if(keep_index >= part.convex_polygon.vertices.size()){
		keep_index = part.convex_polygon.vertices.empty()
			? 0u
			: part.convex_polygon.vertices.size() - 1u;
	}
	std::vector<std::size_t> selected_parts{
		this->edit_selected_part_indices().begin(),
		this->edit_selected_part_indices().end()
	};
	editor_detail::add_selection_index(selected_parts, *selected_part);
	this->set_edit_vertex_selection(selected_part, keep_index, std::move(selected_parts), {keep_index});
	last_error.clear();
	this->push_history();
}

void editor_state::make_selected_polygon_convex_hull(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	const auto selected_part = this->selected_polygon_part_index_for_operation("convex hull");
	if(!selected_part){
		return;
	}
	auto& part = document.parts[*selected_part];
	try{
		part.convex_polygon.vertices = collision::editor::polygon_convex_hull(part.convex_polygon.vertices);
		part.polygon_edges.clear();
		part.polygon_edges_explicit = false;
		part.closed = true;
		collision::editor::snap_part_vertices_to_mirror_axes(part);
		this->set_part_selection_for_current_mode(selected_part, {*selected_part});
		last_error.clear();
		this->push_history();
	}catch(const std::exception& e){
		last_error = std::format("failed to generate convex hull: {}", e.what());
	}
}

void editor_state::split_selected_polygon_to_convex_parts(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	const auto selected_part = this->selected_polygon_part_index_for_operation("split");
	if(!selected_part){
		return;
	}

	const std::size_t source_index = *selected_part;
	const auto source = document.parts[source_index];
	const auto analysis = collision::editor::analyze_part_polygon_graph(source);
	if(!analysis.exportable()){
		last_error = analysis.error;
		return;
	}

	try{
		auto split_source = source;
		split_source.convex_polygon.vertices = analysis.ordered_vertices;
		split_source.polygon_edges.clear();
		split_source.polygon_edges_explicit = false;
		split_source.closed = true;
		if(source.mirror.active()){
			split_source.mirror = {};
		}
		const auto polygons = physics::decompose_polygon(split_source.convex_polygon.vertices);
		if(polygons.empty()){
			last_error = "split produced no convex parts";
			return;
		}

		document.parts.erase(document.parts.begin() + static_cast<std::ptrdiff_t>(source_index));
		std::vector<std::size_t> split_part_indices{};
		split_part_indices.reserve(polygons.size());
		for(std::size_t index = 0u; index != polygons.size(); ++index){
			auto part = split_source;
			part.convex_polygon = polygons[index];
			part.polygon_edges.clear();
			part.polygon_edges_explicit = false;
			part.closed = true;
			document.parts.insert(
				document.parts.begin() + static_cast<std::ptrdiff_t>(source_index + index),
				std::move(part));
			split_part_indices.push_back(source_index + index);
		}
		this->set_part_selection_for_current_mode(source_index, std::move(split_part_indices));
		last_error.clear();
		this->push_history();
	}catch(const std::exception& e){
		last_error = std::format("failed to split polygon: {}", e.what());
	}
}

void editor_state::cut_selected_polygon(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	const auto selected_part = this->edit_selected_part_index();
	std::vector<std::size_t> selected_vertices{
		this->edit_selected_vertex_indices().begin(),
		this->edit_selected_vertex_indices().end()
	};
	if(mode != editor_detail::editor_mode::edit
		|| !selected_part
		|| *selected_part >= document.parts.size()){
		last_error = "cut requires a selected polygon";
		return;
	}
	if(selected_vertices.size() != 2u){
		last_error = "cut requires exactly two selected vertices";
		return;
	}

	auto result = collision::editor::cut_polygon(
		document,
		*selected_part,
		selected_vertices[0u],
		selected_vertices[1u]);
	if(!result){
		last_error = result.error();
		return;
	}

	this->set_edit_vertex_selection(
		result->first_part,
		std::nullopt,
		{result->first_part, result->second_part},
		{});
	last_error.clear();
	this->push_history();
}

void editor_state::start_knife_cut(const math::vec2 cursor){
	if(this->operation_active()){
		this->cancel_operation();
	}
	const auto selected_part = this->edit_selected_part_index();
	if(mode != editor_detail::editor_mode::edit
		|| !selected_part
		|| *selected_part >= document.parts.size()){
		last_error = "knife cut requires a selected polygon";
		return;
	}
	const auto& part = document.parts[*selected_part];
	if(part.type != physics::shape_type::convex_polygon){
		last_error = "knife cut requires a polygon";
		return;
	}
	if(const auto analysis = collision::editor::analyze_part_polygon_graph(part); !analysis.exportable()){
		last_error = analysis.error;
		return;
	}

	interaction = knife_cut_state{
		.part_index = *selected_part,
		.points = {cursor}
	};
	last_error = "knife cut: click points, Enter applies, Esc cancels";
}

void editor_state::add_knife_cut_point(const math::vec2 cursor){
	auto* knife = this->knife_cut();
	if(knife == nullptr){
		return;
	}
	if(!knife->points.empty() && knife->points.back().dst2(cursor) <= editor_detail::hit_epsilon){
		return;
	}
	knife->points.push_back(cursor);
	last_error = "knife cut: click points, Enter applies, Esc cancels";
}

void editor_state::erase_knife_cut_point(){
	auto* knife = this->knife_cut();
	if(knife == nullptr){
		return;
	}
	if(knife->points.size() > 1u){
		knife->points.pop_back();
		last_error = "knife cut: point removed";
		return;
	}
	this->cancel_knife_cut();
}

void editor_state::commit_knife_cut(){
	const auto* knife = this->knife_cut();
	if(knife == nullptr){
		return;
	}
	if(knife->part_index >= document.parts.size()){
		this->cancel_knife_cut();
		last_error = "knife cut target no longer exists";
		return;
	}

	auto result = collision::editor::knife_cut_polygon(
		document,
		knife->part_index,
		knife->points);
	this->clear_interaction();
	if(!result){
		last_error = result.error();
		return;
	}

	const std::optional<std::size_t> selected_part = result->part_indices.empty()
		? std::optional<std::size_t>{}
		: std::optional<std::size_t>{result->part_indices.front()};
	this->set_edit_part_selection_for_current_domain(selected_part, result->part_indices);
	last_error.clear();
	this->push_history();
}

void editor_state::cancel_knife_cut() noexcept{
	this->clear_interaction();
	last_error.clear();
}

void editor_state::merge_selected_polygons_as_hull(){
	if(this->operation_active()){
		this->cancel_operation();
	}

	std::vector<std::size_t> selected_parts{};
	if(mode == editor_detail::editor_mode::edit){
		selected_parts.assign(this->edit_selected_part_indices().begin(), this->edit_selected_part_indices().end());
	}else if(mode == editor_detail::editor_mode::object){
		selected_parts.assign(this->object_selected_part_indices().begin(), this->object_selected_part_indices().end());
	}
	if(selected_parts.empty()){
		if(const auto selected_part = this->current_selected_part_index()){
			selected_parts.push_back(*selected_part);
		}
	}

	auto result = collision::editor::merge_polygons_as_hull(
		document,
		selected_parts,
		this->current_selected_part_index());
	if(!result){
		last_error = result.error();
		return;
	}

	if(mode == editor_detail::editor_mode::edit){
		this->set_edit_part_selection_for_current_domain(*result, {*result});
	}else if(mode == editor_detail::editor_mode::object){
		this->set_object_part_selection(*result, {*result});
	}
	last_error.clear();
	this->push_history();
}

void editor_state::join_selected_polygons(
	const collision::editor::join_origin_mode origin_mode){
	if(this->operation_active()){
		this->cancel_operation();
	}

	std::vector<std::size_t> selected_parts{};
	if(mode == editor_detail::editor_mode::edit){
		selected_parts.assign(this->edit_selected_part_indices().begin(), this->edit_selected_part_indices().end());
	}else if(mode == editor_detail::editor_mode::object){
		selected_parts.assign(this->object_selected_part_indices().begin(), this->object_selected_part_indices().end());
	}
	if(selected_parts.empty()){
		if(const auto selected_part = this->current_selected_part_index()){
			selected_parts.push_back(*selected_part);
		}
	}

	auto result = collision::editor::join_polygon_parts(
		document,
		selected_parts,
		origin_mode,
		this->current_selected_part_index());
	if(!result){
		last_error = result.error();
		return;
	}

	if(mode == editor_detail::editor_mode::edit){
		this->set_edit_part_selection_for_current_domain(*result, {*result});
	}else if(mode == editor_detail::editor_mode::object){
		this->set_object_part_selection(*result, {*result});
	}
	last_error.clear();
	this->push_history();
}

void editor_state::generate_polygon_from_reference_alpha(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	if(alpha_hull_future.valid()){
		last_error = "alpha hull generation is already running";
		return;
	}
	if(!document.reference_image.visible() || document.reference_image.path.empty()){
		last_error = "load a visible reference image before generating an alpha hull";
		return;
	}

	const std::filesystem::path path{document.reference_image.path};
	alpha_hull_future = std::async(std::launch::async, [path]{
		return generate_alpha_hull(path);
	});
	last_error = "alpha hull generation running";
}

void editor_state::poll_alpha_hull_result(){
	if(!alpha_hull_future.valid()){
		return;
	}
	if(alpha_hull_future.wait_for(std::chrono::seconds{0}) != std::future_status::ready){
		return;
	}

	try{
		auto result = alpha_hull_future.get();
		if(result.vertices.size() < 3u){
			last_error = "alpha hull generation produced fewer than three vertices";
			return;
		}
		auto part = collision::editor::part::make_default(
			physics::shape_type::convex_polygon,
			document.reference_image.transform.vec);
		part.local_transform = document.reference_image.transform;
		part.convex_polygon.vertices = std::move(result.vertices);
		document.parts.push_back(std::move(part));
		mode = editor_detail::editor_mode::edit;
		const std::size_t selected_part = document.parts.size() - 1u;
		this->set_edit_vertex_selection(selected_part, std::nullopt, {selected_part}, {});
		last_error = std::format(
			"alpha hull generated{}{}",
			result.mirror_x_detected ? " / mirror X detected" : "",
			result.mirror_y_detected ? " / mirror Y detected" : "");
		this->push_history();
	}catch(const std::exception& e){
		last_error = std::format("alpha hull generation failed: {}", e.what());
	}
}

void editor_state::duplicate_selected(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	std::vector<std::size_t> selected_parts{};
	if(mode == editor_detail::editor_mode::edit){
		selected_parts.assign(this->edit_selected_part_indices().begin(), this->edit_selected_part_indices().end());
	}else if(mode == editor_detail::editor_mode::object){
		selected_parts.assign(this->object_selected_part_indices().begin(), this->object_selected_part_indices().end());
	}
	if(selected_parts.empty()){
		if(const auto selected_part = this->current_selected_part_index()){
			selected_parts.push_back(*selected_part);
		}
	}
	selected_parts.erase(std::ranges::remove_if(selected_parts, [this](const std::size_t index) noexcept{
		return index >= document.parts.size();
	}).begin(), selected_parts.end());
	editor_detail::normalize_selection_indices(selected_parts);
	if(selected_parts.empty()){
		return;
	}

	std::vector<std::size_t> duplicated_indices{};
	duplicated_indices.reserve(selected_parts.size());
	for(const std::size_t index : selected_parts){
		auto copy = document.parts[index];
		copy.local_transform.vec += math::vec2{24.f, 0.f};
		document.parts.push_back(std::move(copy));
		duplicated_indices.push_back(document.parts.size() - 1u);
	}
	if(mode == editor_detail::editor_mode::edit){
		this->set_edit_part_selection_for_current_domain(duplicated_indices.back(), duplicated_indices);
	}else if(mode == editor_detail::editor_mode::object){
		this->set_object_part_selection(duplicated_indices.back(), duplicated_indices);
	}
	last_error.clear();
	this->push_history();
}

void editor_state::toggle_mirror_x(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	auto* part = this->selected();
	if(part == nullptr){
		return;
	}
	part->mirror.mirror_x = !part->mirror.mirror_x;
	collision::editor::snap_part_vertices_to_mirror_axes(*part);
	last_error.clear();
	this->push_history();
}

void editor_state::toggle_mirror_y(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	auto* part = this->selected();
	if(part == nullptr){
		return;
	}
	part->mirror.mirror_y = !part->mirror.mirror_y;
	collision::editor::snap_part_vertices_to_mirror_axes(*part);
	last_error.clear();
	this->push_history();
}

bool editor_state::reset_position_for_current_mode(){
	if(this->operation_active()){
		this->cancel_operation();
	}

	bool changed{};
	switch(mode.kind()){
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

bool editor_state::reset_rotation_for_current_mode(){
	if(this->operation_active()){
		this->cancel_operation();
	}

	bool changed{};
	switch(mode.kind()){
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

bool editor_state::start_operation(
	const editor_detail::operation_kind kind,
	const math::vec2 cursor){
	if(this->operation_active() || kind == editor_detail::operation_kind::none){
		return false;
	}

	switch(mode.kind()){
	case editor_detail::editor_mode::object:{
		const auto selected_part = this->object_selected_part_index();
		if(!selected_part || *selected_part >= document.parts.size()){
			return false;
		}
		const auto& part = document.parts[*selected_part];
		if(!part.payload_valid()){
			last_error = "selected shape payload is invalid";
			return false;
		}
		if(kind == editor_detail::operation_kind::resize){
			return false;
		}
		std::vector<std::size_t> selected_parts{
			this->object_selected_part_indices().begin(),
			this->object_selected_part_indices().end()
		};
		editor_detail::add_selection_index(selected_parts, *selected_part);
		selected_parts.erase(std::ranges::remove_if(selected_parts, [this](const std::size_t index) noexcept{
			return index >= document.parts.size();
		}).begin(), selected_parts.end());
		editor_detail::normalize_selection_indices(selected_parts);

		std::vector<operation_part_snapshot> source_parts{};
		source_parts.reserve(selected_parts.size());
		for(const std::size_t selected_index : selected_parts){
			const auto& selected = document.parts[selected_index];
			if(!selected.payload_valid()){
				last_error = "selected shape payload is invalid";
				return false;
			}
			source_parts.push_back(operation_part_snapshot{
				.part_index = selected_index,
				.part = selected
			});
		}
		math::vec2 operation_pivot = part.local_transform.vec;
		if(kind == editor_detail::operation_kind::rotate){
			const auto pivot = this->rotation_reference_pivot();
			if(!pivot){
				last_error = "rotate requires a valid pivot";
				return false;
			}
			operation_pivot = *pivot;
		}
		interaction = object_part_interaction{
			.operation = {
				.kind = kind,
				.initial_cursor = cursor,
				.pivot = operation_pivot,
				.part_index = *selected_part,
				.source_part = part,
				.source_parts = std::move(source_parts)
			}
		};
		break;
	}
	case editor_detail::editor_mode::edit:{
		const auto selected_part = this->edit_selected_part_index();
		if(!selected_part || *selected_part >= document.parts.size()){
			return false;
		}
		const auto& part = document.parts[*selected_part];
		if(!part.payload_valid()){
			last_error = "selected shape payload is invalid";
			return false;
		}
		std::vector<std::size_t> selected_vertices{
			this->edit_selected_vertex_indices().begin(),
			this->edit_selected_vertex_indices().end()
		};
		if(selected_vertices.empty()){
			if(const auto selected_vertex = this->edit_selected_vertex_index()){
				selected_vertices.push_back(*selected_vertex);
			}
		}
		if(selected_vertices.empty()){
			std::vector<std::size_t> selected_edges{
				this->edit_selected_edge_indices().begin(),
				this->edit_selected_edge_indices().end()
			};
			if(selected_edges.empty()){
				if(const auto selected_edge = this->edit_selected_edge_index()){
					selected_edges.push_back(*selected_edge);
				}
			}
			selected_vertices = editor_detail::polygon_vertices_from_edges(part, selected_edges);
		}
		if(kind == editor_detail::operation_kind::move && selected_vertices.empty()){
			last_error = "edit mode move requires a polygon vertex or edge";
			return false;
		}
		if(kind == editor_detail::operation_kind::rotate){
			if(selected_vertices.empty()){
				last_error = "edit mode rotate requires a polygon vertex or edge";
				return false;
			}
		}
		math::vec2 operation_pivot = part.local_transform.vec;
		if(kind == editor_detail::operation_kind::rotate){
			const auto pivot = this->rotation_reference_pivot();
			if(!pivot){
				last_error = "rotate requires a valid pivot";
				return false;
			}
			operation_pivot = *pivot;
		}
		interaction = edit_part_interaction{
			.operation = {
				.kind = kind,
				.initial_cursor = cursor,
				.pivot = operation_pivot,
				.part_index = *selected_part,
				.vertex_index = this->edit_selected_vertex_index(),
				.vertex_indices = std::move(selected_vertices),
				.source_part = part
			}
		};
		break;
	}
	case editor_detail::editor_mode::reference_image:{
		if(!document.reference_image.visible() || !this->reference_image_loaded()){
			last_error = "reference image is not loaded";
			return false;
		}
		math::vec2 operation_pivot = document.reference_image.transform.vec;
		if(kind == editor_detail::operation_kind::rotate){
			const auto pivot = this->rotation_reference_pivot();
			if(!pivot){
				last_error = "rotate requires a valid pivot";
				return false;
			}
			operation_pivot = *pivot;
		}
		interaction = ref_transform_interaction{
			.operation = {
				.kind = kind,
				.initial_cursor = cursor,
				.pivot = operation_pivot,
				.source_transform = document.reference_image.transform,
				.source_half_extent = document.reference_image.half_extent
			}
		};
		break;
	}
	case editor_detail::editor_mode::origin:{
		if(kind == editor_detail::operation_kind::resize){
			return false;
		}
		math::vec2 operation_pivot = document.total_transform.vec;
		if(kind == editor_detail::operation_kind::rotate){
			const auto pivot = this->rotation_reference_pivot();
			if(!pivot){
				last_error = "rotate requires a valid pivot";
				return false;
			}
			operation_pivot = *pivot;
		}
		interaction = origin_transform_interaction{
			.operation = {
				.kind = kind,
				.initial_cursor = cursor,
				.pivot = operation_pivot,
				.source_transform = document.total_transform
			}
		};
		break;
	}
	default:
		std::unreachable();
	}
	last_error.clear();
	return true;
}

bool editor_state::commit_operation(){
	if(auto* operation = this->current_part_operation(); operation != nullptr && operation->active()){
		if(!this->part_operation_sources_available(*operation)){
			this->clear_interaction();
			this->clear_selection_for_current_mode();
			last_error = "operation target no longer exists";
			return false;
		}

		if(operation->invalid){
			this->restore_part_operation_sources(*operation);
			this->clear_interaction();
			return false;
		}

		this->clear_interaction();
		this->push_history();
		return true;
	}
	if(auto* operation = this->current_transform_operation(); operation != nullptr && operation->active()){
		this->clear_interaction();
		this->push_history();
		return true;
	}
	return false;
}

void editor_state::cancel_operation(){
	if(auto* operation = this->current_part_operation(); operation != nullptr && operation->active()){
		this->restore_part_operation_sources(*operation);
		this->clear_interaction();
		last_error.clear();
		return;
	}
	if(auto* reference = std::get_if<ref_transform_interaction>(
		std::addressof(interaction))){
		document.reference_image.transform = reference->operation.source_transform;
		document.reference_image.half_extent = reference->operation.source_half_extent;
		this->clear_interaction();
		last_error.clear();
		return;
	}
	if(auto* origin = std::get_if<origin_transform_interaction>(std::addressof(interaction))){
		document.total_transform = origin->operation.source_transform;
		this->clear_interaction();
		last_error.clear();
		return;
	}
}

bool editor_state::set_operation_precision(
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

bool editor_state::toggle_operation_constraint(const math::bool2 constrain) noexcept{
	const auto cycle_constraint = [constrain](auto& operation) noexcept{
		if(operation.constrain != constrain){
			operation.constrain = constrain;
			operation.constraint_space = editor_detail::operation_constraint_space::world;
			return;
		}
		if(operation.constraint_space == editor_detail::operation_constraint_space::world){
			operation.constraint_space = editor_detail::operation_constraint_space::local;
			return;
		}
		operation.constrain = {true, true};
		operation.constraint_space = editor_detail::operation_constraint_space::world;
	};
	if(auto* operation = this->current_part_operation(); operation != nullptr && operation->active()){
		cycle_constraint(*operation);
		last_error.clear();
		return true;
	}
	if(auto* operation = this->current_transform_operation(); operation != nullptr && operation->active()){
		cycle_constraint(*operation);
		last_error.clear();
		return true;
	}
	return false;
}

bool editor_state::input_operation_character(const char32_t value){
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

bool editor_state::erase_operation_character(){
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

bool editor_state::clear_operation_command() noexcept{
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

void editor_state::set_reference_image(
	std::filesystem::path path,
	gui::constant_image_region_borrow&& region,
	const math::vec2 image_size,
	const math::vec2 position){
	if(this->operation_active()){
		this->cancel_operation();
	}

	const math::vec2 half_extent = image_size * 0.5f;
	const std::string normalized_path = reference_image_region_name(path);
	document.reference_image = {
		.enabled = true,
		.path = normalized_path,
		.transform = {position, 0.f},
		.half_extent = half_extent,
		.opacity = document.reference_image.opacity > 0.f ? document.reference_image.opacity : 0.35f
	};
	reference_image.image_region = std::move(region);
	reference_image.loaded_path = document.reference_image.path;
	mode = editor_detail::editor_mode::reference_image;
	last_error.clear();
	this->push_history();
}

void editor_state::clear_reference_image(){
	if(this->operation_active()){
		this->cancel_operation();
	}
	document.reference_image = {};
	reference_image.image_region = {};
	reference_image.loaded_path.clear();
	last_error.clear();
	this->push_history();
}

bool editor_state::save_document(const std::filesystem::path& path){
	const auto written = write_document(path, document);
	if(!written){
		last_error = std::format("failed to save collision shape document: {}", written.error());
		return false;
	}
	last_error.clear();
	return true;
}

bool editor_state::load_document(const std::filesystem::path& path){
	auto loaded = read_document(path);
	if(!loaded){
		last_error = std::format("failed to load collision shape document: {}", loaded.error());
		return false;
	}

	if(this->operation_active()){
		this->cancel_operation();
	}

	this->apply_history_entry(history_entry{
		.document = std::move(*loaded),
		.mode = editor_detail::editor_mode::object
	});
	history.clear();
	this->push_history();
	return true;
}

void editor_state::apply_history_entry(const history_entry& entry){
	document = entry.document;
	mode = entry.mode;
	selection = entry.selection;
	this->validate_selection();
	if(!document.reference_image.visible()){
		reference_image.image_region = {};
		reference_image.loaded_path.clear();
	}else if(reference_image.loaded_path != document.reference_image.path){
		try{
			reference_image.image_region = borrow_reference_image_region(document.reference_image.path);
			reference_image.loaded_path = document.reference_image.path;
		}catch(const std::exception& e){
			reference_image.image_region = {};
			reference_image.loaded_path.clear();
			last_error = std::format("failed to load reference image: {}", e.what());
			return;
		}
	}
	last_error.clear();
}

std::optional<math::vec2> editor_state::rotation_reference_pivot() const{
	if(rotation_reference == editor_detail::rotation_reference_mode::cursor){
		return rotation_cursor_pivot;
	}

	const auto active_part_pivot = [this]() -> std::optional<math::vec2>{
		const auto selected_part = this->current_selected_part_index();
		if(!selected_part || *selected_part >= document.parts.size()){
			return std::nullopt;
		}
		return document.parts[*selected_part].local_transform.vec;
	};

	switch(mode.kind()){
	case editor_detail::editor_mode::object:{
		if(rotation_reference == editor_detail::rotation_reference_mode::active){
			return active_part_pivot();
		}

		math::vec2 sum{};
		std::size_t count{};
		const auto add_part = [this, &sum, &count](const std::size_t index) noexcept{
			if(index < document.parts.size()){
				sum += document.parts[index].local_transform.vec;
				++count;
			}
		};

		const auto selected_parts = this->object_selected_part_indices();
		for(const std::size_t index : selected_parts){
			add_part(index);
		}
		if(const auto selected_part = this->object_selected_part_index();
			selected_part && !std::ranges::contains(selected_parts, *selected_part)){
			add_part(*selected_part);
		}
		if(count != 0u){
			sum /= static_cast<float>(count);
			return sum;
		}
		return active_part_pivot();
	}
	case editor_detail::editor_mode::edit:{
		const auto selected_part = this->edit_selected_part_index();
		if(!selected_part || *selected_part >= document.parts.size()){
			return std::nullopt;
		}
		const auto& part = document.parts[*selected_part];
		if(rotation_reference == editor_detail::rotation_reference_mode::active){
			return part.local_transform.vec;
		}

		std::vector<std::size_t> selected_vertices{
			this->edit_selected_vertex_indices().begin(),
			this->edit_selected_vertex_indices().end()
		};
		if(selected_vertices.empty()){
			if(const auto selected_vertex = this->edit_selected_vertex_index()){
				selected_vertices.push_back(*selected_vertex);
			}
		}
		if(selected_vertices.empty()){
			std::vector<std::size_t> selected_edges{
				this->edit_selected_edge_indices().begin(),
				this->edit_selected_edge_indices().end()
			};
			if(selected_edges.empty()){
				if(const auto selected_edge = this->edit_selected_edge_index()){
					selected_edges.push_back(*selected_edge);
				}
			}
			selected_vertices = editor_detail::polygon_vertices_from_edges(part, selected_edges);
		}

		math::vec2 sum{};
		std::size_t count{};
		if(part.type == physics::shape_type::convex_polygon){
			for(const std::size_t vertex_index : selected_vertices){
				if(vertex_index < part.convex_polygon.vertices.size()){
					sum += part.convex_polygon.vertices[vertex_index] >> part.local_transform;
					++count;
				}
			}
		}
		if(count != 0u){
			sum /= static_cast<float>(count);
			return sum;
		}
		return part.local_transform.vec;
	}
	case editor_detail::editor_mode::reference_image:
		return document.reference_image.transform.vec;
	case editor_detail::editor_mode::origin:
		return document.total_transform.vec;
	default:
		std::unreachable();
	}
}

void editor_state::restore_part_operation_sources(
	const part_operation& operation) noexcept{
	if(!operation.source_parts.empty()){
		for(const auto& source : operation.source_parts){
			if(source.part_index < document.parts.size()){
				document.parts[source.part_index] = source.part;
			}
		}
		return;
	}
	if(operation.part_index < document.parts.size()){
		document.parts[operation.part_index] = operation.source_part;
	}
}

bool editor_state::part_operation_sources_available(
	const part_operation& operation) const noexcept{
	if(!operation.source_parts.empty()){
		return std::ranges::all_of(operation.source_parts, [this](const auto& source) noexcept{
			return source.part_index < document.parts.size();
		});
	}
	return operation.part_index < document.parts.size();
}

bool editor_state::save_operation_mid_data(const math::vec2 cursor){
	if(!this->preview_operation(cursor)){
		return false;
	}

	if(auto* operation = this->current_part_operation(); operation != nullptr && operation->active()){
		if(!this->part_operation_sources_available(*operation)){
			return false;
		}
		if(!operation->source_parts.empty()){
			for(auto& source : operation->source_parts){
				source.part = document.parts[source.part_index];
			}
		}
		operation->source_part = document.parts[operation->part_index];
		operation->initial_cursor = cursor;
		operation->invalid = false;
	}else if(auto* reference = std::get_if<ref_transform_interaction>(
		std::addressof(interaction))){
		reference->operation.source_transform = document.reference_image.transform;
		reference->operation.source_half_extent = document.reference_image.half_extent;
		reference->operation.initial_cursor = cursor;
	}else if(auto* origin = std::get_if<origin_transform_interaction>(std::addressof(interaction))){
		origin->operation.source_transform = document.total_transform;
		origin->operation.initial_cursor = cursor;
	}else{
		return false;
	}
	last_error.clear();
	return true;
}

void editor_state::validate_selection() noexcept{
	const auto normalize_parts = [this](auto& target) noexcept{
		target.selected_parts.erase(
			std::ranges::remove_if(target.selected_parts, [this](const std::size_t index) noexcept{
				return index >= document.parts.size();
			}).begin(),
			target.selected_parts.end());
		editor_detail::normalize_selection_indices(target.selected_parts);
		if(!target.selected_part || *target.selected_part >= document.parts.size()){
			target.selected_part = target.selected_parts.empty()
				? std::optional<std::size_t>{}
				: std::optional<std::size_t>{target.selected_parts.back()};
		}else{
			editor_detail::add_selection_index(target.selected_parts, *target.selected_part);
		}
	};

	if(auto* object = this->object_selection(); object != nullptr){
		normalize_parts(*object);
		if(!object->selected_part && object->selected_parts.empty()){
			selection = no_selection{};
		}
		return;
	}

	if(auto* vertices = this->edit_vertex_selection(); vertices != nullptr){
		normalize_parts(*vertices);
		if(!vertices->selected_part || *vertices->selected_part >= document.parts.size()){
			vertices->selected_vertex.reset();
			vertices->selected_vertices.clear();
			return;
		}
		const auto& part = document.parts[*vertices->selected_part];
		if(part.type != physics::shape_type::convex_polygon){
			vertices->selected_vertex.reset();
			vertices->selected_vertices.clear();
			return;
		}
		vertices->selected_vertices.erase(
			std::ranges::remove_if(vertices->selected_vertices, [&part](const std::size_t vertex) noexcept{
				return vertex >= part.convex_polygon.vertices.size();
			}).begin(),
			vertices->selected_vertices.end());
		editor_detail::normalize_selection_indices(vertices->selected_vertices);
		if(vertices->selected_vertex && *vertices->selected_vertex < part.convex_polygon.vertices.size()){
			editor_detail::add_selection_index(vertices->selected_vertices, *vertices->selected_vertex);
		}else{
			vertices->selected_vertex = vertices->selected_vertices.empty()
				? std::optional<std::size_t>{}
				: std::optional<std::size_t>{vertices->selected_vertices.back()};
		}
		return;
	}

	if(auto* edges = this->edit_edge_selection(); edges != nullptr){
		normalize_parts(*edges);
		if(!edges->selected_part || *edges->selected_part >= document.parts.size()){
			edges->selected_edge.reset();
			edges->selected_edges.clear();
			return;
		}
		const auto& part = document.parts[*edges->selected_part];
		if(part.type != physics::shape_type::convex_polygon){
			edges->selected_edge.reset();
			edges->selected_edges.clear();
			return;
		}
		const std::size_t edge_count = editor_detail::polygon_edge_count(part);
		edges->selected_edges.erase(
			std::ranges::remove_if(edges->selected_edges, [edge_count](const std::size_t edge) noexcept{
				return edge >= edge_count;
			}).begin(),
			edges->selected_edges.end());
		editor_detail::normalize_selection_indices(edges->selected_edges);
		if(edges->selected_edge && *edges->selected_edge < edge_count){
			editor_detail::add_selection_index(edges->selected_edges, *edges->selected_edge);
		}else{
			edges->selected_edge = edges->selected_edges.empty()
				? std::optional<std::size_t>{}
				: std::optional<std::size_t>{edges->selected_edges.back()};
		}
	}
}

bool editor_state::preview_part_operation(
	part_operation& target,
	const math::vec2 cursor){
	if(!target.active()){
		return true;
	}
	if(!this->part_operation_sources_available(target)){
		target.reset();
		this->clear_selection_for_current_mode();
		last_error = "operation target no longer exists";
		return false;
	}

	if(mode == editor_detail::editor_mode::object && !target.source_parts.empty()){
		std::vector<operation_part_snapshot> candidates{};
		candidates.reserve(target.source_parts.size());
		switch(target.kind){
		case editor_detail::operation_kind::move:{
			const math::vec2 delta = target.move_delta(cursor);
			for(const auto& source : target.source_parts){
				auto candidate = source.part;
				candidate.local_transform.vec = source.part.local_transform.vec + delta;
				if(!candidate.payload_valid()){
					this->restore_part_operation_sources(target);
					target.invalid = true;
					last_error = "edit would make the shape invalid";
					return false;
				}
				candidates.push_back({
					.part_index = source.part_index,
					.part = std::move(candidate)
				});
			}
			break;
		}
		case editor_detail::operation_kind::rotate:{
			const float delta = target.rotation_delta(cursor);
			for(const auto& source : target.source_parts){
				auto candidate = source.part;
				math::vec2 offset = source.part.local_transform.vec - target.pivot;
				offset.rotate_rad(delta);
				candidate.local_transform.vec = target.pivot + offset;
				candidate.local_transform.rot = source.part.local_transform.rot + delta;
				if(!candidate.payload_valid()){
					this->restore_part_operation_sources(target);
					target.invalid = true;
					last_error = "edit would make the shape invalid";
					return false;
				}
				candidates.push_back({
					.part_index = source.part_index,
					.part = std::move(candidate)
				});
			}
			break;
		}
		case editor_detail::operation_kind::resize:
			return false;
		case editor_detail::operation_kind::none:
			return true;
		default:
			std::unreachable();
		}

		for(auto& candidate : candidates){
			document.parts[candidate.part_index] = std::move(candidate.part);
		}
		target.invalid = false;
		last_error.clear();
		return true;
	}

	auto candidate = target.source_part;
	switch(target.kind){
	case editor_detail::operation_kind::move:
		if(!target.vertex_indices.empty()){
			if(candidate.type != physics::shape_type::convex_polygon){
				last_error = "selected polygon vertex no longer exists";
				return false;
			}
			const math::vec2 delta = target.move_delta(cursor);
			for(const std::size_t vertex_index : target.vertex_indices){
				if(vertex_index >= candidate.convex_polygon.vertices.size()){
					last_error = "selected polygon vertex no longer exists";
					return false;
				}
				math::vec2 target_world =
					target.source_part.convex_polygon.vertices[vertex_index] >> target.source_part.local_transform;
				target_world += delta;
				target_world = collision::editor::clamp_point_to_mirror_source_axes(
					target_world,
					candidate.mirror);
				candidate.convex_polygon.vertices[vertex_index] =
					candidate.local_transform.apply_inv_to(target_world);
			}
		}else{
			candidate.local_transform.vec = target.source_part.local_transform.vec + target.move_delta(cursor);
		}
		break;
	case editor_detail::operation_kind::rotate:
		if(!target.vertex_indices.empty()){
			if(candidate.type != physics::shape_type::convex_polygon){
				last_error = "selected polygon vertex no longer exists";
				return false;
			}
			const float delta = target.rotation_delta(cursor);
			for(const std::size_t vertex_index : target.vertex_indices){
				if(vertex_index >= candidate.convex_polygon.vertices.size()){
					last_error = "selected polygon vertex no longer exists";
					return false;
				}
				const math::vec2 source_world =
					target.source_part.convex_polygon.vertices[vertex_index] >> target.source_part.local_transform;
				math::vec2 offset = source_world - target.pivot;
				offset.rotate_rad(delta);
				candidate.convex_polygon.vertices[vertex_index] =
					candidate.local_transform.apply_inv_to(target.pivot + offset);
			}
		}else{
			const float delta = target.rotation_delta(cursor);
			math::vec2 offset = target.source_part.local_transform.vec - target.pivot;
			offset.rotate_rad(delta);
			candidate.local_transform.vec = target.pivot + offset;
			candidate.local_transform.rot = target.source_part.local_transform.rot + delta;
		}
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

bool editor_state::preview_transform_operation(
	transform_operation& target,
	const math::vec2 cursor){
	if(!target.active()){
		return true;
	}

	switch(mode.kind()){
	case editor_detail::editor_mode::reference_image:
		switch(target.kind){
		case editor_detail::operation_kind::move:
			document.reference_image.transform.vec = target.source_transform.vec + target.move_delta(cursor);
			break;
		case editor_detail::operation_kind::rotate:
		{
			const float delta = target.rotation_delta(cursor);
			math::vec2 offset = target.source_transform.vec - target.pivot;
			offset.rotate_rad(delta);
			document.reference_image.transform.vec = target.pivot + offset;
			document.reference_image.transform.rot = target.source_transform.rot + delta;
			break;
		}
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
		{
			const float delta = target.rotation_delta(cursor);
			math::vec2 offset = target.source_transform.vec - target.pivot;
			offset.rotate_rad(delta);
			document.total_transform.vec = target.pivot + offset;
			document.total_transform.rot = target.source_transform.rot + delta;
			break;
		}
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

	const std::filesystem::path path = paths.front();
	viewport->close_reference_image_file_selector();
	viewport->load_reference_image(path);
}

void collision_shape_editor_viewport::document_path_listener::on_update(
	react_flow::data_carrier<std::span<const std::filesystem::path>>& data){
	const auto paths = data.get();
	if(paths.empty() || viewport == nullptr){
		return;
	}

	const std::filesystem::path path = paths.front();
	const gui::cpd::file_selector_mode mode = viewport->document_file_selector_mode_;
	viewport->close_document_file_selector();
	switch(mode){
	case gui::cpd::file_selector_mode::read:
		viewport->load_document(path);
		return;
	case gui::cpd::file_selector_mode::save:
		viewport->save_document(path);
		return;
	default:
		std::unreachable();
	}
}

collision_shape_editor_viewport::collision_shape_editor_viewport(gui::scene& scene, gui::elem* parent)
	: gui::viewport(scene, parent),
	reference_image_path_node_{this},
	document_path_node_{this}{
	this->set_style(gui::style::family_variant::general_static);
	camera.set_scale_range({0.0625f, 4.f});
}

bool collision_shape_editor_viewport::update(const float delta_in_ticks){
	if(!gui::viewport::update(delta_in_ticks)){
		return false;
	}
	state.poll_alpha_hull_result();
	if(state.operation_active()){
		static_cast<void>(state.preview_operation(this->cursor_world_pos()));
	}
	return true;
}

gui::events::op_afterwards collision_shape_editor_viewport::on_click(
	const gui::events::click event,
	std::span<gui::elem* const> aboves){
	this->refresh_cursor_cache_from_local(event.pos);
	const bool rmb_press = event.key.as_mouse() == input_handle::mouse::RMB
		&& event.key.action == input_handle::act::press
		&& event.within_elem(*this);
	if(add_menu_overlay_ != nullptr
		&& event.key.action == input_handle::act::press
		&& !editor_detail::contains_inbound_elem(*add_menu_overlay_, aboves)){
		this->close_add_menu();
		return gui::events::op_afterwards::intercepted;
	}
	if(merge_menu_overlay_ != nullptr
		&& event.key.action == input_handle::act::press
		&& !editor_detail::contains_inbound_elem(*merge_menu_overlay_, aboves)){
		this->close_merge_menu();
		return gui::events::op_afterwards::intercepted;
	}
	if(context_menu_overlay_ != nullptr
		&& event.key.action == input_handle::act::press
		&& !editor_detail::contains_inbound_elem(*context_menu_overlay_, aboves)){
		this->close_context_menu();
		if(rmb_press){
			this->show_context_menu();
		}
		return gui::events::op_afterwards::intercepted;
	}
	if(reference_image_file_overlay_ != nullptr
		&& event.key.action == input_handle::act::press
		&& !editor_detail::contains_inbound_elem(
			*reference_image_file_overlay_,
			aboves)){
		this->close_reference_image_file_selector();
		return gui::events::op_afterwards::intercepted;
	}
	if(document_file_overlay_ != nullptr
		&& event.key.action == input_handle::act::press
		&& !editor_detail::contains_inbound_elem(*document_file_overlay_, aboves)){
		this->close_document_file_selector();
		return gui::events::op_afterwards::intercepted;
	}
	if(rmb_press){
		this->show_context_menu();
		return gui::events::op_afterwards::intercepted;
	}
	if(event.key.as_mouse() == input_handle::mouse::LMB
		&& event.key.action == input_handle::act::release
		&& state.box_selection() != nullptr
		&& !state.operation_active()){
		auto* box_selection = state.box_selection();
		box_selection->update(this->cursor_world_pos());
		state.select_in_region(
			box_selection->region(),
			this->selection_radius(),
			input_handle::matched(event.key.mode_bits, input_handle::mode::shift),
			input_handle::matched(event.key.mode_bits, input_handle::mode::ctrl));
		state.cancel_box_selection();
		return gui::events::op_afterwards::intercepted;
	}

	if(event.key.as_mouse() == input_handle::mouse::LMB && event.within_elem(*this)){
		const math::vec2 world_pos = this->cursor_world_pos();
		if(state.knife_cut() != nullptr){
			if(event.key.action == input_handle::act::press){
				state.add_knife_cut_point(world_pos);
			}
			return gui::events::op_afterwards::intercepted;
		}
		if(state.operation_active()){
			if(event.key.action == input_handle::act::release){
				static_cast<void>(state.preview_operation(world_pos));
				static_cast<void>(state.commit_operation());
			}
			return gui::events::op_afterwards::intercepted;
		}
		if(event.key.action == input_handle::act::press){
			state.begin_box_selection(world_pos);
			return gui::events::op_afterwards::intercepted;
		}
		if(event.key.action == input_handle::act::release && state.box_selection() != nullptr){
			auto* box_selection = state.box_selection();
			box_selection->update(world_pos);
			state.select_in_region(
				box_selection->region(),
				this->selection_radius(),
				input_handle::matched(event.key.mode_bits, input_handle::mode::shift),
				input_handle::matched(event.key.mode_bits, input_handle::mode::ctrl));
			state.cancel_box_selection();
			return gui::events::op_afterwards::intercepted;
		}
	}
	return gui::viewport::on_click(event, aboves);
}

gui::events::op_afterwards collision_shape_editor_viewport::on_drag(const gui::events::drag event){
	this->refresh_cursor_cache_from_local(event.dst);
	if(event.key.as_mouse() == input_handle::mouse::LMB && state.box_selection() != nullptr && !state.operation_active()){
		state.box_selection()->update(this->cursor_world_pos());
		return gui::events::op_afterwards::intercepted;
	}
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

	if(state.knife_cut() != nullptr){
		switch(key.as_key()){
		case input_handle::key::enter:
			if(key.action == input_handle::act::press){
				state.commit_knife_cut();
				return gui::events::op_afterwards::intercepted;
			}
			break;
		case input_handle::key::backspace:
			if(key.action == input_handle::act::press || key.action == input_handle::act::repeat){
				state.erase_knife_cut_point();
				return gui::events::op_afterwards::intercepted;
			}
			break;
		default:
			break;
		}
	}

	const auto mapped_action = editor_detail::keymap_action(
		key,
		state.operation_active(),
		state.mode);
	if(!mapped_action){
		return gui::events::op_afterwards::fall_through;
	}

	const auto handled = [this, key, cursor](const editor_detail::editor_action action) -> bool{
		switch(action){
		case editor_detail::editor_action::operation_precision:
			static_cast<void>(state.set_operation_precision(key.action == input_handle::act::press, cursor));
			static_cast<void>(state.preview_operation(cursor));
			return true;
		case editor_detail::editor_action::operation_constrain_x:
			static_cast<void>(state.toggle_operation_constraint({true, false}));
			static_cast<void>(state.preview_operation(cursor));
			return true;
		case editor_detail::editor_action::operation_constrain_y:
			static_cast<void>(state.toggle_operation_constraint({false, true}));
			static_cast<void>(state.preview_operation(cursor));
			return true;
		case editor_detail::editor_action::operation_backspace:
			static_cast<void>(state.erase_operation_character());
			static_cast<void>(state.preview_operation(cursor));
			return true;
		case editor_detail::editor_action::operation_consume_delete:
			return true;
		case editor_detail::editor_action::operation_commit:
			static_cast<void>(state.preview_operation(cursor));
			static_cast<void>(state.commit_operation());
			return true;
		case editor_detail::editor_action::toggle_object_edit_mode:
			state.set_mode(state.mode == editor_detail::editor_mode::object
				? editor_detail::editor_mode::edit
				: editor_detail::editor_mode::object);
			return true;
		case editor_detail::editor_action::set_object_mode:
			state.set_mode(editor_detail::editor_mode::object);
			return true;
		case editor_detail::editor_action::set_edit_mode:
			state.set_mode(editor_detail::editor_mode::edit);
			return true;
		case editor_detail::editor_action::set_reference_image_mode:
			state.set_mode(editor_detail::editor_mode::reference_image);
			return true;
		case editor_detail::editor_action::set_origin_mode:
			state.set_mode(editor_detail::editor_mode::origin);
			return true;
		case editor_detail::editor_action::set_vertex_select_mode:
			state.set_edit_selection_domain(editor_detail::polygon_selection_domain::vertex);
			return true;
		case editor_detail::editor_action::set_edge_select_mode:
			state.set_edit_selection_domain(editor_detail::polygon_selection_domain::edge);
			return true;
		case editor_detail::editor_action::select_all:
			state.select_all();
			return true;
		case editor_detail::editor_action::show_add_menu:
			this->show_add_menu();
			return true;
		case editor_detail::editor_action::add_vertex_between_selected:
			if(state.mode != editor_detail::editor_mode::edit){
				return false;
			}
			state.add_vertex_between_selected();
			return true;
		case editor_detail::editor_action::insert_vertex_at_cursor:
			if(state.mode != editor_detail::editor_mode::edit){
				return false;
			}
			state.insert_vertex_at_cursor(cursor);
			return true;
		case editor_detail::editor_action::connect_selected_vertices_as_edge:
			if(state.mode != editor_detail::editor_mode::edit){
				return false;
			}
			state.connect_selected_vertices_as_edge();
			return true;
		case editor_detail::editor_action::show_merge_menu:
			if(state.mode != editor_detail::editor_mode::edit){
				return false;
			}
			this->show_merge_menu();
			return true;
		case editor_detail::editor_action::cut_selected_polygon:
			if(state.mode != editor_detail::editor_mode::edit){
				return false;
			}
			state.cut_selected_polygon();
			return true;
		case editor_detail::editor_action::start_knife_cut:
			if(state.mode != editor_detail::editor_mode::edit){
				return false;
			}
			state.start_knife_cut(cursor);
			return true;
		case editor_detail::editor_action::make_convex_hull:
			if(state.mode != editor_detail::editor_mode::edit){
				return false;
			}
			state.make_selected_polygon_convex_hull();
			return true;
		case editor_detail::editor_action::split_convex_parts:
			if(state.mode != editor_detail::editor_mode::edit){
				return false;
			}
			state.split_selected_polygon_to_convex_parts();
			return true;
		case editor_detail::editor_action::join_polygons_at_center:
			state.join_selected_polygons(collision::editor::join_origin_mode::center);
			return true;
		case editor_detail::editor_action::join_polygons_at_first:
			state.join_selected_polygons(collision::editor::join_origin_mode::first);
			return true;
		case editor_detail::editor_action::join_polygons_at_last:
			state.join_selected_polygons(collision::editor::join_origin_mode::last);
			return true;
		case editor_detail::editor_action::show_reference_image_file_selector:
			if(state.mode != editor_detail::editor_mode::reference_image){
				return false;
			}
			this->show_reference_image_file_selector();
			return true;
		case editor_detail::editor_action::generate_alpha_hull:
			if(state.mode != editor_detail::editor_mode::reference_image){
				return false;
			}
			state.generate_polygon_from_reference_alpha();
			return true;
		case editor_detail::editor_action::duplicate_selected:
			state.duplicate_selected();
			return true;
		case editor_detail::editor_action::erase_selected:
			state.erase_selected();
			return true;
		case editor_detail::editor_action::undo:
			state.undo();
			return true;
		case editor_detail::editor_action::redo:
			state.redo();
			return true;
		case editor_detail::editor_action::reset_position:
			return state.reset_position_for_current_mode();
		case editor_detail::editor_action::reset_rotation:
			return state.reset_rotation_for_current_mode();
		case editor_detail::editor_action::start_move:
			return state.start_operation(editor_detail::operation_kind::move, cursor);
		case editor_detail::editor_action::start_rotate:
			return state.start_operation(editor_detail::operation_kind::rotate, cursor);
		case editor_detail::editor_action::start_resize:
			return state.start_operation(editor_detail::operation_kind::resize, cursor);
		case editor_detail::editor_action::none:
			return false;
		default:
			return false;
		}
	}(*mapped_action);

	if(handled){
		return gui::events::op_afterwards::intercepted;
	}

	return gui::events::op_afterwards::fall_through;
}

gui::events::op_afterwards collision_shape_editor_viewport::on_unicode_input(const char32_t value){
	if(state.input_operation_character(value)){
		static_cast<void>(state.preview_operation(this->cursor_world_pos()));
		return gui::events::op_afterwards::intercepted;
	}
	if(state.box_selection() != nullptr){
		state.cancel_box_selection();
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
	if(state.knife_cut() != nullptr){
		state.cancel_knife_cut();
		return gui::events::op_afterwards::intercepted;
	}
	if(add_menu_overlay_ != nullptr){
		this->close_add_menu();
		return gui::events::op_afterwards::intercepted;
	}
	if(merge_menu_overlay_ != nullptr){
		this->close_merge_menu();
		return gui::events::op_afterwards::intercepted;
	}
	if(context_menu_overlay_ != nullptr){
		this->close_context_menu();
		return gui::events::op_afterwards::intercepted;
	}
	if(reference_image_file_overlay_ != nullptr){
		this->close_reference_image_file_selector();
		return gui::events::op_afterwards::intercepted;
	}
	if(document_file_overlay_ != nullptr){
		this->close_document_file_selector();
		return gui::events::op_afterwards::intercepted;
	}
	if(state.current_selected_part_index()){
		state.clear_selection_for_current_mode();
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

void collision_shape_editor_viewport::show_add_menu(){
	this->close_add_menu();
	this->close_merge_menu();
	this->close_context_menu();
	pending_add_position_ = this->cursor_world_pos();
	auto result = this->get_scene().create_overlay(
		{
			.extent = gui::layout::extent_by_external,
			.align = align::pos::top_left,
			.external_press_policy = gui::overlay_external_press_policy::dismiss_and_intercept,
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
	clear_overlay_pointer_on_dismiss(result.dialog, add_menu_overlay_);
}

void collision_shape_editor_viewport::close_add_menu(){
	if(add_menu_overlay_ != nullptr){
		this->get_scene().close_overlay(add_menu_overlay_);
		add_menu_overlay_ = nullptr;
	}
}

void collision_shape_editor_viewport::show_merge_menu(){
	this->close_merge_menu();
	this->close_add_menu();
	this->close_context_menu();
	auto result = this->get_scene().create_overlay(
		{
			.extent = gui::layout::extent_by_external,
			.align = align::pos::top_left,
			.external_press_policy = gui::overlay_external_press_policy::dismiss_and_intercept,
			.absolute_offset = last_cursor_scene_pos_ + math::vec2{10.f, 10.f}
		},
		[this](gui::table& menu){
			const auto add_merge_button = [this](
				gui::table& target_menu,
				const std::string_view text,
				const editor_detail::vertex_merge_mode mode){
				target_menu.create_back([this, text, mode](gui::button<gui::direct_label>& b){
					b.set_style(gui::style::family_variant::base_only);
					b.set_fit_type(gui::label_fit_type::scl);
					b.text_entire_align = align::pos::center;
					b.set_tokenized_text({text});
					b.set_button_callback([this, mode]{
						state.merge_selected_vertices(mode);
						this->post_task([this]{
							this->close_merge_menu();
						});
					});
				});
			};
			const auto add_action_button = [this](
				gui::table& target_menu,
				const std::string_view text,
				auto action){
				target_menu.create_back([this, text, action](gui::button<gui::direct_label>& b){
					b.set_style(gui::style::family_variant::base_only);
					b.set_fit_type(gui::label_fit_type::scl);
					b.text_entire_align = align::pos::center;
					b.set_tokenized_text({text});
					b.set_button_callback([this, action]{
						std::invoke(action, state);
						this->post_task([this]{
							this->close_merge_menu();
						});
					});
				});
			};

			menu.set_style();
			menu.set_layout_spec(gui::layout::layout_policy::vert_major);
			menu.set_expand_policy(gui::layout::expand_policy::resize_to_fit);
			menu.set_entire_align(align::pos::top_left);
			menu.template_cell.set_size({128.f, 44.f}).set_pad(4.f);
			add_merge_button(menu, "Center", editor_detail::vertex_merge_mode::center);
			add_merge_button(menu, "First", editor_detail::vertex_merge_mode::first);
			add_merge_button(menu, "Last", editor_detail::vertex_merge_mode::last);
			add_action_button(menu, "M Hull", [](editor_state& state){
				state.merge_selected_polygons_as_hull();
			});
			add_action_button(menu, "Join C", [](editor_state& state){
				state.join_selected_polygons(collision::editor::join_origin_mode::center);
			});
			add_action_button(menu, "Join F", [](editor_state& state){
				state.join_selected_polygons(collision::editor::join_origin_mode::first);
			});
			add_action_button(menu, "Join L", [](editor_state& state){
				state.join_selected_polygons(collision::editor::join_origin_mode::last);
			});
		});
	merge_menu_overlay_ = std::addressof(result.elem());
	clear_overlay_pointer_on_dismiss(result.dialog, merge_menu_overlay_);
}

void collision_shape_editor_viewport::close_merge_menu(){
	if(merge_menu_overlay_ != nullptr){
		this->get_scene().close_overlay(merge_menu_overlay_);
		merge_menu_overlay_ = nullptr;
	}
}

void collision_shape_editor_viewport::show_context_menu(){
	this->close_context_menu();
	this->close_add_menu();
	this->close_merge_menu();
	auto result = this->get_scene().create_overlay(
		{
			.extent = gui::layout::extent_by_external,
			.align = align::pos::top_left,
			.external_press_policy = gui::overlay_external_press_policy::dismiss_and_retarget_right_press,
			.absolute_offset = last_cursor_scene_pos_ + math::vec2{10.f, 10.f}
		},
		[this](gui::table& menu){
			const auto add_action_button = [this](
				gui::table& target_menu,
				const std::string_view text,
				auto action){
				target_menu.create_back([this, text, action](gui::button<gui::direct_label>& b){
					b.set_style(gui::style::family_variant::base_only);
					b.set_fit_type(gui::label_fit_type::scl);
					b.text_entire_align = align::pos::center;
					b.set_tokenized_text({text});
					b.set_button_callback([this, action]{
						std::invoke(action);
						this->close_context_menu();
					});
				});
			};

			menu.set_style();
			menu.set_layout_spec(gui::layout::layout_policy::vert_major);
			menu.set_expand_policy(gui::layout::expand_policy::resize_to_fit);
			menu.set_entire_align(align::pos::top_left);
			menu.template_cell.set_size({152.f, 36.f}).set_pad(4.f);

			switch(state.mode.kind()){
			case editor_detail::editor_mode::object:
				add_action_button(menu, "Add", [this]{
					this->show_add_menu();
				});
				add_action_button(menu, "Edit", [this]{
					state.set_mode(editor_detail::editor_mode::edit);
				});
				add_action_button(menu, "Move", [this]{
					static_cast<void>(state.start_operation(editor_detail::operation_kind::move, this->cursor_world_pos()));
				});
				add_action_button(menu, "Rotate", [this]{
					static_cast<void>(state.start_operation(editor_detail::operation_kind::rotate, this->cursor_world_pos()));
				});
				add_action_button(menu, "Dup", [this]{
					state.duplicate_selected();
				});
				add_action_button(menu, "Join C", [this]{
					state.join_selected_polygons(collision::editor::join_origin_mode::center);
				});
				add_action_button(menu, "Join F", [this]{
					state.join_selected_polygons(collision::editor::join_origin_mode::first);
				});
				add_action_button(menu, "Join L", [this]{
					state.join_selected_polygons(collision::editor::join_origin_mode::last);
				});
				add_action_button(menu, "Del", [this]{
					state.erase_selected();
				});
				add_action_button(menu, "Reset Pos", [this]{
					static_cast<void>(state.reset_position_for_current_mode());
				});
				add_action_button(menu, "Reset Rot", [this]{
					static_cast<void>(state.reset_rotation_for_current_mode());
				});
				add_action_button(menu, "Mir X", [this]{
					state.toggle_mirror_x();
				});
				add_action_button(menu, "Mir Y", [this]{
					state.toggle_mirror_y();
				});
				break;
			case editor_detail::editor_mode::edit:
				add_action_button(menu, "Obj", [this]{
					state.set_mode(editor_detail::editor_mode::object);
				});
				add_action_button(menu, "V Sel", [this]{
					state.set_edit_selection_domain(editor_detail::polygon_selection_domain::vertex);
				});
				add_action_button(menu, "E Sel", [this]{
					state.set_edit_selection_domain(editor_detail::polygon_selection_domain::edge);
				});
				add_action_button(menu, "Move", [this]{
					static_cast<void>(state.start_operation(editor_detail::operation_kind::move, this->cursor_world_pos()));
				});
				add_action_button(menu, "+Mid", [this]{
					state.add_vertex_between_selected();
				});
				add_action_button(menu, "+Pt", [this]{
					state.insert_vertex_at_cursor(this->cursor_world_pos());
				});
				add_action_button(menu, "+Edge", [this]{
					state.connect_selected_vertices_as_edge();
				});
				add_action_button(menu, "Merge", [this]{
					this->post_task([this]{
						this->show_merge_menu();
					});
				});
				add_action_button(menu, "Del", [this]{
					state.erase_selected();
				});
				add_action_button(menu, "Knife", [this]{
					state.start_knife_cut(this->cursor_world_pos());
				});
				add_action_button(menu, "Cut", [this]{
					state.cut_selected_polygon();
				});
				add_action_button(menu, "M Hull", [this]{
					state.merge_selected_polygons_as_hull();
				});
				add_action_button(menu, "Join C", [this]{
					state.join_selected_polygons(collision::editor::join_origin_mode::center);
				});
				add_action_button(menu, "Join F", [this]{
					state.join_selected_polygons(collision::editor::join_origin_mode::first);
				});
				add_action_button(menu, "Join L", [this]{
					state.join_selected_polygons(collision::editor::join_origin_mode::last);
				});
				add_action_button(menu, "Hull", [this]{
					state.make_selected_polygon_convex_hull();
				});
				add_action_button(menu, "Split", [this]{
					state.split_selected_polygon_to_convex_parts();
				});
				break;
			case editor_detail::editor_mode::reference_image:
				add_action_button(menu, "Pick", [this]{
					this->show_reference_image_file_selector();
				});
				add_action_button(menu, "Alpha", [this]{
					state.generate_polygon_from_reference_alpha();
				});
				add_action_button(menu, "Move", [this]{
					static_cast<void>(state.start_operation(editor_detail::operation_kind::move, this->cursor_world_pos()));
				});
				add_action_button(menu, "Rotate", [this]{
					static_cast<void>(state.start_operation(editor_detail::operation_kind::rotate, this->cursor_world_pos()));
				});
				add_action_button(menu, "Resize", [this]{
					static_cast<void>(state.start_operation(editor_detail::operation_kind::resize, this->cursor_world_pos()));
				});
				add_action_button(menu, "Reset Pos", [this]{
					static_cast<void>(state.reset_position_for_current_mode());
				});
				add_action_button(menu, "Reset Rot", [this]{
					static_cast<void>(state.reset_rotation_for_current_mode());
				});
				break;
			case editor_detail::editor_mode::origin:
				add_action_button(menu, "Move", [this]{
					static_cast<void>(state.start_operation(editor_detail::operation_kind::move, this->cursor_world_pos()));
				});
				add_action_button(menu, "Rotate", [this]{
					static_cast<void>(state.start_operation(editor_detail::operation_kind::rotate, this->cursor_world_pos()));
				});
				add_action_button(menu, "Reset Pos", [this]{
					static_cast<void>(state.reset_position_for_current_mode());
				});
				add_action_button(menu, "Reset Rot", [this]{
					static_cast<void>(state.reset_rotation_for_current_mode());
				});
				break;
			default:
				break;
			}
		});
	context_menu_overlay_ = std::addressof(result.elem());
	clear_overlay_pointer_on_dismiss(result.dialog, context_menu_overlay_);
}

void collision_shape_editor_viewport::close_context_menu(){
	if(context_menu_overlay_ != nullptr){
		this->get_scene().close_overlay(context_menu_overlay_);
		context_menu_overlay_ = nullptr;
	}
}

void collision_shape_editor_viewport::show_reference_image_file_selector(){
	this->close_reference_image_file_selector();
	this->close_context_menu();
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
		}, gui::cpd::file_selector_mode::read);
	reference_image_file_overlay_ = std::addressof(result.elem());
	clear_overlay_pointer_on_dismiss(result.dialog, reference_image_file_overlay_);
}

void collision_shape_editor_viewport::close_reference_image_file_selector(){
	if(reference_image_file_overlay_ != nullptr){
		this->get_scene().close_overlay(reference_image_file_overlay_);
		reference_image_file_overlay_ = nullptr;
	}
}

void collision_shape_editor_viewport::load_reference_image(const std::filesystem::path& path){
	if(reference_images.page == nullptr || reference_images.atlas == nullptr){
		state.last_error = "reference image page is not configured";
		return;
	}

	try{
		math::vec2 image_size{};
		auto region = borrow_reference_image_region(path, std::addressof(image_size));
		state.set_reference_image(path, std::move(region), image_size, this->cursor_world_pos());
	}catch(const std::exception& e){
		state.last_error = std::format("failed to load reference image: {}", e.what());
	}
}

void collision_shape_editor_viewport::show_document_file_selector(const gui::cpd::file_selector_mode mode){
	this->close_document_file_selector();
	this->close_context_menu();
	document_file_selector_mode_ = mode;
	auto result = this->get_scene().create_overlay(
		{
			.extent = {
				{gui::layout::size_category::passive, 0.88f},
				{gui::layout::size_category::passive, 0.88f}
			},
			.align = align::pos::center
		},
		[this, mode](gui::cpd::file_selector& selector){
			selector.set_cared_suffix({document_suffix});
			if(!document_path_.empty() && document_path_.has_parent_path()){
				selector.visit_directory(document_path_.parent_path());
			}
			if(mode == gui::cpd::file_selector_mode::save){
				selector.set_save_file_name(
					document_path_.empty()
					? std::filesystem::path{std::string{default_document_name}}
					: document_path_.filename());
			}
			selector.get_prov().connect_successor(document_path_node_.node);
		}, mode);
	document_file_overlay_ = std::addressof(result.elem());
	clear_overlay_pointer_on_dismiss(result.dialog, document_file_overlay_);
}

void collision_shape_editor_viewport::close_document_file_selector(){
	if(document_file_overlay_ != nullptr){
		this->get_scene().close_overlay(document_file_overlay_);
		document_file_overlay_ = nullptr;
	}
}

void collision_shape_editor_viewport::save_document(const std::filesystem::path& path){
	if(state.save_document(path)){
		document_path_ = path;
	}
}

void collision_shape_editor_viewport::load_document(const std::filesystem::path& path){
	if(state.load_document(path)){
		document_path_ = path;
	}
}

void collision_shape_editor_viewport::draw_editor_content() const{
	namespace instr = graphic::g2d;

	auto& renderer = this->renderer();
	renderer.update_state(gui::fx::push_constant{
		gui::cfg::builtin::gpip::default_draw_constants{gui::fx::batch_draw_mode::def, 0.f}
	});

	this->viewport_begin();

	const float pixel = 1.f / camera.get_scale();
	const auto screen_stroke = [pixel](const float stroke) noexcept{
		return std::max(stroke * pixel, editor_detail::hit_epsilon);
	};

	const auto draw_grid = [this]{
		const auto viewport = camera.get_viewport();
		auto& grid_renderer = this->renderer();
		grid_renderer.update_state(gui::fx::pipeline_config{
			.pipeline_index = gui::cfg::builtin::gpip::idx::coordinate
		});
		grid_renderer.push(instr::rect_aabb{
			.v00 = viewport.vert_00(),
			.v11 = viewport.vert_11(),
			.vert_color = {graphic::colors::white}
		});
		grid_renderer.update_state(gui::fx::pipeline_config{
			.pipeline_index = gui::cfg::builtin::gpip::idx::def
		});
		grid_renderer.update_state(gui::fx::push_constant{
			gui::cfg::builtin::gpip::default_draw_constants{gui::fx::batch_draw_mode::def, 0.f}
		});
	};

	const auto draw_part = [this](
		const collision::editor::part& part,
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
		case physics::shape_type::convex_polygon:{
			const collision::editor::polygon_graph graph =
				collision::editor::part_effective_polygon_graph(part);
			const auto analysis = collision::editor::analyze_polygon_graph(graph);
			if(analysis.exportable()){
				static_cast<void>(draw::fill_polygon(
					part_renderer,
					analysis.ordered_vertices,
					transform,
					fill_style));
			}
			for(const collision::editor::polygon_edge edge : graph.edges){
				if(edge.first >= graph.vertices.size() || edge.second >= graph.vertices.size()){
					continue;
				}
				draw::push_line(
					part_renderer,
					graph.vertices[edge.first] >> transform,
					graph.vertices[edge.second] >> transform,
					outline_style);
			}
			return;
		}
		default:
			return;
		}
	};

	const auto draw_selected_handles = [this, screen_stroke](const collision::editor::part& part, const math::trans2 transform){
		if(part.type != physics::shape_type::convex_polygon){
			return;
		}

		const float radius = this->selection_radius() * 0.7f;
		const draw::collision_shape_draw_style handle_style{
			.color = graphic::colors::light_gray.copy_set_a(0.95f),
			.stroke = screen_stroke(2.f),
			.depth = 1.f
		};
		const draw::collision_shape_draw_style selected_handle_style{
			.color = graphic::colors::ORANGE.copy_set_a(0.98f),
			.stroke = screen_stroke(2.5f),
			.depth = 2.f
		};

		const auto selected_vertices = state.edit_selected_vertex_indices();
		for(std::size_t index = 0u; index != part.convex_polygon.vertices.size(); ++index){
			const math::vec2 vertex = part.convex_polygon.vertices[index] >> transform;
			const bool selected = std::ranges::find(selected_vertices, index) != selected_vertices.end();
			const auto& style = selected ? selected_handle_style : handle_style;
			draw::fill_shape(this->renderer(), physics::circle_shape{radius}, math::trans2{vertex, 0.f}, style);
			draw::draw_shape(this->renderer(), physics::circle_shape{radius}, math::trans2{vertex, 0.f}, style);
		}
	};

	const auto draw_selected_edges = [this, screen_stroke](const collision::editor::part& part, const math::trans2 transform){
		const auto selected_edges = state.edit_selected_edge_indices();
		if(part.type != physics::shape_type::convex_polygon || selected_edges.empty()){
			return;
		}

		const draw::collision_shape_draw_style selected_edge_style{
			.color = graphic::colors::MAGENTA.copy_set_a(0.98f),
			.stroke = screen_stroke(7.f),
			.depth = 4.f
		};
		const std::size_t edge_count = collision::editor::part_polygon_edge_count(part);
		for(const std::size_t edge_index : selected_edges){
			if(edge_index >= edge_count){
				continue;
			}
			const auto edge = collision::editor::part_polygon_edge_at(part, edge_index);
			const math::vec2 begin = part.convex_polygon.vertices[edge.first] >> transform;
			const math::vec2 end = part.convex_polygon.vertices[edge.second] >> transform;
			draw::push_line(this->renderer(), begin, end, selected_edge_style);
		}
	};

	const auto draw_dashed_line = [this, screen_stroke](
		const math::vec2 begin,
		const math::vec2 end,
		const graphic::color color,
		const float stroke){
		const math::vec2 delta = end - begin;
		const float length = delta.length();
		if(length <= editor_detail::hit_epsilon){
			return;
		}
		const math::vec2 direction = delta.copy().normalize();
		const float dash_length = 10.f / camera.get_scale();
		const float gap_length = 7.f / camera.get_scale();
		for(float offset = 0.f; offset < length; offset += dash_length + gap_length){
			const float segment_end = std::min(offset + dash_length, length);
			this->renderer().push(instr::line{
				.src = begin + direction * offset,
				.dst = begin + direction * segment_end,
				.color = {color, color},
				.stroke = screen_stroke(stroke),
				.cap_length = 0.f
			});
		}
	};

	const auto draw_component_basis = [this, screen_stroke](
		const collision::editor::part& part,
		const bool selected,
		const bool active){
		const math::trans2 transform = editor_detail::display_transform(state.document, part);
		const float radius = this->selection_radius() * 0.42f;
		const float axis_length = this->selection_radius() * 3.2f;
		math::vec2 x_axis{axis_length, 0.f};
		x_axis.rotate_rad(transform.rot);

		const graphic::color axis_color = active
			? graphic::colors::YELLOW.copy_set_a(0.96f)
			: graphic::colors::CRIMSON.copy_set_a(selected ? 0.90f : 0.62f);
		this->renderer().push(instr::line{
			.src = transform.vec,
			.dst = transform.vec + x_axis,
			.color = {axis_color, axis_color},
			.stroke = screen_stroke(1.8f),
			.cap_length = 0.f
		});

		const draw::collision_shape_draw_style dot_fill_style{
			.color = (active ? graphic::colors::YELLOW : (selected ? graphic::colors::aqua : graphic::colors::white))
				.copy_set_a(active ? 0.98f : (selected ? 0.92f : 0.72f)),
			.stroke = screen_stroke(1.f),
			.depth = 2.f
		};
		const draw::collision_shape_draw_style dot_outline_style{
			.color = graphic::colors::CRIMSON.copy_set_a(selected ? 0.90f : 0.72f),
			.stroke = screen_stroke(1.6f),
			.depth = 3.f
		};
		draw::fill_shape(this->renderer(), physics::circle_shape{radius}, math::trans2{transform.vec, 0.f}, dot_fill_style);
		draw::draw_shape(this->renderer(), physics::circle_shape{radius}, math::trans2{transform.vec, 0.f}, dot_outline_style);
	};

	const auto draw_mirror_axes = [this, screen_stroke](const collision::editor::mirror_modifier& mirror){
		const math::trans2 mirror_transform = mirror.origin;
		const math::vec2 origin = mirror_transform.vec;
		const auto push_axis = [this, origin, mirror_transform, screen_stroke](math::vec2 direction, const graphic::color color){
			direction.rotate_rad(mirror_transform.rot);
			this->renderer().push(instr::line{
				.src = origin - direction * 10000.f,
				.dst = origin + direction * 10000.f,
				.color = {color, color},
				.stroke = screen_stroke(2.f)
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

	const auto draw_reference_image = [this, screen_stroke]{
		if(!state.document.reference_image.visible() || !state.reference_image_loaded()){
			return;
		}

		const auto& reference = state.document.reference_image;
		const auto& region = state.reference_image.image_region;
		const auto color = graphic::colors::white.copy_set_a(std::clamp(reference.opacity, 0.f, 1.f));
		this->renderer().push(instr::rectangle{
			.generic = {
				.image = region->texture_binding(),
				.mode = {},
				.depth = 0
			},
			.pos = reference.transform.vec,
			.angle = reference.transform.rot,
			.scale = 1.f,
			.vert_color = {color},
			.extent = reference.half_extent * 2.f,
			.uv00 = region->uv.v01(),
			.uv11 = region->uv.v10()
		});

		if(state.mode == editor_detail::editor_mode::reference_image){
			const math::vec2 half = reference.half_extent;
			const std::array vertices{
				math::vec2{-half.x, -half.y} >> reference.transform,
				math::vec2{half.x, -half.y} >> reference.transform,
				math::vec2{half.x, half.y} >> reference.transform,
				math::vec2{-half.x, half.y} >> reference.transform
			};
			draw::push_closed_line(this->renderer(), vertices, draw::collision_shape_draw_style{
				.color = graphic::colors::ORANGE.copy_set_a(0.75f),
				.stroke = screen_stroke(3.f),
				.depth = 2.f
			});
		}
	};

	draw_grid();
	draw_reference_image();

	const draw::collision_shape_draw_style fill_style{
		.color = graphic::colors::gray.copy_set_a(0.22f),
		.stroke = screen_stroke(1.f),
		.depth = -2.f
	};
	const draw::collision_shape_draw_style outline_style{
		.color = graphic::colors::light_gray.copy_set_a(0.85f),
		.stroke = screen_stroke(3.f),
		.depth = -1.f
	};
	const draw::collision_shape_draw_style non_convex_outline_style{
		.color = graphic::colors::ORANGE.copy_set_a(0.92f),
		.stroke = screen_stroke(3.5f),
		.depth = -0.5f
	};
	const draw::collision_shape_draw_style self_intersect_outline_style{
		.color = graphic::colors::MAGENTA.copy_set_a(0.95f),
		.stroke = screen_stroke(4.f),
		.depth = -0.25f
	};
	const draw::collision_shape_draw_style open_outline_style{
		.color = graphic::colors::CRIMSON.copy_set_a(0.92f),
		.stroke = screen_stroke(3.5f),
		.depth = -0.5f
	};
	const draw::collision_shape_draw_style selected_style{
		.color = graphic::colors::aqua.copy_set_a(0.95f),
		.stroke = screen_stroke(5.f),
		.depth = 0.f
	};
	const draw::collision_shape_draw_style active_style{
		.color = graphic::colors::YELLOW.copy_set_a(0.98f),
		.stroke = screen_stroke(6.f),
		.depth = 0.5f
	};
	const draw::collision_shape_draw_style source_fill_style{
		.color = graphic::colors::gray.copy_set_a(0.07f),
		.stroke = screen_stroke(1.f),
		.depth = -5.f
	};
	const draw::collision_shape_draw_style source_outline_style{
		.color = graphic::colors::aqua.copy_set_a(0.24f),
		.stroke = screen_stroke(4.f),
		.depth = -4.f
	};

	if(state.mode == editor_detail::editor_mode::object
		&& state.object_part_operation() != nullptr
		&& state.object_part_operation()->kind == editor_detail::operation_kind::move){
		const collision::editor::part& source_part = state.object_part_operation()->source_part;
		draw_part(
			source_part,
			editor_detail::display_transform(state.document, source_part),
			source_fill_style,
			source_outline_style);
	}

	for(const collision::editor::part& part : state.document.parts){
		if(part.mirror.active()){
			draw_mirror_axes(part.mirror);
		}
	}

	for(std::size_t index = 0u; index != state.document.parts.size(); ++index){
		const auto& part = state.document.parts[index];
		const bool selected = state.mode == editor_detail::editor_mode::object
			? std::ranges::contains(state.object_selected_part_indices(), index)
			: state.mode == editor_detail::editor_mode::edit && std::ranges::contains(state.edit_selected_part_indices(), index);
		const bool active = state.mode == editor_detail::editor_mode::object
			? state.object_selected_part_index() == index
			: state.mode == editor_detail::editor_mode::edit && state.edit_selected_part_index() == index;
		const collision::editor::graph_state graph_state =
			part.type == physics::shape_type::convex_polygon
				? collision::editor::analyze_part_polygon_graph(part).state
				: collision::editor::graph_state::convex;
		const bool self_intersecting = graph_state == collision::editor::graph_state::self_intersecting;
		const bool non_convex = graph_state == collision::editor::graph_state::concave;
		const bool open = graph_state == collision::editor::graph_state::open;
		draw_part(
			part,
			editor_detail::display_transform(state.document, part),
			fill_style,
			active
				? active_style
				: selected
				? selected_style
				: (open
					? open_outline_style
					: (self_intersecting
						? self_intersect_outline_style
						: (non_convex ? non_convex_outline_style : outline_style))));
	}

	if(state.mode == editor_detail::editor_mode::object){
		for(std::size_t index = 0u; index != state.document.parts.size(); ++index){
			draw_component_basis(
				state.document.parts[index],
				std::ranges::contains(state.object_selected_part_indices(), index),
				state.object_selected_part_index() == index);
		}
	}

	if(state.mode == editor_detail::editor_mode::edit){
		if(const auto* selected = state.selected(); selected != nullptr){
			draw_selected_edges(*selected, editor_detail::display_transform(state.document, *selected));
			draw_selected_handles(*selected, editor_detail::display_transform(state.document, *selected));
		}
	}

	draw_transform_axes(
		state.document.total_transform,
		state.mode == editor_detail::editor_mode::origin ? screen_stroke(3.f) : screen_stroke(1.5f));

	if(const auto rotation_pivot = state.active_rotation_operation_pivot()){
		const float pivot_radius = this->selection_radius() * 0.55f;
		const graphic::color pivot_color = graphic::colors::YELLOW.copy_set_a(0.96f);
		draw_dashed_line(this->cursor_world_pos(), *rotation_pivot, pivot_color, 2.f);
		draw::fill_shape(
			this->renderer(),
			physics::circle_shape{pivot_radius},
			math::trans2{*rotation_pivot, 0.f},
			draw::collision_shape_draw_style{
				.color = pivot_color,
				.stroke = screen_stroke(1.f),
				.depth = 4.f
			});
		draw::draw_shape(
			this->renderer(),
			physics::circle_shape{pivot_radius},
			math::trans2{*rotation_pivot, 0.f},
			draw::collision_shape_draw_style{
				.color = graphic::colors::white.copy_set_a(0.95f),
				.stroke = screen_stroke(2.f),
				.depth = 5.f
			});
	}

	if(const auto* box_selection = state.box_selection(); box_selection != nullptr){
		const math::frect region = box_selection->region();
		this->renderer().push(instr::rect_aabb{
			.generic = {
				.depth = 0.25f
			},
			.v00 = region.vert_00(),
			.v11 = region.vert_11(),
			.vert_color = {graphic::colors::aqua.copy_set_a(0.14f)}
		});
		const std::array box_vertices{
			region.vert_00(),
			region.vert_10(),
			region.vert_11(),
			region.vert_01()
		};
		draw::push_closed_line(this->renderer(), box_vertices, draw::collision_shape_draw_style{
			.color = graphic::colors::aqua.copy_set_a(0.82f),
			.stroke = screen_stroke(2.f),
			.depth = 2.f
		});
	}

	if(const auto* knife = state.knife_cut(); knife != nullptr && !knife->points.empty()){
		std::vector<math::vec2> preview_points = knife->points;
		const math::vec2 cursor = this->cursor_world_pos();
		if(!preview_points.empty() && preview_points.back().dst2(cursor) > editor_detail::hit_epsilon){
			preview_points.push_back(cursor);
		}
		draw::push_open_line(this->renderer(), preview_points, draw::collision_shape_draw_style{
			.color = graphic::colors::YELLOW.copy_set_a(0.95f),
			.stroke = screen_stroke(3.f),
			.depth = 4.f
		});
		for(const math::vec2 point : knife->points){
			draw::fill_shape(this->renderer(), physics::circle_shape{this->selection_radius() * 0.45f}, math::trans2{point, 0.f}, draw::collision_shape_draw_style{
				.color = graphic::colors::YELLOW.copy_set_a(0.95f),
				.stroke = screen_stroke(1.f),
				.depth = 5.f
			});
		}
	}

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

	object_mode_button_ = add_button("Obj", [this]{
			viewport_->state.set_mode(editor_detail::editor_mode::object);
		});
	edit_mode_button_ = add_button("Edit", [this]{
		viewport_->state.set_mode(editor_detail::editor_mode::edit);
	});
	reference_mode_button_ = add_button("Ref", [this]{
		viewport_->state.set_mode(editor_detail::editor_mode::reference_image);
	});
	add_button("Pick", [this]{
		viewport_->state.set_mode(editor_detail::editor_mode::reference_image);
		viewport_->open_reference_image_file_selector();
	});
	origin_mode_button_ = add_button("Origin", [this]{
		viewport_->state.set_mode(editor_detail::editor_mode::origin);
	});
	rotation_reference_button_ = add_button("Pivot Med", [this]{
		if(viewport_ != nullptr){
			viewport_->state.set_rotation_reference_mode(
				viewport_->state.rotation_reference,
				viewport_->cursor_world_pos());
		}
	});
	rotation_reference_button_->set_tooltip_state(
		{
			.layout_info = gui::tooltip::align_meta{
				.follow = gui::tooltip::anchor_type::owner,
				.attach_point_spawner = align::pos::bottom_left,
				.attach_point_tooltip = align::pos::top_left,
				.offset = {0.f, 4.f}
			},
			.auto_release = true,
			.min_hover_time = 0.f
		},
		[this](gui::button<gui::direct_label>& owner, gui::table& tooltip){
			static_cast<void>(owner);
			const auto add_reference_button = [this](
				gui::table& target,
				const editor_detail::rotation_reference_mode mode){
				target.create_back([this, mode](gui::button<gui::direct_label>& button){
					button.set_style(gui::style::family_variant::base_only);
					button.set_fit_type(gui::label_fit_type::scl);
					button.text_entire_align = align::pos::center;
					button.set_tokenized_text({editor_detail::rotation_reference_mode_label(mode)});
					button.set_button_callback([this, mode]{
						if(viewport_ != nullptr){
							viewport_->state.set_rotation_reference_mode(mode, viewport_->cursor_world_pos());
						}
					});
				});
			};

			tooltip.set_style();
			tooltip.set_layout_spec(gui::layout::layout_policy::vert_major);
			tooltip.set_expand_policy(gui::layout::expand_policy::resize_to_fit);
			tooltip.set_entire_align(align::pos::top_left);
			tooltip.template_cell.set_size({116.f, 36.f}).set_pad(4.f);
			add_reference_button(tooltip, editor_detail::rotation_reference_mode::median);
			add_reference_button(tooltip, editor_detail::rotation_reference_mode::cursor);
			add_reference_button(tooltip, editor_detail::rotation_reference_mode::active);
		});
	add_button("Load", [this]{
		viewport_->open_document_file_selector(gui::cpd::file_selector_mode::read);
	});
	add_button("Save", [this]{
		viewport_->open_document_file_selector(gui::cpd::file_selector_mode::save);
	});
	add_button("+Circle", [this]{
		this->add_shape_at_cursor(physics::shape_type::circle);
	});
	add_button("+Capsule", [this]{
		this->add_shape_at_cursor(physics::shape_type::capsule);
	});
	add_button("+Box", [this]{
		this->add_shape_at_cursor(physics::shape_type::box);
	});
	add_button("+Poly", [this]{
		this->add_shape_at_cursor(physics::shape_type::convex_polygon);
	});
	add_button("Dup", [this]{
		viewport_->state.duplicate_selected();
	});
	add_button("Del", [this]{
		viewport_->state.erase_selected();
	});
	add_button("+Mid", [this]{
		viewport_->state.add_vertex_between_selected();
	});
	add_button("+Pt", [this]{
		viewport_->state.insert_vertex_at_cursor(viewport_->cursor_world_pos());
	});
	add_button("+Edge", [this]{
		viewport_->state.connect_selected_vertices_as_edge();
	});
	add_button("Merge", [this]{
		viewport_->open_merge_menu();
	});
	add_button("Cut", [this]{
		viewport_->state.cut_selected_polygon();
	});
	add_button("Knife", [this]{
		viewport_->state.start_knife_cut(viewport_->cursor_world_pos());
	});
	add_button("M Hull", [this]{
		viewport_->state.merge_selected_polygons_as_hull();
	});
	add_button("Join C", [this]{
		viewport_->state.join_selected_polygons(collision::editor::join_origin_mode::center);
	});
	add_button("Join F", [this]{
		viewport_->state.join_selected_polygons(collision::editor::join_origin_mode::first);
	});
	add_button("Join L", [this]{
		viewport_->state.join_selected_polygons(collision::editor::join_origin_mode::last);
	});
	add_button("Hull", [this]{
		viewport_->state.make_selected_polygon_convex_hull();
	});
	add_button("Split", [this]{
		viewport_->state.split_selected_polygon_to_convex_parts();
	});
	add_button("Alpha", [this]{
		viewport_->state.generate_polygon_from_reference_alpha();
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
	add_button("Load R", [this]{
		viewport_->state.set_mode(editor_detail::editor_mode::reference_image);
		viewport_->open_reference_image_file_selector();
	});
	add_button("Clear R", [this]{
		viewport_->state.clear_reference_image();
	});
	add_button("Mir X", [this]{
		viewport_->state.toggle_mirror_x();
	});
	add_button("Mir Y", [this]{
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
	properties_scroll.elem().set_max_extent({600.f, std::numeric_limits<float>::infinity()});
	properties_scroll.cell().region_scale = {0.f, 0.f, 0.40f, 0.50f};
	properties_scroll.cell().region_align = align::pos::bottom_left;
	properties_scroll.cell().unsaturate_cell_elem_align = align::pos::bottom_left;
	properties_scroll.cell().margin = gui::border_t{.left = 8.f, .bottom = 8.f};
	properties_scroll->set_style();

	auto& properties_sequence = properties_scroll.elem().get_elem();
	// properties_sequence.set_style();
	properties_sequence.set_layout_spec(gui::layout::layout_policy::hori_major);
	properties_sequence.set_expand_policy(gui::layout::expand_policy::prefer);
	properties_sequence.set_align_to_tail(true);

	auto properties = properties_sequence.emplace_back<collision_shape_editor_prop::panel>();
	properties.elem().bind(*viewport_);
	properties.cell().set_pending();
	properties_panel_ = std::addressof(properties.elem());

	auto status = viewport_stack.create_back([this](gui::direct_label& label){
		label.set_style(gui::style::family_variant::base_only);
		label.set_fit_type(gui::label_fit_type::scl);
		label.set_self_border(gui::border_t{}.set(6.f));
		label.max_fit_scale_bound.y = 28.f;
		label.text_entire_align = align::pos::center_left;
		status_ = std::addressof(label);
	});
	status.cell().region_scale = {0.f, 0.f, 1.f, 0.1f};
	status.cell().region_align = align::pos::top_left;
	status.cell().margin = gui::border_t{.left = 8.f, .right = 8.f, .top = 8.f};

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
	if(properties_panel_ != nullptr){
		properties_panel_->refresh();
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
		if(rotation_reference_button_ != nullptr){
			rotation_reference_button_->set_tokenized_text(typesetting::tokenized_text{
				std::format(
					"Pivot {}",
					editor_detail::rotation_reference_mode_short_label(state.rotation_reference)),
				typesetting::tokenize_tag::raw
			});
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
				[](const collision::editor::part& part){
					return part.mirror.active();
				});
			mirror_text = std::format("{} on", active_mirror_count);
		}

		const std::string polygon_status = state.document.has_self_intersecting_polygon()
			? "self"
			: (state.document.has_open_polygon()
				? "open"
				: (state.document.has_non_convex_polygon() ? "conc" : "conv"));
		std::string status_text = std::format(
			"m: {} / p: {} / sel: {} / op: {} / piv: {} / mir: {} / poly: {} / ref: {}",
			editor_detail::mode_name(state.mode),
			state.part_count(),
			state.selected_text(),
			state.operation_text(),
			state.rotation_reference_text(),
			mirror_text,
			polygon_status,
			state.reference_image_loaded() && state.document.reference_image.visible() ? "on" : "none");
		if(const std::string hint_text = state.operation_hint_text(); !hint_text.empty()){
			status_text += std::format(" / {}", hint_text);
		}
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

const collision::editor::document& collision_shape_editor::editor_metadata() const noexcept{
	return viewport_->state.document;
}

collision::shape collision_shape_editor::shape_metadata() const{
	return viewport_->state.document.to_runtime_shape();
}

collision::record collision_shape_editor::packed_shape_record() const{
	return viewport_->state.document.to_packed_record();
}
}
