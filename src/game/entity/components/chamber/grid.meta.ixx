module;

#include <cassert>
#include "plf_hive.h"

export module mo_yanxi.game.ecs.component.chamber:grid_meta;

import :chamber_meta;
import mo_yanxi.game.ecs.component.physical_property;
import mo_yanxi.game.physics;
import mo_yanxi.open_addr_hash_map;
import mo_yanxi.algo;
import mo_yanxi.algo.timsort;

namespace mo_yanxi::game::meta::chamber{
	export constexpr inline int tile_size_integral = ecs::chamber::tile_size_integral;
	export constexpr inline float tile_size = tile_size_integral;

	using chamber_meta = const basic_chamber*;


	export
	struct grid_building{
	private:
		math::upoint2 identity_pos{};
		chamber_meta meta_info;
		std::unique_ptr<chamber_instance_data> instance_data{};

	public:
		[[nodiscard]] grid_building(const math::upoint2& identity_pos, const basic_chamber& chamber)
			: identity_pos(identity_pos),
			  meta_info(std::addressof(chamber)), instance_data(chamber.create_instance_data()){
		}

		void set_chamber(const basic_chamber& chamber){
			meta_info = std::addressof(chamber);
			instance_data = chamber.create_instance_data();
		}

		[[nodiscard]] math::urect get_indexed_region() const noexcept{
			return {tags::from_extent, identity_pos, meta_info->extent};
		}

		[[nodiscard]] const basic_chamber& get_meta_info() const noexcept{
			assert(meta_info != nullptr);
			return *meta_info;
		}

		[[nodiscard]] chamber_instance_data* get_instance_data() const noexcept{
			return instance_data.get();
		}

		void reset_instance_data(){
			instance_data = meta_info->create_instance_data();
		}

		[[nodiscard]] math::upoint2 get_identity_pos() const noexcept{
			return identity_pos;
		}

		[[nodiscard]] bool has_instance_data() const noexcept{
			return instance_data != nullptr;
		}

		void try_draw(math::ivec2 origin_offset, graphic::renderer_ui_ref renderer, const float camera_scale) const{
			if(auto* p = get_instance_data()){
				p->draw(
					get_meta_info(),
					math::irect{tags::from_extent, identity_pos.as<int>() + origin_offset, meta_info->extent.as<int>()}.
					scl(tile_size_integral, tile_size_integral).as<float>(),
					renderer, camera_scale
				);
			}
		}
	};


	struct cascade_unqualified_buildings{
		std::vector<math::upoint2> building_pos{};
	};


	export
	struct grid_complex{
		plf::hive<grid_building> buildings{};
	};

	export using corridor_group_id = unsigned;
	export constexpr inline corridor_group_id invalid_group_id = std::numeric_limits<corridor_group_id>::max();

	export
	struct grid_tile{
		bool placeable{};

		bool is_corridor{}; //TODO make it uint8 t as room capacity
		corridor_group_id corridor_group_id{std::numeric_limits<chamber::corridor_group_id>::max()};
		grid_building* building{};

		[[nodiscard]] bool is_accessible() const noexcept{
			return building && is_corridor;
		};

		[[nodiscard]] bool is_building_identity(const math::upoint2 tile_pos) const noexcept{
			return building && building->get_identity_pos() == tile_pos;
		}


		[[nodiscard]] bool is_idle() const noexcept{
			return placeable && !building;
		}
	};
}

namespace mo_yanxi::game{

	// template <>
	// struct quad_tree_trait_adaptor<const meta::chamber::grid_building*, int> : quad_tree_adaptor_base<const meta::chamber::grid_building*, int>{
	// 	[[nodiscard]] static rect_type get_bound(const_reference self) noexcept{
	// 		return self->get_indexed_region().as<int>().expand(self->get_meta_info().structural_adjacent_distance());
	// 	}
	//
	// 	[[nodiscard]] static bool contains(const_reference self, vector_type::const_pass_t point){
	// 		return get_bound(self).contains(point);
	// 	}
	// };

	namespace meta::chamber{

		export constexpr inline unsigned grid_placeable_tile_weight = 50;


		export
		struct grid{
			using index_coord = math::upoint2;

		private:

			/**
			 * @brief Coord of the tile to be the real original tile after instancing
			 */
			math::point2 origin_coord_{};
			math::usize2 extent_{};
			std::vector<grid_tile> tiles_{};
			plf::hive<grid_building> buildings_{};

