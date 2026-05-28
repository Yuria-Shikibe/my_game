module;

//#define GCH_DISABLE_CONCEPTS //otherwise clang ice...
#include <gch/small_vector.hpp>
#include <cassert>

export module mo_yanxi.game.ecs.component.chamber:chamber;

import :grid_meta;

export import mo_yanxi.math.rect_ortho;
export import mo_yanxi.math.vector2;
export import mo_yanxi.game.quad_tree_interface;
export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.component.damage;
export import mo_yanxi.game.ecs.component.hit_point;

export import mo_yanxi.game.ecs.component.ui.builder;

export import mo_yanxi.game.ecs.world.top;
export import mo_yanxi.game.ecs.component.chamber.general;

import std;

namespace mo_yanxi::game::ecs{

	namespace chamber{


		export struct tile;
		export class tile_grid;
		export class chamber_grid_fields;
		export struct chamber_manifold;
		export struct chamber_manifold_dump;

		export using tile_coord = math::point2;
		export using tile_region = math::irect;

		export enum struct damage_consume_result{
			success,
			damage_exhaust,
			hitpoint_exhaust,
		};

		export
		struct building_data;

		export
		struct tile_damage_event{
			using damage_type = float;
			math::ushort_point2 local_coord;
			damage_type damage;
		};


		struct tile_damage{
			float building_damage;
			float structural_damage;

			float sum() const noexcept{
				return building_damage + structural_damage;
			}
		};

		export
		struct tile_status{
			float valid_hit_point{};

			constexpr float take_damage(float damage) noexcept{

				if(damage >= valid_hit_point){
					return std::exchange(valid_hit_point, 0);
				}else{
					valid_hit_point -= damage;
					return damage;
				}
			}
		};

		export
		struct empty_building{};

		export
		struct building : ui_builder{
			friend building_data;
			friend game::ecs::component_custom_behavior<chamber::building_data>;
			friend game::ecs::component_custom_behavior<chamber::building>;

		private:
			building_data* data_{};

		protected:
			[[nodiscard]] float get_update_delta() const noexcept;

		public:
			[[nodiscard]] math::vec2 get_local_to_global(math::vec2 p) const noexcept;

			[[nodiscard]] math::trans2 get_local_to_global_trans(math::vec2 p) const noexcept;

			[[nodiscard]] math::trans2 get_local_to_global_trans(math::trans2 p) const noexcept;

			[[nodiscard]] building_data& data() const noexcept{
				assert(data_ != nullptr);
				return *data_;
			}

			~building() override = default;

			explicit(false) operator build_ref() noexcept{
				return reinterpret_cast<build_ref>(*this);
			}

			explicit(false) operator build_ptr() noexcept{
				return reinterpret_cast<build_ptr>(this);
			}

			explicit(false) operator const_build_ref() const noexcept{
				return reinterpret_cast<const_build_ref>(*this);
			}

			virtual void draw_hud_on_building_selection(graphic::renderer_ui_ref renderer) const{

			}

			virtual void draw_hud_on_grid_selection(graphic::renderer_ui_ref renderer) const{

			}

			virtual void draw_upper_building(world::graphic_context& graphic_context) const{

			}

			virtual void draw_chamber(world::graphic_context& graphic_context) const{

			}

			virtual void update(world::entity_top_world& top_world){

			}

		};

		export
		template <std::derived_from<building> T = building>
		[[nodiscard]] T& building_cast(build_ref ptr) noexcept{
#if DEBUG_CHECK
			return dynamic_cast<T&>(reinterpret_cast<building&>(ptr));
#else
			return static_cast<T&>(reinterpret_cast<building&>(ptr));

			// if constexpr (std::is_pointer_interconvertible_base_of_v<building, T>){
			// 	return *reinterpret_cast<T*>(ptr);
			// }else{
			// 	return *static_cast<T*>(reinterpret_cast<building*>(ptr));
			// }
#endif
		}

		export
		template <std::derived_from<building> T = building>
		[[nodiscard]] const T& building_cast(const_build_ref ptr) noexcept{
			return building_cast<T>(const_cast<build_ref>(ptr));
		}

		export
		template <typename ...Bases>
		struct building_trait_base{
			using base_types = std::tuple<building, ui_builder, Bases ...>;
		};

		export
		struct power_generator_building_tag{};

		export
		struct power_consumer_building_tag{};

		export
		struct upper_building_drawable_tag{};

		export
		enum class building_data_category{
			general,
			structural
		};

