export module mo_yanxi.game.ui.collision_shape_editor;

import mo_yanxi.game.physics.collision_shape_editor_metadata;
import mo_yanxi.graphic.image_atlas;
import mo_yanxi.gui.elem.button;
import mo_yanxi.gui.elem.head_body_elem;
import mo_yanxi.gui.elem.label;

namespace mo_yanxi::game::ui{
struct collision_shape_editor_viewport;
struct collision_shape_editor_properties_panel;

export
void configure_collision_shape_editor_reference_images(graphic::image_atlas& image_atlas);

export
void clear_collision_shape_editor_reference_images() noexcept;

export
struct collision_shape_editor : gui::head_body{
private:
	collision_shape_editor_viewport* viewport_{};
	gui::direct_label* status_{};
	collision_shape_editor_properties_panel* properties_panel_{};
	gui::button<gui::direct_label>* object_mode_button_{};
	gui::button<gui::direct_label>* edit_mode_button_{};
	gui::button<gui::direct_label>* reference_mode_button_{};
	gui::button<gui::direct_label>* origin_mode_button_{};

public:
	[[nodiscard]] collision_shape_editor(gui::scene& scene, gui::elem* parent);

	void on_display_state_changed(bool is_shown, bool is_scene_notified) override;

	bool update(float delta_in_ticks) override;

	[[nodiscard]] const physics::collision_shape_editor_document& editor_metadata() const noexcept;

	[[nodiscard]] physics::collision_shape shape_metadata() const;

	[[nodiscard]] physics::collision_shape_record packed_shape_record() const;

private:
	void refresh_status_label() const;

	void add_shape_at_cursor(physics::shape_type type) const;
};

export
using collision_box_editor = collision_shape_editor;
}
