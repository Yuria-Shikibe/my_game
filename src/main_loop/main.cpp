#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdlib>
#include <cstdio>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

import std;

import mo_yanxi.vk;
import mo_yanxi.vk.cmd;

import mo_yanxi.backend.vulkan.context;
import mo_yanxi.backend.glfw.window;
import mo_yanxi.backend.application_timer;
import mo_yanxi.backend.vulkan.renderer;

import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.graphic.image_atlas;
import mo_yanxi.graphic.compositor.manager;
import mo_yanxi.graphic.compositor.post_process_pass;
import mo_yanxi.graphic.compositor.post_process_pass_with_ubo;
import mo_yanxi.graphic.compositor.bloom;
import mo_yanxi.graphic.compositor.fullscreen_present_pass;

import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.elem.group;
import mo_yanxi.gui.global;
import mo_yanxi.gui.assets.manager;
import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.gui.fx.instruction_extension;

import mo_yanxi.gui.default_config.assets;
import mo_yanxi.gui.image_regions;

import mo_yanxi.font;
import mo_yanxi.font.plat;
import mo_yanxi.font.manager;
import mo_yanxi.typesetting;
import mo_yanxi.typesetting.segmented_layout;
import mo_yanxi.typesetting.util;

import mo_yanxi.react_flow.common;
import mo_yanxi.react_flow;

import mo_yanxi.core.platform;

import mo_yanxi.game.profile.runtime;
import mo_yanxi.gui.default_config.main_loop;
import mo_yanxi.gui.examples.default_config.colored_cerr;
import mo_yanxi.gui.examples.default_config.font_styles;

import mo_yanxi.gui.game_examples;
import mo_yanxi.gui.game_examples.loop_exec;
import mo_yanxi.game.runtime.game_render_device;
import mo_yanxi.log;


struct alignas(16) high_light_filter_args{
	float threshold{1.3f};
	float smoothness{.5f};
	float max_brightness{10};
};

struct alignas(16) tonemap_args{
	float exposure{1};
	float contrast{1};
	float gamma{1};
	float saturation{1};
};

bool game_has_runtime_assets(const std::filesystem::path& base){
	return std::filesystem::exists(base / "assets/shader/spv/ui.draw.vert.spv")
		&& std::filesystem::exists(base / "assets/font/SourceHanSansCN-Regular.otf");
}

std::optional<std::string> game_get_environment_variable(const char* name){
	char* value{};
	std::size_t size{};
	if(_dupenv_s(&value, &size, name) != 0 || value == nullptr){
		return std::nullopt;
	}

	std::string result{value};
	std::free(value);
	return result;
}

void game_configure_runtime_working_directory(){
	const auto cwd = std::filesystem::current_path();
	if(game_has_runtime_assets(cwd)){
		return;
	}

	std::array candidates{
		cwd / "properties",
		cwd / "external/xrgui/properties"
	};
	for(const auto& candidate : candidates){
		if(game_has_runtime_assets(candidate)){
			mo_yanxi::log::info(
				{"Assets"},
				"using runtime asset directory {}",
				candidate.string());
			std::filesystem::current_path(candidate);
			return;
		}
	}

	std::string checked = cwd.string();
	for(const auto& candidate : candidates){
		checked += "; ";
		checked += candidate.string();
	}
	throw std::runtime_error{std::format("Runtime assets not found. Checked: {}", checked)};
}

void game_initialize_logging(){
	if(auto level = game_get_environment_variable("MY_GAME_LOG_LEVEL"); level && !level->empty()){
		mo_yanxi::log::set_min_level(mo_yanxi::log::parse_level(*level));
	}
	if(auto path = game_get_environment_variable("MY_GAME_TRACE_FILE"); path && !path->empty()){
		mo_yanxi::log::add_file_sink(std::filesystem::path{*path});
		mo_yanxi::log::set_min_level(mo_yanxi::log::level::trace);
	}
}

void game_trace(std::string_view message){
	mo_yanxi::log::trace({"GameTrace"}, "{}", message);
}

template <
	typename AdvanceGame,
	typename BeginGameRender,
	typename WaitGameRender,
	typename GetGameCommandBuffer,
	typename SetGameGpuFence>