		export
		struct building_data : chunk_meta{
			friend chamber_grid_fields;
			friend chamber_manifold;
			friend ecs::component_custom_behavior<building_data>;
			friend chamber_manifold_dump;

		private:
			tile_region region_{};
			chamber_manifold* grid_{};

		public:
			building_data_category category{};

			hit_point hit_point{};

			std::vector<tile_damage_event> damage_events{};

			//TODO should this be public?
			//TODO uses unique ptr to save bytes?

			//
			std::vector<tile_status> tile_states{};
			float building_damage_take{};

			energy_status energy_status{}; //TODO private?
			energy_acquisition ideal_energy_acquisition{};

			unsigned valid_energy{}; //TODO private?

		private:
			energy_dynamic_status energy_dynamic_status_{};

		public:
			bool set_ideal_energy_acquisition(energy_acquisition acq);
			bool set_ideal_energy_acquisition(unsigned min, unsigned max);
			bool set_ideal_energy_acquisition(float priority);

			void set_energy_status(const chamber::energy_status& status) noexcept{
				energy_status = status;
				energy_dynamic_status_ = {};

				if(energy_status.power < 0){
					ideal_energy_acquisition.maximum_count = -energy_status.power;
					ideal_energy_acquisition.minimum_count = 1;
				}else{
					ideal_energy_acquisition = {};
				}

			}

			[[nodiscard]] energy_acquisition get_real_energy_acquisition() const noexcept{
				unsigned max = math::abs(get_max_usable_energy());
				return energy_acquisition{
						.maximum_count = math::min(max, ideal_energy_acquisition.maximum_count),
						.minimum_count = math::min(max, ideal_energy_acquisition.minimum_count),
						.priority = ideal_energy_acquisition.priority,
					};
			}

			[[nodiscard]] const energy_dynamic_status& get_energy_dynamic_status() const noexcept{
				return energy_dynamic_status_;
			}

			[[nodiscard]] float get_capability_factor() const noexcept{
				return hit_point.get_capability_factor();
			}

			[[nodiscard]] float get_efficiency() const noexcept{
				return energy_status ? energy_dynamic_status_.get_energy_factor(energy_status) : 1;
			}

			[[nodiscard]] int get_max_usable_energy() const noexcept{
				return energy_status.power * hit_point.get_capability_factor();
			}

			[[nodiscard]] tile_region region() const noexcept{
				return region_;
			}

			[[nodiscard]] tile_coord get_src_coord() const noexcept{
				return region_.src;
			}

			[[nodiscard]] math::vec2 real_extent() const noexcept{
				return region_.extent().scl(tile_size_integral).as<float>();
			}

			[[nodiscard]] chamber_manifold& grid() const noexcept{
				assert(grid_ != nullptr);
				return *grid_;
			}

			decltype(auto) operator[](this auto&& self, tile_coord global_pos) noexcept{
				return self.tile_states[self.local_to_index(global_pos - self.region_.src)];
			}

			decltype(auto) operator[](this auto&& self, const tile& tile) noexcept;

			[[nodiscard]] float get_tile_individual_max_hitpoint() const noexcept{
				return hit_point.max / region_.area() * 2;
			}

			template <std::integral T = tile_coord::value_type>
			[[nodiscard]] std::size_t local_to_index(math::vector2<T> coord) const noexcept{
				return coord.x + coord.y * region_.width();
			}

			[[nodiscard]] math::usize2 index_to_local(unsigned index) const noexcept{
				return math::vector2{index % region_.width(), index / region_.width()};
			}

			[[nodiscard]] bool has_any_hit_events() const noexcept{
				return !damage_events.empty();
			}

			void clear_hit_events() noexcept{
				damage_events.clear();
				building_damage_take = 0;
			}


			damage_consume_result consume_damage(tile_coord coord, damage_group& total_damage){
				// if(critical_damage_take > hit_point.cur)return damage_consume_result::hitpoint_exhaust;
				auto sum = total_damage.sum();

				if(sum <= 0.f){
					return damage_consume_result::damage_exhaust;
				}

				auto local = (coord - region_.src);
				auto dmg = tile_states[local_to_index(local)].take_damage(sum);

				if(dmg > 0.){
					building_damage_take += dmg;
					damage_events.emplace_back(local.as<unsigned short>(), sum);
					total_damage.material_damage.direct -= dmg;
					return damage_consume_result::success;
				}else{
					return damage_consume_result::hitpoint_exhaust;
				}
			}

