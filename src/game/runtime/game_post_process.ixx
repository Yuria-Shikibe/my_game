module;

#include <vulkan/vulkan.h>

export module mo_yanxi.game.runtime.game_post_process;

export import mo_yanxi.graphic.compositor.post_process_pass;
export import mo_yanxi.graphic.compositor.post_process_pass_with_ubo;
export import mo_yanxi.game.runtime.game_render_packet;
export import mo_yanxi.vk;

import mo_yanxi.game.runtime.game_renderer;
import mo_yanxi.math.vector2;
import std;

namespace mo_yanxi::game{
export
struct game_post_process_shader_names{
	static constexpr std::string_view oit_blend{"post_process.game.oit_blend.spv"};
	static constexpr std::string_view oit_clear{"post_process.game.oit_clear.spv"};
	static constexpr std::string_view oit_emit{"post_process.game.oit_emit.spv"};
	static constexpr std::string_view ssao{"post_process.game.ssao.spv"};
	static constexpr std::string_view world_merge{"post_process.game.world_merge.spv"};
};

export
struct game_post_process_shaders{
	vk::shader_module oit_blend;
	vk::shader_module oit_clear;
	vk::shader_module oit_emit;
	vk::shader_module ssao;
	vk::shader_module world_merge;

	[[nodiscard]] game_post_process_shaders() = default;

