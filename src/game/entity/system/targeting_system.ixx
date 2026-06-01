export module mo_yanxi.game.ecs.system.targeting;

export import mo_yanxi.game.ecs.component.chamber.damage_grid;
export import mo_yanxi.game.ecs.component.faction;
export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.component.physics;
export import mo_yanxi.game.ecs.component.projectile.manifold;
export import mo_yanxi.game.ecs.component.targeting;
export import mo_yanxi.game.ecs.system.physics;

import std;

namespace mo_yanxi::game::ecs::system{
export
struct targeting_system_statistics{
	std::uint64_t target_snapshot_count{};
	std::uint64_t building_target_snapshot_count{};
	std::uint64_t entity_sensor_count{};
	std::uint64_t chamber_sensor_count{};
	std::uint64_t unique_scan_count{};
	std::uint64_t selected_target_count{};
};

export
struct targeting_system{
private:
	struct entity_sensor_request{
		targeting_sensor* sensor{};
		target_memory* memory{};
		math::vec2 origin{};
		std::uint32_t faction_id{};
	};

	struct scan_group{
		entity_id owner{};
		math::frect query_region{};
		bool has_query_region{};
		std::vector<entity_sensor_request> entity_sensors{};
		chamber::chamber_manifold* chamber{};
		math::trans2 chamber_transform{};
		std::uint32_t chamber_faction_id{};
		bool update_chamber{};
		std::vector<target_snapshot> candidates{};
	};

	std::unordered_map<entity_id, target_snapshot> entity_targets_{};
	std::unordered_map<entity_id, std::vector<target_snapshot>> building_targets_{};
	std::vector<scan_group> scan_groups_{};
	std::unordered_map<entity_id, std::size_t> scan_group_indices_{};
	targeting_system_statistics last_statistics_{};
	std::uint64_t scan_tick_{};

	[[nodiscard]] static const targetable_profile& resolved_profile(
		const entity_id entity,
		const targetable_profile& fallback) noexcept{
		const targetable_profile* profile = entity.try_get<targetable_profile>();
		return profile != nullptr ? *profile : fallback;
	}

	[[nodiscard]] static std::uint32_t faction_id_of(const entity_id entity) noexcept{
		const faction_data* faction = entity.try_get<faction_data>();
		return faction != nullptr ? faction->id : 0u;
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
		const auto* chamber = target.entity.try_get<chamber::chamber_manifold>();
		if(chamber == nullptr){
			return false;
		}
		const chamber::building_common* building = chamber->try_building(target.chamber_building);
		return building != nullptr && !building->hit_points.is_killed();
	}

