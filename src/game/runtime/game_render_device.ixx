module;

#include <cassert>
#include <vulkan/vulkan.h>

export module mo_yanxi.game.runtime.game_render_device;

export import mo_yanxi.game.runtime.game_renderer;
export import mo_yanxi.graphic.compositor.resource;

import mo_yanxi.backend.vulkan.attachment_manager;
import mo_yanxi.backend.vulkan.pipeline_manager;
import mo_yanxi.backend.vulkan.renderer.components;
import mo_yanxi.game.profile.runtime;
import mo_yanxi.graphic.g2d;
import mo_yanxi.graphic.g2d.batch.backend.vulkan;
import mo_yanxi.graphic.image_view_registry;
import mo_yanxi.graphic_state_context;
import mo_yanxi.gui.cfg.builtin.constants;
import mo_yanxi.gui.fx.config;
import mo_yanxi.gui.renderer.abi;
import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.log;
import mo_yanxi.math.matrix3;
import mo_yanxi.math.rect_ortho;
import mo_yanxi.math.vector2;
import mo_yanxi.vk;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk.record_context;
import mo_yanxi.vk.sync_processor;
import mo_yanxi.vk.util;
import std;

namespace mo_yanxi::game{
namespace instr = graphic::g2d;

template <typename T>
constexpr T game_renderer_bit_mask(const unsigned count) noexcept{
	return count >= std::numeric_limits<std::make_unsigned_t<T>>::digits
		       ? ~T{0}
		       : (static_cast<T>(1) << count) - 1;
}

[[nodiscard]] gui::fx::render_target_mask get_game_render_target(
	const gui::fx::pipeline_config& param,
	const backend::vulkan::graphic_pipeline_option& data) noexcept{
	if(param.draw_targets.any()){
		return param.draw_targets;
	}
	return data.default_target_attachments;
}

[[nodiscard]] gui::fx::render_target_mask make_game_render_target_mask(
	const backend::vulkan::graphic_pipeline_option& current_pipe_option,
	const gui::fx::pipeline_config& config,
	const unsigned bits) noexcept{
	return (bits ? gui::fx::render_target_mask{bits} : gui::fx::render_target_mask{~0U})
		& gui::fx::render_target_mask{
			game::game_renderer_bit_mask<std::uint32_t>(
				game::get_game_render_target(config, current_pipe_option).popcount())
		};
}

export
struct game_2d_renderer_shader_names{
	static constexpr std::string_view draw_vert{"ui.draw.vert.spv"};
	static constexpr std::string_view draw_frag_basic{"ui.draw.frag_basic.spv"};
	static constexpr std::string_view blit_alpha_blend{"ui.blit.alpha_blend.spv"};
	static constexpr std::string_view instruction_resolve{"ui.instruction_resolve_comp.spv"};
};

export
struct game_2d_renderer_create_info{
	vk::allocator_usage allocator_usage;
	VkCommandPool command_pool{};
	VkSampler sampler{};
	std::filesystem::path shader_spv_path;
};

export
class game_2d_renderer{
public:
	static constexpr std::size_t frames_in_flight = 3;

private:
	struct game_renderer_tables{
		graphic::g2d::data_layout_table<> vertex{
			std::in_place_type<gui::gui_reserved_user_data_tuple>
		};
		graphic::g2d::data_layout_table<> non_vertex{
			std::in_place_type<std::tuple<gui::fx::ui_state, gui::fx::slide_line_config>>
		};
	};

	struct command_recording_context{
		struct per_record_context_value{
			bool is_rendering{};
			bool current_pass_msaa{};
			gui::fx::render_target_mask current_pass_mask{};
		};

		struct section_state_apply_params{
			const instr::section_state_delta_set::exported_entry& entry;
			backend::vulkan::graphics_context_trace& context_trace;
			gui::fx::pipeline_config& draw_cfg;
			per_record_context_value& ctx_val;

			void flush(command_recording_context& ctx, const VkCommandBuffer buffer) const{
				ctx.flush_pass_(buffer, ctx_val);
			}
		};

	private:
		graphic::g2d::record_context<> cache_descriptor_context_{};
		vk::sync::sync_barrier_batch cache_barrier_gen_{};
		vk::dynamic_rendering cache_rendering_config_{};
		std::vector<std::uint8_t> cache_attachment_enter_mark_{};
		backend::vulkan::graphics_context_trace cache_graphic_context_{};

		static constexpr unsigned kMaxShaderStages = 6;
		std::array<std::vector<std::byte>, kMaxShaderStages> cache_push_constants_{};

		std::vector<VkClearAttachment> cache_clear_attachments_{};
		std::vector<VkClearRect> cache_clear_rects_{};
		vk::sync::sync_processor cache_sync_mgr_{};
		std::vector<vk::sync::image_slot> draw_attachment_slots_{};
		std::vector<vk::sync::image_slot> blit_attachment_slots_{};

		[[nodiscard]] static constexpr unsigned stage_index(VkShaderStageFlags flags) noexcept{
			unsigned idx = 0;
			while(flags >>= 1){
				++idx;
			}
			return idx < kMaxShaderStages ? idx : 0;
		}

		void flush_pass_(const VkCommandBuffer command_buffer, per_record_context_value& ctx_val){
			if(!ctx_val.is_rendering){
				return;
			}
			vkCmdEndRendering(command_buffer);
			ctx_val.is_rendering = false;
			cache_graphic_context_.set_rebind_required();
		}

		void ensure_render_pass_(
			game_2d_renderer& renderer,
			VkCommandBuffer command_buffer,
			const gui::fx::pipeline_config& draw_cfg,
			per_record_context_value& ctx_val);

