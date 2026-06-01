export module mo_yanxi.game.ecs.component.chamber.damage_grid;

export import mo_yanxi.game.ecs.component.damage;
export import mo_yanxi.game.ecs.component.physics;
export import mo_yanxi.game.ecs.component.targeting;
export import mo_yanxi.game.ecs.object_storage;

import std;

namespace mo_yanxi::game::ecs::chamber{
	export constexpr inline float world_units_per_tile = 20.f;
	export constexpr inline float tile_size = world_units_per_tile;
	export constexpr inline std::uint32_t invalid_corridor_group = std::numeric_limits<std::uint32_t>::max();

	export
	[[nodiscard]] constexpr float tiles_to_world_units(const float tiles) noexcept{
		return tiles * world_units_per_tile;
	}

	export
	struct tile_region{
		math::point2 src{};
		math::point2 extent{};

		[[nodiscard]] constexpr int width() const noexcept{
			return extent.x;
		}

		[[nodiscard]] constexpr int height() const noexcept{
			return extent.y;
		}

		[[nodiscard]] constexpr int area() const noexcept{
			return width() * height();
		}

		[[nodiscard]] constexpr bool contains(const math::point2 coord) const noexcept{
			return coord.x >= src.x && coord.y >= src.y
				&& coord.x < src.x + extent.x
				&& coord.y < src.y + extent.y;
		}
	};

	export
	struct tile_damage_event{
		math::point2 tile_coord{};
		float actual_damage{};
	};

	export
	struct tile_status{
		float hit_point{};

		[[nodiscard]] constexpr bool destroyed() const noexcept{
			return hit_point <= 0.f;
		}

		constexpr float take_damage(const float amount) noexcept{
			if(amount <= 0.f || destroyed()){
				return 0.f;
			}
			const float actual = std::min(amount, hit_point);
			hit_point -= actual;
			return actual;
		}
	};

	export
	struct tile_state{
		bool placeable{true};
		bool corridor{};
	};

	export
	struct energy_acquisition{
		std::uint32_t maximum_count{};
		std::uint32_t minimum_count{};
		float priority{};

		[[nodiscard]] constexpr std::uint32_t get_append_count() const noexcept{
			return maximum_count - minimum_count;
		}

		friend constexpr bool operator==(const energy_acquisition&, const energy_acquisition&) noexcept = default;
	};

	export
	struct energy_status_update_result{
		int power{};
		bool changed{};
	};

	export
	struct maneuver_component{
		float force_longitudinal{};
		float force_transverse{};
		float torque{};
		float torque_absolute{};
		float boost{};

		[[nodiscard]] constexpr maneuver_component operator*(const float value) const noexcept{
			return {
				.force_longitudinal = force_longitudinal * value,
				.force_transverse = force_transverse * value,
				.torque = torque * value,
				.torque_absolute = torque_absolute * value,
				.boost = boost * value
			};
		}
	};

	export
	struct energy_status{
		int power{};
		float charge_duration{};
		bool reserve_energy_if_power_off{};
		bool reserve_charge_reload_if_power_off{};
		bool safe_under_damage{};

		constexpr explicit operator bool() const noexcept{
			return power != 0;
		}

		[[nodiscard]] constexpr bool is_consumer() const noexcept{
			return power < 0;
		}

		[[nodiscard]] constexpr bool is_generator() const noexcept{
			return power > 0;
		}

		[[nodiscard]] constexpr std::uint32_t abs_power() const noexcept{
			const int value = power < 0 ? -power : power;
			return static_cast<std::uint32_t>(value);
		}
	};

	export
	struct energy_dynamic_status{
		int power{};
		float charge{};

		[[nodiscard]] constexpr float get_energy_factor(const energy_status status) const noexcept{
			if(status.power == 0){
				return 1.f;
			}
			return static_cast<float>(power) / static_cast<float>(status.power);
		}

		constexpr explicit operator bool() const noexcept{
			return power != 0;
		}

		bool update(
			const energy_status status,
			const std::uint32_t assigned_energy,
			const float capability_factor,
			const float delta_seconds) noexcept{
			const int previous = power;
			if(status.power == 0){
				power = 0;
				charge = 0.f;
				return previous != power;
			}

			const int direction = status.power > 0 ? 1 : -1;
			if(power * direction < 0){
				power = 0;
				charge = 0.f;
			}

			const int max_usable = static_cast<int>(std::trunc(static_cast<float>(status.power) * capability_factor));
			const int max_abs = max_usable > 0
				? max_usable
				: static_cast<int>(std::min<std::uint32_t>(
					static_cast<std::uint32_t>(-max_usable),
					assigned_energy));
			const int current_abs = power < 0 ? -power : power;
			if(max_abs <= 0){
				if(!status.reserve_energy_if_power_off){
					power = 0;
				}
				if(!status.reserve_charge_reload_if_power_off){
					charge = 0.f;
				}
				return previous != power;
			}

			if(current_abs >= max_abs){
				if(!status.reserve_energy_if_power_off){
					power = direction * max_abs;
				}
				if(!status.reserve_charge_reload_if_power_off){
					charge = 0.f;
				}
				return previous != power;
			}

			if(charge < status.charge_duration){
				charge += delta_seconds;
			} else{
				charge = 0.f;
				power += direction;
			}
			return previous != power;
		}
	};

	export
	struct building_common{
		object_handle handle{};
		tile_region region{};
		hit_point hit_points{};
		std::uint32_t tile_status_offset{};
		std::uint32_t tile_status_count{};
		std::vector<tile_damage_event> damage_events{};
		float pending_damage{};
		energy_status energy{};
		energy_acquisition ideal_energy_acquisition{};
		std::uint32_t valid_energy{};
		energy_dynamic_status energy_dynamic{};
		maneuver_component ideal_maneuver{};
		bool structural{};
		std::uint32_t structural_support_radius{};
		bool maneuvering{};
		targetable_profile targetable{};

		[[nodiscard]] constexpr bool has_tile_statuses() const noexcept{
			return tile_status_count != 0;
		}

		[[nodiscard]] constexpr std::size_t local_tile_index(const math::point2 coord) const noexcept{
			const math::point2 local = coord - region.src;
			return static_cast<std::size_t>(local.x + local.y * region.width());
		}

		void set_energy_status(const chamber::energy_status status) noexcept{
			energy = status;
			energy_dynamic = {};
			valid_energy = 0;
			if(energy.is_consumer()){
				ideal_energy_acquisition.maximum_count = energy.abs_power();
				ideal_energy_acquisition.minimum_count = std::min<std::uint32_t>(1u, ideal_energy_acquisition.maximum_count);
			} else{
				ideal_energy_acquisition = {};
			}
		}

		[[nodiscard]] constexpr float get_capability_factor() const noexcept{
			return hit_points.get_capability_factor();
		}

		[[nodiscard]] int get_max_usable_energy() const noexcept{
			return static_cast<int>(std::trunc(static_cast<float>(energy.power) * this->get_capability_factor()));
		}

		[[nodiscard]] energy_acquisition get_real_energy_acquisition() const noexcept{
			const int max_usable = this->get_max_usable_energy();
			const std::uint32_t maximum = max_usable < 0 ? static_cast<std::uint32_t>(-max_usable) : 0u;
			return {
				.maximum_count = std::min(maximum, ideal_energy_acquisition.maximum_count),
				.minimum_count = std::min(maximum, ideal_energy_acquisition.minimum_count),
				.priority = ideal_energy_acquisition.priority
			};
		}

		[[nodiscard]] float get_efficiency() const noexcept{
			return energy ? energy_dynamic.get_energy_factor(energy) : 1.f;
		}

		[[nodiscard]] energy_status_update_result update_energy_state(const float delta_seconds) noexcept{
			energy_status_update_result result{};
			if(energy){
				result.changed = energy_dynamic.update(
					energy,
					valid_energy,
					this->get_capability_factor(),
					delta_seconds);
			}
			result.power = energy_dynamic.power;
			return result;
		}

		void set_maneuver(const maneuver_component maneuver, const bool enabled = true) noexcept{
			ideal_maneuver = maneuver;
			maneuvering = enabled;
		}
	};

	export
	struct basic_building{
	};

	export
	struct armor_building{
	};

	export
	struct energy_generator_building{
	};

	export
	struct thruster_building{
		maneuver_component ideal_maneuver{};
	};

	export
	struct radar_building{
		float rotation{};
		targeting_sensor sensor{.max_targets = 1u};
		target_memory memory{};
	};

	export
	struct turret_building{
		float rotation{};
		float reload_seconds{};
		float reload_timer{};
		float shooting_field_angle{};
		float projectile_speed{10.f};
		damage_group projectile_damage{.material_damage = {.direct = 10.f}};
		std::uint32_t max_targets{1u};
		target_memory memory{};
	};