	static void prune_memory(target_memory& memory) noexcept{
		std::erase_if(memory.targets, [](const target_snapshot& target){
			return !targeting_system::target_ref_alive(target.target);
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

	static void append_region(
		math::frect& target,
		bool& has_region,
		const math::frect region) noexcept{
		if(!has_region){
			target = region;
			has_region = true;
			return;
		}
		target.expand_by(region);
	}

	[[nodiscard]] scan_group& scan_group_for(const entity_id owner){
		if(const auto it = scan_group_indices_.find(owner); it != scan_group_indices_.end()){
			return scan_groups_[it->second];
		}
		const std::size_t index = scan_groups_.size();
		scan_groups_.push_back(scan_group{.owner = owner});
		(void)scan_group_indices_.try_emplace(owner, index);
		return scan_groups_.back();
	}

	void rebuild_target_cache(component_manager& manager, targeting_system_statistics& statistics){
		entity_targets_.clear();
		building_targets_.clear();

		targetable_profile fallback_profile{};
		manager.each([&](
			const chunk_meta& meta,
			const mech_motion& motion,
			const collider&){
			const entity_id entity = meta.id();
			if(!entity || entity.is_expired() || entity.try_get<projectile_state>() != nullptr){
				return;
			}

			const targetable_profile& profile =
				targeting_system::resolved_profile(entity, fallback_profile);
			if(!profile.enabled){
				return;
			}

			target_snapshot snapshot{
				.target = target_ref::entity_target(entity),
				.position = motion.trans.vec,
				.velocity = motion.vel.vec,
				.faction_id = targeting_system::faction_id_of(entity),
				.priority = profile.priority,
				.signature = profile.signature,
				.stealth_strength = profile.stealth_strength,
				.detectable_channels = profile.detectable_channels
			};
			(void)entity_targets_.try_emplace(entity, snapshot);
			++statistics.target_snapshot_count;

			const auto* chamber = entity.try_get<chamber::chamber_manifold>();
			if(chamber == nullptr){
				return;
			}

			std::vector<target_snapshot> building_snapshots{};
			const math::trans2 chamber_transform = static_cast<math::trans2>(motion.trans);
			(void)chamber->buildings().for_each_common([&](const chamber::building_common& building){
				if(building.hit_points.is_killed() || !building.targetable.enabled){
					return;
				}
				building_snapshots.push_back(target_snapshot{
					.target = target_ref::chamber_building_target(entity, building.handle),
					.position = chamber::chamber_manifold::chamber_to_world_point(
						chamber::chamber_manifold::building_center(building),
						chamber_transform),
					.velocity = motion.vel.vec,
					.faction_id = snapshot.faction_id,
					.priority = building.targetable.priority,
					.signature = building.targetable.signature,
					.stealth_strength = building.targetable.stealth_strength,
					.detectable_channels = building.targetable.detectable_channels
				});
				++statistics.target_snapshot_count;
				++statistics.building_target_snapshot_count;
			});
			if(!building_snapshots.empty()){
				(void)building_targets_.try_emplace(entity, std::move(building_snapshots));
			}
		});
	}

	void collect_entity_sensor_groups(
		component_manager& manager,
		const float delta_seconds,
		targeting_system_statistics& statistics){
		manager.each([&](
			const chunk_meta& meta,
			const mech_motion& motion,
			targeting_sensor& sensor,
			target_memory& memory){
			const entity_id entity = meta.id();
			if(!entity || entity.is_expired()){
				memory.clear();
				return;
			}

			targeting_system::prune_memory(memory);
			if(!sensor.can_scan()){
				memory.clear();
				return;
			}

			scan_group& group = this->scan_group_for(entity);
			group.entity_sensors.push_back(entity_sensor_request{
				.sensor = std::addressof(sensor),
				.memory = std::addressof(memory),
				.origin = motion.trans.vec,
				.faction_id = targeting_system::faction_id_of(entity)
			});
			++statistics.entity_sensor_count;

			const float next_timer = std::max(0.f, sensor.scan_timer - delta_seconds);
			if(next_timer <= 0.f){
				targeting_system::append_region(
					group.query_region,
					group.has_query_region,
					math::frect{motion.trans.vec, sensor.range * 2.f});
			}
		});
	}

	void collect_chamber_sensor_groups(
		component_manager& manager,
		const float delta_seconds,
		targeting_system_statistics& statistics){
		manager.each([&](
			const chunk_meta& meta,
			const mech_motion& motion,
			chamber::chamber_manifold& chamber){
			const entity_id entity = meta.id();
			if(!entity || entity.is_expired()){
				return;
			}

			bool has_targeting_building{};
			bool has_region{};
			math::frect query_region{};
			const math::trans2 chamber_transform = static_cast<math::trans2>(motion.trans);
			(void)chamber.buildings().template for_each<chamber::radar_building>(
				[&](const chamber::building_common& common, const chamber::radar_building& radar){
					has_targeting_building = true;
					++statistics.chamber_sensor_count;
					const float efficiency = common.get_efficiency();
					if(common.hit_points.is_killed() || efficiency <= 0.f || !radar.sensor.can_scan()){
						return;
					}

					const float next_timer = std::max(
						0.f,
						radar.sensor.scan_timer - delta_seconds * efficiency);
					if(next_timer > 0.f){
						return;
					}

					const math::vec2 origin = chamber::chamber_manifold::chamber_to_world_point(
						chamber::chamber_manifold::building_center(common),
						chamber_transform);
					targeting_system::append_region(
						query_region,
						has_region,
						math::frect{origin, radar.sensor.range * 2.f});
				});
			(void)chamber.buildings().template for_each<chamber::turret_building>(
				[&](const chamber::building_common&, const chamber::turret_building&){
					has_targeting_building = true;
				});

			if(!has_targeting_building){
				return;
			}

			scan_group& group = this->scan_group_for(entity);
			group.chamber = std::addressof(chamber);
			group.chamber_transform = chamber_transform;
			group.chamber_faction_id = targeting_system::faction_id_of(entity);
			group.update_chamber = true;
			if(has_region){
				targeting_system::append_region(
					group.query_region,
					group.has_query_region,
					query_region);
			}
		});
	}

	void append_query_candidate(scan_group& group, const entity_id candidate){
		if(!candidate || candidate == group.owner || candidate.is_expired()){
			return;
		}
		if(const auto entity_it = entity_targets_.find(candidate); entity_it != entity_targets_.end()){
			group.candidates.push_back(entity_it->second);
		}
		if(const auto building_it = building_targets_.find(candidate); building_it != building_targets_.end()){
			group.candidates.insert(
				group.candidates.end(),
				building_it->second.begin(),
				building_it->second.end());
		}
	}

	void query_scan_groups(const physics_system& physics, targeting_system_statistics& statistics){
		for(scan_group& group : scan_groups_){
			group.candidates.clear();
			if(!group.has_query_region){
				continue;
			}

			++statistics.unique_scan_count;
			physics.spatial_query(group.query_region, [&](const physics_query_result& result){
				this->append_query_candidate(group, result.id());
			});
		}
	}

	[[nodiscard]] static std::vector<target_snapshot> select_targets(
		std::span<const target_snapshot> candidates,
		const targeting_sensor& sensor,
		const math::vec2 origin,
		const std::uint32_t faction_id){
		std::vector<target_snapshot> selected{};
		for(const target_snapshot& candidate : candidates){
			if(!targeting_system::target_ref_alive(candidate.target)
				|| !target_is_hostile(faction_id, candidate.faction_id)
				|| !target_is_detectable(sensor, candidate)){
				continue;
			}

			target_snapshot snapshot = candidate;
			snapshot.distance = origin.dst(snapshot.position);
			snapshot.preference += sensor.preference;
			if(snapshot.distance > sensor.range){
				continue;
			}
			selected.push_back(snapshot);
		}

		std::ranges::sort(selected, targeting_system::target_snapshot_less);
		const auto unique = std::ranges::unique(
			selected,
			std::ranges::equal_to{},
			[](const target_snapshot& snapshot) noexcept{
				return snapshot.target;
			});
		selected.erase(unique.begin(), unique.end());
		if(selected.size() > sensor.max_targets){
			selected.resize(sensor.max_targets);
		}
		return selected;
	}

	void resolve_entity_sensor_requests(
		const float delta_seconds,
		targeting_system_statistics& statistics){
		for(scan_group& group : scan_groups_){
			for(entity_sensor_request& request : group.entity_sensors){
				if(request.sensor == nullptr || request.memory == nullptr){
					std::terminate();
				}
				targeting_sensor& sensor = *request.sensor;
				target_memory& memory = *request.memory;
				targeting_system::prune_memory(memory);
				if(!sensor.can_scan()){
					memory.clear();
					continue;
				}

				sensor.scan_timer = std::max(0.f, sensor.scan_timer - delta_seconds);
				if(sensor.scan_timer > 0.f){
					continue;
				}

				std::vector<target_snapshot> selected = targeting_system::select_targets(
					std::span<const target_snapshot>{group.candidates},
					sensor,
					request.origin,
					request.faction_id);
				statistics.selected_target_count += selected.size();
				memory.assign(std::move(selected), scan_tick_);
				sensor.scan_timer = std::max(0.f, sensor.scan_interval);
			}
		}
	}

	void resolve_chamber_requests(
		const float delta_seconds,
		targeting_system_statistics& statistics){
		for(scan_group& group : scan_groups_){
			if(!group.update_chamber || group.chamber == nullptr){
				continue;
			}

			(void)group.chamber->execute(chamber::update_targeting_command{
				.candidates = group.candidates,
				.faction_id = group.chamber_faction_id,
				.delta_seconds = delta_seconds,
				.chamber_transform = group.chamber_transform,
				.scan_tick = scan_tick_
			});
			for(const chamber::chamber_fire_request& request : group.chamber->last_fire_requests()){
				if(request.target){
					++statistics.selected_target_count;
				}
			}
		}
	}

public:
	void clear(){
		entity_targets_.clear();
		building_targets_.clear();
		scan_groups_.clear();
		scan_group_indices_.clear();
		last_statistics_ = {};
		scan_tick_ = 0;
	}

	void step(component_manager& manager, const physics_system& physics){
		++scan_tick_;
		targeting_system_statistics statistics{};
		scan_groups_.clear();
		scan_group_indices_.clear();
		this->rebuild_target_cache(manager, statistics);
		const float delta_seconds = manager.get_update_delta();
		this->collect_entity_sensor_groups(manager, delta_seconds, statistics);
		this->collect_chamber_sensor_groups(manager, delta_seconds, statistics);
		this->query_scan_groups(physics, statistics);
		this->resolve_entity_sensor_requests(delta_seconds, statistics);
		this->resolve_chamber_requests(delta_seconds, statistics);
		last_statistics_ = statistics;
	}

	void run(component_manager& manager, const physics_system& physics){
		this->step(manager, physics);
	}

	[[nodiscard]] targeting_system_statistics last_statistics() const noexcept{
		return last_statistics_;
	}
};
}
