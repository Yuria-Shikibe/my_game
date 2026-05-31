module;

#include <vulkan/vulkan.h>

export module mo_yanxi.game.runtime.game_render_packet;

export import mo_yanxi.game.runtime.game_renderer;

import mo_yanxi.math.vector2;
import std;

namespace mo_yanxi::game{
export
struct alignas(16) game_ssao_sample_gpu{
	math::vec2 offset{};
	float weight{};
	float radius{};

	friend bool operator==(const game_ssao_sample_gpu&, const game_ssao_sample_gpu&) noexcept = default;
};

export
struct alignas(16) game_ssao_params_gpu{
	std::array<game_ssao_sample_gpu, game_ssao_kernel::max_samples> samples{};
	std::int32_t sample_count{};
	std::int32_t enabled{1};
	float strength{1.f};
	float reserved0{};
	float scale{4.25f};
	float radius{2.5f};
	float depth_threshold{0.025f};
	float reserved1{};

	friend bool operator==(const game_ssao_params_gpu&, const game_ssao_params_gpu&) noexcept = default;
};

export
struct alignas(16) game_oit_emit_shape_gpu{
	static constexpr std::size_t max_polygon_vertices = 8;

	math::vec2 center{};
	math::vec2 half_extent{};
	math::vec2 axis_begin{};
	math::vec2 axis_end{};
	std::array<std::array<float, 4>, max_polygon_vertices / 2> polygon_vertices{};
	std::array<float, 4> color_base{};
	std::array<float, 4> color_light{};
	float radius{};
	float depth{};
	float opacity{};
	std::int32_t shape_kind{};
	std::int32_t enabled{};
	std::int32_t polygon_vertex_count{};
	float reserved0{};
	float reserved1{};

	friend bool operator==(const game_oit_emit_shape_gpu&, const game_oit_emit_shape_gpu&) noexcept = default;
};

export
struct alignas(16) game_oit_emit_params_gpu{
	static constexpr std::size_t max_shapes = 96;
	static constexpr std::uint32_t tile_size = 16;
	static constexpr std::size_t tile_mask_word_count = 4;
	static constexpr std::size_t max_tile_count = 65536;

	std::int32_t shape_count{};
	std::int32_t tile_grid_width{};
	std::int32_t tile_grid_height{};
	std::int32_t use_tile_mask{};
	std::int32_t debug_line_count{};
	std::int32_t debug_ring_count{};
	std::int32_t debug_closed_polyline_count{};
	std::int32_t debug_vertex_count{};
	float time_seconds{};
	float bloom_hint_strength{1.f};
	std::int32_t transparent_overlap_enabled{1};
	std::int32_t bloom_enabled{1};
	float reserved0{};
	float reserved1{};

	friend bool operator==(const game_oit_emit_params_gpu&, const game_oit_emit_params_gpu&) noexcept = default;
};

export
inline constexpr float game_oit_shape_softness = 0.012f;

export
struct alignas(16) game_oit_tile_mask_gpu{
	std::array<std::uint32_t, game_oit_emit_params_gpu::tile_mask_word_count> shape_words{};

	friend bool operator==(const game_oit_tile_mask_gpu&, const game_oit_tile_mask_gpu&) noexcept = default;
};

export
struct alignas(16) game_oit_emit_shape_storage_gpu{
	std::array<game_oit_emit_shape_gpu, game_oit_emit_params_gpu::max_shapes> shapes{};

	friend bool operator==(const game_oit_emit_shape_storage_gpu&, const game_oit_emit_shape_storage_gpu&) noexcept = default;
};

export
struct game_oit_emit_packet_gpu{
	game_oit_emit_params_gpu params{};
	game_oit_emit_shape_storage_gpu shape_storage{};
	std::vector<game_oit_tile_mask_gpu> tile_masks{};