	export
	struct structural_joint_building{
	};

	export
	enum class standard_building_type{
		armor,
		energy_generator,
		thruster,
		radar,
		turret,
		structural_joint
	};

	export
	enum class chamber_dump_building_type{
		basic,
		armor,
		energy_generator,
		thruster,
		radar,
		turret,
		structural_joint
	};

	export
	struct chamber_building_dump{
		chamber_dump_building_type type{};
		tile_region region{};
		hit_point hit_points{};
		std::vector<tile_status> tile_statuses{};
		energy_status energy{};
		energy_acquisition energy_acquisition{};
		std::uint32_t valid_energy{};
		energy_dynamic_status energy_dynamic{};
		maneuver_component maneuver{};
		bool maneuvering{};
		bool structural{};
		std::uint32_t structural_support_radius{};
		targetable_profile targetable{};
		thruster_building thruster{};
		radar_building radar{};
		turret_building turret{};
	};

	export
	struct chamber_dump{
		math::point2 extent{};
		std::vector<tile_state> tile_states{};
		hit_point structural_hit_points{};
		std::vector<chamber_building_dump> buildings{};
	};

	export
	using chamber_target_candidate = target_snapshot;

	export
	struct chamber_fire_request{
		object_handle turret{};
		target_ref target{};
		math::vec2 origin{};
		math::vec2 direction{1.f, 0.f};
		float projectile_speed{};
		damage_group damage{};
		std::uint32_t faction_id{};
	};

	export
	struct chamber_manifold;

	export
	struct chamber_update_context{
		chamber_manifold& chamber;
		float delta_seconds{};
	};

	export
	struct basic_building_system{
		void update(chamber_update_context&, building_common&, basic_building&) const noexcept{
		}
	};

	export
	struct armor_building_system{
		void update(chamber_update_context&, building_common&, armor_building&) const noexcept{
		}
	};

	export
	struct energy_generator_building_system{
		void update(chamber_update_context&, building_common&, energy_generator_building&) const noexcept{
		}
	};

	export
	struct thruster_building_system{
		void update(chamber_update_context&, building_common& common, thruster_building& building) const noexcept{
			common.set_maneuver(building.ideal_maneuver, true);
		}
	};

	export
	struct radar_building_system{
		void update(chamber_update_context&, building_common&, radar_building&) const noexcept{
		}
	};

	export
	struct turret_building_system{
		void update(chamber_update_context&, building_common&, turret_building&) const noexcept{
		}
	};

	export
	struct structural_joint_building_system{
		void update(chamber_update_context&, building_common&, structural_joint_building&) const noexcept{
		}
	};

	export
	enum class chamber_command_status{
		applied,
		invalid_region,
		invalid_energy,
		invalid_maneuver,
		unsupported_structure,
		out_of_bounds,
		not_placeable,
		occupied,
		expired_target
	};

	export
	struct chamber_command_result{
		chamber_command_status status{};
		object_handle target{};
		std::vector<object_handle> cascaded_targets{};

		[[nodiscard]] constexpr bool applied() const noexcept{
			return status == chamber_command_status::applied;
		}
	};

	export
	template <typename Building>
	struct place_building_command{
		tile_region region{};
		float hit_points{};
		bool structural{};
		std::uint32_t structural_support_radius{1};
		Building building{};
		energy_status energy{};
		energy_acquisition energy_acquisition{};
	};

	export
	using place_basic_building_command = place_building_command<basic_building>;

	export
	struct place_standard_building_command{
		standard_building_type type{};
		tile_region region{};
		float hit_points{};
		std::uint32_t structural_support_radius{1};
		energy_status energy{};
		energy_acquisition energy_acquisition{};
		maneuver_component maneuver{};
		radar_building radar{};
		turret_building turret{};
	};

	export
	struct erase_building_command{
		object_handle target{};
	};

	export
	struct set_tile_placeable_command{
		tile_region region{};
		bool placeable{};
	};

	export
	struct set_tile_corridor_command{
		tile_region region{};
		bool corridor{};
	};

	export
	struct set_building_energy_command{
		object_handle target{};
		energy_status energy{};
		energy_acquisition acquisition{};
	};

	export
	struct set_building_maneuver_command{
		object_handle target{};
		maneuver_component maneuver{};
		bool enabled{true};
	};

	export
	struct update_targeting_command{
		std::vector<target_snapshot> candidates{};
		std::uint32_t faction_id{};
		float delta_seconds{};
		math::trans2 chamber_transform{};
		std::uint64_t scan_tick{};
	};

	export
	struct clear_fire_requests_command{
	};

	export
	using chamber_command = std::variant<
		place_basic_building_command,
		place_standard_building_command,
		erase_building_command,
		set_tile_placeable_command,
		set_tile_corridor_command,
		set_building_energy_command,
		set_building_maneuver_command,
		update_targeting_command,
		clear_fire_requests_command
	>;

	export
	struct projectile_hit_context{
		entity_id projectile{};
		entity_id target{};
		const physics::collision_shape_record& shape;
		physics_contact_endpoint_snapshot projectile_endpoint{};
		physics_contact_endpoint_snapshot target_endpoint{};
		math::vec2 point{};
		math::vec2 normal{1.f, 0.f};
		damage_group& damage;
	};

	export
	struct projectile_tile_hit{
		math::point2 coord{};
		float fraction{};
		float lateral_distance2{};
	};

	export
	struct projectile_hit_result{
		bool hit_any_tile{};
		bool damage_exhausted{};
		bool target_destroyed{};
		std::uint64_t tile_hit_count{};
		std::uint64_t damaged_tile_count{};
		float actual_damage{};
	};

	export
	struct projectile_hit_command{
		projectile_hit_context& context;
	};

	export
	struct update_buildings_command{
		float delta_seconds{};
	};

	export
	struct update_energy_command{
		float delta_seconds{};
	};

	export
	struct update_maneuver_command{
	};

	export
	struct settle_pending_damage_command{
	};

	export
	struct energy_summary{
		std::uint32_t generator_count{};
		std::uint32_t consumer_count{};
		std::uint32_t generated_energy{};
		std::uint32_t minimum_requested_energy{};
		std::uint32_t requested_energy{};
		std::uint32_t assigned_energy{};
		bool changed{};
	};

	export
	struct maneuver_summary{
		float force_longitudinal{};
		float force_transverse{};
		float torque{};
		float boost{};

		[[nodiscard]] float get_force_at(const float yaw_angle_in_rad) const noexcept{
			const float cos_value = std::cos(yaw_angle_in_rad);
			const float sin_value = std::sin(yaw_angle_in_rad);
			return std::abs(cos_value) * force_longitudinal
				+ std::abs(sin_value) * force_transverse;
		}

		void append(const maneuver_component component, const math::vec2 position) noexcept{
			force_longitudinal += component.force_longitudinal;
			force_transverse += component.force_transverse;
			torque += component.torque_absolute + component.torque * position.length();
			boost += component.boost;
		}
	};

	void collect_projectile_tile_hits(
		const chamber_manifold& chamber,
		const projectile_hit_context& context,
		std::vector<projectile_tile_hit>& hits);

	export
	struct chamber_manifold{
		using building_collection = object_collection<chamber_update_context, building_common>;

	private:
		struct tile_status_range{
			std::uint32_t offset{};
			std::uint32_t count{};

			[[nodiscard]] constexpr std::uint32_t end_offset() const noexcept{
				return offset + count;
			}
		};

		math::point2 extent_{};
		std::vector<object_handle> tile_buildings_{};
		std::vector<tile_state> tile_states_{};
		std::vector<std::uint32_t> corridor_groups_{};
		std::vector<tile_status> tile_statuses_{};
		std::vector<std::uint32_t> structural_support_counts_{};
		std::vector<tile_status_range> free_tile_status_ranges_{};
		hit_point structural_hit_points_{};
		building_collection buildings_{};
		std::vector<chamber_command> pending_commands_{};
		std::vector<chamber_command_result> last_command_results_{};
		energy_summary last_energy_summary_{};
		maneuver_summary last_maneuver_summary_{};
		std::vector<chamber_fire_request> last_fire_requests_{};
		std::vector<projectile_tile_hit> projectile_hit_workspace_{};
		std::vector<object_handle> pending_damage_buildings_{};
		std::uint64_t collider_revision_{1};
		std::uint64_t synchronized_collider_revision_{};

		void mark_collider_dirty() noexcept{
			if(collider_revision_ == std::numeric_limits<std::uint64_t>::max()){
				std::terminate();
			}
			++collider_revision_;
		}