void app_run(
	mo_yanxi::gui::example::main_loop_type& main_loop,
	std::vector<mo_yanxi::vk::command_buffer>& compositor_cmds,
	AdvanceGame&& advance_game,
	BeginGameRender&& begin_game_render,
	WaitGameRender&& wait_game_render,
	GetGameCommandBuffer&& get_game_command_buffer,
	SetGameGpuFence&& set_game_gpu_fence,
	std::shared_ptr<mo_yanxi::game::profile::profile_session> profile_session
){
	using namespace mo_yanxi;
	using profile_clock = std::chrono::steady_clock;

	backend::application_timer timer{backend::application_timer<double>::get_default()};

	auto& current_focus = main_loop.get_scene();
	log::info({"App"}, "entering main loop");
	game_trace("app_run: entering main loop");
	main_loop.wait_term_and_reset();

	auto& ctx = main_loop.get_ctx();
	std::uint64_t frame_index{};
	auto* profile_output = profile_session.get();
	while(!ctx.window().should_close()){
		const auto current_frame = frame_index++;
		game::profile::profile_main_frame_metrics profile_metrics{
			.frame_index = current_frame
		};
		const auto frame_begin_time = profile_clock::now();
		auto phase_begin_time = frame_begin_time;
		auto record_profile_phase = [&](double& out){
			if(profile_output == nullptr){
				return;
			}
			const auto now = profile_clock::now();
			out = std::chrono::duration<double, std::milli>(now - phase_begin_time).count();
			phase_begin_time = now;
		};
		game_trace(std::format("frame {}: begin", current_frame));
		ctx.window().poll_events();
		timer.fetch_time();
		const auto frame_delta = timer.global_delta();
		profile_metrics.delta_seconds = static_cast<double>(frame_delta);
		record_profile_phase(profile_metrics.poll_events_ms);
		//
		gui::global::event_queue.push_frame_split(frame_delta);
		std::invoke(advance_game, static_cast<double>(frame_delta));
		record_profile_phase(profile_metrics.advance_game_ms);
		game_trace(std::format("frame {}: acquiring output", current_frame));
		auto output_token = ctx.acquire_output_frame();
		record_profile_phase(profile_metrics.acquire_ms);
		if(!output_token.acquired){
			game_trace(std::format("frame {}: acquire skipped", current_frame));
			if(profile_output != nullptr){
				profile_metrics.acquired = false;
				profile_metrics.total_frame_ms = std::chrono::duration<double, std::milli>(
					profile_clock::now() - frame_begin_time).count();
				profile_output->write_frame(profile_metrics);
				if(profile_output->should_stop_after_frame(current_frame)){
					break;
				}
			}
			continue;
		}
		profile_metrics.acquired = true;
		profile_metrics.image_index = output_token.image.index;
		game_trace(std::format("frame {}: acquired image {}", current_frame, output_token.image.index));

		game_trace(std::format("frame {}: request game render begin", current_frame));
		const auto game_render_request = std::invoke(begin_game_render);
		record_profile_phase(profile_metrics.request_game_render_ms);
		game_trace(std::format("frame {}: request game render end {}", current_frame, game_render_request));
		game_trace(std::format("frame {}: permit burst begin", current_frame));
		main_loop.permit_burst();
		record_profile_phase(profile_metrics.permit_burst_ms);
		game_trace(std::format("frame {}: permit burst end", current_frame));
		game_trace(std::format("frame {}: consume async queue begin", current_frame));
		current_focus.get_output_communicate_async_task_queue(0).consume();
		record_profile_phase(profile_metrics.consume_async_queue_ms);
		game_trace(std::format("frame {}: consume async queue end", current_frame));

		game_trace(std::format("frame {}: wait term begin", current_frame));
		main_loop.wait_term();
		record_profile_phase(profile_metrics.wait_gui_ms);
		game_trace(std::format("frame {}: wait term end", current_frame));
		game_trace(std::format("frame {}: wait game render begin {}", current_frame, game_render_request));
		std::invoke(wait_game_render, game_render_request);
		record_profile_phase(profile_metrics.wait_game_render_ms);
		game_trace(std::format("frame {}: wait game render end {}", current_frame, game_render_request));

		std::array<VkCommandBuffer, 3> buffers{
			main_loop.get_renderer().get_valid_cmd_buf(),
			std::invoke(get_game_command_buffer),
			compositor_cmds.at(output_token.image.index)
		};
		game_trace(std::format("frame {}: submit begin image {}", current_frame, output_token.image.index));
		vk::cmd::submit_command(
			ctx.graphic_queue(),
			buffers,
			output_token.frame_fence,
			output_token.acquire_semaphore,
			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			output_token.render_finished_semaphore,
			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
		record_profile_phase(profile_metrics.submit_ms);
		game_trace(std::format("frame {}: submit end image {}", current_frame, output_token.image.index));
		main_loop.get_renderer().set_current_external_submit_fence(output_token.frame_fence);
		std::invoke(set_game_gpu_fence, output_token.frame_fence);
		game_trace(std::format("frame {}: present begin image {}", current_frame, output_token.image.index));
		ctx.present_output_frame(output_token);
		record_profile_phase(profile_metrics.present_ms);
		game_trace(std::format("frame {}: present end image {}", current_frame, output_token.image.index));
		game_trace(std::format("frame {}: reset term begin", current_frame));
		main_loop.reset_term();
		record_profile_phase(profile_metrics.reset_term_ms);
		game_trace(std::format("frame {}: reset term end", current_frame));
		if(profile_output != nullptr){
			profile_metrics.total_frame_ms = std::chrono::duration<double, std::milli>(
				profile_clock::now() - frame_begin_time).count();
			profile_output->write_frame(profile_metrics);
			if(profile_output->should_stop_after_frame(current_frame)){
				break;
			}
		}
	}
}

void prepare(
	mo_yanxi::backend::vulkan::context& ctx,
	std::shared_ptr<mo_yanxi::game::profile::profile_session> profile_session){
	using namespace mo_yanxi;
	using namespace graphic;

	game_trace("prepare: begin");
	const auto shader_spv_path = std::filesystem::current_path().append("assets/shader/spv").make_preferred();

	log::info({"GUI"}, "core initialize");
	gui::global::initialize();
	gui::global::initialize_assets_manager(gui::global::manager.get_arena_id());
	log::info({"GUI"}, "core initialize done");

#pragma region InitRenderer
	log::info({"GUI"}, "renderer initialize");
	vk::sampler sampler_ui{ctx.get_device(), vk::preset::ui_texture_sampler};
	//renderer should belong to main loop actually
	auto renderer = [&]() -> backend::vulkan::renderer{
		vk::shader_module draw_shader_vert{ctx.get_device(), shader_spv_path / "ui.draw.vert.spv"};
		vk::shader_module draw_shader_frag_basic{ctx.get_device(), shader_spv_path / "ui.draw.frag_basic.spv"};
		vk::shader_module draw_shader_frag_outlined{ctx.get_device(), shader_spv_path / "ui.draw.frag_outlined.spv"};
		vk::shader_module draw_shader_coord{ctx.get_device(), shader_spv_path / "ui.draw.coord_draw.spv"};
		vk::shader_module draw_shader_mask{ctx.get_device(), shader_spv_path / "ui.draw.frag_mask.spv"};
		vk::shader_module draw_shader_mask_apply{ctx.get_device(), shader_spv_path / "ui.draw.frag_mask_apply.spv"};

		vk::shader_module blit_shader_merge{ctx.get_device(), shader_spv_path / "ui.blit.basic.spv"};
		vk::shader_module blit_shader_blend{ctx.get_device(), shader_spv_path / "ui.blit.alpha_blend.spv"};
		vk::shader_module blit_shader_inverse{ctx.get_device(), shader_spv_path / "ui.blit.inverse.spv"};

		vk::shader_module shader_instr_resolve{ctx.get_device(), shader_spv_path / "ui.instruction_resolve_comp.spv"};

		using namespace backend::vulkan;
		return {
				renderer_create_info{
					.allocator_usage = ctx.get_allocator(),
					.command_pool = ctx.get_graphic_command_pool(),
					.sampler = sampler_ui,
					.attachment_draw_config = {
						{
							draw_attachment_config{
								.attachment = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT}
							},
							draw_attachment_config{
								.attachment = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT}
							},
						},
						// VK_SAMPLE_COUNT_4_BIT
					},
					.attachment_blit_config = {
						{
							attachment_config{VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
							attachment_config{VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
							attachment_config{VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
						}
					},
					.draw_pipe_config = graphic_pipeline_create_config{
						{
							//basic draw
							graphic_pipeline_create_config::config{
								{
									draw_shader_vert.get_stage_bundle(VK_SHADER_STAGE_VERTEX_BIT, "main_vert"),
									draw_shader_frag_basic.get_stage_bundle(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
								},
								graphic_pipeline_option{
									false, mask_usage::ignore, {0b1}, {},
									{
										{vk::blending::premultiplied_alpha_blend}, blend_dynamic_flags::equation | blend_dynamic_flags::write_flag
									}
								}
							},
							//outline sdf
							graphic_pipeline_create_config::config{
								{
									draw_shader_vert.get_stage_bundle(VK_SHADER_STAGE_VERTEX_BIT, "main_vert"),
									draw_shader_frag_outlined.get_stage_bundle(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
								},
								graphic_pipeline_option{
									false, mask_usage::ignore, {0b1}, {},
									{
										{vk::blending::premultiplied_alpha_blend}
									}
								}
							},
							//coordinate draw
							graphic_pipeline_create_config::config{
								{
									draw_shader_coord.get_stage_bundle(VK_SHADER_STAGE_VERTEX_BIT, "main_vert"),
									draw_shader_coord.get_stage_bundle(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
								},
								graphic_pipeline_option{
									false, mask_usage::ignore, {0b1}, {},
									{
										{vk::blending::premultiplied_alpha_blend}
									}
								}
							},
							//mask draw
							graphic_pipeline_create_config::config{
								{
									draw_shader_vert.get_stage_bundle(VK_SHADER_STAGE_VERTEX_BIT, "main_vert"),
									draw_shader_mask.get_stage_bundle(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
								},
								graphic_pipeline_option{
									false, mask_usage::write, {}, {},
									{
										{vk::blending::mask_draw}, blend_dynamic_flags::equation
									}
								}
							},
							//pipeline apply
							graphic_pipeline_create_config::config{
								{
									draw_shader_vert.get_stage_bundle(VK_SHADER_STAGE_VERTEX_BIT, "main_vert"),
									draw_shader_mask_apply.get_stage_bundle(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
								},
								graphic_pipeline_option{
									false, mask_usage::read, {0b1}, {},
									{
										{vk::blending::max_alpha_blend}
									}
								}
							},
						},
						{}
					},
					.blit_pipe_config = compute_pipeline_create_config{
						{
							compute_pipeline_create_config::config{
								.shader_bundle = blit_shader_merge.get_stage_bundle(VK_SHADER_STAGE_COMPUTE_BIT),
								.option = {
									.inout = compute_pipeline_blit_inout_config{
										{
											{0, 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
											{1, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
										},
										{
											{2, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
											{3, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
										}
									},
								}
							},
							compute_pipeline_create_config::config{
								.shader_bundle = blit_shader_blend.get_stage_bundle(VK_SHADER_STAGE_COMPUTE_BIT),
								.option = {
									.inout = compute_pipeline_blit_inout_config{
										{
											{0, 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
										},
										{
											{1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
										}
									},
								}
							},
							compute_pipeline_create_config::config{
								.shader_bundle = blit_shader_inverse.get_stage_bundle(VK_SHADER_STAGE_COMPUTE_BIT),
								.option = {
									.inout = compute_pipeline_blit_inout_config{
										{
											{0, 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
										},
										{
											{1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
										}
									},
								}
							},
						},
						{
							compute_pipeline_blit_inout_config{
								{
									{0, 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
								},
								{
									{1, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
								}
							}
						}
					},
					.resolver_shader_stage = shader_instr_resolve.get_create_info(VK_SHADER_STAGE_COMPUTE_BIT),
					.stride_config = {
						.vertex_stride = sizeof(gui_vertex_mock),
						.primitive_stride = sizeof(gui_primitive_mock),
					}
				}
			};
	}();
	log::info({"GUI"}, "renderer initialize done");
	game_trace("prepare: renderer initialized");

#pragma endregion

#pragma region LoadResource
	log::info({"GUI"}, "image atlas initialize");
	image_atlas image_atlas{
			ctx,
			ctx.graphic_family(),
			ctx.get_device().graphic_queue(1),
			renderer.get_heap_dynamic_image_section()
		};
	log::info({"GUI"}, "image atlas initialize done");

	log::info({"GUI"}, "font manager initialize");
	font::font_manager font_manager{};
	gui::example::init_font_manager(font_manager, image_atlas);
	log::info({"GUI"}, "font manager initialize done");

	{
		log::info({"GUI"}, "load logo image");
		auto& icon_p = image_atlas.create_image_page("tex.logo", {
			.extent = {1920, 1080},
			.format = VK_FORMAT_R8G8B8A8_SRGB,
			.margin = 0
		});
		const auto image_path = std::filesystem::current_path().append("assets/images").make_preferred();

		auto rst = icon_p.register_named_region("logo", image_load_description{
			bitmap_path_load{(image_path / "logo.png").string()}
		}, true);

		gui::assets::builtin::get_page().insert(gui::assets::builtin::shape_id::logo, gui::constant_image_region_borrow{rst.region});
	}

	{
		log::info({"GUI"}, "generate icons and shapes");

		gui::example::generate_default_shapes(image_atlas);
		gui::example::load_default_icons(image_atlas);
	}
#pragma endregion

#pragma region SetupRenderGraph
	log::info({"Compositor"}, "initialize");

	compositor::manager manager{ctx.get_allocator()};
	vk::shader_module shader_filter_high_light = {
			ctx.get_device(), shader_spv_path / "post_process.highlight_extract.spv"
		};
	vk::shader_module shader_merge = {ctx.get_device(), shader_spv_path / "ui.merge.spv"};
	vk::shader_module shader_present_vert = {
		ctx.get_device(), shader_spv_path / "post_process.fullscreen_present.vert.spv"
	};
	vk::shader_module shader_present_frag = {
		ctx.get_device(), shader_spv_path / "post_process.fullscreen_present.frag.spv"
	};

	vk::sampler sampler_blit{ctx.get_device(), vk::preset::default_blit_sampler};
	vk::shader_module shader_bloom{ctx.get_device(), shader_spv_path / "post_process.bloom.spv"};
	vk::command_pool game_graphic_command_pool{
		ctx.get_device(),
		ctx.graphic_family(),
		VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
	};
	vk::allocator game_allocator = ctx.create_allocator(VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT);
	game::game_2d_renderer game_renderer{
		game_allocator,
		ctx.get_device(),
		game_graphic_command_pool,
		sampler_ui,
		shader_spv_path
	};
	game_renderer.set_profile_session(profile_session);

	auto& ui_input_base = manager.add_external_resource(compositor::resource_entity_external{
			compositor::image_entity{}, compositor::resource_dependency{
				.src_access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				.dst_access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			}
		});

	auto& ui_input_back = manager.add_external_resource(compositor::resource_entity_external{
			compositor::image_entity{}, compositor::resource_dependency{
				.src_access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				.dst_access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			}
		});

	auto& input_background = manager.add_external_resource(game_renderer.make_output_resource());

	auto& present_output = manager.add_external_resource(compositor::resource_entity_external{
			compositor::image_entity{}, compositor::resource_dependency{
				.src_stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
				.src_access = VK_ACCESS_2_NONE,
				.dst_stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
				.dst_access = VK_ACCESS_2_NONE,
				.src_layout = VK_IMAGE_LAYOUT_UNDEFINED,
				.dst_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			}
		});

	auto pass_filter_high_light = manager.add_pass<compositor::post_process_pass_with_ubo<high_light_filter_args>>(
		compositor::post_process_meta{
			{std::move(shader_filter_high_light)}, {
				{{0}, compositor::no_slot, 0},
				{{1}, 0, compositor::no_slot},
			}
		});

	pass_filter_high_light.id()->add_input({{input_background, 0}});


	auto pass_bloom = manager.add_pass<compositor::bloom_pass>(compositor::get_bloom_default_meta({auto{shader_bloom}}, {}));
	pass_bloom.data.set_sampler_at_binding(0, sampler_blit);
	pass_bloom.pass.add_dep({pass_filter_high_light.id(), 0, 0});
	pass_bloom.pass.add_local({1, compositor::no_slot});


	static constexpr VkSpecializationMapEntry SpecEntry{0, 0, 4};
	static constexpr VkBool32 SpecData{true};
	auto pass_blur = manager.add_pass<compositor::bloom_pass>(compositor::get_bloom_default_meta({
			auto{shader_bloom}, "main", VkSpecializationInfo{
				.mapEntryCount = 1,
				.pMapEntries = &SpecEntry,
				.dataSize = sizeof(SpecData),
				.pData = &SpecData
			}
		}, {
			.target_scale = 1,
			.format = VK_FORMAT_R8G8B8A8_UNORM

		}));
	pass_blur.data.set_max_mip_level(5);
	pass_blur.data.set_sampler_at_binding(0, sampler_blit);
	pass_blur.id()->add_input({{input_background, 0}});
	pass_blur.id()->add_local({1, compositor::no_slot});


	auto pass_merge = manager.add_pass<compositor::post_process_stage>(compositor::post_process_meta{
			{std::move(shader_merge)}, {
				{{0}, compositor::no_slot, 0},
				{{1}, 0, compositor::no_slot},
				{{2}, 1, compositor::no_slot},
				{{3}, 2, compositor::no_slot},
				{{4}, 3, compositor::no_slot},
				{{5}, 4, compositor::no_slot},
			}
		});
	pass_merge.data.set_sampler_at_binding(5, sampler_blit);

	pass_merge.id()->add_input({{ui_input_base, 0}});
	pass_merge.id()->add_input({{ui_input_back, 1}});
	pass_merge.id()->add_dep({pass_bloom.id(), 0, 2});
	pass_merge.id()->add_input({{input_background, 3}});
	pass_merge.id()->add_dep({pass_blur.id(), 0, 4});


	auto pass_present = manager.add_pass<compositor::fullscreen_present_stage>(
		compositor::fullscreen_present_shader_info{
			.vertex_shader = std::move(shader_present_vert),
			.fragment_shader = std::move(shader_present_frag),
		},
		ctx.output_image(0).format);
	pass_present.id()->add_dep({pass_merge.id(), 0, 0});
	pass_present.id()->add_output({{present_output, 0}});

	manager.sort();

	renderer.resize({64, 64});
	game_renderer.resize({64, 64});
	ui_input_base.resource = compositor::image_entity{.handle = renderer.get_blit_attachments()[0]};
	ui_input_back.resource = compositor::image_entity{.handle = renderer.get_blit_attachments()[1]};
	input_background = game_renderer.make_output_resource();
	manager.set_frame_count(ctx.output_image_count());
	pass_present.data.set_output_format(ctx.output_image(0).format);
	for(std::uint32_t frame_slot = 0; frame_slot < ctx.output_image_count(); ++frame_slot){
		present_output.set_frame_resource(
			frame_slot,
			compositor::image_entity{.handle = ctx.output_image(frame_slot).handle});
	}
	manager.resize({64, 64}, true);

	pass_blur.data.set_scale(1.25f);
	pass_blur.data.set_mix_factor(.025f);
	pass_blur.data.set_strength(1.f, 1.f);
	log::info({"Compositor"}, "initialize done");
	game_trace("prepare: compositor initialized");
#pragma endregion

#pragma region GuiBindingFn
	auto init_fn = [&](gui::example::main_loop_type& loop) -> gui::example::main_loop_init_return_t {
		gui::example::main_loop_init_return_t ret{};

		auto ui_providers = gui::example::build_main_ui(loop.get_ctx(), loop.get_renderer().create_frontend());
		auto& scene = *ui_providers.scene_ptr;
		ret.main_scene = ui_providers.scene_ptr;

		static constexpr auto post_task = []<typename F>(gui::scene& scene, F&& fn){
			scene.get_output_communicate_async_task_queue(0).post(std::forward<F>(fn));
		};

		auto& bloom_scale = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_bloom.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_scale(val);
				});
			}));
		auto& bloom_src_recv = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_bloom.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_strength_src(val);
				});
			}));
		auto& bloom_dst_recv = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_bloom.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_strength_dst(val);
				});

			}));
		auto& bloom_mix_recv = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_bloom.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_mix_factor(val);
				});
			}));

		auto& highlight_thres_recv = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_filter_high_light.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_ubo_value(&high_light_filter_args::threshold, val);
				});
			}));
		auto& highlight_smooth_recv = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_filter_high_light.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_ubo_value(&high_light_filter_args::smoothness, val);
				});
			}));

		auto& tonemap_contrast = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_present.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_ubo_value(&compositor::fullscreen_present_params::contrast, val);
				});
			}));
		auto& tonemap_exposure = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_present.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_ubo_value(&compositor::fullscreen_present_params::exposure, val);
				});
			}));
		auto& tonemap_saturation = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_present.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_ubo_value(&compositor::fullscreen_present_params::saturation, val);
				});
			}));
		auto& tonemap_gamma = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_present.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_ubo_value(&compositor::fullscreen_present_params::gamma, val);
				});
			}));

		bloom_scale.connect_predecessor(*ui_providers.shader_bloom_scale);
		bloom_src_recv.connect_predecessor(*ui_providers.shader_bloom_src_factor);
		bloom_dst_recv.connect_predecessor(*ui_providers.shader_bloom_dst_factor);
		bloom_mix_recv.connect_predecessor(*ui_providers.shader_bloom_mix_factor);

		highlight_thres_recv.connect_predecessor(*ui_providers.highlight_filter_threshold);
		highlight_smooth_recv.connect_predecessor(*ui_providers.highlight_filter_smooth);

		tonemap_contrast.connect_predecessor(*ui_providers.tonemap_contrast);
		tonemap_exposure.connect_predecessor(*ui_providers.tonemap_exposure);
		tonemap_saturation.connect_predecessor(*ui_providers.tonemap_saturation);
		tonemap_gamma.connect_predecessor(*ui_providers.tonemap_gamma);

		ui_providers.apply(scene);

		return ret;
	};

