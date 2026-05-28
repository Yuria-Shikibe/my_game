module;

#define NAME_OF(e) ((void)category::e, #e)

export module mo_yanxi.game.ecs.component.chamber:chamber_meta;

import :decl;
import :grid_maneuver;

export import mo_yanxi.game.ecs.component.chamber.general;
export import mo_yanxi.game.srl;

import mo_yanxi.math.vector2;
import mo_yanxi.math.rect_ortho;
import mo_yanxi.game.ecs.component.hit_point;
import mo_yanxi.graphic.renderer.predecl;
import mo_yanxi.graphic.renderer.world;
import mo_yanxi.graphic.camera;

import mo_yanxi.ui.elem.table;
import mo_yanxi.owner;

import std;

namespace mo_yanxi::game::meta::chamber{
	export struct chamber_instance_data;

	export
	struct ui_edit_context{
		chamber_instance_data* current_changed_building{};
		bool dynamic_energy_usage_changed{};

	};

	export
	struct ui_build_handle{
		struct promise_type;
		using handle = std::coroutine_handle<promise_type>;

		[[nodiscard]] ui_build_handle() noexcept = default;

		[[nodiscard]] explicit ui_build_handle(handle&& hdl)
			: hdl{std::move(hdl)}{}

		struct promise_type{
			bool has_ui{};

			[[nodiscard]] promise_type() = default;

			ui_build_handle get_return_object() noexcept{
				return ui_build_handle{handle::from_promise(*this)};
			}

			[[nodiscard]] static auto initial_suspend() noexcept{ return std::suspend_never{}; }

			[[nodiscard]] static auto final_suspend() noexcept{ return std::suspend_always{}; }

			std::suspend_always yield_value(bool has_ui) noexcept{
				this->has_ui = has_ui;
				return {};
			}

			static void return_void() noexcept{
			}

			[[noreturn]] static void unhandled_exception() noexcept{
				std::terminate();
			}


		private:
			friend ui_build_handle;

		};

		void resume() const{
			hdl.resume();
		}

		[[nodiscard]] bool done() const noexcept{
			return hdl.done();
		}

		ui_build_handle(const ui_build_handle& other) = delete;

		ui_build_handle(ui_build_handle&& other) noexcept
			: hdl{std::exchange(other.hdl, {})}{
		}

		ui_build_handle& operator=(const ui_build_handle& other) = delete;

		ui_build_handle& operator=(ui_build_handle&& other) noexcept{
			if(this == &other) return *this;
			dstry();
			hdl = std::exchange(other.hdl, {});
			return *this;
		}

		~ui_build_handle(){
			dstry();
		}


		[[nodiscard]] bool has_ui() const{
			return hdl.promise().has_ui;
		}

	private:
		void dstry() noexcept{
			if(hdl){
				if(!done()){
					resume();
				}
				hdl.destroy();
			}
		}

		handle hdl{};
	};

	export
	enum struct category{
		misc,
		weaponry,
		utility,
		maneuvering,
		powering,
		defensive,

		LAST
	};

	export constexpr inline std::array<std::string_view, std::to_underlying(category::LAST)>
		category_names{
			NAME_OF(misc),
			NAME_OF(weaponry),
			NAME_OF(utility),
			NAME_OF(maneuvering),
			NAME_OF(powering),
			NAME_OF(defensive),
		};



	export struct basic_chamber;

	struct chamber_instance_data{
		virtual ~chamber_instance_data() = default;

		virtual void install(ecs::chamber::build_ref build_ref){

		}

		virtual srl::chunk_serialize_handle write(std::ostream& stream) const{
			co_yield 0;
		}

		virtual void read(std::ispanstream& bounded_stream){

		}


		virtual void draw(
			const basic_chamber& meta,
			math::frect region,
			graphic::renderer_ui_ref renderer_ui,
			const float camera_scale
		) const{

		}

		virtual ui_build_handle build_editor_ui(ui::table& table, ui_edit_context& context){
			co_yield false;
		}

		// virtual void mouse_events()
	};

	// export
	// using
	struct basic_chamber{
		std::string_view name;
		category category;
		math::usize2 extent;

		unsigned mass;
		ecs::static_hit_point hit_point;

		bool has_placement_direction;
		// bool is_pure_static;

		virtual ~basic_chamber() = default;

		[[nodiscard]] virtual std::optional<math::vec2> get_part_offset() const noexcept{
			return std::nullopt;
		}

		[[nodiscard]] virtual std::optional<float> get_part_direction() const noexcept{
			return std::nullopt;
		}

		[[nodiscard]] virtual std::optional<math::range> get_radius_range() const noexcept{
			return std::nullopt;
		}

		[[nodiscard]] virtual std::optional<math::range> get_range_angular() const noexcept{
			return std::nullopt;
		}

		[[nodiscard]] virtual std::optional<ecs::chamber::energy_status> get_energy_consumption() const noexcept{
			return std::nullopt;
		}

		[[nodiscard]] virtual graphic::color get_edit_outline_color() const noexcept{
			return graphic::colors::aqua;
		}

		[[nodiscard]] virtual const ecs::chamber::maneuver_component* get_maneuver_comp() const noexcept{
			return nullptr;
		}

		virtual void draw(math::frect region, graphic::renderer_ui_ref renderer_ui, const graphic::camera2& camera) const{

		}

		virtual void draw(graphic::renderer_world& renderer_world) const{

		}

		[[nodiscard]] int get_energy_usage() const noexcept{
			return get_energy_consumption().transform([](ecs::chamber::energy_status&& c){return c.power;}).value_or(0);
		}

		virtual void install(ecs::chamber::build_ref build_ref) const;

		virtual void build_editor_ui(ui::table& table) const;

		[[nodiscard]] virtual unsigned structural_adjacent_distance() const noexcept{
			return 0;
		}

		[[nodiscard]] bool is_structural() const noexcept{
			return structural_adjacent_distance() > 0;
		}

		virtual ecs::chamber::build_ptr create_instance_chamber(ecs::chamber::chamber_manifold& grid, math::point2 where) const;

		template <typename S>
		auto create_instance_data(this const S& self)  {
			return std::unique_ptr<std::remove_pointer_t<decltype(std::declval<const S&>().create_instance_data_impl())>>{self.create_instance_data_impl()};
		}

	protected:
		[[nodiscard]] virtual owner<chamber_instance_data*> create_instance_data_impl() const{
			return nullptr;
		}

		template <typename S>
		std::optional<ecs::chamber::energy_status> deduced_get_energy_consumption(this const S& self){
			if constexpr (std::derived_from<S, ecs::chamber::energy_status>){
				return static_cast<const ecs::chamber::energy_status&>(self);
			}else{
				return std::nullopt;
			}
		}
	};

	export constexpr basic_chamber empty_chamber{[]{
		basic_chamber c{};
		c.hit_point = {.max = 100};
		c.extent = {1, 1};
		return c;
	}()};

}