		void register_default_channels(){
			if(!buildings_.template contains_channel<basic_building>()){
				buildings_.template register_channel<basic_building>(basic_building_system{});
			}
			if(!buildings_.template contains_channel<armor_building>()){
				buildings_.template register_channel<armor_building>(armor_building_system{});
			}
			if(!buildings_.template contains_channel<energy_generator_building>()){
				buildings_.template register_channel<energy_generator_building>(energy_generator_building_system{});
			}
			if(!buildings_.template contains_channel<thruster_building>()){
				buildings_.template register_channel<thruster_building>(thruster_building_system{});
			}
			if(!buildings_.template contains_channel<radar_building>()){
				buildings_.template register_channel<radar_building>(radar_building_system{});
			}
			if(!buildings_.template contains_channel<turret_building>()){
				buildings_.template register_channel<turret_building>(turret_building_system{});
			}
			if(!buildings_.template contains_channel<structural_joint_building>()){
				buildings_.template register_channel<structural_joint_building>(structural_joint_building_system{});
			}
		}

		[[nodiscard]] chamber_command_status validate_place_region(
			const tile_region region,
			const bool structural) const noexcept{
			if(region.area() <= 0){
				return chamber_command_status::invalid_region;
			}

			for(int y = region.src.y; y != region.src.y + region.extent.y; ++y){
				for(int x = region.src.x; x != region.src.x + region.extent.x; ++x){
					const math::point2 coord{x, y};
					if(!this->contains(coord)){
						return chamber_command_status::out_of_bounds;
					}
					if(!this->tile_state_at(coord).placeable){
						return chamber_command_status::not_placeable;
					}
					if(this->building_at(coord) != nullptr){
						return chamber_command_status::occupied;
					}
				}
			}

			if(!structural && !this->region_has_structural_support(region)){
				return chamber_command_status::unsupported_structure;
			}
			return chamber_command_status::applied;
		}

		[[nodiscard]] chamber_command_status validate_tile_region(const tile_region region) const noexcept{
			if(region.area() <= 0){
				return chamber_command_status::invalid_region;
			}

			for(int y = region.src.y; y != region.src.y + region.extent.y; ++y){
				for(int x = region.src.x; x != region.src.x + region.extent.x; ++x){
					if(!this->contains({x, y})){
						return chamber_command_status::out_of_bounds;
					}
				}
			}
			return chamber_command_status::applied;
		}

		[[nodiscard]] static bool valid_energy_configuration(
			const energy_status status,
			const energy_acquisition acquisition) noexcept{
			if(acquisition.minimum_count > acquisition.maximum_count){
				return false;
			}
			if(!std::isfinite(acquisition.priority) || !std::isfinite(status.charge_duration)){
				return false;
			}
			if(status.charge_duration < 0.f){
				return false;
			}
			if(!status.is_consumer()){
				return acquisition.minimum_count == 0 && acquisition.maximum_count == 0;
			}
			return acquisition.maximum_count <= status.abs_power();
		}

		[[nodiscard]] static bool valid_maneuver_configuration(const maneuver_component maneuver) noexcept{
			return std::isfinite(maneuver.force_longitudinal)
				&& std::isfinite(maneuver.force_transverse)
				&& std::isfinite(maneuver.torque)
				&& std::isfinite(maneuver.torque_absolute)
				&& std::isfinite(maneuver.boost);
		}

		static void set_building_energy(
			building_common& building,
			const energy_status status,
			const energy_acquisition acquisition) noexcept{
			building.energy = status;
			building.energy_dynamic = {};
			building.valid_energy = 0;
			building.ideal_energy_acquisition = acquisition;
		}

		static void set_building_maneuver(
			building_common& building,
			const maneuver_component maneuver,
			const bool enabled) noexcept{
			building.set_maneuver(maneuver, enabled);
		}

		void assign_region_tiles(const tile_region region, const object_handle handle) noexcept{
			for(int y = region.src.y; y != region.src.y + region.extent.y; ++y){
				for(int x = region.src.x; x != region.src.x + region.extent.x; ++x){
					tile_buildings_[this->tile_index({x, y})] = handle;
				}
			}
		}

		void clear_region_tiles(const tile_region region, const object_handle handle) noexcept{
			for(int y = region.src.y; y != region.src.y + region.extent.y; ++y){
				for(int x = region.src.x; x != region.src.x + region.extent.x; ++x){
					object_handle& tile = tile_buildings_[this->tile_index({x, y})];
					if(tile == handle){
						tile = {};
					}
				}
			}
		}

		[[nodiscard]] building_common* mutable_building_at(const math::point2 coord) noexcept{
			const object_handle handle = this->building_handle_at(coord);
			if(!handle){
				return nullptr;
			}
			return buildings_.try_common(handle);
		}

		[[nodiscard]] building_common* mutable_try_building(const object_handle handle) noexcept{
			return buildings_.try_common(handle);
		}

		[[nodiscard]] tile_status& mutable_tile_status_at(const building_common& building, const math::point2 coord) noexcept{
			return tile_statuses_[building.tile_status_offset + building.local_tile_index(coord)];
		}

		[[nodiscard]] tile_state& mutable_tile_state_at(const math::point2 coord) noexcept{
			return tile_states_[this->tile_index(coord)];
		}

		void rebuild_corridor_groups(){
			corridor_groups_.assign(tile_states_.size(), invalid_corridor_group);
			if(extent_.x <= 0 || extent_.y <= 0){
				return;
			}

			std::uint32_t next_group{};
			std::vector<math::point2> pending{};
			pending.reserve(tile_states_.size());
			static constexpr std::array directions{
				math::point2{1, 0},
				math::point2{-1, 0},
				math::point2{0, 1},
				math::point2{0, -1}
			};

			for(int y = 0; y != extent_.y; ++y){
				for(int x = 0; x != extent_.x; ++x){
					const math::point2 start{x, y};
					const std::size_t start_index = this->tile_index(start);
					if(!tile_states_[start_index].corridor || corridor_groups_[start_index] != invalid_corridor_group){
						continue;
					}

					const std::uint32_t group = next_group++;
					corridor_groups_[start_index] = group;
					pending.push_back(start);
					while(!pending.empty()){
						const math::point2 coord = pending.back();
						pending.pop_back();
						for(const math::point2 direction : directions){
							const math::point2 next = coord + direction;
							if(!this->contains(next)){
								continue;
							}
							const std::size_t next_index = this->tile_index(next);
							if(!tile_states_[next_index].corridor || corridor_groups_[next_index] != invalid_corridor_group){
								continue;
							}
							corridor_groups_[next_index] = group;
							pending.push_back(next);
						}
					}
				}
			}
		}