			struct structural_status{
			private:
				struct tile_structural_status{
					unsigned char is_source : 1;
					unsigned char count : 7;

					bool any() const noexcept{
						return count > 0;
					}

				};

				std::vector<tile_structural_status> valid_indexed_points_{};
				unsigned count{};

				tile_structural_status& value_at(math::upoint2 pos, math::usize2 extent) noexcept{
					return valid_indexed_points_[pos.x + pos.y * extent.x];
				}

			public:
				[[nodiscard]] const tile_structural_status& value_at(math::upoint2 pos, math::usize2 extent) const noexcept{
					return  valid_indexed_points_[pos.x + pos.y * extent.x];
				}

				[[nodiscard]] structural_status() = default;

				[[nodiscard]] explicit structural_status(math::isize2 region)
					: valid_indexed_points_(region.area()){
				}

				[[nodiscard]] bool empty() const noexcept{
					return count == 0;
				}

				void update_on_erase(const grid_building& building, math::usize2 extent) noexcept{
					if(auto dst = building.get_meta_info().structural_adjacent_distance()){
						--count;

						building.get_indexed_region().each([&, this](math::upoint2 pos){
							value_at(pos, extent).is_source = false;
						});

						building.get_indexed_region().as<int>().expand(dst).intersection_with(math::irect{extent}).as<unsigned>().each([&, this](math::upoint2 pos){
							--value_at(pos, extent).count;
						});

						//TODO erase proximity check
					}
				}

				void update_on_add(const grid_building& building, math::usize2 extent) {
					if(auto dst = building.get_meta_info().structural_adjacent_distance()){
						++count;

						building.get_indexed_region().each([&, this](math::upoint2 pos){
							value_at(pos, extent).is_source = true;
						});

						building.get_indexed_region().as<int>().expand(dst).intersection_with(math::irect{extent}).as<unsigned>().each([&, this](math::upoint2 pos){
							++value_at(pos, extent).count;
						});
					}
				}

				[[nodiscard]] bool is_within_structural(math::urect indexed_region, math::usize2 extent) const noexcept{
					return indexed_region.each_any([&, this](math::upoint2 pos){
						return value_at(pos, extent).count;
					});
				}

				[[nodiscard]] bool is_within_structural(math::upoint2 indexed_pos, math::usize2 extent) const noexcept{
					return value_at(indexed_pos, extent).count;
				}
			};

			struct energy_status{
				friend grid;

			private:
				std::vector<grid_building*> energy_consumer{};
				std::vector<grid_building*> energy_generator{};

				unsigned maximum_energy_generate_{};
				unsigned maximum_energy_consume_{};

				void update_energy_status_on_erase(grid_building& building) noexcept{
					if(auto e_usage = building.get_meta_info().get_energy_usage()){
						if(e_usage > 0){
							algo::erase_unique_unstable(energy_generator, &building);
							maximum_energy_generate_ -= e_usage;
						}else{
							algo::erase_unique_unstable(energy_consumer, &building);
							maximum_energy_consume_ -= static_cast<decltype(maximum_energy_consume_)>(-e_usage);
						}
					}
				}

				void update_energy_status_on_add(grid_building& building){
					if(auto e_usage = building.get_meta_info().get_energy_usage()){
						if(e_usage > 0){
							energy_generator.push_back(&building);
							maximum_energy_generate_ += e_usage;
						}else{
							energy_consumer.push_back(&building);
							maximum_energy_consume_ += static_cast<decltype(maximum_energy_consume_)>(-e_usage);
						}
					}
				}

			public:
				[[nodiscard]] auto max_generate() const noexcept{
					return maximum_energy_generate_;
				}

				[[nodiscard]] auto max_consume() const noexcept{
					return maximum_energy_consume_;
				}

				[[nodiscard]] std::span<const grid_building* const> generators() const noexcept{
					return energy_generator;
				}

				[[nodiscard]] std::span<const grid_building* const> consumers() const noexcept{
					return energy_consumer;
				}

			};


			energy_status energy_status_{};
			structural_status structural_status_{};
			unsigned total_mass_{};
			unsigned total_moment_of_inertia_{};

		public:

			[[nodiscard]] grid() noexcept = default;

			[[nodiscard]] explicit grid(const physics::collision_shape& shape);

			[[nodiscard]] grid(const math::usize2 extent, const math::point2 origin_coord, std::vector<grid_tile>&& tiles) noexcept;