	public:
		[[nodiscard]] command_recording_context() = default;

		void resize(const std::size_t draw_count, const std::size_t blit_count){
			cache_attachment_enter_mark_.resize(draw_count);
			draw_attachment_slots_.resize(draw_count);
			blit_attachment_slots_.resize(blit_count);
		}

		void rebuild_sync_resources_(game_2d_renderer& renderer);
		void record(game_2d_renderer& renderer, VkCommandBuffer command_buffer);
		void cmd_draw_(
			game_2d_renderer& renderer,
			VkCommandBuffer command_buffer,
			std::uint32_t index,
			const gui::fx::pipeline_config& draw_cfg);
		void blit_(game_2d_renderer& renderer, gui::fx::blit_config cfg, VkCommandBuffer command_buffer);
		bool apply_section_state_(
			game_2d_renderer& renderer,
			const section_state_apply_params& params,
			VkCommandBuffer command_buffer);
	};

	struct game_2d_render_worker{
	private:
		game_2d_renderer* renderer_{};
		std::move_only_function<game_render_frame_stats(game_2d_renderer&, math::vec2)> frame_builder_{};
		std::jthread worker_thread_{};
		std::mutex state_mutex_{};
		std::mutex renderer_mutex_{};
		std::condition_variable_any state_signal_{};
		math::vec2 requested_extent_{};
		std::uint64_t request_index_{};
		std::uint64_t completed_request_index_{};
		bool has_pending_request_{};
		std::exception_ptr worker_exception_{};

		void set_worker_exception(std::exception_ptr exception, const std::uint64_t request_index){
			std::lock_guard lock{state_mutex_};
			worker_exception_ = std::move(exception);
			completed_request_index_ = std::max(completed_request_index_, request_index);
			state_signal_.notify_all();
		}

		void publish_completion(const std::uint64_t request_index){
			std::lock_guard lock{state_mutex_};
			completed_request_index_ = std::max(completed_request_index_, request_index);
			state_signal_.notify_all();
		}

		void run(const std::stop_token stop_token){
			while(!stop_token.stop_requested()){
				std::uint64_t current_request{};
				math::vec2 current_extent{};
				{
					std::unique_lock lock{state_mutex_};
					state_signal_.wait(lock, stop_token, [&]{
						return has_pending_request_ || worker_exception_ != nullptr;
					});

					if(stop_token.stop_requested() || worker_exception_ != nullptr){
						return;
					}

					current_request = request_index_;
					current_extent = requested_extent_;
					has_pending_request_ = false;
				}

				try{
					std::lock_guard renderer_lock{renderer_mutex_};
					(void)std::invoke(frame_builder_, *renderer_, current_extent);
				} catch(...){
					log::error({"GameRenderer"}, "worker failed while preparing request {}", current_request);
					this->set_worker_exception(std::current_exception(), current_request);
					return;
				}

				this->publish_completion(current_request);
			}
		}

	public:
		[[nodiscard]] game_2d_render_worker() = default;

		[[nodiscard]] game_2d_render_worker(
			game_2d_renderer& renderer,
			std::move_only_function<game_render_frame_stats(game_2d_renderer&, math::vec2)> frame_builder)
			: renderer_{&renderer},
			  frame_builder_{std::move(frame_builder)}{
		}

		game_2d_render_worker(const game_2d_render_worker&) = delete;
		game_2d_render_worker(game_2d_render_worker&&) = delete;
		game_2d_render_worker& operator=(const game_2d_render_worker&) = delete;
		game_2d_render_worker& operator=(game_2d_render_worker&&) = delete;

		~game_2d_render_worker(){
			this->stop();
		}

		void start(){
			if(worker_thread_.joinable()){
				return;
			}

			log::info({"GameRenderer"}, "worker start");
			worker_thread_ = std::jthread{[this](const std::stop_token stop_token){
				this->run(stop_token);
			}};
		}

		void stop(){
			if(!worker_thread_.joinable()){
				return;
			}

			worker_thread_.request_stop();
			state_signal_.notify_all();
			worker_thread_.join();
			log::info({"GameRenderer"}, "worker stopped");
		}

		void resize(const VkExtent2D extent){
			std::lock_guard lock{renderer_mutex_};
			renderer_->resize(extent);
		}

		template <typename F>
		decltype(auto) with_render_lock(F&& fn){
			std::lock_guard lock{renderer_mutex_};
			return std::invoke(std::forward<F>(fn));
		}

		void set_external_gpu_fence(const VkFence fence) noexcept{
			std::lock_guard lock{renderer_mutex_};
			renderer_->set_current_external_submit_fence(fence);
		}

		[[nodiscard]] std::uint64_t request_full_frame(const math::vec2 extent){
			std::lock_guard lock{state_mutex_};
			if(worker_exception_ != nullptr){
				std::rethrow_exception(worker_exception_);
			}

			requested_extent_ = extent;
			const auto request = ++request_index_;
			if(extent.x <= 0.f || extent.y <= 0.f){
				has_pending_request_ = false;
				completed_request_index_ = request;
				log::debug({"GameRenderer"}, "skip render request {} for invalid extent=({}, {})", request, extent.x, extent.y);
			} else{
				has_pending_request_ = true;
				log::trace({"GameRenderer"}, "queue render request {} extent=({}, {})", request, extent.x, extent.y);
			}
			state_signal_.notify_all();
			return request;
		}

		void wait_frame(const std::uint64_t request_index){
			std::unique_lock lock{state_mutex_};
			state_signal_.wait(lock, [&]{
				return completed_request_index_ >= request_index || worker_exception_ != nullptr;
			});
			if(worker_exception_ != nullptr){
				std::rethrow_exception(worker_exception_);
			}
		}
	};