		[[nodiscard]] tile_region structural_support_region(
			const tile_region region,
			const std::uint32_t radius) const noexcept{
			if(radius == 0 || extent_.x <= 0 || extent_.y <= 0){
				return {};
			}

			const int distance = static_cast<int>(std::min<std::uint32_t>(
				radius,
				static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
			const int min_x = std::max(0, region.src.x - distance);
			const int min_y = std::max(0, region.src.y - distance);
			const int max_x = std::min(extent_.x, region.src.x + region.extent.x + distance);
			const int max_y = std::min(extent_.y, region.src.y + region.extent.y + distance);
			return {
				.src = {min_x, min_y},
				.extent = {max_x - min_x, max_y - min_y}
			};
		}

		[[nodiscard]] bool region_has_structural_support(const tile_region region) const noexcept{
			if(structural_support_counts_.empty()){
				return false;
			}

			for(int y = region.src.y; y != region.src.y + region.extent.y; ++y){
				for(int x = region.src.x; x != region.src.x + region.extent.x; ++x){
					if(structural_support_counts_[this->tile_index({x, y})] != 0){
						return true;
					}
				}
			}
			return false;
		}

		void add_structural_support(const building_common& building){
			if(!building.structural || building.structural_support_radius == 0){
				return;
			}

			const tile_region region = this->structural_support_region(
				building.region,
				building.structural_support_radius);
			for(int y = region.src.y; y != region.src.y + region.extent.y; ++y){
				for(int x = region.src.x; x != region.src.x + region.extent.x; ++x){
					++structural_support_counts_[this->tile_index({x, y})];
				}
			}
		}

		void remove_structural_support(const building_common& building) noexcept{
			if(!building.structural || building.structural_support_radius == 0){
				return;
			}

			const tile_region region = this->structural_support_region(
				building.region,
				building.structural_support_radius);
			for(int y = region.src.y; y != region.src.y + region.extent.y; ++y){
				for(int x = region.src.x; x != region.src.x + region.extent.x; ++x){
					std::uint32_t& count = structural_support_counts_[this->tile_index({x, y})];
					if(count == 0){
						std::terminate();
					}
					--count;
				}
			}
		}

		[[nodiscard]] std::vector<object_handle> collect_unsupported_buildings() const{
			std::vector<object_handle> unsupported{};
			(void)buildings_.for_each_common([this, &unsupported](const building_common& building){
				if(building.structural){
					return;
				}
				if(!this->region_has_structural_support(building.region)){
					unsupported.push_back(building.handle);
				}
			});
			return unsupported;
		}

		[[nodiscard]] std::uint32_t acquire_tile_status_range(const std::uint32_t count, const float tile_hit_points){
			for(auto it = free_tile_status_ranges_.begin(); it != free_tile_status_ranges_.end(); ++it){
				if(it->count < count){
					continue;
				}

				const std::uint32_t offset = it->offset;
				for(std::uint32_t i = 0; i != count; ++i){
					tile_statuses_[static_cast<std::size_t>(offset + i)] = tile_status{tile_hit_points};
				}

				it->offset += count;
				it->count -= count;
				if(it->count == 0){
					free_tile_status_ranges_.erase(it);
				}
				return offset;
			}

			if(static_cast<std::uint64_t>(tile_statuses_.size()) + static_cast<std::uint64_t>(count)
				> std::numeric_limits<std::uint32_t>::max()){
				throw std::length_error{"chamber tile status capacity exceeded"};
			}
			const auto offset = static_cast<std::uint32_t>(tile_statuses_.size());
			tile_statuses_.resize(tile_statuses_.size() + count, tile_status{tile_hit_points});
			return offset;
		}

		void release_tile_status_range(const std::uint32_t offset, const std::uint32_t count){
			if(count == 0){
				return;
			}

			const tile_status_range range{offset, count};
			auto it = std::ranges::lower_bound(
				free_tile_status_ranges_,
				range.offset,
				{},
				&tile_status_range::offset);
			it = free_tile_status_ranges_.insert(it, range);

			if(it != free_tile_status_ranges_.begin()){
				auto previous = it - 1;
				if(previous->end_offset() == it->offset){
					previous->count += it->count;
					it = free_tile_status_ranges_.erase(it);
					it = previous;
				}
			}

			if(it + 1 != free_tile_status_ranges_.end()){
				auto next = it + 1;
				if(it->end_offset() == next->offset){
					it->count += next->count;
					free_tile_status_ranges_.erase(next);
				}
			}
		}

		float consume_damage(building_common& building, const math::point2 coord, damage_group& damage){
			if(!building.region.contains(coord) || damage.exhausted()){
				return 0.f;
			}

			const float actual = this->mutable_tile_status_at(building, coord).take_damage(damage.sum());
			if(actual <= 0.f){
				return 0.f;
			}

			if(building.pending_damage <= 0.f){
				pending_damage_buildings_.push_back(building.handle);
			}
			damage.consume(actual);
			building.pending_damage += actual;
			building.damage_events.push_back({
				.tile_coord = coord,
				.actual_damage = actual
			});
			return actual;
		}

		[[nodiscard]] projectile_hit_result apply_projectile_hit(projectile_hit_context& context){
			projectile_hit_result result{};
			chamber::collect_projectile_tile_hits(*this, context, projectile_hit_workspace_);
			result.tile_hit_count = projectile_hit_workspace_.size();
			for(const projectile_tile_hit& hit : projectile_hit_workspace_){
				if(context.damage.exhausted()){
					break;
				}

				building_common* building = this->mutable_building_at(hit.coord);
				if(!building){
					continue;
				}

				const float actual = this->consume_damage(*building, hit.coord, context.damage);
				if(actual <= 0.f){
					continue;
				}
				result.hit_any_tile = true;
				++result.damaged_tile_count;
				result.actual_damage += actual;
			}
			result.damage_exhausted = context.damage.exhausted();
			return result;
		}

		[[nodiscard]] float settle_pending_damage(building_common& building) noexcept{
			const bool was_alive = !building.hit_points.is_killed();
			const float applied = building.hit_points.accept(building.pending_damage);
			building.pending_damage = 0.f;
			if(was_alive && building.hit_points.is_killed()){
				this->mark_collider_dirty();
			}
			return applied;
		}

		void update_buildings(const float delta_seconds){
			this->update_energy(delta_seconds);
			chamber_update_context context{*this, delta_seconds};
			buildings_.update_all(context);
			(void)buildings_.deliver_events(context);
			this->update_maneuver();
		}

		void update_energy(const float delta_seconds) noexcept{
			struct energy_entry{
				building_common* building{};
				energy_acquisition acquisition{};
			};

			std::vector<energy_entry> consumers{};
			energy_summary summary{};
			(void)buildings_.for_each_common([&](building_common& building){
				building.valid_energy = 0;
				if(building.energy.is_generator()){
					++summary.generator_count;
					const energy_status_update_result result = building.update_energy_state(delta_seconds);
					if(result.changed){
						summary.changed = true;
					}
					if(result.power > 0){
						summary.generated_energy += static_cast<std::uint32_t>(result.power);
					}
					return;
				}

				if(building.energy.is_consumer()){
					++summary.consumer_count;
					energy_acquisition acquisition = building.get_real_energy_acquisition();
					summary.minimum_requested_energy += acquisition.minimum_count;
					summary.requested_energy += acquisition.maximum_count;
					consumers.push_back({
						.building = std::addressof(building),
						.acquisition = acquisition
					});
				}
			});

			std::ranges::sort(consumers, [](const energy_entry& lhs, const energy_entry& rhs) noexcept{
				if(lhs.acquisition.priority != rhs.acquisition.priority){
					return lhs.acquisition.priority > rhs.acquisition.priority;
				}
				const object_handle lhs_handle = lhs.building->handle;
				const object_handle rhs_handle = rhs.building->handle;
				return std::tie(lhs_handle.channel, lhs_handle.slot, lhs_handle.generation)
					< std::tie(rhs_handle.channel, rhs_handle.slot, rhs_handle.generation);
			});

			std::uint32_t remaining = summary.generated_energy;
			if(summary.requested_energy <= remaining){
				for(const energy_entry& consumer : consumers){
					consumer.building->valid_energy = consumer.acquisition.maximum_count;
					summary.assigned_energy += consumer.building->valid_energy;
				}
			} else if(summary.minimum_requested_energy <= remaining){
				remaining -= summary.minimum_requested_energy;
				for(const energy_entry& consumer : consumers){
					consumer.building->valid_energy = consumer.acquisition.minimum_count;
					if(remaining > 0){
						const std::uint32_t assigned = std::min(consumer.acquisition.get_append_count(), remaining);
						consumer.building->valid_energy += assigned;
						remaining -= assigned;
					}
					summary.assigned_energy += consumer.building->valid_energy;
				}
			} else{
				for(const energy_entry& consumer : consumers){
					const std::uint32_t assigned = std::min(consumer.acquisition.minimum_count, remaining);
					consumer.building->valid_energy = assigned;
					remaining -= assigned;
					summary.assigned_energy += assigned;
				}
			}

			for(const energy_entry& consumer : consumers){
				const energy_status_update_result result = consumer.building->update_energy_state(delta_seconds);
				if(result.changed){
					summary.changed = true;
				}
			}
			last_energy_summary_ = summary;
		}

		void update_maneuver() noexcept{
			maneuver_summary summary{};
			(void)buildings_.for_each_common([&](const building_common& building){
				if(!building.maneuvering || building.hit_points.is_killed()){
					return;
				}

				const math::vec2 center{
					(static_cast<float>(building.region.src.x) + static_cast<float>(building.region.width()) * 0.5f) * tile_size,
					(static_cast<float>(building.region.src.y) + static_cast<float>(building.region.height()) * 0.5f) * tile_size
				};
				summary.append(building.ideal_maneuver * building.get_efficiency(), center);
			});
			last_maneuver_summary_ = summary;
		}

		[[nodiscard]] static bool candidate_is_hostile(
			const target_snapshot& candidate,
			const std::uint32_t faction_id) noexcept{
			if(!candidate.target || !candidate.target.entity || candidate.target.entity.is_expired()){
				return false;
			}
			return target_is_hostile(faction_id, candidate.faction_id);
		}

		[[nodiscard]] static bool target_ref_alive(const target_ref& target) noexcept{
			if(!target || !target.entity || target.entity.is_expired()){
				return false;
			}
			if(target.kind == target_kind::entity){
				return true;
			}
			if(target.kind != target_kind::chamber_building){
				return false;
			}
			const auto* chamber = target.entity.try_get<chamber_manifold>();
			if(chamber == nullptr){
				return false;
			}
			const building_common* building = chamber->try_building(target.chamber_building);
			return building != nullptr && !building->hit_points.is_killed();
		}

		static void prune_target_memory(target_memory& memory) noexcept{
			std::erase_if(memory.targets, [](const target_snapshot& target){
				return !chamber_manifold::target_ref_alive(target.target);
			});
		}

		[[nodiscard]] static bool target_snapshot_less(
			const target_snapshot& lhs,
			const target_snapshot& rhs) noexcept{
			const float lhs_score = target_score(lhs);
			const float rhs_score = target_score(rhs);
			if(lhs_score < rhs_score){
				return true;
			}
			if(rhs_score < lhs_score){
				return false;
			}
			return lhs.target < rhs.target;
		}

		void update_radar_targets(const update_targeting_command& command){
			(void)buildings_.template for_each<radar_building>([&](building_common& common, radar_building& radar){
				const float efficiency = common.get_efficiency();
				if(common.hit_points.is_killed() || efficiency <= 0.f || !radar.sensor.can_scan()){
					radar.memory.clear();
					return;
				}

				chamber_manifold::prune_target_memory(radar.memory);
				radar.sensor.scan_timer = std::max(0.f, radar.sensor.scan_timer - command.delta_seconds * efficiency);
				if(radar.sensor.scan_timer > 0.f){
					return;
				}

				std::vector<target_snapshot> selected{};
				const math::vec2 local_origin = chamber_manifold::building_center(common);
				const math::vec2 world_origin =
					chamber_manifold::chamber_to_world_point(local_origin, command.chamber_transform);
				for(const target_snapshot& candidate : command.candidates){
					if(!chamber_manifold::candidate_is_hostile(candidate, command.faction_id)
						|| !chamber_manifold::target_ref_alive(candidate.target)
						|| !target_is_detectable(radar.sensor, candidate)){
						continue;
					}

					target_snapshot snapshot = candidate;
					snapshot.distance = world_origin.dst(snapshot.position);
					snapshot.preference += radar.sensor.preference;
					if(snapshot.distance > radar.sensor.range){
						continue;
					}
					selected.push_back(snapshot);
				}

				std::ranges::sort(selected, chamber_manifold::target_snapshot_less);
				if(selected.size() > radar.sensor.max_targets){
					selected.resize(radar.sensor.max_targets);
				}
				radar.memory.assign(std::move(selected), command.scan_tick);
				radar.sensor.scan_timer = std::max(0.f, radar.sensor.scan_interval);

				if(const target_snapshot* primary = radar.memory.primary()){
					const math::vec2 world_delta = primary->position - world_origin;
					if(world_delta.length2() > 1.0e-6f){
						const math::vec2 local_delta =
							chamber_manifold::world_to_chamber_vector(world_delta, command.chamber_transform);
						radar.rotation = std::atan2(local_delta.y, local_delta.x);
					}
				}
			});
		}

		void update_turrets(const update_targeting_command& command){
			(void)buildings_.template for_each<turret_building>([&](building_common& common, turret_building& turret){
				const float efficiency = common.get_efficiency();
				if(common.hit_points.is_killed() || efficiency <= 0.f || turret.projectile_speed <= 0.f){
					turret.memory.clear();
					return;
				}

				std::vector<target_snapshot> selected{};
				const math::vec2 local_origin = chamber_manifold::building_center(common);
				const math::vec2 world_origin =
					chamber_manifold::chamber_to_world_point(local_origin, command.chamber_transform);
				(void)buildings_.template for_each<radar_building>([&](const building_common&, const radar_building& radar){
					for(const target_snapshot& radar_target : radar.memory.targets){
						if(!chamber_manifold::candidate_is_hostile(radar_target, command.faction_id)
							|| !chamber_manifold::target_ref_alive(radar_target.target)){
							continue;
						}
						target_snapshot snapshot = radar_target;
						snapshot.distance = world_origin.dst(snapshot.position);
						selected.push_back(snapshot);
					}
				});

				std::ranges::sort(selected, chamber_manifold::target_snapshot_less);
				const auto duplicate_projection = [](const target_snapshot& snapshot) noexcept{
					return snapshot.target;
				};
				const auto unique = std::ranges::unique(
					selected,
					std::ranges::equal_to{},
					duplicate_projection);
				selected.erase(unique.begin(), unique.end());
				const std::uint32_t max_targets = std::max(1u, turret.max_targets);
				if(selected.size() > max_targets){
					selected.resize(max_targets);
				}
				turret.memory.assign(std::move(selected), command.scan_tick);

				turret.reload_timer = std::max(0.f, turret.reload_timer - command.delta_seconds * efficiency);
				if(turret.reload_timer > 0.f || turret.memory.empty()){
					return;
				}

				bool fired{};
				for(const target_snapshot& target : turret.memory.targets){
					math::vec2 world_direction = target.position - world_origin;
					if(world_direction.length2() <= 1.0e-6f){
						continue;
					}
					world_direction = world_direction.normalize();
					const math::vec2 local_direction =
						chamber_manifold::world_to_chamber_vector(world_direction, command.chamber_transform);
					turret.rotation = std::atan2(local_direction.y, local_direction.x);
					last_fire_requests_.push_back(chamber_fire_request{
						.turret = common.handle,
						.target = target.target,
						.origin = local_origin,
						.direction = local_direction,
						.projectile_speed = turret.projectile_speed,
						.damage = turret.projectile_damage,
						.faction_id = command.faction_id
					});
					fired = true;
				}
				if(fired){
					turret.reload_timer = std::max(0.f, turret.reload_seconds);
				}
			});
		}

		void update_targeting(const update_targeting_command& command){
			last_fire_requests_.clear();
			this->update_radar_targets(command);
			this->update_turrets(command);
		}

		[[nodiscard]] projectile_hit_result settle_pending_damage() noexcept{
			projectile_hit_result result{};
			for(const object_handle target : pending_damage_buildings_){
				building_common* building = this->mutable_try_building(target);
				if(building == nullptr){
					std::terminate();
				}

				const float applied = this->settle_pending_damage(*building);
				if(applied <= 0.f){
					continue;
				}
				result.actual_damage += applied;
				if(building->structural){
					structural_hit_points_.accept(applied);
				}
			}
			pending_damage_buildings_.clear();
			result.target_destroyed = structural_hit_points_.is_killed();
			return result;
		}

		template <typename Building>
		[[nodiscard]] chamber_command_result place_building(
			const tile_region region,
			const float hit_points,
			const bool structural,
			const std::uint32_t structural_support_radius,
			Building&& building,
			const energy_status energy,
			const energy_acquisition acquisition){
			const chamber_command_status validation = this->validate_place_region(region, structural);
			if(validation != chamber_command_status::applied){
				return {.status = validation};
			}
			if(!chamber_manifold::valid_energy_configuration(energy, acquisition)){
				return {.status = chamber_command_status::invalid_energy};
			}

			if(!std::in_range<std::uint32_t>(region.area())){
				throw std::length_error{"chamber tile status capacity exceeded"};
			}

			const auto tile_status_count = static_cast<std::uint32_t>(region.area());
			const float tile_hp = hit_points / static_cast<float>(region.area()) * 2.f;
			const std::uint32_t tile_status_offset = this->acquire_tile_status_range(tile_status_count, tile_hp);

			building_common common{
				.region = region,
				.tile_status_offset = tile_status_offset,
				.tile_status_count = tile_status_count,
				.energy = energy,
				.ideal_energy_acquisition = acquisition,
				.structural = structural,
				.structural_support_radius = structural ? std::max(1u, structural_support_radius) : 0u
			};
			common.hit_points.reset_to(hit_points);

			object_handle handle{};
			try{
				handle = buildings_.template emplace<std::remove_cvref_t<Building>>(
					std::move(common),
					std::forward<Building>(building));
			} catch(...){
				this->release_tile_status_range(tile_status_offset, tile_status_count);
				throw;
			}

			this->assign_region_tiles(region, handle);
			if(building_common* placed = this->mutable_try_building(handle)){
				this->add_structural_support(*placed);
			}
			this->mark_collider_dirty();
			return {.status = chamber_command_status::applied, .target = handle};
		}

		[[nodiscard]] chamber_command_result erase_building_no_cascade(const object_handle target){
			building_common* building = this->mutable_try_building(target);
			if(building == nullptr){
				return {.status = chamber_command_status::expired_target, .target = target};
			}

			const tile_region region = building->region;
			const std::uint32_t tile_status_offset = building->tile_status_offset;
			const std::uint32_t tile_status_count = building->tile_status_count;
			this->remove_structural_support(*building);
			this->clear_region_tiles(region, target);
			(void)buildings_.erase(target);
			this->release_tile_status_range(tile_status_offset, tile_status_count);
			this->mark_collider_dirty();
			return {.status = chamber_command_status::applied, .target = target};
		}

		void erase_unsupported_buildings(std::vector<object_handle>& cascaded_targets){
			for(;;){
				std::vector<object_handle> unsupported = this->collect_unsupported_buildings();
				if(unsupported.empty()){
					return;
				}
				for(const object_handle target : unsupported){
					const chamber_command_result erased = this->erase_building_no_cascade(target);
					if(erased.applied()){
						cascaded_targets.push_back(target);
					}
				}
			}
		}

		[[nodiscard]] chamber_building_dump dump_building(const building_common& common) const{
			chamber_building_dump dump{
				.region = common.region,
				.hit_points = common.hit_points,
				.energy = common.energy,
				.energy_acquisition = common.ideal_energy_acquisition,
				.valid_energy = common.valid_energy,
				.energy_dynamic = common.energy_dynamic,
				.maneuver = common.ideal_maneuver,
				.maneuvering = common.maneuvering,
				.structural = common.structural,
				.structural_support_radius = common.structural_support_radius,
				.targetable = common.targetable
			};
			dump.tile_statuses.reserve(common.tile_status_count);
			for(std::uint32_t i = 0; i != common.tile_status_count; ++i){
				dump.tile_statuses.push_back(tile_statuses_[common.tile_status_offset + i]);
			}

			if(buildings_.template try_get<basic_building>(common.handle) != nullptr){
				dump.type = chamber_dump_building_type::basic;
			} else if(buildings_.template try_get<armor_building>(common.handle) != nullptr){
				dump.type = chamber_dump_building_type::armor;
			} else if(buildings_.template try_get<energy_generator_building>(common.handle) != nullptr){
				dump.type = chamber_dump_building_type::energy_generator;
			} else if(const auto* thruster = buildings_.template try_get<thruster_building>(common.handle)){
				dump.type = chamber_dump_building_type::thruster;
				dump.thruster = *thruster;
			} else if(const auto* radar = buildings_.template try_get<radar_building>(common.handle)){
				dump.type = chamber_dump_building_type::radar;
				dump.radar = *radar;
				dump.radar.memory.clear();
			} else if(const auto* turret = buildings_.template try_get<turret_building>(common.handle)){
				dump.type = chamber_dump_building_type::turret;
				dump.turret = *turret;
				dump.turret.memory.clear();
			} else if(buildings_.template try_get<structural_joint_building>(common.handle) != nullptr){
				dump.type = chamber_dump_building_type::structural_joint;
			} else{
				throw std::logic_error{"chamber dump does not support custom building channels"};
			}
			return dump;
		}

		[[nodiscard]] chamber_command_result place_dump_building(const chamber_building_dump& dump){
			switch(dump.type){
			case chamber_dump_building_type::basic:
				return this->place_building(
					dump.region,
					dump.hit_points.max,
					dump.structural,
					dump.structural_support_radius,
					basic_building{},
					dump.energy,
					dump.energy_acquisition);
			case chamber_dump_building_type::armor:
				return this->place_building(
					dump.region,
					dump.hit_points.max,
					false,
					0u,
					armor_building{},
					dump.energy,
					dump.energy_acquisition);
			case chamber_dump_building_type::energy_generator:
				return this->place_building(
					dump.region,
					dump.hit_points.max,
					false,
					0u,
					energy_generator_building{},
					dump.energy,
					dump.energy_acquisition);
			case chamber_dump_building_type::thruster:
				return this->place_building(
					dump.region,
					dump.hit_points.max,
					false,
					0u,
					dump.thruster,
					dump.energy,
					dump.energy_acquisition);
			case chamber_dump_building_type::radar:
				return this->place_building(
					dump.region,
					dump.hit_points.max,
					false,
					0u,
					dump.radar,
					dump.energy,
					dump.energy_acquisition);
			case chamber_dump_building_type::turret:
				return this->place_building(
					dump.region,
					dump.hit_points.max,
					false,
					0u,
					dump.turret,
					dump.energy,
					dump.energy_acquisition);
			case chamber_dump_building_type::structural_joint:
				return this->place_building(
					dump.region,
					dump.hit_points.max,
					true,
					dump.structural_support_radius,
					structural_joint_building{},
					dump.energy,
					dump.energy_acquisition);
			}
			std::unreachable();
		}

	public:
		[[nodiscard]] explicit chamber_manifold(math::point2 extent = {}) : extent_(extent){
			this->register_default_channels();
			if(extent_.x > 0 && extent_.y > 0){
				tile_buildings_.assign(static_cast<std::size_t>(extent_.x * extent_.y), {});
				tile_states_.assign(static_cast<std::size_t>(extent_.x * extent_.y), {});
				corridor_groups_.assign(static_cast<std::size_t>(extent_.x * extent_.y), invalid_corridor_group);
				structural_support_counts_.assign(static_cast<std::size_t>(extent_.x * extent_.y), {});
				structural_hit_points_.reset_to(static_cast<float>(extent_.x * extent_.y) * 100.f);
			}
		}

		chamber_manifold(const chamber_manifold&) = delete;
		chamber_manifold& operator=(const chamber_manifold&) = delete;
		chamber_manifold(chamber_manifold&&) noexcept = default;
		chamber_manifold& operator=(chamber_manifold&&) noexcept = default;

		[[nodiscard]] const building_collection& buildings() const noexcept{
			return buildings_;
		}

		[[nodiscard]] static math::vec2 building_center(const building_common& building) noexcept{
			return {
				(static_cast<float>(building.region.src.x) + static_cast<float>(building.region.width()) * 0.5f) * tile_size,
				(static_cast<float>(building.region.src.y) + static_cast<float>(building.region.height()) * 0.5f) * tile_size
			};
		}

		[[nodiscard]] static math::vec2 chamber_to_world_point(
			const math::vec2 position,
			const math::trans2 chamber_transform) noexcept{
			return position >> chamber_transform;
		}

		[[nodiscard]] static math::vec2 world_to_chamber_vector(
			math::vec2 vector,
			const math::trans2 chamber_transform) noexcept{
			vector.rotate_rad(-static_cast<float>(chamber_transform.rot));
			return vector;
		}

		[[nodiscard]] static math::vec2 chamber_to_world_vector(
			math::vec2 vector,
			const math::trans2 chamber_transform) noexcept{
			vector.rotate_rad(static_cast<float>(chamber_transform.rot));
			return vector;
		}

		[[nodiscard]] const energy_summary& last_energy_summary() const noexcept{
			return last_energy_summary_;
		}

		[[nodiscard]] const maneuver_summary& last_maneuver_summary() const noexcept{
			return last_maneuver_summary_;
		}

		[[nodiscard]] std::span<const chamber_fire_request> last_fire_requests() const noexcept{
			return last_fire_requests_;
		}

		[[nodiscard]] bool collider_needs_synchronization() const noexcept{
			return collider_revision_ != synchronized_collider_revision_;
		}

		[[nodiscard]] std::uint64_t collider_revision() const noexcept{
			return collider_revision_;
		}

		void mark_collider_synchronized() noexcept{
			synchronized_collider_revision_ = collider_revision_;
		}

		template <typename Building, typename System>
			requires object_update_system<std::remove_cvref_t<System>, chamber_update_context, Building, building_common>
		void register_building_type(System&& system){
			(void)buildings_.template register_channel<Building>(std::forward<System>(system));
		}

		[[nodiscard]] math::point2 extent() const noexcept{
			return extent_;
		}

		[[nodiscard]] const hit_point& structural_hit_points() const noexcept{
			return structural_hit_points_;
		}

		[[nodiscard]] bool contains(const math::point2 coord) const noexcept{
			return coord.x >= 0 && coord.y >= 0 && coord.x < extent_.x && coord.y < extent_.y;
		}

		[[nodiscard]] std::size_t tile_index(const math::point2 coord) const noexcept{
			return static_cast<std::size_t>(coord.x + coord.y * extent_.x);
		}

		[[nodiscard]] object_handle building_handle_at(const math::point2 coord) const noexcept{
			if(!this->contains(coord)){
				return {};
			}
			return tile_buildings_[this->tile_index(coord)];
		}

		[[nodiscard]] const building_common* building_at(const math::point2 coord) const noexcept{
			const object_handle handle = this->building_handle_at(coord);
			if(!handle){
				return nullptr;
			}
			return buildings_.try_common(handle);
		}

		[[nodiscard]] const building_common* try_building(const object_handle handle) const noexcept{
			return buildings_.try_common(handle);
		}

		[[nodiscard]] const tile_status& tile_status_at(const building_common& building, const math::point2 coord) const noexcept{
			return tile_statuses_[building.tile_status_offset + building.local_tile_index(coord)];
		}

		[[nodiscard]] const tile_state& tile_state_at(const math::point2 coord) const noexcept{
			return tile_states_[this->tile_index(coord)];
		}

		[[nodiscard]] std::uint32_t corridor_group_at(const math::point2 coord) const noexcept{
			if(!this->contains(coord)){
				return invalid_corridor_group;
			}
			return corridor_groups_[this->tile_index(coord)];
		}

		[[nodiscard]] bool reachable_between(const math::point2 lhs, const math::point2 rhs) const noexcept{
			const std::uint32_t group = this->corridor_group_at(lhs);
			return group != invalid_corridor_group && group == this->corridor_group_at(rhs);
		}

		[[nodiscard]] chamber_dump dump() const{
			chamber_dump result{
				.extent = extent_,
				.tile_states = tile_states_,
				.structural_hit_points = structural_hit_points_
			};
			(void)buildings_.for_each_common([this, &result](const building_common& building){
				result.buildings.push_back(this->dump_building(building));
			});
			return result;
		}

		void load_dump(const chamber_dump& dump){
			if(dump.extent.x < 0 || dump.extent.y < 0){
				throw std::invalid_argument{"chamber dump extent is negative"};
			}
			const std::size_t tile_count = static_cast<std::size_t>(dump.extent.x * dump.extent.y);
			if(dump.tile_states.size() != tile_count){
				throw std::invalid_argument{"chamber dump tile state count does not match extent"};
			}

			chamber_manifold rebuilt{dump.extent};
			rebuilt.tile_states_ = dump.tile_states;
			rebuilt.rebuild_corridor_groups();
			auto load_building = [&rebuilt](const chamber_building_dump& building_dump){
				chamber_command_result placed = rebuilt.place_dump_building(building_dump);
				if(!placed.applied()){
					throw std::logic_error{"chamber dump building placement failed"};
				}
				building_common* building = rebuilt.mutable_try_building(placed.target);
				if(building == nullptr){
					throw std::logic_error{"chamber dump building placement did not create a building"};
				}
				if(building_dump.tile_statuses.size() != building->tile_status_count){
					throw std::invalid_argument{"chamber dump tile status count does not match building region"};
				}
				building->hit_points = building_dump.hit_points;
				building->valid_energy = building_dump.valid_energy;
				building->energy_dynamic = building_dump.energy_dynamic;
				building->ideal_maneuver = building_dump.maneuver;
				building->maneuvering = building_dump.maneuvering;
				building->targetable = building_dump.targetable;
				for(std::uint32_t i = 0; i != building->tile_status_count; ++i){
					rebuilt.tile_statuses_[building->tile_status_offset + i] = building_dump.tile_statuses[i];
				}
			};

			for(const chamber_building_dump& building_dump : dump.buildings){
				if(building_dump.structural){
					load_building(building_dump);
				}
			}
			for(const chamber_building_dump& building_dump : dump.buildings){
				if(!building_dump.structural){
					load_building(building_dump);
				}
			}
			rebuilt.structural_hit_points_ = dump.structural_hit_points;
			*this = std::move(rebuilt);
			this->mark_collider_dirty();
		}

		[[nodiscard]] chamber_command_result execute(const place_basic_building_command& command){
			return this->place_building(
				command.region,
				command.hit_points,
				command.structural,
				command.structural_support_radius,
				basic_building{},
				command.energy,
				command.energy_acquisition);
		}

		[[nodiscard]] chamber_command_result execute(const place_standard_building_command& command){
			switch(command.type){
			case standard_building_type::armor:
				return this->place_building(
					command.region,
					command.hit_points,
					false,
					0u,
					armor_building{},
					command.energy,
					command.energy_acquisition);
			case standard_building_type::energy_generator:
				return this->place_building(
					command.region,
					command.hit_points,
					false,
					0u,
					energy_generator_building{},
					command.energy,
					command.energy_acquisition);
			case standard_building_type::thruster:
				return this->place_building(
					command.region,
					command.hit_points,
					false,
					0u,
					thruster_building{.ideal_maneuver = command.maneuver},
					command.energy,
					command.energy_acquisition);
			case standard_building_type::radar:
				return this->place_building(
					command.region,
					command.hit_points,
					false,
					0u,
					command.radar,
					command.energy,
					command.energy_acquisition);
			case standard_building_type::turret:
				return this->place_building(
					command.region,
					command.hit_points,
					false,
					0u,
					command.turret,
					command.energy,
					command.energy_acquisition);
			case standard_building_type::structural_joint:
				return this->place_building(
					command.region,
					command.hit_points,
					true,
					command.structural_support_radius,
					structural_joint_building{},
					command.energy,
					command.energy_acquisition);
			}
			std::unreachable();
		}

		template <typename Building>
		[[nodiscard]] chamber_command_result execute(place_building_command<Building>&& command){
			return this->place_building(
				command.region,
				command.hit_points,
				command.structural,
				command.structural_support_radius,
				std::move(command.building),
				command.energy,
				command.energy_acquisition);
		}

		[[nodiscard]] chamber_command_result execute(const erase_building_command& command){
			chamber_command_result result = this->erase_building_no_cascade(command.target);
			if(result.applied()){
				this->erase_unsupported_buildings(result.cascaded_targets);
			}
			return result;
		}

		[[nodiscard]] chamber_command_result execute(const set_tile_placeable_command& command){
			const chamber_command_status validation = this->validate_tile_region(command.region);
			if(validation != chamber_command_status::applied){
				return {.status = validation};
			}

			if(!command.placeable){
				for(int y = command.region.src.y; y != command.region.src.y + command.region.extent.y; ++y){
					for(int x = command.region.src.x; x != command.region.src.x + command.region.extent.x; ++x){
						if(this->building_at({x, y}) != nullptr){
							return {.status = chamber_command_status::occupied};
						}
					}
				}
			}

			for(int y = command.region.src.y; y != command.region.src.y + command.region.extent.y; ++y){
				for(int x = command.region.src.x; x != command.region.src.x + command.region.extent.x; ++x){
					this->mutable_tile_state_at({x, y}).placeable = command.placeable;
				}
			}
			return {.status = chamber_command_status::applied};
		}

		[[nodiscard]] chamber_command_result execute(const set_tile_corridor_command& command){
			const chamber_command_status validation = this->validate_tile_region(command.region);
			if(validation != chamber_command_status::applied){
				return {.status = validation};
			}

			for(int y = command.region.src.y; y != command.region.src.y + command.region.extent.y; ++y){
				for(int x = command.region.src.x; x != command.region.src.x + command.region.extent.x; ++x){
					this->mutable_tile_state_at({x, y}).corridor = command.corridor;
				}
			}
			this->rebuild_corridor_groups();
			return {.status = chamber_command_status::applied};
		}

		[[nodiscard]] chamber_command_result execute(const set_building_energy_command& command){
			building_common* building = this->mutable_try_building(command.target);
			if(building == nullptr){
				return {.status = chamber_command_status::expired_target, .target = command.target};
			}
			if(!chamber_manifold::valid_energy_configuration(command.energy, command.acquisition)){
				return {.status = chamber_command_status::invalid_energy, .target = command.target};
			}
			chamber_manifold::set_building_energy(*building, command.energy, command.acquisition);
			return {.status = chamber_command_status::applied, .target = command.target};
		}

		[[nodiscard]] chamber_command_result execute(const set_building_maneuver_command& command){
			building_common* building = this->mutable_try_building(command.target);
			if(building == nullptr){
				return {.status = chamber_command_status::expired_target, .target = command.target};
			}
			if(!chamber_manifold::valid_maneuver_configuration(command.maneuver)){
				return {.status = chamber_command_status::invalid_maneuver, .target = command.target};
			}
			chamber_manifold::set_building_maneuver(*building, command.maneuver, command.enabled);
			return {.status = chamber_command_status::applied, .target = command.target};
		}

		[[nodiscard]] chamber_command_result execute(const chamber_command& command){
			return std::visit([this](const auto& item){
				return this->execute(item);
			}, command);
		}

		[[nodiscard]] projectile_hit_result execute(const projectile_hit_command command){
			return this->apply_projectile_hit(command.context);
		}

		void execute(const update_buildings_command command){
			this->update_buildings(command.delta_seconds);
		}

		void execute(const update_energy_command command) noexcept{
			this->update_energy(command.delta_seconds);
		}

		void execute(update_maneuver_command) noexcept{
			this->update_maneuver();
		}

		[[nodiscard]] chamber_command_result execute(const update_targeting_command& command){
			this->update_targeting(command);
			return {.status = chamber_command_status::applied};
		}

		[[nodiscard]] chamber_command_result execute(clear_fire_requests_command) noexcept{
			last_fire_requests_.clear();
			return {.status = chamber_command_status::applied};
		}

		[[nodiscard]] projectile_hit_result execute(settle_pending_damage_command) noexcept{
			return this->settle_pending_damage();
		}

		void post_command(chamber_command command){
			pending_commands_.push_back(std::move(command));
		}

		[[nodiscard]] const std::vector<chamber_command_result>& last_command_results() const noexcept{
			return last_command_results_;
		}

		[[nodiscard]] std::vector<chamber_command_result> execute_pending_commands(){
			std::vector<chamber_command> commands = std::exchange(pending_commands_, {});
			last_command_results_.clear();
			last_command_results_.reserve(commands.size());
			for(const chamber_command& command : commands){
				last_command_results_.push_back(this->execute(command));
			}
			return last_command_results_;
		}
	};

	export
	[[nodiscard]] inline physics::collision_shape make_chamber_collision_shape(const chamber_manifold& chamber){
		physics::collision_shape shape{};
		(void)chamber.buildings().for_each_common([&](const building_common& building){
			if(building.region.area() <= 0 || building.hit_points.is_killed()){
				return;
			}

			const math::vec2 half_extent{
				static_cast<float>(building.region.width()) * tile_size * 0.5f,
				static_cast<float>(building.region.height()) * tile_size * 0.5f
			};
			if(half_extent.x <= 0.f || half_extent.y <= 0.f){
				return;
			}

			const math::trans2 local_transform{
				{
					(static_cast<float>(building.region.src.x) + static_cast<float>(building.region.width()) * 0.5f)
						* tile_size,
					(static_cast<float>(building.region.src.y) + static_cast<float>(building.region.height()) * 0.5f)
						* tile_size
				},
				0.f
			};
			shape.add(physics::shape_of<physics::box_shape>{
				.local_transform = local_transform,
				.shape = {half_extent}
			});
		});
		return shape;
	}

	export
	inline void synchronize_chamber_collider(collider& collider, const chamber_manifold& chamber){
		physics::collision_shape shape = chamber::make_chamber_collision_shape(chamber);
		collider.shape = shape.to_record();
		collider.enabled = !collider.shape.empty();
	}

	[[nodiscard]] inline math::trans2 local_shape_transform(
		const math::trans2 shape_transform,
		const math::uniform_trans2 target_motion) noexcept{
		return shape_transform << static_cast<math::trans2>(target_motion);
	}

	[[nodiscard]] inline math::frect swept_aabb(
		const physics::collision_shape_record& shape,
		const math::trans2 from,
		const math::trans2 to){
		math::frect result = shape.aabb(from);
		result.expand_by(shape.aabb(to));
		return result;
	}

	[[nodiscard]] inline math::vec2 trace_direction(const math::trans2 from, const math::trans2 to) noexcept{
		math::vec2 direction = to.vec - from.vec;
		if(direction.length2() <= 1.0e-6f){
			return {1.f, 0.f};
		}

		return direction.normalize();
	}

	[[nodiscard]] inline float lateral_distance2(
		const math::vec2 origin,
		const math::vec2 direction,
		const math::vec2 point) noexcept{
		const math::vec2 delta = point - origin;
		const float projected = delta.dot(direction);
		return (delta - direction * projected).length2();
	}

	[[nodiscard]] inline bool segment_intersects_aabb(
		const math::vec2 begin,
		const math::vec2 end,
		const math::vec2 minimum,
		const math::vec2 maximum) noexcept{
		float low{};
		float high{1.f};
		const math::vec2 delta = end - begin;

		auto clip = [&low, &high](const float origin, const float direction, const float min_value, const float max_value) noexcept{
			if(std::abs(direction) <= 1.0e-6f){
				return origin >= min_value && origin <= max_value;
			}

			float axis_low = (min_value - origin) / direction;
			float axis_high = (max_value - origin) / direction;
			if(axis_low > axis_high){
				std::swap(axis_low, axis_high);
			}
			low = std::max(low, axis_low);
			high = std::min(high, axis_high);
			return low <= high;
		};

		return clip(begin.x, delta.x, minimum.x, maximum.x)
			&& clip(begin.y, delta.y, minimum.y, maximum.y);
	}

	[[nodiscard]] inline bool projectile_center_trace_can_hit_tile(
		const physics::collision_shape_record& projectile_shape,
		const math::trans2 from,
		const math::trans2 to,
		const math::point2 coord) noexcept{
		constexpr float conservative_margin = 1.0e-4f;
		const float radius = std::max(projectile_shape.radius_bound(), 0.f) + conservative_margin;
		const math::vec2 minimum{
			static_cast<float>(coord.x) * tile_size - radius,
			static_cast<float>(coord.y) * tile_size - radius
		};
		const math::vec2 maximum{
			static_cast<float>(coord.x + 1) * tile_size + radius,
			static_cast<float>(coord.y + 1) * tile_size + radius
		};
		return chamber::segment_intersects_aabb(from.vec, to.vec, minimum, maximum);
	}

	[[nodiscard]] inline const physics::collision_shape_record& tile_collision_shape_record(){
		static const physics::collision_shape_record shape =
			physics::make_box_collision_shape({tile_size * 0.5f, tile_size * 0.5f}).to_record();
		return shape;
	}

	inline void collect_projectile_tile_hits(
		const chamber_manifold& chamber,
		const projectile_hit_context& context,
		std::vector<projectile_tile_hit>& hits){
		hits.clear();
		const math::point2 extent = chamber.extent();
		if(extent.x <= 0 || extent.y <= 0){
			return;
		}

		const math::trans2 from = chamber::local_shape_transform(
			context.projectile_endpoint.previous_shape,
			context.target_endpoint.previous_motion);
		const math::trans2 to = chamber::local_shape_transform(
			context.projectile_endpoint.current_shape,
			context.target_endpoint.current_motion);
		const math::frect trace_bound = chamber::swept_aabb(context.shape, from, to);

		const int min_x = std::clamp(static_cast<int>(std::floor(trace_bound.get_src_x() / tile_size)), 0, extent.x - 1);
		const int min_y = std::clamp(static_cast<int>(std::floor(trace_bound.get_src_y() / tile_size)), 0, extent.y - 1);
		const int max_x = std::clamp(static_cast<int>(std::ceil(trace_bound.get_end_x() / tile_size)) - 1, 0, extent.x - 1);
		const int max_y = std::clamp(static_cast<int>(std::ceil(trace_bound.get_end_y() / tile_size)) - 1, 0, extent.y - 1);
		if(max_x < min_x || max_y < min_y){
			return;
		}

		const math::vec2 direction = chamber::trace_direction(from, to);
		const physics::collision_shape_record& tile_shape = chamber::tile_collision_shape_record();

		(void)chamber.buildings().for_each_common([&](const building_common& building){
			if(building.region.area() <= 0 || building.hit_points.is_killed()){
				return;
			}

			const int begin_x = std::max(min_x, building.region.src.x);
			const int begin_y = std::max(min_y, building.region.src.y);
			const int end_x = std::min(max_x + 1, building.region.src.x + building.region.extent.x);
			const int end_y = std::min(max_y + 1, building.region.src.y + building.region.extent.y);
			if(begin_x >= end_x || begin_y >= end_y){
				return;
			}

			for(int y = begin_y; y != end_y; ++y){
				for(int x = begin_x; x != end_x; ++x){
					const math::point2 coord{x, y};
					if(chamber.tile_status_at(building, coord).destroyed()){
						continue;
					}
					if(!chamber::projectile_center_trace_can_hit_tile(context.shape, from, to, coord)){
						continue;
					}

					const math::trans2 tile_transform{
						{(static_cast<float>(x) + 0.5f) * tile_size, (static_cast<float>(y) + 0.5f) * tile_size},
						0.f
					};
					const auto toi = physics::linear_time_of_impact(
						context.shape,
						from,
						to,
						tile_shape,
						tile_transform,
						tile_transform,
						16,
						6);
					if(!toi.hit){
						continue;
					}

					hits.push_back({
						.coord = coord,
						.fraction = toi.fraction,
						.lateral_distance2 = chamber::lateral_distance2(from.vec, direction, tile_transform.vec)
					});
				}
			}
		});

		std::ranges::sort(hits, [](const projectile_tile_hit& lhs, const projectile_tile_hit& rhs) noexcept{
			if(lhs.fraction != rhs.fraction){
				return lhs.fraction < rhs.fraction;
			}
			if(lhs.lateral_distance2 != rhs.lateral_distance2){
				return lhs.lateral_distance2 < rhs.lateral_distance2;
			}
			if(lhs.coord.y != rhs.coord.y){
				return lhs.coord.y < rhs.coord.y;
			}
			return lhs.coord.x < rhs.coord.x;
		});
	}
}