			[[nodiscard]] grid(const math::usize2 extent, const math::point2 origin_coord, std::span<const grid_tile> tiles) :
				grid(extent, origin_coord, {std::from_range, tiles}){}

			[[nodiscard]] unsigned mass() const noexcept{
				return total_mass_;
			}

			[[nodiscard]] unsigned moment_of_inertia() const noexcept{
				return total_moment_of_inertia_;
			}

			[[nodiscard]] const energy_status& get_energy_status() const noexcept{
				return energy_status_;
			}

			[[nodiscard]] math::vector2<int> get_origin_coord() const noexcept{
				return origin_coord_;
			}

			[[nodiscard]] math::vector2<int> get_origin_offset() const noexcept{
				return -origin_coord_;
			}

			[[nodiscard]] const decltype(buildings_)& get_buildings() const noexcept{
				return buildings_;
			}

			[[nodiscard]] std::span<const grid_tile> get_tiles() const noexcept{
				return tiles_;
			}

			void move(math::point2 offset) noexcept;

			[[nodiscard]] math::usize2 get_extent() const noexcept{
				return extent_;
			}

			[[nodiscard]] math::irect get_grid_region() const noexcept{
				return math::irect{tags::from_extent, -origin_coord_, extent_.as<int>()};
			}

			[[nodiscard]] std::optional<index_coord> coord_to_index(math::point2 world_tile_coord) const noexcept{
				world_tile_coord += origin_coord_;
				if(!math::irect{extent_.as<int>()}.contains(world_tile_coord))return {};
				return world_tile_coord.as<unsigned>();
			}

			[[nodiscard]] std::optional<math::urect> coord_to_index(math::irect world_tile_region) const noexcept{
				world_tile_region.src += origin_coord_;
				if(!math::irect{extent_.as<int>()}.contains_loose(world_tile_region))return {};
				return world_tile_region.as<unsigned>();
			}

			[[nodiscard]] corridor_group_id get_tile_corridor_id(math::upoint2 pos) const noexcept{
				return get_tile_corridor_id(pos_index_to_vec_index(pos));
			}

			[[nodiscard]] corridor_group_id get_tile_corridor_id(corridor_group_id cid) const noexcept{
				auto& t = tiles_[cid];
				if(!t.is_accessible())return invalid_group_id;
				if(t.corridor_group_id != cid){
					return get_tile_corridor_id(t.corridor_group_id);
				}
				return t.corridor_group_id;
			}

			[[nodiscard]] bool reachable_between(math::upoint2 p1, math::upoint2 p2) const noexcept{
				auto g1 = get_tile_corridor_id(p1);
				return g1 == get_tile_corridor_id(p2) && g1 != invalid_group_id;
			}

