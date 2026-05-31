export module mo_yanxi.game.runtime.game_renderer;

export import mo_yanxi.game.physics.shape;
export import mo_yanxi.game.runtime.game_debug_draw_packet;
export import mo_yanxi.game.runtime.draw.collision_shape_style;
export import mo_yanxi.graphic.camera;

import mo_yanxi.graphic.color;
import std;

namespace mo_yanxi::game{
export
struct alignas(16) game_oit_statistics_gpu{
	std::uint32_t capacity{};
	std::uint32_t size{};
	std::uint32_t reserved0{};
	std::uint32_t reserved1{};
};

export
struct alignas(16) game_oit_node_gpu{
	std::array<float, 4> color_base{};
	std::array<float, 4> color_light{};
	float depth{};
	std::uint32_t next{};
	std::uint32_t reserved0{};
	std::uint32_t reserved1{};
};

export
struct game_oit_buffer_layout{
	static constexpr std::uint32_t default_nodes_per_pixel = 6;
	static constexpr std::uint32_t invalid_node = std::numeric_limits<std::uint32_t>::max();

	std::uint32_t width{};
	std::uint32_t height{};
	std::uint32_t nodes_per_pixel{default_nodes_per_pixel};

	[[nodiscard]] std::uint64_t pixel_count() const noexcept{
		return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
	}

	[[nodiscard]] std::uint64_t node_capacity() const noexcept{
		return pixel_count() * nodes_per_pixel;
	}

	[[nodiscard]] std::uint64_t node_storage_bytes() const noexcept{
		return node_capacity() * sizeof(game_oit_node_gpu);
	}

	[[nodiscard]] std::uint64_t storage_buffer_bytes() const noexcept{
		return sizeof(game_oit_statistics_gpu) + node_storage_bytes();
	}

	[[nodiscard]] game_oit_statistics_gpu initial_statistics() const noexcept{
		return {
			.capacity = static_cast<std::uint32_t>(std::min<std::uint64_t>(
				node_capacity(),
				std::numeric_limits<std::uint32_t>::max())),
			.size = invalid_node
		};
	}
};

export
struct game_ssao_sample{
	math::vec2 offset{};
	float weight{};
	float radius{};
};

export
struct game_ssao_kernel{
	static constexpr std::size_t max_samples = 64;

	std::array<game_ssao_sample, max_samples> samples{};
	std::uint32_t sample_count{};
	float scale{4.25f};
};

export
[[nodiscard]] game_oit_buffer_layout make_game_oit_buffer_layout(const math::vec2 extent) noexcept{
	return {
		.width = static_cast<std::uint32_t>(std::max(extent.x, 0.f)),
		.height = static_cast<std::uint32_t>(std::max(extent.y, 0.f))
	};
}

export
[[nodiscard]] game_ssao_kernel make_game_ssao_kernel(const math::vec2 extent, const float scale = 4.25f) noexcept{
	struct ring_config{
		std::uint32_t count;
		float radius;
		float weight;
	};

	static constexpr std::array rings{
		ring_config{16, 0.15702702700f, 1.00f},
		ring_config{12, 0.32702702700f, 0.72f},
		ring_config{8, 0.55062162162f, 0.48f},
		ring_config{4, 0.83062162162f, 0.28f}
	};

	const float inv_width = extent.x > 0.f ? 1.f / extent.x : 0.f;
	const float inv_height = extent.y > 0.f ? 1.f / extent.y : 0.f;

	game_ssao_kernel kernel{.scale = scale};
	for(const auto ring : rings){
		for(std::uint32_t i = 0; i != ring.count && kernel.sample_count < kernel.samples.size(); ++i){
			const float angle = std::numbers::pi_v<float> * 2.f * static_cast<float>(i) / static_cast<float>(ring.count);
			kernel.samples[kernel.sample_count++] = {
				.offset = {std::cos(angle) * ring.radius * inv_width, std::sin(angle) * ring.radius * inv_height},
				.weight = ring.weight,
				.radius = ring.radius
			};
		}
	}
	return kernel;
}

export
struct game_collision_shape_render_item{
	physics::collision_shape_record shape{};
	math::trans2 transform{};
	math::frect aabb{};
	draw::collision_shape_draw_style style{};
	float screen_clip_margin{8.f};
};

export
struct game_render_effect_config{
	bool bloom_enabled{true};
	bool transparent_overlap_enabled{true};
	bool ssao_enabled{};
	float bloom_hint_strength{1.f};
	float ssao_hint_scale{4.25f};
	game_oit_buffer_layout oit_layout{};
	game_ssao_kernel ssao_kernel{};
};

export
struct game_render_snapshot{
	graphic::camera2 camera{};
	std::vector<game_collision_shape_render_item> collision_shapes{};
	game_render_effect_config effects{};
	std::uint64_t frame_index{};
};

export
struct game_render_submission{
	graphic::camera2 camera{};
	math::vec2 extent{};
	std::vector<game_collision_shape_render_item> collision_shapes{};
	game_render_effect_config effects{};
	std::uint64_t frame_index{};

