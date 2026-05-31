#include <cstdlib>
#include <cstdio>

import std;
import mo_yanxi.game.instance;
import mo_yanxi.game.physics;
import mo_yanxi.game.runtime.game_render_device;
import mo_yanxi.game.runtime.game_post_process;

namespace{
	using namespace mo_yanxi::game;
	using namespace mo_yanxi::game::ecs;
	namespace math = mo_yanxi::math;

	using physics_desc = std::tuple<chunk_meta, mech_motion, collider, physics_body>;

	entity_id spawn_physics_entity(
		component_manager& manager,
		const math::vec2 position,
		const physics::collision_shape& shape,
		physics_body body,
		const math::vec2 velocity = {}){
		tuple_to_comp_t<physics_desc> components{};
		components.template get<mech_motion>().trans.vec = position;
		components.template get<mech_motion>().vel.vec = velocity;
		components.template get<collider>().shape = shape.to_record();
		components.template get<physics_body>() = body;
		return manager.spawn<physics_desc>(std::move(components));
	}
}

int main(){
	using namespace mo_yanxi::game;
	using namespace mo_yanxi::game::ecs;

	{
		game_instance demo_instance{};
		demo_instance.initialize();
		const auto snapshot = demo_instance.latest_render_snapshot();
		if(snapshot.collision_shapes.size() < 4){
			std::println(stderr, "default render scene did not publish enough debug shapes");
			return EXIT_FAILURE;
		}
		if(!snapshot.effects.bloom_enabled || !snapshot.effects.transparent_overlap_enabled){
			std::println(stderr, "default render effects were not enabled");
			return EXIT_FAILURE;
		}
		const auto submission = mo_yanxi::game::make_game_render_submission(snapshot, math::vec2{1280.f, 720.f});
		if(!submission.valid_extent() || submission.collision_shapes.size() != snapshot.collision_shapes.size()){
			std::println(stderr, "default render submission did not preserve visible debug shapes");
			return EXIT_FAILURE;
		}
		if(submission.effects.oit_layout.width != 1280u || submission.effects.oit_layout.height != 720u){
			std::println(stderr, "render submission did not update OIT layout for the target extent");
			return EXIT_FAILURE;
		}
		const auto frame = mo_yanxi::game::make_game_render_frame(submission);
		if(!frame.valid_extent()
			|| frame.frame_index() != submission.frame_index
			|| frame.submission.collision_shapes.size() != submission.collision_shapes.size()){
			std::println(stderr, "game render frame did not preserve submission state");
			return EXIT_FAILURE;
		}
		const auto debug_draw_packet = mo_yanxi::game::make_game_debug_draw_packet(submission);
		if(debug_draw_packet.empty()
			|| debug_draw_packet.rings.empty()
			|| debug_draw_packet.lines.empty()
			|| debug_draw_packet.closed_polylines.empty()
			|| debug_draw_packet.vertices.empty()){
			std::println(stderr, "default render scene did not build a complete game debug draw packet");
			return EXIT_FAILURE;
		}
		const auto device_debug_draw_packet =
			mo_yanxi::game::game_2d_render_device::make_frame_debug_draw_packet(submission);
		if(device_debug_draw_packet.lines.size() != debug_draw_packet.lines.size()
			|| device_debug_draw_packet.rings.size() != debug_draw_packet.rings.size()
			|| device_debug_draw_packet.closed_polylines.size() != debug_draw_packet.closed_polylines.size()
			|| device_debug_draw_packet.vertices.size() != debug_draw_packet.vertices.size()){
			std::println(stderr, "game render device did not build the same debug draw packet");
			return EXIT_FAILURE;
		}
		const auto device_frame = mo_yanxi::game::game_2d_render_device::make_frame(submission);
		if(device_frame.debug_draw.lines.size() != frame.debug_draw.lines.size()
			|| device_frame.debug_draw.rings.size() != frame.debug_draw.rings.size()
			|| device_frame.debug_draw.closed_polylines.size() != frame.debug_draw.closed_polylines.size()
			|| device_frame.debug_draw.vertices.size() != frame.debug_draw.vertices.size()){
			std::println(stderr, "game render device frame did not reuse shared CPU frame data");
			return EXIT_FAILURE;
		}
		const auto debug_gpu_packet = mo_yanxi::game::make_game_debug_draw_gpu_packet(frame.debug_draw);
		if(debug_gpu_packet.empty()
			|| debug_gpu_packet.params.line_count != frame.debug_draw.lines.size()
			|| debug_gpu_packet.params.ring_count != frame.debug_draw.rings.size()
			|| debug_gpu_packet.params.closed_polyline_count != frame.debug_draw.closed_polylines.size()
			|| debug_gpu_packet.params.vertex_count != frame.debug_draw.vertices.size()){
			std::println(stderr, "game debug draw GPU packet did not preserve CPU packet counts");
			return EXIT_FAILURE;
		}
		if(debug_gpu_packet.lines.front().color[3] <= 0.f
			|| debug_gpu_packet.rings.front().segments < 6u
			|| debug_gpu_packet.closed_polylines.front().vertex_count < 3u){
			std::println(stderr, "game debug draw GPU packet carried invalid draw instance data");
			return EXIT_FAILURE;
		}
		const auto device_debug_gpu_packet =
			mo_yanxi::game::game_2d_render_device::make_frame_debug_draw_gpu_packet(frame);
		if(device_debug_gpu_packet.params != debug_gpu_packet.params){
			std::println(stderr, "game render device did not build matching debug draw GPU packet params");
			return EXIT_FAILURE;
		}
		const auto emit_packet = mo_yanxi::game::make_game_oit_emit_packet_gpu(submission);
		if(emit_packet.params.shape_count <= 0 || emit_packet.shape_storage.shapes[0].enabled == 0){
			std::println(stderr, "default render scene did not feed OIT emit params");
			return EXIT_FAILURE;
		}
		if(mo_yanxi::game::get_game_oit_emit_shape_storage_buffer_size() != sizeof(game_oit_emit_shape_storage_gpu)){
			std::println(stderr, "OIT shape storage buffer size is incorrect");
			return EXIT_FAILURE;
		}
		if(mo_yanxi::game::get_game_oit_tile_mask_storage_buffer_size()
			!= sizeof(game_oit_tile_mask_gpu) * game_oit_emit_params_gpu::max_tile_count){
			std::println(stderr, "OIT tile mask storage buffer size is incorrect");
			return EXIT_FAILURE;
		}
		if(emit_packet.params.tile_grid_width != 80
			|| emit_packet.params.tile_grid_height != 45
			|| emit_packet.params.use_tile_mask == 0){
			std::println(stderr, "OIT emit params did not publish tile bin metadata");
			return EXIT_FAILURE;
		}
		if(std::ranges::none_of(
			emit_packet.tile_masks,
			[](const game_oit_tile_mask_gpu& tile){
				return std::ranges::any_of(tile.shape_words, [](const std::uint32_t word){
					return word != 0u;
				});
			})){
			std::println(stderr, "OIT tile bins did not reference any emitted shapes");
			return EXIT_FAILURE;
		}
		const auto shape_kinds = emit_packet.shape_storage.shapes
			| std::views::take(static_cast<std::size_t>(emit_packet.params.shape_count))
			| std::views::transform(&game_oit_emit_shape_gpu::shape_kind);
		if(!std::ranges::contains(shape_kinds, 0)
			|| !std::ranges::contains(shape_kinds, 1)
			|| !std::ranges::contains(shape_kinds, 2)
			|| !std::ranges::contains(shape_kinds, 3)){
			std::println(stderr, "default render scene did not feed circle/capsule/box/polygon OIT params");
			return EXIT_FAILURE;
		}
		if(std::ranges::none_of(
			emit_packet.shape_storage.shapes | std::views::take(static_cast<std::size_t>(emit_packet.params.shape_count)),
			[](const game_oit_emit_shape_gpu& shape){
				return shape.shape_kind == 3 && shape.polygon_vertex_count >= 3;
			})){
			std::println(stderr, "default render polygon did not feed OIT polygon vertices");
			return EXIT_FAILURE;
		}
		if(std::ranges::any_of(
			emit_packet.shape_storage.shapes | std::views::take(static_cast<std::size_t>(emit_packet.params.shape_count)),
			[](const game_oit_emit_shape_gpu& shape){
				return shape.half_extent.x <= 0.f || shape.half_extent.y <= 0.f;
			})){
			std::println(stderr, "OIT emit shape did not publish conservative bounds");
			return EXIT_FAILURE;
		}
		if(!std::ranges::is_sorted(
			emit_packet.shape_storage.shapes | std::views::take(static_cast<std::size_t>(emit_packet.params.shape_count)),
			{},
			&game_oit_emit_shape_gpu::depth)){
			std::println(stderr, "OIT emit shapes were not sorted by GPU depth");
			return EXIT_FAILURE;
		}
		if(mo_yanxi::game::make_game_emit_depth(std::numeric_limits<float>::quiet_NaN(), 0) != 0.5f){
			std::println(stderr, "OIT emit depth did not sanitize invalid style depth");
			return EXIT_FAILURE;
		}
		auto many_shape_snapshot = snapshot;
		while(many_shape_snapshot.collision_shapes.size() < game_oit_emit_params_gpu::max_shapes + 8u){
			many_shape_snapshot.collision_shapes.push_back(
				snapshot.collision_shapes[many_shape_snapshot.collision_shapes.size() % snapshot.collision_shapes.size()]);
		}
		const auto many_submission = mo_yanxi::game::make_game_render_submission(
			many_shape_snapshot,
			math::vec2{1280.f, 720.f});
		const auto many_emit_packet = mo_yanxi::game::make_game_oit_emit_packet_gpu(many_submission);
		if(many_emit_packet.params.shape_count != static_cast<std::int32_t>(game_oit_emit_params_gpu::max_shapes)){
			std::println(stderr, "OIT emit params did not keep the expanded shape packet capacity");
			return EXIT_FAILURE;
		}
		if(many_emit_packet.params.shape_count <= static_cast<std::int32_t>(game_oit_buffer_layout::default_nodes_per_pixel)){
			std::println(stderr, "OIT emit params are still capped by per-pixel node capacity");
			return EXIT_FAILURE;
		}
		const auto packet = mo_yanxi::game::game_2d_render_device::make_frame_packet(submission);
		if(packet.oit_emit.params.shape_count != emit_packet.params.shape_count || packet.ssao.sample_count != 40){
			std::println(stderr, "game render GPU packet did not carry OIT/SSAO params");
			return EXIT_FAILURE;
		}
		if(packet.oit_emit.params.bloom_enabled != 1
			|| packet.oit_emit.params.transparent_overlap_enabled != 1
			|| packet.ssao.enabled != 0){
			std::println(stderr, "game render GPU packet did not reflect default effect toggles");
			return EXIT_FAILURE;
		}
		auto disabled_effect_snapshot = snapshot;
		disabled_effect_snapshot.effects.bloom_enabled = false;
		disabled_effect_snapshot.effects.transparent_overlap_enabled = false;
		disabled_effect_snapshot.effects.ssao_enabled = true;
		const auto disabled_effect_packet = mo_yanxi::game::game_2d_render_device::make_frame_packet(
			disabled_effect_snapshot,
			math::vec2{1280.f, 720.f});
		if(disabled_effect_packet.oit_emit.params.bloom_enabled != 0
			|| disabled_effect_packet.oit_emit.params.bloom_hint_strength != 0.f
			|| disabled_effect_packet.oit_emit.params.transparent_overlap_enabled != 0
			|| disabled_effect_packet.ssao.enabled != 1){
			std::println(stderr, "game render GPU packet did not propagate effect toggle overrides");
			return EXIT_FAILURE;
		}
		const auto packet_from_snapshot = mo_yanxi::game::game_2d_render_device::make_frame_packet(
			snapshot,
			math::vec2{1280.f, 720.f});
		if(packet_from_snapshot.frame_index != snapshot.frame_index
			|| packet_from_snapshot.oit_emit.params.shape_count != packet.oit_emit.params.shape_count){
			std::println(stderr, "game render device frame packet entry is inconsistent");
			return EXIT_FAILURE;
		}
		const auto high_res_emit_packet = mo_yanxi::game::make_game_oit_emit_packet_gpu(
			mo_yanxi::game::make_game_render_submission(snapshot, math::vec2{3840.f, 2160.f}));
		if(high_res_emit_packet.params.tile_grid_width != 240
			|| high_res_emit_packet.params.tile_grid_height != 135
			|| high_res_emit_packet.params.use_tile_mask == 0
			|| high_res_emit_packet.tile_masks.size() != 240u * 135u){
			std::println(stderr, "OIT tile bins did not stay active at 4K extent");
			return EXIT_FAILURE;
		}
		const auto oversized_emit_packet = mo_yanxi::game::make_game_oit_emit_packet_gpu(
			mo_yanxi::game::make_game_render_submission(snapshot, math::vec2{8192.f, 8192.f}));
		if(oversized_emit_packet.params.use_tile_mask != 0 || !oversized_emit_packet.tile_masks.empty()){
			std::println(stderr, "OIT tile bins did not fall back when the tile grid exceeded capacity");
			return EXIT_FAILURE;
		}
		auto empty_snapshot = snapshot;
		empty_snapshot.collision_shapes.clear();
		const auto empty_emit_packet = mo_yanxi::game::make_game_oit_emit_packet_gpu(
			mo_yanxi::game::make_game_render_submission(empty_snapshot, math::vec2{1280.f, 720.f}));
		if(empty_emit_packet.params.shape_count != 0
			|| empty_emit_packet.params.use_tile_mask != 0
			|| !empty_emit_packet.tile_masks.empty()){
			std::println(stderr, "empty OIT packet still allocated tile bins");
			return EXIT_FAILURE;
		}
		const auto invalid_submission = mo_yanxi::game::make_game_render_submission(snapshot, math::vec2{0.f, 720.f});
		if(invalid_submission.valid_extent()
			|| mo_yanxi::game::make_game_oit_emit_packet_gpu(invalid_submission).params.shape_count != 0){
			std::println(stderr, "invalid render extent still produced drawable OIT params");
			return EXIT_FAILURE;
		}
		demo_instance.shutdown();
	}

	{
		const auto layout = mo_yanxi::game::make_game_oit_buffer_layout({1280.f, 720.f});
		if(layout.node_capacity() != 1280u * 720u * game_oit_buffer_layout::default_nodes_per_pixel){
			std::println(stderr, "OIT buffer layout capacity is incorrect");
			return EXIT_FAILURE;
		}
		if(layout.node_storage_bytes() != layout.node_capacity() * sizeof(game_oit_node_gpu)){
			std::println(stderr, "OIT node storage size is incorrect");
			return EXIT_FAILURE;
		}
		if(layout.storage_buffer_bytes() != layout.node_storage_bytes() + sizeof(game_oit_statistics_gpu)){
			std::println(stderr, "OIT storage buffer size is incorrect");
			return EXIT_FAILURE;
		}
		if(mo_yanxi::game::get_game_oit_node_storage_buffer_size({1280u, 720u}) != layout.node_storage_bytes()){
			std::println(stderr, "OIT compositor node buffer size is incorrect");
			return EXIT_FAILURE;
		}
		if(mo_yanxi::game::get_game_oit_node_storage_offset() != sizeof(game_oit_statistics_gpu)){
			std::println(stderr, "OIT node storage offset is incorrect");
			return EXIT_FAILURE;
		}

		const auto ssao_kernel = mo_yanxi::game::make_game_ssao_kernel({1280.f, 720.f});
		if(ssao_kernel.sample_count != 40u){
			std::println(stderr, "SSAO kernel sample count is incorrect");
			return EXIT_FAILURE;
		}
	}

	game_instance instance{};
	entity_id dynamic_entity{};
	entity_id static_entity{};

	instance.post_command([&](game_world& world){
		auto& manager = world.components();
		const auto shape = physics::make_box_collision_shape({0.5f, 0.5f});
		dynamic_entity = spawn_physics_entity(
			manager,
			{-0.25f, 0.f},
			shape,
			physics_body::make_dynamic(1.f),
			{1.f, 0.f});
		static_entity = spawn_physics_entity(
			manager,
			{0.25f, 0.f},
			shape,
			physics_body::make_static());
	});

	instance.update(1.f / 30.f);

	if(dynamic_entity == nullptr || static_entity == nullptr){
		std::println(stderr, "spawn command did not run");
		return EXIT_FAILURE;
	}
	if(!dynamic_entity.is_inserted() || !static_entity.is_inserted()){
		std::println(stderr, "spawned entities were not committed");
		return EXIT_FAILURE;
	}

	const auto events = instance.world().physics().contact_events();
	if(std::ranges::none_of(events, [&](const physics_contact_event& event){
		return event.key == physics_contact_key::ordered(dynamic_entity, static_entity);
	})){
		std::println(stderr, "physics contact event was not emitted");
		return EXIT_FAILURE;
	}

	instance.reset();
	std::size_t visited{};
	instance.components().each([&](const mech_motion&){
		++visited;
	});
	if(visited != 0){
		std::println(stderr, "reset did not clear the world");
		return EXIT_FAILURE;
	}
	if(!instance.world().physics().contact_events().empty()){
		std::println(stderr, "reset did not clear physics contacts");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