		public:
			bool set_corridor_at(math::upoint2 indexed_pos, bool accessible) noexcept {
				auto union_set_finder = [this](this const auto& self, corridor_group_id cid) noexcept -> corridor_group_id {
					auto& t = tiles_[cid];
					if(t.corridor_group_id != cid){
						auto root = self(t.corridor_group_id);
						t.corridor_group_id = root;
					}
					return t.corridor_group_id;
				};

				auto union_union_set = [&](corridor_group_id cur, corridor_group_id target) noexcept{
					if(tiles_[target].corridor_group_id == invalid_group_id)return;

					auto rootA = union_set_finder(cur);
					auto rootB = union_set_finder(target);

					if(rootA != rootB){
						tiles_[rootA].corridor_group_id = rootB;
					}
				};

				static constexpr std::array<math::point2, 4> off{math::point2{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
				auto& tile = tile_at(indexed_pos);
				if(accessible && !tile.is_corridor){
					tile.is_corridor = true;
					tile.corridor_group_id = pos_index_to_vec_index(indexed_pos);
					if(tile.is_accessible())for (const auto& vector2 : off){
						auto next = indexed_pos.as<int>() + vector2;
						if(next.x < 0 || next.y < 0 || next.x >= extent_.x || next.y >= extent_.y){
							continue;
						}
						union_union_set(pos_index_to_vec_index(indexed_pos), pos_index_to_vec_index(next.as<unsigned>()));

					}
					return true;
				}else if(!accessible && tile.is_corridor){
					optimal_corridor_group_id();
					tile.is_corridor = false;
					tile.corridor_group_id = invalid_group_id;
					if(!tile.is_accessible())return true;

					std::array<corridor_group_id, 4> nearby_idt{};
					nearby_idt.fill(invalid_group_id);

					std::uint8_t paired_groups{};

					{
						for(unsigned i = 0; i < off.size(); ++i){
							const auto next = indexed_pos.as<int>() + off[i];
							if(!next.within({}, extent_.as<int>())){
								continue;
							}
							nearby_idt[i] = tile_at(next.as<unsigned>()).corridor_group_id;
						}

						const auto target_index = pos_index_to_vec_index(indexed_pos);

						for(unsigned i = 0; i < nearby_idt.size(); ++i){
							if(nearby_idt[i] == target_index){
								paired_groups |= 1u << i;
							}

							for(unsigned j = 0; j < nearby_idt.size() - i - 1; ++j){
								if(nearby_idt[i] == nearby_idt[j + 1 + i] && nearby_idt[i] != invalid_group_id){
									paired_groups |= 1u << i | 1u << (j + 1 + i);
								}
							}
						}
					}


					for(corridor_group_id idx = 0; idx < tiles_.size(); ++idx){
						auto& tileCur = tiles_[idx];
						if(!tileCur.is_accessible())continue;

						for(unsigned i = 0; i < nearby_idt.size(); ++i){
							if(((paired_groups >> i) & 0b1u) && nearby_idt[i] == tileCur.corridor_group_id && tileCur.corridor_group_id != idx){
								tileCur.corridor_group_id = invalid_group_id;
								break;
							}
						}
					}

					for(unsigned y = 0; y < extent_.y; ++y){
						for(unsigned x = 0; x < extent_.x; ++x){
							const math::upoint2 pos{x, y};
							auto& cur_tile = tile_at(pos);
							if(!cur_tile.is_accessible() || cur_tile.corridor_group_id != invalid_group_id) continue;

							cur_tile.corridor_group_id = pos_index_to_vec_index(pos);
							for(const auto& vector2 : off){
								auto next = pos.as<int>() + vector2;
								if(!next.within({}, extent_.as<int>())){
									continue;
								}

								union_union_set(cur_tile.corridor_group_id, pos_index_to_vec_index(next.as<unsigned>()));
							}
						}
					}

					return true;
				}
				return false;

			}

			bool set_placeable_at(math::upoint2 indexed_pos, bool placeable) noexcept {
				auto& t = tile_at(indexed_pos);
				if(t.is_idle() && !placeable){
					t.placeable = false;
					total_mass_ -= grid_placeable_tile_weight;
					total_moment_of_inertia_ -= get_moment_of_inertia_of_tile(indexed_pos);
					return true;
				}else if(!t.placeable && placeable){
					t.placeable = true;
					total_mass_ += grid_placeable_tile_weight;
					total_moment_of_inertia_ += get_moment_of_inertia_of_tile(indexed_pos);
					return true;
				}
				return false;
			}

			grid_building* try_place_building_at(const index_coord indexed_pos, const basic_chamber& info){
				if(!is_building_placeable_at(indexed_pos, info))return nullptr;
				return &place_building_at(indexed_pos, info);
			}

			template <std::invocable<const grid_building&> Fn = std::identity>
			cascade_unqualified_buildings erase_building_at(const math::upoint2 indexed_pos, Fn before_erase = {}) noexcept {
				if(indexed_pos.beyond_equal(extent_))return {};

				const auto& tile = tile_at(indexed_pos);
				auto* b = tile.building;
				if(!b)return {};

				energy_status_.update_energy_status_on_erase(*b);
				structural_status_.update_on_erase(*b, extent_);
				total_mass_ -= b->get_meta_info().mass;
				total_moment_of_inertia_ -= get_moment_of_inertia_of(*b);

				cascade_unqualified_buildings result{};

				if(const auto dst = b->get_meta_info().structural_adjacent_distance()){
					const auto region = b->get_indexed_region().as<int>().expand(dst).intersection_with({tags::unchecked, tags::from_extent, {}, extent_.as<int>()}).as<unsigned>();
					region.each([&, this](math::upoint2 pos){
						if(!std::as_const(structural_status_).value_at(pos, extent_).any()){
							if(auto bd = tile_at(pos).building){
								if(!std::ranges::contains(result.building_pos, bd->get_identity_pos())){
									result.building_pos.push_back(bd->get_identity_pos());
								}
							}
						}
					});

					std::erase_if(result.building_pos, [&, this](math::upoint2 pos){
						return structural_status_.is_within_structural(tile_at(pos).building->get_indexed_region(), extent_);
					});
				}

				std::invoke(before_erase, *b);
				tile.building->get_indexed_region().each([this](const math::upoint2 p){
					auto& tile = tile_at(p);
					if(tile.is_accessible()){
						set_corridor_at(p, false);
						tile.is_corridor = true;
						tile.corridor_group_id = pos_index_to_vec_index(p);
					}
				});

				tile.building->get_indexed_region().each([this](const math::upoint2 p){
					tile_at(p).building = nullptr;
				});

				const auto itr = buildings_.get_iterator(b);
				assert(itr != buildings_.end());
				buildings_.erase(itr);
				return result;
			}

			[[nodiscard]] bool is_building_placeable_at(const index_coord indexed_pos, const basic_chamber& info) const noexcept{
				const math::urect region{tags::from_extent, indexed_pos, info.extent};

				return is_building_basic_placeable_at(region) && is_building_structural_placeable_at(indexed_pos, info);
			}

		// private:
			//TODO make this function private
			grid_building& place_building_at(const index_coord indexed_pos, const basic_chamber& info){
				auto& b = buildings_.insert(grid_building{indexed_pos.as<unsigned>(), info}).operator*();
				const math::urect region{tags::from_extent, indexed_pos.as<unsigned>(), info.extent};

				energy_status_.update_energy_status_on_add(b);
				structural_status_.update_on_add(b, extent_);

				total_mass_ += info.mass;
				total_moment_of_inertia_ += get_moment_of_inertia_of(b);

				region.each([&](const math::upoint2 p){
					auto& t = tile_at(p);
					t.building = std::addressof(b);
					if(t.is_corridor){
						t.is_corridor = false;
						set_corridor_at(p, true);
					}
				});
				return b;
			}
		// public:

			[[nodiscard]] bool is_building_basic_placeable_at(const math::urect region) const noexcept{
				if(!math::urect{extent_}.contains_loose(region))return false;

				for(auto y = region.src.y; y < region.get_end_y(); ++y){
					for(auto x = region.src.x; x < region.get_end_x(); ++x){
						auto& tile = tile_at({x, y});
						if(!tile.placeable || tile.building != nullptr)return false;
					}
				}

				return true;
			}

			[[nodiscard]] bool is_building_structural_placeable_at(const index_coord indexed_pos, const basic_chamber& info) const noexcept{
				const math::urect region{tags::from_extent, indexed_pos, info.extent};
				if(auto dst = info.structural_adjacent_distance()){
					// if(!structural_status_.empty()){
					// 	if(
					// 		auto nearby = math::rect::get_proximity_of(region.as<int>());
					// 		std::ranges::none_of(nearby, [this](math::irect rect_ortho){
					//
					// 		return rect_ortho.each_any([this](math::point2 pos){
					// 			if(pos.x < 0 || pos.y < 0)return false;
					// 			return (bool)std::as_const(structural_status_).value_at(pos.as<unsigned>(), extent_).is_source;
					// 		});
					//
					// 	})){
					// 		return false;
					// 	}
					// }

				}else{
					if(!structural_status_.is_within_structural(region, extent_))return false;
				}

				return true;
			}

			[[nodiscard]] bool is_placeable_at(const math::urect region_in_index) const noexcept{
				for(unsigned y = 0; y < region_in_index.height(); ++y){
					for(unsigned x = 0; x < region_in_index.width(); ++x){
						if(!tile_at(region_in_index.src + math::vector2{x, y}).placeable)return false;
					}
				}

				return true;
			}

		private:
			[[nodiscard]] grid_tile& tile_at(const math::upoint2 index_pos) noexcept{
				assert(index_pos.within(extent_));
				return tiles_[extent_.x * index_pos.y + index_pos.x];
			}

			[[nodiscard]] const grid_tile& tile_at(const math::upoint2 index_pos) const noexcept{
				assert(index_pos.within(extent_));
				return tiles_[extent_.x * index_pos.y + index_pos.x];
			}

		public:
			void optimal_corridor_group_id() noexcept{
				auto union_set_finder = [this](this const auto& self, corridor_group_id cid) noexcept -> corridor_group_id {
					auto& t = tiles_[cid];
					if(t.corridor_group_id != cid){
						t.corridor_group_id = self(t.corridor_group_id);
					}
					return t.corridor_group_id;
				};

				for(std::size_t i = 0; i < tiles_.size(); ++i){
					if(tiles_[i].is_accessible()){
						union_set_finder(i);
					}
				}
				// for(std::size_t i = 0; i < tiles_.size(); ++i){
				// 	if(tiles_[i].is_corridor){
				// 		assert(tiles_[i].corridor_group_id == union_set_finder(i));
				// 	}
				// }
			}

			decltype(auto) operator[](this const grid& self, math::upoint2 index_pos) noexcept{
				return self.tile_at(index_pos);
			}

			decltype(auto) operator[](this const grid& self, unsigned x, unsigned y) noexcept{
				return self.tile_at({x, y});
			}


			[[nodiscard]] bool is_within_structural(math::urect indexed_region) const noexcept{
				return structural_status_.is_within_structural(indexed_region, extent_);
			}

			[[nodiscard]] bool is_within_structural(math::upoint2 indexed_pos) const noexcept{
				return structural_status_.is_within_structural(indexed_pos, extent_);
			}


			//TODO move draw to other place?


			void dump(ecs::chamber::chamber_manifold& clear_grid_manifold) const;

		private:
			[[nodiscard]] unsigned get_moment_of_inertia_of(const grid_building& b) const noexcept{
				return b.get_meta_info().mass * b.get_indexed_region().get_center().as<int>().dst2(get_origin_coord());
			}

			[[nodiscard]] unsigned get_moment_of_inertia_of_tile(math::upoint2 pos) const noexcept{
				return math::dst2<int>(pos.x, pos.y, get_origin_coord().x, get_origin_coord().y) * grid_placeable_tile_weight;
			}

			[[nodiscard]] std::size_t pos_index_to_vec_index(math::upoint2 pos) const noexcept{
				return pos.x + pos.y * extent_.x;
			}

		public:
			static math::point2 pos_to_tile_coord(math::vec2 coord) noexcept{
				return coord.div(tile_size).floor().as<int>();
			}
		};

		export
		struct grid_structural_statistic{
		private:
			struct structural_cluster{
				std::vector<const grid_building*> buildings;

				[[nodiscard]] structural_cluster() = default;

				[[nodiscard]] explicit(false) structural_cluster(const grid_building* idt) : buildings{idt}{

				}

				[[nodiscard]] const grid_building* identity() const noexcept{
					return buildings.front();
				}

				void add(const grid_building* b){
					buildings.push_back(b);
				}

				auto size() const noexcept{
					return buildings.size();
				}

				[[nodiscard]] float get_total_hitpoint() const noexcept{
					return std::ranges::fold_left(buildings | std::views::transform([](const grid_building* b){
						return b->get_meta_info().hit_point.max;
					}), 0.f, std::plus{});
				}
			};

			struct union_set_solver{
				struct cluster_node{
					const grid_building* identity{};
					const grid_building* parent{};

					[[nodiscard]] cluster_node() = default;

					[[nodiscard]] explicit(false) cluster_node(const grid_building& building) : identity(&building), parent{identity}{

					}

					bool operator==(const cluster_node&) const noexcept = default;

					auto operator<=>(const cluster_node& other) const noexcept{
						return std::compare_three_way{}(identity, other.identity);
					}
				};

				std::vector<cluster_node> structurals{};

				[[nodiscard]] union_set_solver() = default;

				[[nodiscard]] union_set_solver(const grid& grid) :
					structurals{
						std::from_range, grid.get_buildings() | std::views::filter([](const grid_building& b){
							return b.get_meta_info().structural_adjacent_distance();
						})
					}{
					algo::timsort(structurals);

					const math::irect bound{grid.get_extent().as<int>()};
					for(auto& structural : structurals){
						const auto nearby = math::rect::get_proximity_of(
							structural.identity->get_indexed_region().as<int>());
						for(const auto& rect_ortho : nearby){
							rect_ortho.each([&](math::point2 pos){
								if(!bound.contains(pos)) return;
								const auto building = grid[pos.as<unsigned>()].building;
								if(building && building->get_meta_info().is_structural()){
									union_clusters(structural, (*this)[building]);
								}
							});
						}
					}
				}

				// 查找根节点（路径压缩）
				cluster_node& find(cluster_node& c) noexcept{
					if(c.parent != c.identity){
						auto root = find((*this)[c.parent]);
						c.parent = root.identity;
					}
					return (*this)[c.parent];
				}

				// 合并两个集群
				void union_clusters(cluster_node& a, cluster_node& b) noexcept{
					auto& rootA = find(a);
					auto& rootB = find(b);

					if(rootA != rootB){
						rootB.parent = rootA.identity;
					}
				}

				cluster_node& operator[](const grid_building* where) noexcept{
					return std::ranges::lower_bound(structurals, where, {}, &cluster_node::identity).operator*();
				}

				// 获取所有连通分量的方法
				[[nodiscard]] std::vector<structural_cluster> get_connected_components() {
					std::vector<structural_cluster> result{};

					for (const auto & structural : structurals){
						if(structural.identity == structural.parent){
							result.push_back({structural.identity});
						}
					}

					std::ranges::sort(result, {}, &structural_cluster::identity);

					for (auto& cluster : structurals) {
						auto& root = find(cluster);
						if(cluster.identity == root.identity)continue;
						std::ranges::lower_bound(result, root.identity, {}, &structural_cluster::identity)->add(cluster.identity);
					}

					return result;
				}
			};

			//clusters sort by identity
			std::vector<structural_cluster> clusters_{};
		public:
			[[nodiscard]] grid_structural_statistic() = default;

			[[nodiscard]] explicit(false) grid_structural_statistic(const grid& grid) : clusters_(union_set_solver{grid}.get_connected_components()){
			}

			[[nodiscard]] const structural_cluster& operator[](const grid_building* cluster_identity) const noexcept{
				const auto itr = std::ranges::lower_bound(clusters_, cluster_identity, {}, &structural_cluster::identity);
				assert(itr != clusters_.end());
				return *itr;
			}

			[[nodiscard]] auto size() const noexcept{
				return clusters_.size();
			}

			[[nodiscard]] std::span<const structural_cluster> get_clusters() const noexcept{
				return clusters_;
			}

			[[nodiscard]] float get_structural_hitpoint() const noexcept{
				std::size_t sum{};
				std::size_t max_count{};

				for (const auto & cluster : clusters_){
					max_count = std::ranges::max(max_count, cluster.size());
					sum += std::ranges::fold_left(cluster.buildings | std::views::transform([](const grid_building* b){
						return b->get_meta_info().hit_point.max;
					}), 0.f, std::plus{}) * cluster.size();
				}

				return static_cast<float>(static_cast<double>(sum) / static_cast<double>(std::max<std::size_t>(max_count + clusters_.size() - 1, 1)));
			}
		};

		struct grid_inertia_return_type{
			unsigned mass;
			unsigned unit_moment_of_inertia;
		};

		export
		[[nodiscard]] math::urect get_grid_corridor_region(const grid& grid) noexcept{
			const auto [w, h] = grid.get_extent();
			unsigned minx = w, miny = h, maxx = 0, maxy = 0;
			for(unsigned x = 0; x < w; ++x){
				for(unsigned y = 0; y < h; ++y){
					auto& tile = grid[x, y];
					if(tile.is_accessible()){
						minx = std::min(minx, x);
						miny = std::min(miny, y);
						maxx = std::max(maxx, x);
						maxy = std::max(maxy, y);
					}
				}
			}

			if(minx > maxx || miny > maxy){
				return {};
			}

			return math::urect{tags::unchecked, tags::from_vertex, {minx, miny}, {maxx, maxy}};
		}


		export
		unsigned get_grid_mass(const grid& grid) noexcept{
			unsigned mass{};
			{
				auto rng = grid.get_tiles();
				mass = std::transform_reduce(std::execution::unseq, rng.begin(), rng.end(), mass, std::plus{}, [](const grid_tile& tile){
					return tile.placeable * grid_placeable_tile_weight;
				});
			}
			{
				auto& rng = grid.get_buildings();
				mass = std::transform_reduce(std::execution::unseq, rng.begin(), rng.end(), mass, std::plus{}, [](const grid_building& tile){
					return tile.get_meta_info().mass;
				});
			}
			return mass;
		}

		export
		unsigned get_grid_tile_moment_of_inertia(const grid& grid) noexcept{
			unsigned rst{};
			for(unsigned y = 0; y < grid.get_extent().y; ++y){
				for(unsigned x = 0; x < grid.get_extent().x; ++x){
					auto& t = grid[x, y];
					if(t.placeable){
						rst += math::dst2<int>(x, y, grid.get_origin_coord().x, grid.get_origin_coord().y) * grid_placeable_tile_weight;
					}
				}
			}
			return rst;
		}
	}
}
