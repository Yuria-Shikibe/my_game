export module mo_yanxi.game.runtime.game_renderer;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.physics.shape;
export import mo_yanxi.game.runtime.draw.collision_shape_style;
export import mo_yanxi.graphic.camera;
export import mo_yanxi.graphic.color;
export import mo_yanxi.math.rect_ortho;
export import mo_yanxi.math.trans2;
export import mo_yanxi.math.vector2;

import mo_yanxi.graphic.draw.instruction;
import std;

namespace mo_yanxi::game{
export
enum class game_render_shape_surface : std::uint8_t{
	outline,
	fill,
	fill_outline
};

export
[[nodiscard]] constexpr bool game_render_shape_has_fill(const game_render_shape_surface surface) noexcept{
	return surface == game_render_shape_surface::fill
		|| surface == game_render_shape_surface::fill_outline;
}

export
[[nodiscard]] constexpr bool game_render_shape_has_outline(const game_render_shape_surface surface) noexcept{
	return surface == game_render_shape_surface::outline
		|| surface == game_render_shape_surface::fill_outline;
}

export
struct game_render_effect_config{
	bool bloom_enabled{true};
	bool transparent_overlap_enabled{true};
	bool ssao_enabled{};
	float bloom_hint_strength{1.f};
	float ssao_hint_scale{4.25f};
};

export
struct game_render_frame_state{
	graphic::camera2 camera{};
	math::vec2 extent{};
	math::frect viewport{};
	game_render_effect_config effects{};
	std::uint64_t frame_index{};
	std::uint64_t simulation_tick{};
	double simulation_time_seconds{};

	[[nodiscard]] bool valid_extent() const noexcept{
		return extent.x > 0.f && extent.y > 0.f;
	}
};

export
struct game_render_frame_stats{
	game_render_frame_state frame{};
	std::size_t drawable_visited{};
	std::size_t drawable_drawn{};
	std::size_t drawable_culled{};

	[[nodiscard]] bool valid_extent() const noexcept{
		return frame.valid_extent();
	}
};

export
struct game_draw_cull_bounds{
	math::frect world_aabb{};
	float screen_clip_margin{};
	bool enabled{true};
};

export
struct game_draw_context{
	ecs::component_manager& manager;
	ecs::entity_id entity{};
	const game_render_frame_state& frame;

	[[nodiscard]] const graphic::camera2& camera() const noexcept{
		return frame.camera;
	}

	[[nodiscard]] const math::frect& viewport() const noexcept{
		return frame.viewport;
	}

	[[nodiscard]] math::vec2 extent() const noexcept{
		return frame.extent;
	}
};
}