	vk::allocator_usage allocator_usage_{};
	game_renderer_tables tables_{};
	std::unique_ptr<graphic::image_view_registry> image_view_registry_{std::make_unique<graphic::image_view_registry>()};
	graphic::sampler_descriptor_index default_sampler_index_{graphic::auto_sampler_index};

	graphic::g2d::draw_list_context batch_host_{};
	graphic::g2d::batch_vulkan_executor batch_device_{};
	backend::vulkan::attachment_manager attachment_manager_{};
	backend::vulkan::graphic_pipeline_manager draw_pipeline_manager_{};
	backend::vulkan::renderer_blit_resources blit_resources_{};
	backend::vulkan::renderer_instruction_resolver instruction_resolver_{};
	backend::vulkan::renderer_frame_ring<frames_in_flight> frames_{};
	vk::command_buffer attachment_clear_and_init_command_buffer_{};
	command_recording_context record_ctx_{};
	game_render_frame_state current_frame_state_{};
	game_render_frame_stats last_frame_stats_{};
	std::unique_ptr<game_2d_render_worker> worker_{};
	std::shared_ptr<profile::profile_session> profile_session_{};

	[[nodiscard]] static game_renderer_tables make_tables(){
		return {};
	}

	[[nodiscard]] static graphic::g2d::hardware_limit_config query_hardware_limits(
		const vk::allocator_usage& allocator){
		VkPhysicalDeviceMeshShaderPropertiesEXT mesh_properties{
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT
		};
		VkPhysicalDeviceProperties2 properties{
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
			&mesh_properties
		};
		vkGetPhysicalDeviceProperties2(allocator.get_physical_device(), &properties);
		return {
			mesh_properties.maxTaskWorkGroupCount[0],
			mesh_properties.maxTaskWorkGroupSize[0],
			mesh_properties.maxMeshOutputVertices,
			mesh_properties.maxMeshOutputPrimitives
		};
	}