			energy_status_update_result update_energy_state(float update_delta_tick) noexcept{
				energy_status_update_result rst{};
				if(energy_status){
					rst.changed = energy_dynamic_status_.update(energy_status, valid_energy, get_capability_factor(), update_delta_tick);
				}

				rst.power = energy_dynamic_status_.power;
				return rst;
			}

		public:
			void notify_grid_power_state_maybe_changed() const noexcept;

			using chunk_meta::chunk_meta;

			[[nodiscard]] building_data() = default;

			[[nodiscard]] building_data(
				const tile_region& region, chamber_manifold* grid)
				: region_(region),
				  grid_(grid){
				if (region.area() == 0){
					throw std::invalid_argument("tile region has no area");
				}
			}

			[[nodiscard]] constexpr math::frect get_bound() const noexcept{
				return region_.as<float>().scl(tile_size, tile_size);
			}

			[[nodiscard]] math::trans2 get_trans() const noexcept;

			[[nodiscard]] math::trans2 get_trans_offseted(math::vec2 scale) const noexcept{
				const auto trs = get_trans();
				return {real_extent() * scale | trs, trs.rot};
			}

			[[nodiscard]] math::vec2 get_pos_offseted(math::vec2 scale) const noexcept{
				return real_extent() * scale | get_trans();
			}

			[[nodiscard]] math::vec2 get_local_pos_offseted(const math::vec2 scale) const noexcept{
				return (region_.extent().as<float>() * scale + region_.get_src().as<float>()) * tile_size;
			}

			[[nodiscard]] const meta::chamber::grid_building* get_meta() const noexcept;
		};


	}


	// export
	// template <>
	// struct component_custom_behavior<chamber::building> : component_custom_behavior_base<chamber::building>{
	// 	using base_types = ui_builder;
	//
	// 	static void on_relocate(const chunk_meta& meta, value_type& comp){
	// 		comp.data_ = std::addressof(meta.id()->at<chamber::building_data>());
	// 	}
	// };

}


namespace mo_yanxi::game::ecs::chamber{
	template <typename ...T>
	using building_of = std::tuple<building_data, T...>;



	export
	struct building_entity_ref : entity_ref{
		using entity_ref::entity_ref;

		[[nodiscard]] explicit(false) building_entity_ref(entity_ref&& entity_id)
			: entity_ref(std::move(entity_id)){
		}

		[[nodiscard]] explicit(false) building_entity_ref(const entity_ref& entity_id)
			: entity_ref(entity_id){
		}

		[[nodiscard]] building_data& data() const noexcept{
			return id()->at<building_data>();
		}

		template <std::derived_from<building> Building = building>
		[[nodiscard]] Building& build() const noexcept{
			return id()->at<Building>();
		}

		template <std::derived_from<building> Building = building>
		[[nodiscard]] Building* build_if() const noexcept{
			return id()->try_get<Building>();
		}
	};

	export
	[[nodiscard]] constexpr math::vec2 grid_coord_to_real(tile_coord coord) noexcept{
		return (coord.as<float>() * tile_size).add(tile_size / 2, tile_size / 2);
	}

	struct tile{
		tile_coord tile_pos{};
		building_entity_ref building{};
		// bool placeable{};

		constexpr explicit operator bool() const noexcept{
			return static_cast<bool>(building);
		}

		void set_pos(tile_coord p) noexcept{
			this->tile_pos = p;
		}

		[[nodiscard]] constexpr math::frect get_bound() const noexcept{
			return math::frect{tags::unchecked, tile_pos.x * tile_size, tile_pos.y * tile_size, tile_size, tile_size};
		}


		[[nodiscard]] constexpr math::vec2 get_real_pos() const noexcept{
			return grid_coord_to_real(tile_pos);
		}


		[[nodiscard]] constexpr tile_coord get_local_tile_pos() const noexcept{
			if(building){
				return building.data().region().src - tile_pos;
			}
			return {};
		}

		[[nodiscard]] constexpr bool is_building_holder() const noexcept{
			if(building){
				return building.data().region().src == tile_pos;
			}else{
				return false;
			}
		}

		[[nodiscard]] tile_status& get_status() const noexcept{
			return building.data()[tile_pos];
		}
	};


	decltype(auto) building_data::operator[](this auto&& self, const tile& tile) noexcept{
		return std::forward_like<decltype(self)>(self[tile.tile_pos]);
	}
}
