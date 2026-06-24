export module mo_yanxi.game.ui.collision_shape_editor;

#ifdef MO_YANXI_GAME_ENABLE_EDITOR_TESTS
import std;
#endif
import mo_yanxi.game.physics.collision_shape_editor_metadata;
import mo_yanxi.graphic.image_atlas;
import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;
import mo_yanxi.gui.elem.button;
import mo_yanxi.gui.elem.head_body_elem;
import mo_yanxi.gui.elem.label;

namespace mo_yanxi::game::ui{
struct collision_shape_editor_viewport;
namespace collision_shape_editor_prop{
struct panel;
}

export
void configure_collision_shape_editor_reference_images(graphic::image_atlas& image_atlas);

export
void clear_collision_shape_editor_reference_images() noexcept;

export
struct collision_shape_editor : gui::head_body{
private:
	collision_shape_editor_viewport* viewport_{};
	gui::direct_label* status_{};
	collision_shape_editor_prop::panel* properties_panel_{};
	gui::button<gui::direct_label>* object_mode_button_{};
	gui::button<gui::direct_label>* edit_mode_button_{};
	gui::button<gui::direct_label>* reference_mode_button_{};
	gui::button<gui::direct_label>* origin_mode_button_{};
	gui::button<gui::direct_label>* rotation_reference_button_{};

public:
	[[nodiscard]] collision_shape_editor(gui::scene& scene, gui::elem* parent);

	void set_default_appearance() override;

	void on_display_state_changed(bool is_shown, bool is_scene_notified) override;

	bool update(float delta_in_ticks) override;

	[[nodiscard]] const collision::editor::document& editor_metadata() const noexcept;

	[[nodiscard]] collision::shape shape_metadata() const;

	[[nodiscard]] collision::record packed_shape_record() const;

private:
	void refresh_status_label() const;

	void add_shape_at_cursor(physics::shape_type type) const;
};

export
using collision_box_editor = collision_shape_editor;

#ifdef MO_YANXI_GAME_ENABLE_EDITOR_TESTS
export namespace test{
struct collision_shape_editor_polygon_operation_result{
	collision::editor::document document{};
	std::optional<std::size_t> selected_part{};
	std::vector<std::size_t> selected_parts{};
	std::string last_error{};
};

struct collision_shape_editor_edge_connection_result{
	collision::editor::document document{};
	std::optional<std::size_t> selected_vertex{};
	std::vector<std::size_t> selected_vertices{};
	std::string last_error{};
};

struct collision_shape_editor_insert_point_result{
	collision::editor::document document{};
	std::optional<std::size_t> selected_vertex{};
	std::vector<std::size_t> selected_vertices{};
	std::string selected_text{};
	bool has_open_polygon{};
	bool has_non_convex_polygon{};
	bool has_self_intersecting_polygon{};
	std::string runtime_export_error{};
	std::string last_error{};
};

struct collision_shape_editor_edit_operation_result{
	collision::editor::document document{};
	std::optional<std::size_t> selected_part{};
	std::vector<std::size_t> selected_parts{};
	std::optional<std::size_t> selected_vertex{};
	std::vector<std::size_t> selected_vertices{};
	std::optional<std::size_t> selected_edge{};
	std::vector<std::size_t> selected_edges{};
	bool has_open_polygon{};
	bool has_non_convex_polygon{};
	bool has_self_intersecting_polygon{};
	std::string runtime_export_error{};
	std::string last_error{};
};

[[nodiscard]] collision_shape_editor_insert_point_result insert_closed_polygon_point_for_test();

[[nodiscard]] collision_shape_editor_insert_point_result add_midpoint_to_selected_polygon_edge_for_test();

[[nodiscard]] collision_shape_editor_edge_connection_result connect_inserted_polygon_vertex_for_test();

[[nodiscard]] collision_shape_editor_edit_operation_result connect_open_polygon_closing_edge_for_test();

[[nodiscard]] collision_shape_editor_edit_operation_result erase_selected_polygon_vertex_for_test();

[[nodiscard]] collision_shape_editor_edit_operation_result erase_selected_polygon_edge_for_test();

[[nodiscard]] collision_shape_editor_edit_operation_result merge_selected_polygon_vertices_for_test();

[[nodiscard]] collision_shape_editor_polygon_operation_result hull_object_selected_polygon_for_test();

[[nodiscard]] collision_shape_editor_polygon_operation_result split_object_selected_polygon_for_test();

[[nodiscard]] collision_shape_editor_polygon_operation_result cut_selected_polygon_between_vertices_for_test();
}
#endif
}