#pragma endregion

	log::info({"GUI"}, "async scene setup");
	gui::example::main_loop_type main_loop{std::move(renderer), ctx, {
			.init_fn = init_fn,
			.main_loop_fn = gui::example::main_loop_fn,
			.exit_fn = [](gui::example::main_loop_type& loop){
				gui::example::clear_main_ui();
			}
	}};

	log::info({"GUI"}, "async scene setup done");
	game_trace("prepare: async scene setup done");
	if(profile_session != nullptr){
		main_loop.payload.game->configure_profile(profile_session->config().scenario, profile_session);
		log::info(
			{"Profile"},
			"profile enabled: config={} output={}",
			profile_session->config().config_path.string(),
			profile_session->output_dir().string());
	}
	main_loop.payload.game->initialize();
	game_trace("prepare: game initialized");

	game_renderer.set_frame_builder([&](game::game_2d_renderer& renderer, const math::vec2 extent){
		return main_loop.payload.game->render_frame(renderer, extent);
	});
	game_renderer.start();
	game_trace("prepare: game renderer started");

	std::vector<vk::command_buffer> post_process_cmds{};
	auto rebuild_post_process_commands = [&](
		backend::vulkan::context& context,
		const VkExtent2D extent){
		const auto frame_count = context.output_image_count();
		manager.set_frame_count(frame_count);
		pass_present.data.set_output_format(context.output_image(0).format);
		for(std::uint32_t frame_slot = 0; frame_slot < frame_count; ++frame_slot){
			present_output.set_frame_resource(
				frame_slot,
				compositor::image_entity{.handle = context.output_image(frame_slot).handle});
		}

		manager.resize(extent, true);

		if(post_process_cmds.size() != frame_count){
			post_process_cmds.clear();
			post_process_cmds.reserve(frame_count);
			for(std::uint32_t frame_slot = 0; frame_slot < frame_count; ++frame_slot){
				post_process_cmds.push_back(context.get_graphic_command_pool().obtain());
			}
		}

		for(std::uint32_t frame_slot = 0; frame_slot < frame_count; ++frame_slot){
			vk::scoped_recorder recorder{
				post_process_cmds[frame_slot],
				VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT
			};
			manager.create_command(post_process_cmds[frame_slot], frame_slot);
		}
	};

	ctx.register_post_resize("test", [&](backend::vulkan::context& context, window_instance::resize_event event){
		game_trace(std::format("resize: {}x{}", event.size.width, event.size.height));
		main_loop.resize({event.size.width, event.size.height});
		{
			auto& r = main_loop.get_renderer();

			ui_input_base.resource = compositor::image_entity{.handle = r.get_blit_attachments()[0]};
			ui_input_back.resource = compositor::image_entity{.handle = r.get_blit_attachments()[1]};
		}

		game_renderer.with_render_lock([&]{
			game_renderer.resize(event.size);
			input_background = game_renderer.make_output_resource();
			rebuild_post_process_commands(context, event.size);
		});
	});

	app_run(main_loop, post_process_cmds, [&](const double delta_seconds){
		if(const auto events = main_loop.unhandled_events.fetch()){
			for(const auto event : *events){
				main_loop.payload.game->handle_event(event);
			}
		}

		const auto extent = math::vector2{ctx.get_extent().width, ctx.get_extent().height}.as<float>();
		main_loop.payload.game->update_for_render(extent, delta_seconds);
	}, [&]{
		const auto extent = math::vector2{ctx.get_extent().width, ctx.get_extent().height}.as<float>();
		return game_renderer.request_full_frame(extent);
	}, [&](const std::uint64_t game_render_request){
		game_renderer.wait_frame(game_render_request);
	}, [&]{
		return game_renderer.get_valid_cmd_buf();
	}, [&](const VkFence fence){
		game_renderer.set_external_gpu_fence(fence);
	}, profile_session);
	game_renderer.stop();
	main_loop.payload.game->shutdown();

	log::info({"GUI"}, "exiting");

	main_loop.join();

	gui::example::dispose_generated_shapes();
	gui::global::terminate_assets_manager();
	gui::global::terminate();

	image_atlas.request_stop();
	image_atlas.wait_load();
	ctx.wait_on_device();
}