	friend bool operator==(const game_oit_emit_packet_gpu&, const game_oit_emit_packet_gpu&) noexcept = default;
};

static_assert(game_oit_emit_params_gpu::max_shapes > game_oit_buffer_layout::default_nodes_per_pixel);
static_assert(sizeof(game_oit_emit_params_gpu) == 64u);
static_assert(sizeof(game_oit_tile_mask_gpu) == 16u);
static_assert(sizeof(game_oit_emit_shape_storage_gpu) <= 48u * 1024u);

export
struct game_render_gpu_packet{
	game_oit_emit_packet_gpu oit_emit{};
	game_ssao_params_gpu ssao{};
	game_render_effect_config effects{};
	std::uint64_t frame_index{};
};

export
[[nodiscard]] constexpr VkDeviceSize get_game_oit_emit_shape_storage_buffer_size() noexcept{
	return sizeof(game_oit_emit_shape_storage_gpu);
}

export
[[nodiscard]] constexpr VkDeviceSize get_game_oit_tile_mask_storage_buffer_size() noexcept{
	return sizeof(game_oit_tile_mask_gpu) * game_oit_emit_params_gpu::max_tile_count;
}

export
[[nodiscard]] math::u32size2 make_game_oit_tile_grid(const math::vec2 extent) noexcept{
	const auto tile_size = game_oit_emit_params_gpu::tile_size;
	const auto width = static_cast<std::uint32_t>(std::max(extent.x, 0.f));
	const auto height = static_cast<std::uint32_t>(std::max(extent.y, 0.f));
	return {
		(width + tile_size - 1u) / tile_size,
		(height + tile_size - 1u) / tile_size
	};
}

export
[[nodiscard]] game_ssao_params_gpu make_game_ssao_params_gpu(
	const math::vec2 extent,
	const float scale = 4.25f,
	const bool enabled = true,
	const float strength = 1.f) noexcept{
	const auto kernel = game::make_game_ssao_kernel(extent, scale);
	game_ssao_params_gpu params{
		.sample_count = static_cast<std::int32_t>(kernel.sample_count),
		.enabled = enabled ? 1 : 0,
		.strength = std::max(strength, 0.f),
		.scale = kernel.scale
	};

	for(std::size_t i = 0; i != kernel.sample_count; ++i){
		params.samples[i] = {
			.offset = kernel.samples[i].offset,
			.weight = kernel.samples[i].weight,
			.radius = kernel.samples[i].radius
		};
	}
	return params;
}

export
[[nodiscard]] game_ssao_params_gpu make_game_ssao_params_gpu(
	const VkExtent2D extent,
	const float scale = 4.25f,
	const bool enabled = true,
	const float strength = 1.f) noexcept{
	return game::make_game_ssao_params_gpu(
		math::vec2{static_cast<float>(extent.width), static_cast<float>(extent.height)},
		scale,
		enabled,
		strength);
}

export
[[nodiscard]] std::array<float, 4> make_game_emit_color_array(const graphic::color color) noexcept{
	return {color.r, color.g, color.b, color.a};
}

export
void set_game_emit_polygon_vertex(game_oit_emit_shape_gpu& shape, const std::size_t index, const math::vec2 vertex) noexcept{
	if(index >= game_oit_emit_shape_gpu::max_polygon_vertices){
		return;
	}

	auto& packed = shape.polygon_vertices[index / 2];
	packed[(index % 2) * 2] = vertex.x;
	packed[(index % 2) * 2 + 1] = vertex.y;
}

export
[[nodiscard]] float make_game_emit_depth(const float style_depth, const std::size_t stable_order) noexcept{
	const float finite_depth = std::isfinite(style_depth) ? style_depth : 0.f;
	const float sorted_depth = 0.50f - finite_depth * 0.001f + static_cast<float>(stable_order) * 0.0001f;
	return std::clamp(sorted_depth, 0.05f, 0.95f);
}

export
[[nodiscard]] game_oit_emit_packet_gpu make_game_oit_emit_packet_gpu(
	const game_render_submission& submission){
	game_oit_emit_packet_gpu packet{
		.params = {
			.shape_count = static_cast<std::int32_t>(
				std::min(submission.collision_shapes.size(), game_oit_emit_params_gpu::max_shapes)),
			.time_seconds = static_cast<float>(submission.frame_index) / 60.f,
			.bloom_hint_strength = submission.effects.bloom_enabled
				? std::max(submission.effects.bloom_hint_strength, 0.f)
				: 0.f,
			.transparent_overlap_enabled = submission.effects.transparent_overlap_enabled ? 1 : 0,
			.bloom_enabled = submission.effects.bloom_enabled ? 1 : 0
		}
	};

	if(!submission.valid_extent()){
		packet.params.shape_count = 0;
		return packet;
	}

	const auto& camera = submission.camera;
	const float inv_height = 1.f / std::max(submission.extent.y, 1.f);
	const auto screen_center = submission.extent * 0.5f;

	auto to_emit_space = [&](const math::vec2 world_position) noexcept{
		return (camera.get_world_to_screen(world_position) - screen_center) * inv_height;
	};

	auto to_emit_extent = [&](const math::vec2 world_extent) noexcept{
		return world_extent * (camera.get_scale() * inv_height);
	};

	struct game_oit_emit_shape_candidate{
		game_oit_emit_shape_gpu shape{};
		std::size_t stable_order{};
	};

	std::vector<game_oit_emit_shape_candidate> candidates{};
	candidates.reserve(std::min(submission.collision_shapes.size(), game_oit_emit_params_gpu::max_shapes));
	std::size_t stable_order{};
	auto push_shape = [&](const game_collision_shape_render_item& item,
		const physics::collision_shape_record_part& part) noexcept{
		const math::trans2 part_transform = part.local_transform >> item.transform;
		const auto color = item.style.color;
		const float opacity = std::clamp(color.a, 0.05f, 1.f);
		game_oit_emit_shape_gpu shape{
			.color_base = game::make_game_emit_color_array(color),
			.color_light = game::make_game_emit_color_array(color.to_light(submission.effects.bloom_hint_strength)),
			.depth = game::make_game_emit_depth(item.style.depth, stable_order),
			.opacity = opacity,
			.enabled = 1
		};

		switch(part.type){
		case physics::shape_type::circle:{
			const float radius = std::max(part.payload.circle.radius * camera.get_scale() * inv_height, 0.004f);
			shape.center = to_emit_space(part_transform.vec);
			shape.half_extent = {radius, radius};
			shape.radius = radius;
			shape.shape_kind = 0;
			break;
		}
		case physics::shape_type::capsule:{
			shape.axis_begin = to_emit_space(part.payload.capsule.begin >> part_transform);
			shape.axis_end = to_emit_space(part.payload.capsule.end >> part_transform);
			shape.center = (shape.axis_begin + shape.axis_end) * 0.5f;
			shape.radius = std::max(part.payload.capsule.radius * camera.get_scale() * inv_height, 0.004f);
			const auto half_axis = (shape.axis_end - shape.axis_begin) * 0.5f;
			shape.half_extent = {std::abs(half_axis.x) + shape.radius, std::abs(half_axis.y) + shape.radius};
			shape.shape_kind = 1;
			break;
		}
		case physics::shape_type::box:{
			const auto half = to_emit_extent(part.payload.box.half_extent);
			shape.center = to_emit_space(part_transform.vec);
			shape.half_extent = {std::max(half.x, 0.004f), std::max(half.y, 0.004f)};
			shape.radius = static_cast<float>(part_transform.rot);
			shape.shape_kind = 2;
			break;
		}
		case physics::shape_type::convex_polygon:{
			const auto payload = part.payload.convex_polygon;
			const auto vertices = item.shape.polygon_vertices();
			const auto offset = static_cast<std::size_t>(payload.vertex_offset);
			const auto count = static_cast<std::size_t>(payload.vertex_count);
			shape.shape_kind = 3;
			if(count >= 3 && offset <= vertices.size() && count <= vertices.size() - offset){
				const auto vertex_count = std::min(count, game_oit_emit_shape_gpu::max_polygon_vertices);
				math::vec2 bounds_min{
					std::numeric_limits<float>::max(),
					std::numeric_limits<float>::max()
				};
				math::vec2 bounds_max{
					std::numeric_limits<float>::lowest(),
					std::numeric_limits<float>::lowest()
				};
				for(std::size_t i = 0; i != vertex_count; ++i){
					const auto vertex = to_emit_space(vertices[offset + i] >> part_transform);
					game::set_game_emit_polygon_vertex(shape, i, vertex);
					bounds_min.x = std::min(bounds_min.x, vertex.x);
					bounds_min.y = std::min(bounds_min.y, vertex.y);
					bounds_max.x = std::max(bounds_max.x, vertex.x);
					bounds_max.y = std::max(bounds_max.y, vertex.y);
				}
				shape.center = (bounds_min + bounds_max) * 0.5f;
				const auto bounds_half_extent = (bounds_max - bounds_min) * 0.5f;
				shape.half_extent = {
					std::max(bounds_half_extent.x, 0.004f),
					std::max(bounds_half_extent.y, 0.004f)
				};
				shape.polygon_vertex_count = static_cast<std::int32_t>(vertex_count);
			} else{
				const auto aabb_center = item.aabb.get_center();
				const auto screen_half_extent = to_emit_extent(item.aabb.extent() * 0.5f);
				shape.center = to_emit_space(aabb_center);
				shape.half_extent = {
					std::max(screen_half_extent.x, 0.012f),
					std::max(screen_half_extent.y, 0.012f)
				};
			}
			break;
		}
		}

		candidates.push_back({
			.shape = shape,
			.stable_order = stable_order++
		});
	};

	for(const auto& item : submission.collision_shapes){
		for(const auto& part : item.shape.records()){
			push_shape(item, part);
		}
	}

	std::ranges::stable_sort(candidates, {}, [](const game_oit_emit_shape_candidate& candidate){
		return candidate.shape.depth;
	});

	const auto shape_count = std::min(candidates.size(), game_oit_emit_params_gpu::max_shapes);
	for(std::size_t shape_index = 0; shape_index != shape_count; ++shape_index){
		packet.shape_storage.shapes[shape_index] = candidates[shape_index].shape;
	}
	packet.params.shape_count = static_cast<std::int32_t>(shape_count);

	const auto tile_grid = game::make_game_oit_tile_grid(submission.extent);
	const auto tile_count = static_cast<std::uint64_t>(tile_grid.x) * static_cast<std::uint64_t>(tile_grid.y);
	packet.params.tile_grid_width = static_cast<std::int32_t>(tile_grid.x);
	packet.params.tile_grid_height = static_cast<std::int32_t>(tile_grid.y);
	packet.params.use_tile_mask =
		shape_count > 0u && tile_count > 0u && tile_count <= game_oit_emit_params_gpu::max_tile_count ? 1 : 0;
	if(packet.params.use_tile_mask != 0){
		packet.tile_masks.assign(static_cast<std::size_t>(tile_count), {});
		const float aspect = submission.extent.x / std::max(submission.extent.y, 1.f);
		const auto to_tile_index = [&](const math::vec2 point) noexcept{
			const float pixel_x = ((point.x / std::max(aspect, 0.0001f)) + 0.5f) * submission.extent.x;
			const float pixel_y = (point.y + 0.5f) * submission.extent.y;
			const auto tile_x = static_cast<std::int32_t>(std::floor(pixel_x / game_oit_emit_params_gpu::tile_size));
			const auto tile_y = static_cast<std::int32_t>(std::floor(pixel_y / game_oit_emit_params_gpu::tile_size));
			return math::ivec2{
				std::clamp(tile_x, 0, static_cast<std::int32_t>(tile_grid.x) - 1),
				std::clamp(tile_y, 0, static_cast<std::int32_t>(tile_grid.y) - 1)
			};
		};

		for(std::size_t shape_index = 0; shape_index != shape_count; ++shape_index){
			const auto& shape = packet.shape_storage.shapes[shape_index];
			const auto tile_half_extent = [&shape]{
				if(shape.shape_kind == 2){
					const auto radius_bound = shape.half_extent.length() + game_oit_shape_softness;
					return math::vec2{radius_bound, radius_bound};
				}
				return shape.half_extent + math::vec2{game_oit_shape_softness, game_oit_shape_softness};
			}();
			const auto tile_min = to_tile_index(shape.center - tile_half_extent);
			const auto tile_max = to_tile_index(shape.center + tile_half_extent);
			const auto word_index = shape_index / 32u;
			const auto word_mask = std::uint32_t{1} << (shape_index % 32u);
			for(std::int32_t y = tile_min.y; y <= tile_max.y; ++y){
				for(std::int32_t x = tile_min.x; x <= tile_max.x; ++x){
					const auto tile_index = static_cast<std::size_t>(
						static_cast<std::uint32_t>(y) * tile_grid.x + static_cast<std::uint32_t>(x));
					packet.tile_masks[tile_index].shape_words[word_index] |= word_mask;
				}
			}
		}
	}
	return packet;
}

export
[[nodiscard]] game_oit_emit_packet_gpu make_game_oit_emit_packet_gpu(
	const game_render_snapshot& snapshot,
	const math::vec2 extent){
	return game::make_game_oit_emit_packet_gpu(game::make_game_render_submission(snapshot, extent));
}

export
[[nodiscard]] game_oit_emit_packet_gpu make_game_oit_emit_packet_gpu(
	const game_render_snapshot& snapshot,
	const VkExtent2D extent){
	return game::make_game_oit_emit_packet_gpu(
		snapshot,
		math::vec2{static_cast<float>(extent.width), static_cast<float>(extent.height)});
}

export
[[nodiscard]] game_render_gpu_packet make_game_render_gpu_packet(
	const game_render_submission& submission){
	return {
		.oit_emit = game::make_game_oit_emit_packet_gpu(submission),
		.ssao = game::make_game_ssao_params_gpu(
			submission.extent,
			submission.effects.ssao_hint_scale,
			submission.effects.ssao_enabled),
		.effects = submission.effects,
		.frame_index = submission.frame_index
	};
}

export
[[nodiscard]] game_render_gpu_packet make_game_render_gpu_packet(
	const game_render_snapshot& snapshot,
	const math::vec2 extent){
	return game::make_game_render_gpu_packet(game::make_game_render_submission(snapshot, extent));
}

export
[[nodiscard]] game_render_gpu_packet make_game_render_gpu_packet(
	const game_render_snapshot& snapshot,
	const VkExtent2D extent){
	return game::make_game_render_gpu_packet(
		snapshot,
		math::vec2{static_cast<float>(extent.width), static_cast<float>(extent.height)});
}
}