	[[nodiscard]] bool valid_extent() const noexcept{
		return extent.x > 0.f && extent.y > 0.f;
	}
};

export
[[nodiscard]] game_render_submission make_game_render_submission(
	const game_render_snapshot& snapshot,
	const math::vec2 extent){
	game_render_submission submission{
		.camera = snapshot.camera,
		.extent = extent,
		.effects = snapshot.effects,
		.frame_index = snapshot.frame_index
	};
	submission.effects.oit_layout = game::make_game_oit_buffer_layout(extent);
	submission.effects.ssao_kernel = game::make_game_ssao_kernel(extent, submission.effects.ssao_hint_scale);

	if(!submission.valid_extent()){
		return submission;
	}

	submission.camera.resize_screen(extent.x, extent.y);
	submission.camera.update(0.f);

	const auto viewport = submission.camera.get_viewport();
	const float camera_scale = std::max(submission.camera.get_scale(), 0.0001f);
	submission.collision_shapes.reserve(snapshot.collision_shapes.size());
	for(const auto& item : snapshot.collision_shapes){
		const float world_margin = std::max(item.screen_clip_margin, 0.f) / camera_scale;
		if(!item.aabb.copy().expand(world_margin, world_margin).overlap_inclusive(viewport)){
			continue;
		}

		submission.collision_shapes.push_back(item);
	}
	return submission;
}

export
[[nodiscard]] game_debug_draw_packet make_game_debug_draw_packet(const game_render_submission& submission){
	game_debug_draw_packet packet{};
	for(const auto& item : submission.collision_shapes){
		game::append_game_debug_draw_shape(packet, item.shape, item.transform, item.style);
	}
	return packet;
}

export
struct game_render_frame{
	game_render_submission submission{};
	game_debug_draw_packet debug_draw{};

	[[nodiscard]] bool valid_extent() const noexcept{
		return submission.valid_extent();
	}

	[[nodiscard]] std::uint64_t frame_index() const noexcept{
		return submission.frame_index;
	}
};

export
[[nodiscard]] game_render_frame make_game_render_frame(game_render_submission submission){
	game_render_frame frame{
		.submission = std::move(submission)
	};
	frame.debug_draw = game::make_game_debug_draw_packet(frame.submission);
	return frame;
}

export
[[nodiscard]] game_render_frame make_game_render_frame(
	const game_render_snapshot& snapshot,
	const math::vec2 extent){
	return game::make_game_render_frame(game::make_game_render_submission(snapshot, extent));
}

export
[[nodiscard]] game_debug_draw_gpu_packet make_game_debug_draw_gpu_packet(const game_render_frame& frame){
	auto packet = game::make_game_debug_draw_gpu_packet(frame.debug_draw);
	if(!frame.valid_extent()){
		return packet;
	}

	const auto& camera = frame.submission.camera;
	const float inv_height = 1.f / std::max(frame.submission.extent.y, 1.f);
	const auto screen_center = frame.submission.extent * 0.5f;
	const float extent_scale = camera.get_scale() * inv_height;

	auto to_emit_space = [&](const math::vec2 world_position) noexcept{
		return (camera.get_world_to_screen(world_position) - screen_center) * inv_height;
	};

	for(auto& line : packet.lines){
		line.src = to_emit_space(line.src);
		line.dst = to_emit_space(line.dst);
		line.stroke = std::max(line.stroke * extent_scale, game_debug_draw_epsilon * inv_height);
	}

	for(auto& ring : packet.rings){
		ring.center = to_emit_space(ring.center);
		ring.radius *= extent_scale;
	}

	for(auto& polyline : packet.closed_polylines){
		polyline.stroke = std::max(polyline.stroke * extent_scale, game_debug_draw_epsilon * inv_height);
	}

	for(auto& vertex : packet.vertices){
		vertex.position = to_emit_space(vertex.position);
	}

	return packet;
}

}