int game_main(int argc, char** argv){
	using namespace mo_yanxi;
	using namespace graphic;

	const auto profile_command_line = game::profile::parse_profile_command_line(argc, argv);
	if(profile_command_line.help_requested){
		std::print("{}", game::profile::profile_help_text());
		return 0;
	}

	std::shared_ptr<game::profile::profile_session> profile_session{};
	if(profile_command_line.config_path){
		auto profile_config = game::profile::load_profile_config(
			*profile_command_line.config_path,
			profile_command_line.output_dir_override);
		profile_session = std::make_shared<game::profile::profile_session>(std::move(profile_config));
	}

	game_initialize_logging();
	if(profile_session != nullptr){
		mo_yanxi::log::add_file_sink(profile_session->output_dir() / "game.log");
		mo_yanxi::log::set_min_level(mo_yanxi::log::parse_level(profile_session->config().run.log_level));
	}
	game_configure_runtime_working_directory();
	game_trace(std::format("main: cwd {}", std::filesystem::current_path().string()));

#ifndef NDEBUG
	if(auto nsight = game_get_environment_variable("NSIGHT"); nsight && *nsight == "1"){
		vk::enable_validation_layers = false;
	} else{
		vk::enable_validation_layers = true;
	}
#endif

	platform::initialize();
	font::initialize();
	backend::glfw::initialize();

	typesetting::rich_text_look_up_table table;
	typesetting::look_up_table = &table;

	VkApplicationInfo appInfo{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "Hello Xrgui",
		.applicationVersion = VK_MAKE_API_VERSION(1, 0, 0, 0),
		.pEngineName = "No Engine",
		.engineVersion = VK_MAKE_API_VERSION(1, 0, 0, 0),
		.apiVersion = VK_API_VERSION_1_4,
	};


	if (std::uint32_t supportedVersion = 0; vkEnumerateInstanceVersion(&supportedVersion) == VK_SUCCESS) {
		if (supportedVersion >= VK_API_VERSION_1_4) {
			// appInfo.apiVersion = VK_API_VERSION_1_3;
			//currently using 1.4 cause the code dead, I really have no idea why
			appInfo.apiVersion = VK_API_VERSION_1_3;
		} else {
			appInfo.apiVersion = VK_API_VERSION_1_3;
		}
	} else {
		appInfo.apiVersion = VK_API_VERSION_1_0;
	}

	log::info({"Vulkan"}, "API version: {}.{}.{}.{}", VK_API_VERSION_VARIANT(appInfo.apiVersion), VK_API_VERSION_MAJOR(appInfo.apiVersion), VK_API_VERSION_MINOR(appInfo.apiVersion), VK_API_VERSION_PATCH(appInfo.apiVersion));
	{
		backend::vulkan::context ctx{appInfo};
		vk::load_ext(ctx.get_instance());
		vk::register_default_requirements(ctx.get_device(), ctx.get_physical_device());

		prepare(ctx, profile_session);
	}

	backend::glfw::terminate();
	font::terminate();
	platform::terminate();
	if(profile_session != nullptr){
		profile_session->close();
	}

	return 0;
}

int main(int argc, char** argv){
	try{
		return game_main(argc, argv);
	} catch(const std::exception& exception){
		mo_yanxi::log::fatal({"Fatal"}, "unhandled exception: {}", exception.what());
		return 1;
	} catch(...){
		mo_yanxi::log::fatal({"Fatal"}, "unknown unhandled exception");
		return 1;
	}
}