	[[nodiscard]] game_post_process_shaders(
		const VkDevice device,
		const std::filesystem::path& shader_spv_path)
		: oit_blend(device, shader_spv_path / game_post_process_shader_names::oit_blend),
		  oit_clear(device, shader_spv_path / game_post_process_shader_names::oit_clear),
		  oit_emit(device, shader_spv_path / game_post_process_shader_names::oit_emit),
		  ssao(device, shader_spv_path / game_post_process_shader_names::ssao),
		  world_merge(device, shader_spv_path / game_post_process_shader_names::world_merge){
	}
};

export
[[nodiscard]] VkDeviceSize get_game_oit_storage_buffer_size(const VkExtent2D extent) noexcept{
	return static_cast<VkDeviceSize>(game::make_game_oit_buffer_layout(math::vec2{
		static_cast<float>(extent.width),
		static_cast<float>(extent.height)
	}).storage_buffer_bytes());
}

export
[[nodiscard]] VkDeviceSize get_game_oit_node_storage_buffer_size(const VkExtent2D extent) noexcept{
	return static_cast<VkDeviceSize>(game::make_game_oit_buffer_layout(math::vec2{
		static_cast<float>(extent.width),
		static_cast<float>(extent.height)
	}).node_storage_bytes());
}

[[nodiscard]] VkDeviceSize get_game_oit_node_storage_buffer_size_for_compositor(const VkExtent2D extent){
	return game::get_game_oit_node_storage_buffer_size(extent);
}

export
[[nodiscard]] VkDeviceSize get_game_oit_node_storage_offset() noexcept{
	return sizeof(game_oit_statistics_gpu);
}

export
[[nodiscard]] graphic::compositor::post_process_meta make_game_oit_blend_meta(vk::shader_module&& shader){
	auto meta = graphic::compositor::post_process_meta{
		{std::move(shader)},
		{
			{{0}, 0, graphic::compositor::no_slot},
			{{1}, 1, graphic::compositor::no_slot},
			{{2}, 2, graphic::compositor::no_slot},
			{{3}, 3, graphic::compositor::no_slot},
			{{4}, 4, graphic::compositor::no_slot},
			{{5}, graphic::compositor::no_slot, 0},
			{{6}, graphic::compositor::no_slot, 1},
		}
	};
	meta[{0, 0}].get<graphic::compositor::buffer_requirement>().size.extent =
		static_cast<graphic::compositor::buffer_size_spec::cropper*>(
			&game::get_game_oit_node_storage_buffer_size_for_compositor);
	meta.sockets.at_in(0).get<graphic::compositor::buffer_requirement>().size.extent =
		static_cast<graphic::compositor::buffer_size_spec::cropper*>(
			&game::get_game_oit_node_storage_buffer_size_for_compositor);
	meta[{1, 0}].get<graphic::compositor::image_requirement>().format = VK_FORMAT_R32_UINT;
	meta[{1, 0}].get<graphic::compositor::image_requirement>().usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	meta[{2, 0}].get<graphic::compositor::image_requirement>().format = VK_FORMAT_R32_SFLOAT;
	meta[{5, 0}].get<graphic::compositor::image_requirement>().format = VK_FORMAT_R16G16B16A16_SFLOAT;
	meta[{6, 0}].get<graphic::compositor::image_requirement>().format = VK_FORMAT_R16G16B16A16_SFLOAT;
	return meta;
}

export
[[nodiscard]] graphic::compositor::post_process_meta make_game_oit_clear_meta(vk::shader_module&& shader){
	auto meta = graphic::compositor::post_process_meta{
		{std::move(shader)},
		{
			{{0}, graphic::compositor::no_slot, 0},
			{{1}, graphic::compositor::no_slot, 1},
		}
	};
	meta[{0, 0}].get<graphic::compositor::image_requirement>().format = VK_FORMAT_R32_UINT;
	meta[{0, 0}].get<graphic::compositor::image_requirement>().usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	meta[{1, 0}].get<graphic::compositor::image_requirement>().format = VK_FORMAT_R32_SFLOAT;
	return meta;
}

export
[[nodiscard]] graphic::compositor::post_process_meta make_game_oit_emit_meta(vk::shader_module&& shader){
	auto meta = graphic::compositor::post_process_meta{
		{std::move(shader)},
		{
			{{0}, graphic::compositor::no_slot, 0},
			{{1}, graphic::compositor::no_slot, 1},
			{{2}, graphic::compositor::no_slot, 2},
			{{3}, graphic::compositor::no_slot, 3},
			{{4}, graphic::compositor::no_slot, 4},
			{{5}, 0, graphic::compositor::no_slot},
			{{6}, 1, graphic::compositor::no_slot},
			{{7}, 2, graphic::compositor::no_slot},
			{{9}, 3, graphic::compositor::no_slot},
			{{10}, 4, graphic::compositor::no_slot},
			{{11}, 5, graphic::compositor::no_slot},
			{{12}, 6, graphic::compositor::no_slot},
		}
	};
	meta[{0, 0}].get<graphic::compositor::buffer_requirement>().size.extent =
		static_cast<graphic::compositor::buffer_size_spec::cropper*>(
			&game::get_game_oit_node_storage_buffer_size_for_compositor);
	meta.sockets.at_out(0).get<graphic::compositor::buffer_requirement>().size.extent =
		static_cast<graphic::compositor::buffer_size_spec::cropper*>(
			&game::get_game_oit_node_storage_buffer_size_for_compositor);
	meta[{1, 0}].get<graphic::compositor::image_requirement>().format = VK_FORMAT_R32_UINT;
	meta[{1, 0}].get<graphic::compositor::image_requirement>().usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	meta[{2, 0}].get<graphic::compositor::image_requirement>().format = VK_FORMAT_R32_SFLOAT;
	meta[{3, 0}].get<graphic::compositor::image_requirement>().format = VK_FORMAT_R16G16B16A16_SFLOAT;
	meta[{4, 0}].get<graphic::compositor::image_requirement>().format = VK_FORMAT_R16G16B16A16_SFLOAT;
	meta[{6, 0}].get<graphic::compositor::buffer_requirement>().size.extent =
		game::get_game_oit_emit_shape_storage_buffer_size();
	meta[{7, 0}].get<graphic::compositor::buffer_requirement>().size.extent =
		game::get_game_oit_tile_mask_storage_buffer_size();
	meta[{9, 0}].get<graphic::compositor::buffer_requirement>().size.extent =
		sizeof(game_debug_draw_line_gpu) * game_debug_draw_params_gpu::max_lines;
	meta[{10, 0}].get<graphic::compositor::buffer_requirement>().size.extent =
		sizeof(game_debug_draw_ring_gpu) * game_debug_draw_params_gpu::max_rings;
	meta[{11, 0}].get<graphic::compositor::buffer_requirement>().size.extent =
		sizeof(game_debug_draw_closed_polyline_gpu) * game_debug_draw_params_gpu::max_closed_polylines;
	meta[{12, 0}].get<graphic::compositor::buffer_requirement>().size.extent =
		sizeof(game_debug_draw_vertex_gpu) * game_debug_draw_params_gpu::max_vertices;
	return meta;
}

export
[[nodiscard]] graphic::compositor::post_process_meta make_game_ssao_meta(vk::shader_module&& shader){
	return graphic::compositor::post_process_meta{
		{std::move(shader)},
		{
			{{0}, graphic::compositor::no_slot, 0},
			{{1}, 0, graphic::compositor::no_slot},
			{{2}, 1, graphic::compositor::no_slot},
		}
	};
}

export
[[nodiscard]] graphic::compositor::post_process_meta make_game_world_merge_meta(vk::shader_module&& shader){
	return graphic::compositor::post_process_meta{
		{std::move(shader)},
		{
			{{0}, graphic::compositor::no_slot, 0},
			{{1}, 0, graphic::compositor::no_slot},
			{{2}, 1, graphic::compositor::no_slot},
			{{3}, 2, graphic::compositor::no_slot},
			{{4}, 3, graphic::compositor::no_slot},
		}
	};
}

export
struct game_post_process_meta_set{
	graphic::compositor::post_process_meta oit_blend;
	graphic::compositor::post_process_meta oit_clear;
	graphic::compositor::post_process_meta oit_emit;
	graphic::compositor::post_process_meta ssao;
	graphic::compositor::post_process_meta world_merge;
};

export
[[nodiscard]] game_post_process_meta_set make_game_post_process_meta_set(game_post_process_shaders&& shaders){
	return {
		.oit_blend = game::make_game_oit_blend_meta(std::move(shaders.oit_blend)),
		.oit_clear = game::make_game_oit_clear_meta(std::move(shaders.oit_clear)),
		.oit_emit = game::make_game_oit_emit_meta(std::move(shaders.oit_emit)),
		.ssao = game::make_game_ssao_meta(std::move(shaders.ssao)),
		.world_merge = game::make_game_world_merge_meta(std::move(shaders.world_merge)),
	};
}

export
using game_oit_emit_stage = graphic::compositor::post_process_pass_with_ubo<game_oit_emit_params_gpu>;

export
using game_ssao_stage = graphic::compositor::post_process_pass_with_ubo<game_ssao_params_gpu>;

export
struct game_oit_pass_chain{
	graphic::compositor::pass_data* emit_pass{};
	game_oit_emit_stage* emit_stage{};
	graphic::compositor::pass_data* blend_pass{};
	graphic::compositor::post_process_stage* blend_stage{};
};

export
struct game_world_post_process_chain{
	graphic::compositor::pass_data* ssao_pass{};
	game_ssao_stage* ssao_stage{};
	graphic::compositor::pass_data* world_merge_pass{};
	graphic::compositor::post_process_stage* world_merge_stage{};
};

export
[[nodiscard]] game_oit_pass_chain add_game_oit_pass_chain(
	graphic::compositor::manager& manager,
	graphic::compositor::post_process_meta&& emit_meta,
	graphic::compositor::post_process_meta&& blend_meta,
	graphic::compositor::resource_entity_external& input_background,
	graphic::compositor::resource_entity_external& shape_input,
	graphic::compositor::resource_entity_external& tile_mask_input,
	graphic::compositor::resource_entity_external& debug_line_input,
	graphic::compositor::resource_entity_external& debug_ring_input,
		graphic::compositor::resource_entity_external& debug_closed_polyline_input,
		graphic::compositor::resource_entity_external& debug_vertex_input){
	auto emit = manager.add_pass<game_oit_emit_stage>(std::move(emit_meta));
	emit.id()->add_input({
		{input_background, 0},
		{shape_input, 1},
		{tile_mask_input, 2},
		{debug_line_input, 3},
		{debug_ring_input, 4},
		{debug_closed_polyline_input, 5},
		{debug_vertex_input, 6}
	});

	auto blend = manager.add_pass<graphic::compositor::post_process_stage>(std::move(blend_meta));
	blend.id()->add_dep({
		{emit.id(), 0, 0},
		{emit.id(), 1, 1},
		{emit.id(), 2, 2},
		{emit.id(), 3, 3},
		{emit.id(), 4, 4}
	});

	return {
		.emit_pass = emit.id(),
		.emit_stage = &emit.data,
		.blend_pass = blend.id(),
		.blend_stage = &blend.data
	};
}

export
[[nodiscard]] game_world_post_process_chain add_game_world_post_process_chain(
	graphic::compositor::manager& manager,
	graphic::compositor::post_process_meta&& ssao_meta,
	graphic::compositor::post_process_meta&& world_merge_meta,
	const game_oit_pass_chain& oit,
	graphic::compositor::pass_data& bloom_pass,
	const VkSampler sampler_blit){
	auto ssao = manager.add_pass<game_ssao_stage>(std::move(ssao_meta));
	ssao.data.set_sampler_at_binding(1, sampler_blit);
	ssao.data.set_sampler_at_binding(2, sampler_blit);
	ssao.id()->add_dep({
		{oit.emit_pass, 2, 0},
		{oit.blend_pass, 1, 1}
	});

	auto world_merge = manager.add_pass<graphic::compositor::post_process_stage>(std::move(world_merge_meta));
	world_merge.id()->add_dep({
		{oit.blend_pass, 0, 0},
		{oit.blend_pass, 1, 1},
		{&bloom_pass, 0, 2},
		{ssao.id(), 0, 3}
	});

	return {
		.ssao_pass = ssao.id(),
		.ssao_stage = &ssao.data,
		.world_merge_pass = world_merge.id(),
		.world_merge_stage = &world_merge.data
	};
}
}