	[[nodiscard]] static backend::vulkan::graphic_pipeline_create_config make_draw_pipeline_config(
		vk::shader_module& draw_shader_vert,
		vk::shader_module& draw_shader_frag_basic){
		using namespace backend::vulkan;
		return graphic_pipeline_create_config{
			{
				graphic_pipeline_create_config::config{
					{
						draw_shader_vert.get_stage_bundle(VK_SHADER_STAGE_VERTEX_BIT, "main_vert"),
						draw_shader_frag_basic.get_stage_bundle(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
					},
					graphic_pipeline_option{
						false,
						mask_usage::ignore,
						{0b1},
						{},
						{
							{vk::blending::premultiplied_alpha_blend},
							blend_dynamic_flags::equation | blend_dynamic_flags::write_flag
						}
					}
				}
			},
			{}
		};
	}

	[[nodiscard]] static backend::vulkan::compute_pipeline_create_config make_blit_pipeline_config(
		vk::shader_module& blit_shader_alpha_blend){
		using namespace backend::vulkan;
		return compute_pipeline_create_config{
			{
				compute_pipeline_create_config::config{
					.shader_bundle = blit_shader_alpha_blend.get_stage_bundle(VK_SHADER_STAGE_COMPUTE_BIT),
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
				}
			},
			{}
		};
	}

	void initialize_frames(const VkCommandPool command_pool){
		frames_.initialize(allocator_usage_.get_device(), command_pool);
		attachment_clear_and_init_command_buffer_ = vk::command_buffer{
			allocator_usage_.get_device(),
			command_pool,
			VK_COMMAND_BUFFER_LEVEL_SECONDARY
		};
	}

	void create_clear_and_init_command() const{
		vk::scoped_recorder recorder{
			attachment_clear_and_init_command_buffer_,
			VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
			true
		};
		backend::vulkan::record_renderer_attachment_clear_and_init_command(
			recorder,
			attachment_manager_);
	}

	template <gui::fx::directly_emitable_state State>
	void update_state(const State& state, const unsigned offset = 0){
		const gui::fx::state_push_config config = state;
		const gui::fx::binary_diff_tag tag = state;
		if constexpr(std::is_empty_v<State> || std::is_void_v<State>){
			batch_host_.push_state(config, tag, {}, offset);
		} else{
			batch_host_.push_state(
				config,
				tag,
				std::span{
					reinterpret_cast<const std::byte*>(std::addressof(state)),
					sizeof(State)
				},
				offset);
		}
	}

	template <gui::fx::state_type_deducable State>
	void update_state(const State& state){
		batch_host_.push_state(
			gui::fx::state_type_deduce<State>::make_push_config(),
			gui::fx::make_state_tag(gui::fx::state_type_deduce<State>::type),
			std::span{
				reinterpret_cast<const std::byte*>(std::addressof(state)),
				sizeof(State)
			});
	}

	template <gui::fx::state_type_deducable State, typename MinorTag>
		requires(gui::fx::state_type_deduce<State>::requires_minor_tag)
	void update_state(const State& state, const MinorTag minor_tag, const unsigned offset = 0){
		batch_host_.push_state(
			gui::fx::state_type_deduce<State>::make_push_config(),
			gui::fx::make_state_tag(gui::fx::state_type_deduce<State>::type, minor_tag),
			std::span{
				reinterpret_cast<const std::byte*>(std::addressof(state)),
				sizeof(State)
			},
			offset);
	}

	template <instr::known_instruction Instruction>
	void push_instruction(const Instruction& instruction){
		batch_host_.push_instr(
			instr::make_instruction_head(instruction),
			reinterpret_cast<const std::byte*>(std::addressof(instruction)));
	}

	template <instr::known_instruction Instruction, typename... Args>
	void push_instruction(const Instruction& instruction, const Args&... args){
		const auto payload_size = instr::get_payload_size<Instruction, Args...>(args...);
		instr::instruction_buffer buffer{payload_size};
		const auto head = instr::place_instruction_at(buffer.data(), instruction, args...);
		batch_host_.push_instr(head, buffer.data());
	}

	template <typename T>
	void push_vertex_data(const std::uint32_t index, const T& data){
		static_assert(std::is_trivially_copyable_v<T>);
		const instr::instruction_head head{
			.type = instr::instr_type::uniform_update,
			.payload_size = static_cast<std::uint32_t>(instr::get_payload_size<T>()),
			.payload = {.ubo = instr::user_data_indices{index, 0}}
		};
		batch_host_.push_instr(head, reinterpret_cast<const std::byte*>(std::addressof(data)));
	}

public:
	void begin_frame(const game_render_frame_state& state){
		current_frame_state_ = state;
		const math::vec2 extent = state.extent;
		batch_host_.begin_rendering();
		batch_host_.get_data_group_non_vertex_info().push_default(gui::fx::ui_state(
			extent,
			static_cast<float>(state.simulation_time_seconds)));
		batch_host_.get_data_group_non_vertex_info().push_default(gui::fx::slide_line_config{});

		const auto screen_to_uniform = math::mat3{}.set_orthogonal({}, extent);
		const auto element_to_screen = state.camera.get_v2v_mat({});
		this->push_vertex_data(0, gui::ubo_screen_info{screen_to_uniform});
		this->push_vertex_data(2, gui::accumulated_state{
			.overlay_color = graphic::color{},
			.base_mult = graphic::color{1.f, 1.f, 1.f, 1.f}
		});
		this->push_vertex_data(1, gui::ubo_layer_info{
			element_to_screen,
			{
				.src = {},
				.dst = extent,
				.margin = 0.f
			}
		});

		gui::fx::viewport viewport{};
		viewport.src = {};
		viewport.extent = extent;
		this->update_state(gui::fx::scissor{
			.pos = {},
			.size = {
				static_cast<std::uint32_t>(std::max(extent.x, 0.f)),
				static_cast<std::uint32_t>(std::max(extent.y, 0.f))
			}
		});
		this->update_state(viewport);
		this->update_state(gui::fx::pipeline_config{.pipeline_index = 0});
		this->update_state(gui::fx::push_constant{gui::cfg::builtin::gpip::default_draw_constants{}});
		this->update_state(gui::fx::blend::pma::standard);
		this->update_state(gui::fx::make_blend_write_mask(true), 0);
	}

	void end_frame(const game_render_frame_state&){
		this->update_state(gui::fx::blit_config{
			.blit_region = gui::fx::blit_config::full_screen_region,
			.pipe_info = {.pipeline_index = 0}
		});
		batch_host_.end_rendering();
	}

private:
	void upload(){
		frames_.advance();
		auto& frame = frames_.current_frame();
		try{
			if(frame.external_submit_fence){
				frame.external_submit_fence = VK_NULL_HANDLE;
			} else{
				frame.fence.wait_and_reset();
			}
			batch_device_.upload(batch_host_, *image_view_registry_, frames_.current_index());
		} catch(...){
			if(!frame.external_submit_fence){
				frame.fence.reset();
			}
			frame.external_submit_fence = VK_NULL_HANDLE;
			throw;
		}
	}

	void create_command(){
		vk::scoped_recorder recorder{
			frames_.current_command_buffer(),
			VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
		};
		record_ctx_.record(*this, recorder);
	}

public:
	[[nodiscard]] game_2d_renderer() = default;

	[[nodiscard]] explicit game_2d_renderer(game_2d_renderer_create_info&& create_info)
		: allocator_usage_{create_info.allocator_usage},
		  tables_{game_2d_renderer::make_tables()},
		  default_sampler_index_{image_view_registry_->register_sampler(create_info.sampler)},
		  batch_host_{
			  game_2d_renderer::query_hardware_limits(create_info.allocator_usage),
			  tables_.vertex,
			  tables_.non_vertex,
			  *image_view_registry_
		  },
		  batch_device_{
			  allocator_usage_,
			  batch_host_,
			  {
				  .vertex_stride = sizeof(backend::vulkan::gui_vertex_mock),
				  .primitive_stride = sizeof(backend::vulkan::gui_primitive_mock),
			  },
			  frames_in_flight
		  },
		  attachment_manager_{
			  allocator_usage_,
			  backend::vulkan::draw_attachment_create_info{
				  {
					  backend::vulkan::draw_attachment_config{
						  .attachment = {
							  VK_FORMAT_R16G16B16A16_SFLOAT,
							  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
						  }
					  }
				  }
			  },
			  backend::vulkan::blit_attachment_create_info{
				  {
					  backend::vulkan::attachment_config{
						  VK_FORMAT_R16G16B16A16_SFLOAT,
						  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
					  }
				  }
			  }
		  }{
		vk::shader_module draw_shader_vert{
			allocator_usage_.get_device(),
			create_info.shader_spv_path / game_2d_renderer_shader_names::draw_vert
		};
		vk::shader_module draw_shader_frag_basic{
			allocator_usage_.get_device(),
			create_info.shader_spv_path / game_2d_renderer_shader_names::draw_frag_basic
		};
		vk::shader_module blit_shader_alpha_blend{
			allocator_usage_.get_device(),
			create_info.shader_spv_path / game_2d_renderer_shader_names::blit_alpha_blend
		};
		vk::shader_module shader_instr_resolve{
			allocator_usage_.get_device(),
			create_info.shader_spv_path / game_2d_renderer_shader_names::instruction_resolve
		};

		draw_pipeline_manager_ = backend::vulkan::graphic_pipeline_manager(
			allocator_usage_,
			game_2d_renderer::make_draw_pipeline_config(draw_shader_vert, draw_shader_frag_basic),
			batch_device_.get_gfx_descriptor_set_layout(),
			attachment_manager_.get_draw_config());
		blit_resources_ = backend::vulkan::renderer_blit_resources(
			allocator_usage_,
			game_2d_renderer::make_blit_pipeline_config(blit_shader_alpha_blend));
		instruction_resolver_ = backend::vulkan::renderer_instruction_resolver(
			allocator_usage_.get_device(),
			batch_device_.get_cs_descriptor_set_layout(),
			shader_instr_resolve.get_create_info(VK_SHADER_STAGE_COMPUTE_BIT));

		record_ctx_.resize(
			attachment_manager_.get_draw_attachments().size(),
			attachment_manager_.get_blit_attachments().size());
		record_ctx_.rebuild_sync_resources_(*this);
		this->initialize_frames(create_info.command_pool);

		log::info(
			{"GameRenderer"},
			"create GUI-style game renderer with shaders: {}, {}, {}, {}",
			game_2d_renderer_shader_names::draw_vert,
			game_2d_renderer_shader_names::draw_frag_basic,
			game_2d_renderer_shader_names::blit_alpha_blend,
			game_2d_renderer_shader_names::instruction_resolve);
	}

	[[nodiscard]] game_2d_renderer(
		const vk::allocator_usage& allocator,
		const VkDevice,
		const VkCommandPool command_pool,
		const VkSampler sampler,
		const std::filesystem::path& shader_spv_path)
		: game_2d_renderer{game_2d_renderer_create_info{
			.allocator_usage = allocator,
			.command_pool = command_pool,
			.sampler = sampler,
			.shader_spv_path = shader_spv_path
		}}{
	}

	[[nodiscard]] game_2d_renderer(
		const vk::allocator_usage& allocator,
		const VkDevice device,
		const VkCommandPool command_pool,
		const VkSampler sampler,
		const std::filesystem::path& shader_spv_path,
		std::move_only_function<game_render_frame_stats(game_2d_renderer&, math::vec2)> frame_builder)
		: game_2d_renderer{allocator, device, command_pool, sampler, shader_spv_path}{
		this->set_frame_builder(std::move(frame_builder));
	}

	[[nodiscard]] graphic::image_view_registry& image_view_registry() noexcept{
		return *image_view_registry_;
	}

	[[nodiscard]] const graphic::image_view_registry& image_view_registry() const noexcept{
		return *image_view_registry_;
	}

	[[nodiscard]] graphic::sampler_descriptor_index default_sampler_index() const noexcept{
		return default_sampler_index_;
	}

	[[nodiscard]] gui::renderer_frontend create_frontend() noexcept{
		return gui::renderer_frontend{
			tables_.vertex,
			tables_.non_vertex,
			{
				*this,
				[](game_2d_renderer& renderer, instr::instruction_head head, const std::byte* payload) static{
					renderer.batch_host_.push_instr(head, payload);
				},
				[](game_2d_renderer& renderer, std::span<const instr::instruction_head> heads, const std::byte* payload) static{
					renderer.batch_host_.push_instr_batch(heads, payload);
				},
				[](game_2d_renderer& renderer, auto config, auto tag, auto payload, auto offset) static{
					renderer.batch_host_.push_state(config, tag, payload, offset);
				}
			}
		};
	}

	void set_frame_builder(std::move_only_function<game_render_frame_stats(game_2d_renderer&, math::vec2)> frame_builder){
		this->stop();
		log::info({"GameRenderer"}, "set frame builder");
		worker_ = std::make_unique<game_2d_render_worker>(*this, std::move(frame_builder));
	}

	void set_profile_session(std::shared_ptr<profile::profile_session> session){
		profile_session_ = std::move(session);
	}

	void start(){
		if(worker_ == nullptr){
			throw std::logic_error{"game renderer frame builder is not set"};
		}
		worker_->start();
	}

	void stop(){
		if(worker_ != nullptr){
			worker_->stop();
		}
	}

	void resize(const VkExtent2D extent){
		log::info({"GameRenderer"}, "resize: {}x{}", extent.width, extent.height);
		(void)attachment_manager_.resize(extent);
		record_ctx_.resize(
			attachment_manager_.get_draw_attachments().size(),
			attachment_manager_.get_blit_attachments().size());
		record_ctx_.rebuild_sync_resources_(*this);
		blit_resources_.update_descriptors(attachment_manager_);
		this->create_clear_and_init_command();
	}

	template <typename F>
	decltype(auto) with_render_lock(F&& fn){
		if(worker_ == nullptr){
			throw std::logic_error{"game renderer frame builder is not set"};
		}
		return worker_->with_render_lock(std::forward<F>(fn));
	}

	void commit_prepared_frame(){
		auto* profile_session = profile_session_.get();
		{
			profile::profile_scope_timer timer{profile_session, current_frame_state_.frame_index, "renderer.upload"};
			this->upload();
		}
		{
			profile::profile_scope_timer timer{profile_session, current_frame_state_.frame_index, "renderer.create_command"};
			this->create_command();
		}
		log::trace(
			{"GameRenderer"},
			"prepared frame={} extent=({}, {}) visited={} drawn={} culled={}",
			current_frame_state_.frame_index,
			current_frame_state_.extent.x,
			current_frame_state_.extent.y,
			last_frame_stats_.drawable_visited,
			last_frame_stats_.drawable_drawn,
			last_frame_stats_.drawable_culled);
	}

	[[nodiscard]] std::uint64_t request_full_frame(const math::vec2 extent){
		if(worker_ == nullptr){
			throw std::logic_error{"game renderer frame builder is not set"};
		}
		return worker_->request_full_frame(extent);
	}

	void wait_frame(const std::uint64_t request_index){
		if(worker_ == nullptr){
			throw std::logic_error{"game renderer frame builder is not set"};
		}
		worker_->wait_frame(request_index);
	}

	void render_full_frame(const math::vec2 extent){
		const auto request = this->request_full_frame(extent);
		this->wait_frame(request);
	}

	void set_external_gpu_fence(const VkFence fence) noexcept{
		if(worker_ == nullptr){
			return;
		}
		worker_->set_external_gpu_fence(fence);
	}

	void set_current_external_submit_fence(const VkFence fence) noexcept{
		frames_.current_frame().external_submit_fence = fence;
	}

	[[nodiscard]] VkCommandBuffer get_valid_cmd_buf() const noexcept{
		return frames_.current_command_buffer();
	}

	[[nodiscard]] vk::image_handle get_output() const noexcept{
		return attachment_manager_.get_blit_attachments()[0];
	}

	[[nodiscard]] graphic::compositor::resource_entity_external make_output_resource() const{
		return graphic::compositor::resource_entity_external{
			graphic::compositor::image_entity{.handle = this->get_output()},
			graphic::compositor::resource_dependency{
				.src_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.src_access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				.dst_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dst_access = VK_ACCESS_2_SHADER_READ_BIT,
				.src_layout = VK_IMAGE_LAYOUT_GENERAL,
				.dst_layout = VK_IMAGE_LAYOUT_GENERAL,
			}
		};
	}

	void set_last_frame_stats(const game_render_frame_stats& stats) noexcept{
		last_frame_stats_ = stats;
		current_frame_state_ = stats.frame;
	}

	[[nodiscard]] const game_render_frame_state& current_frame_state() const noexcept{
		return current_frame_state_;
	}

	[[nodiscard]] const game_render_frame_stats& last_frame_stats() const noexcept{
		return last_frame_stats_;
	}
};

void game_2d_renderer::command_recording_context::rebuild_sync_resources_(game_2d_renderer& renderer){
	cache_sync_mgr_.clear_resources();
	cache_sync_mgr_.reset_transient();
	cache_sync_mgr_.reserve_resources(
		renderer.attachment_manager_.get_draw_attachments().size()
			+ renderer.attachment_manager_.get_blit_attachments().size(),
		0);

	draw_attachment_slots_.clear();
	blit_attachment_slots_.clear();
	draw_attachment_slots_.reserve(renderer.attachment_manager_.get_draw_attachments().size());
	blit_attachment_slots_.reserve(renderer.attachment_manager_.get_blit_attachments().size());

	for(std::size_t i = 0; i < renderer.attachment_manager_.get_draw_attachments().size(); ++i){
		draw_attachment_slots_.push_back(cache_sync_mgr_.register_image(backend::vulkan::renderer_draw_write_state));
	}
	for(std::size_t i = 0; i < renderer.attachment_manager_.get_blit_attachments().size(); ++i){
		blit_attachment_slots_.push_back(cache_sync_mgr_.register_image(backend::vulkan::renderer_blit_rw_state));
	}
}

void game_2d_renderer::command_recording_context::ensure_render_pass_(
	game_2d_renderer& renderer,
	const VkCommandBuffer command_buffer,
	const gui::fx::pipeline_config& draw_cfg,
	per_record_context_value& ctx_val){
	if(ctx_val.is_rendering){
		return;
	}

	const auto& pipe_option = renderer.draw_pipeline_manager_.get_pipelines()[draw_cfg.pipeline_index].option;
	const bool is_msaa = pipe_option.enables_multisample && renderer.attachment_manager_.enables_multisample();
	const auto target_mask = game::get_game_render_target(draw_cfg, pipe_option);

	cache_barrier_gen_.clear();
	target_mask.for_each_popbit([&](const unsigned idx){
		(void)cache_sync_mgr_.transition_image(
			cache_barrier_gen_,
			draw_attachment_slots_[idx],
			renderer.attachment_manager_.get_draw_attachments()[idx].get_image(),
			backend::vulkan::renderer_draw_write_state);
	});

	pipe_option.input_attachments_mask.for_each_popbit([&](const unsigned idx){
		(void)cache_sync_mgr_.transition_image(
			cache_barrier_gen_,
			draw_attachment_slots_[idx],
			renderer.attachment_manager_.get_draw_attachments()[idx].get_image(),
			backend::vulkan::renderer_draw_sample_state);
	});

	for(std::size_t idx = 0; idx < blit_attachment_slots_.size(); ++idx){
		(void)cache_sync_mgr_.transition_image(
			cache_barrier_gen_,
			blit_attachment_slots_[idx],
			renderer.attachment_manager_.get_blit_attachments()[idx].get_image(),
			backend::vulkan::renderer_blit_rw_state);
	}
	cache_barrier_gen_.apply(command_buffer);

	renderer.attachment_manager_.configure_dynamic_rendering<32>(
		cache_rendering_config_,
		target_mask,
		{},
		is_msaa,
		backend::vulkan::mask_usage::ignore,
		0);

	std::size_t cur_slot = 0;
	target_mask.for_each_popbit([&](const unsigned idx){
		auto& info = cache_rendering_config_.get_color_attachment_infos()[cur_slot++];
		info.loadOp = cache_attachment_enter_mark_[idx]
			              ? VK_ATTACHMENT_LOAD_OP_LOAD
			              : VK_ATTACHMENT_LOAD_OP_CLEAR;
		info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		cache_attachment_enter_mark_[idx] = true;
	});

	cache_rendering_config_.begin_rendering(command_buffer, renderer.attachment_manager_.get_screen_area());
	ctx_val.is_rendering = true;
	ctx_val.current_pass_mask = target_mask;
	ctx_val.current_pass_msaa = is_msaa;
}

void game_2d_renderer::command_recording_context::record(
	game_2d_renderer& renderer,
	const VkCommandBuffer command_buffer){
	per_record_context_value ctx_val{};

	cache_descriptor_context_.reset_binding_state();
	vkCmdExecuteCommands(command_buffer, 1, renderer.attachment_clear_and_init_command_buffer_.as_data());

	const auto section_count = renderer.batch_host_.get_section_count();
	if(renderer.batch_host_.get_valid_submit_groups().empty()){
		return;
	}
	if(renderer.batch_device_.is_frame_empty()){
		return;
	}

	renderer.batch_device_.cmd_copy_cpu_resolved_geometry(command_buffer, renderer.frames_.current_index());
	renderer.instruction_resolver_.record_if_needed(
		renderer.batch_device_,
		cache_descriptor_context_,
		command_buffer,
		renderer.frames_.current_index());

	cache_graphic_context_.reset();
	for(auto& cache : cache_push_constants_){
		cache.clear();
	}

	gui::fx::pipeline_config draw_cfg{.pipeline_index = 0};
	const auto& initial_pipe_option = renderer.draw_pipeline_manager_.get_pipelines()[draw_cfg.pipeline_index].option;
	cache_graphic_context_.update_pipeline(draw_cfg.pipeline_index, initial_pipe_option);

	cache_sync_mgr_.reset_transient();
	for(const auto& slot : draw_attachment_slots_){
		cache_sync_mgr_.set_image_state(slot, backend::vulkan::renderer_draw_write_state);
	}
	for(const auto& slot : blit_attachment_slots_){
		cache_sync_mgr_.set_image_state(slot, backend::vulkan::renderer_blit_rw_state);
	}
	std::ranges::fill(cache_attachment_enter_mark_, 0);
	cache_rendering_config_.clear_color_attachments();

	auto get_section_params = [&](const instr::section_state_delta_set::exported_entry& entry) noexcept{
		return section_state_apply_params{
			entry,
			cache_graphic_context_,
			draw_cfg,
			ctx_val
		};
	};

	for(unsigned i = 0; i < section_count; ++i){
		bool requires_clear = false;
		if(renderer.batch_device_.is_section_empty(i)){
			for(const auto& entry : renderer.batch_host_.get_section_state_deltas(i).get_entries()){
				requires_clear |= this->apply_section_state_(renderer, get_section_params(entry), command_buffer);
			}
		} else{
			this->ensure_render_pass_(renderer, command_buffer, draw_cfg, ctx_val);
			cache_graphic_context_.apply(command_buffer, renderer.draw_pipeline_manager_.get_pipelines());
			this->cmd_draw_(renderer, command_buffer, i, draw_cfg);

			for(const auto& entry : renderer.batch_host_.get_section_state_deltas(i).get_entries()){
				requires_clear |= this->apply_section_state_(renderer, get_section_params(entry), command_buffer);
			}
		}

		if(requires_clear){
			this->flush_pass_(command_buffer, ctx_val);
			std::ranges::fill(cache_attachment_enter_mark_, 0);
		}
	}

	this->flush_pass_(command_buffer, ctx_val);
}

bool game_2d_renderer::command_recording_context::apply_section_state_(
	game_2d_renderer& renderer,
	const section_state_apply_params& params,
	const VkCommandBuffer command_buffer){
	auto& current_pipe = renderer.draw_pipeline_manager_.get_pipelines()[params.draw_cfg.pipeline_index];
	using namespace gui::fx;
	switch(static_cast<state_type>(params.entry.tag.major)){
	case state_type::blit:{
		params.flush(*this, command_buffer);
		auto cfg = params.entry.as<blit_config>();
		this->blit_(renderer, cfg, command_buffer);
		return !cfg.reserve_original;
	}
	case state_type::pipe:{
		auto param = params.entry.as<pipeline_config>();
		if(param.use_fallback_pipeline()){
			param.pipeline_index = params.draw_cfg.pipeline_index;
		}
		params.draw_cfg = param;

		const auto& pipe_option = renderer.draw_pipeline_manager_.get_pipelines()[params.draw_cfg.pipeline_index].option;
		params.context_trace.update_pipeline(params.draw_cfg.pipeline_index, pipe_option);
		for(auto& cache : cache_push_constants_){
			cache.clear();
		}

		if(params.ctx_val.is_rendering){
			const bool is_new_msaa = pipe_option.enables_multisample && renderer.attachment_manager_.enables_multisample();
			const auto new_target = game::get_game_render_target(params.draw_cfg, pipe_option);
			if(new_target != params.ctx_val.current_pass_mask || is_new_msaa != params.ctx_val.current_pass_msaa){
				this->flush_pass_(command_buffer, params.ctx_val);
			}
		}
		return false;
	}
	case state_type::split:
		params.flush(*this, command_buffer);
		return false;
	case state_type::push_constant:{
		const auto flags = static_cast<VkShaderStageFlags>(params.entry.tag.minor);
		const auto stg_idx = command_recording_context::stage_index(flags);
		auto& cache = cache_push_constants_[stg_idx];
		const auto offset = params.entry.logical_offset;
		const auto size = params.entry.payload.size();
		if(offset + size <= cache.size()
			&& std::memcmp(cache.data() + offset, params.entry.payload.data(), size) == 0){
			break;
		}
		if(offset + size > cache.size()){
			cache.resize(offset + size, std::byte{0});
		}
		std::memcpy(cache.data() + offset, params.entry.payload.data(), size);
		vkCmdPushConstants(
			command_buffer,
			current_pipe.pipeline_layout,
			flags,
			offset,
			static_cast<std::uint32_t>(size),
			params.entry.payload.data());
		break;
	}
	case state_type::set_color_blend_enable:{
		auto param = params.entry.as<blend_enable_flag>();
		auto mask = game::make_game_render_target_mask(current_pipe.option, params.draw_cfg, params.entry.tag.minor);
		mask.for_each_popbit([&](const unsigned i){
			params.context_trace.set_blend_enable(i, param);
		});
		break;
	}
	case state_type::set_color_blend_equation:{
		auto param = params.entry.as<blend_equation>();
		auto mask = game::make_game_render_target_mask(current_pipe.option, params.draw_cfg, params.entry.tag.minor);
		mask.for_each_popbit([&](const unsigned i){
			params.context_trace.set_blend_equation(i, param);
		});
		break;
	}
	case state_type::set_color_write_mask:{
		auto param = params.entry.as<blend_write_mask_type>();
		auto mask = game::make_game_render_target_mask(current_pipe.option, params.draw_cfg, params.entry.tag.minor);
		mask.for_each_popbit([&](const unsigned i){
			params.context_trace.set_blend_write_mask(i, param);
		});
		break;
	}
	case state_type::set_scissor:{
		auto param = params.entry.as<scissor>();
		params.context_trace.set_scissor(param);
		break;
	}
	case state_type::set_viewport:{
		auto param = params.entry.as<viewport>();
		params.context_trace.set_viewport(param);
		break;
	}
	case state_type::fill_color_local:{
		this->ensure_render_pass_(renderer, command_buffer, params.draw_cfg, params.ctx_val);
		auto mask = game::make_game_render_target_mask(current_pipe.option, params.draw_cfg, params.entry.tag.minor);
		cache_clear_attachments_.clear();
		cache_clear_rects_.clear();

		if(params.entry.payload.size() != sizeof(color_clear_value)){
			throw std::runtime_error{"invalid color clear payload size"};
		}

		auto param = params.entry.as<color_clear_value>();
		mask.for_each_popbit([&](const unsigned i){
			cache_clear_attachments_.push_back({
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.colorAttachment = i,
				.clearValue = param
			});
		});
		const VkClearRect rect{
			.rect = {{}, renderer.attachment_manager_.get_extent()},
			.baseArrayLayer = 0,
			.layerCount = 1
		};
		vkCmdClearAttachments(
			command_buffer,
			static_cast<std::uint32_t>(cache_clear_attachments_.size()),
			cache_clear_attachments_.data(),
			1,
			&rect);
		break;
	}
	case state_type::fill_color_other_lazy:{
		render_target_mask mask{params.entry.tag.minor};
		mask.for_each_popbit([&](const unsigned i){
			if(i < cache_attachment_enter_mark_.size()){
				cache_attachment_enter_mark_[i] = 0;
			}
		});
		break;
	}
	case state_type::mask_op:
		throw std::runtime_error{"game renderer does not support mask operations"};
	default:
		throw std::runtime_error{"game renderer state is not implemented"};
	}
	return false;
}

void game_2d_renderer::command_recording_context::cmd_draw_(
	game_2d_renderer& renderer,
	const VkCommandBuffer command_buffer,
	const std::uint32_t index,
	const gui::fx::pipeline_config& draw_cfg){
	const auto& pipeline_data = renderer.draw_pipeline_manager_.get_pipelines()[draw_cfg.pipeline_index];

	cache_descriptor_context_.clear();
	renderer.batch_device_.load_gfx_descriptors(cache_descriptor_context_, renderer.frames_.current_index());
	auto& input_attachment_descriptor =
		renderer.draw_pipeline_manager_.get_input_attachment_mock_descriptor()[draw_cfg.pipeline_index];
	if(input_attachment_descriptor.buffer){
		cache_descriptor_context_.push(2, input_attachment_descriptor.buffer);
	}

	cache_descriptor_context_.prepare_bindings();
	cache_descriptor_context_(pipeline_data.pipeline_layout, command_buffer, index, VK_PIPELINE_BIND_POINT_GRAPHICS);
	renderer.batch_device_.cmd_draw_direct(command_buffer, renderer.frames_.current_index(), index);
}

void game_2d_renderer::command_recording_context::blit_(
	game_2d_renderer& renderer,
	const gui::fx::blit_config cfg,
	const VkCommandBuffer command_buffer){
	renderer.blit_resources_.record(
		renderer.attachment_manager_,
		cache_sync_mgr_,
		cache_barrier_gen_,
		draw_attachment_slots_,
		blit_attachment_slots_,
		cache_descriptor_context_,
		backend::vulkan::renderer_blit_request{
			.blit_region = cfg.blit_region,
			.pipe_info = {
				.pipeline_index = cfg.pipe_info.pipeline_index,
				.inout_define_index = cfg.pipe_info.inout_define_index
			},
			.reserve_original = cfg.reserve_original
		},
		command_buffer);
}
}
