export module mo_yanxi.game.ecs.component.chamber.damage_grid;

export import mo_yanxi.game.ecs.component.damage;
export import mo_yanxi.game.ecs.component.physics;

import std;

namespace mo_yanxi::game::ecs::chamber{
	export constexpr inline float tile_size = 1.f;

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
	struct building_data{
		tile_region region{};
		hit_point hit_points{};
		std::vector<tile_status> tiles{};
		std::vector<tile_damage_event> damage_events{};
		float pending_damage{};
		bool structural{};

		[[nodiscard]] tile_status& tile_at(const math::point2 coord) noexcept{
			const math::point2 local = coord - region.src;
			return tiles[static_cast<std::size_t>(local.x + local.y * region.width())];
		}

		[[nodiscard]] const tile_status& tile_at(const math::point2 coord) const noexcept{
			const math::point2 local = coord - region.src;
			return tiles[static_cast<std::size_t>(local.x + local.y * region.width())];
		}

		float consume_damage(const math::point2 coord, damage_group& damage) noexcept{
			if(!region.contains(coord) || damage.exhausted()){
				return 0.f;
			}

			const float actual = tile_at(coord).take_damage(damage.sum());
			if(actual <= 0.f){
				return 0.f;
			}

			damage.consume(actual);
			pending_damage += actual;
			damage_events.push_back({
				.tile_coord = coord,
				.actual_damage = actual
			});
			return actual;
		}

		float settle_pending_damage() noexcept{
			const float applied = hit_points.accept(pending_damage);
			pending_damage = 0.f;
			return applied;
		}
	};

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
		float actual_damage{};
	};

	export
	struct chamber_manifold{
		math::point2 extent{};
		std::vector<int> tile_buildings{};
		std::vector<building_data> buildings{};
		hit_point structural_hit_points{};

		[[nodiscard]] explicit chamber_manifold(math::point2 extent = {}) : extent(extent){
			if(extent.x > 0 && extent.y > 0){
				tile_buildings.assign(static_cast<std::size_t>(extent.x * extent.y), -1);
				structural_hit_points.reset_to(static_cast<float>(extent.x * extent.y) * 100.f);
			}
		}

		[[nodiscard]] bool contains(const math::point2 coord) const noexcept{
			return coord.x >= 0 && coord.y >= 0 && coord.x < extent.x && coord.y < extent.y;
		}

		[[nodiscard]] std::size_t tile_index(const math::point2 coord) const noexcept{
			return static_cast<std::size_t>(coord.x + coord.y * extent.x);
		}

		[[nodiscard]] building_data* building_at(const math::point2 coord) noexcept{
			if(!contains(coord)){
				return nullptr;
			}
			const int building_index = tile_buildings[tile_index(coord)];
			if(building_index < 0){
				return nullptr;
			}
			return std::addressof(buildings[static_cast<std::size_t>(building_index)]);
		}

		[[nodiscard]] const building_data* building_at(const math::point2 coord) const noexcept{
			if(!contains(coord)){
				return nullptr;
			}
			const int building_index = tile_buildings[tile_index(coord)];
			if(building_index < 0){
				return nullptr;
			}
			return std::addressof(buildings[static_cast<std::size_t>(building_index)]);
		}

		building_data& place_building(const tile_region region, const float hit_points, const bool structural = false){
			if(region.area() <= 0){
				throw std::invalid_argument{"building region must not be empty"};
			}
			for(int y = region.src.y; y != region.src.y + region.extent.y; ++y){
				for(int x = region.src.x; x != region.src.x + region.extent.x; ++x){
					const math::point2 coord{x, y};
					if(!contains(coord) || building_at(coord) != nullptr){
						throw std::invalid_argument{"building region is not placeable"};
					}
				}
			}

			const int building_index = static_cast<int>(buildings.size());
			building_data& building = buildings.emplace_back();
			building.region = region;
			building.hit_points.reset_to(hit_points);
			building.structural = structural;
			const float tile_hp = hit_points / static_cast<float>(region.area()) * 2.f;
			building.tiles.assign(static_cast<std::size_t>(region.area()), tile_status{tile_hp});

			for(int y = region.src.y; y != region.src.y + region.extent.y; ++y){
				for(int x = region.src.x; x != region.src.x + region.extent.x; ++x){
					tile_buildings[tile_index({x, y})] = building_index;
				}
			}
			return building;
		}

		projectile_hit_result settle_pending_damage() noexcept{
			projectile_hit_result result{};
			for(building_data& building : buildings){
				const float applied = building.settle_pending_damage();
				if(applied <= 0.f){
					continue;
				}
				result.actual_damage += applied;
				if(building.structural){
					structural_hit_points.accept(applied);
				}
			}
			result.target_destroyed = structural_hit_points.is_killed();
			return result;
		}
	};

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

	[[nodiscard]] inline std::vector<projectile_tile_hit> collect_projectile_tile_hits(
		const chamber_manifold& chamber,
		const projectile_hit_context& context){
		if(chamber.extent.x <= 0 || chamber.extent.y <= 0){
			return {};
		}

		const math::trans2 from = chamber::local_shape_transform(
			context.projectile_endpoint.previous_shape,
			context.target_endpoint.previous_motion);
		const math::trans2 to = chamber::local_shape_transform(
			context.projectile_endpoint.current_shape,
			context.target_endpoint.current_motion);
		const math::frect trace_bound = chamber::swept_aabb(context.shape, from, to);

		const int min_x = std::clamp(static_cast<int>(std::floor(trace_bound.get_src_x() / tile_size)), 0, chamber.extent.x - 1);
		const int min_y = std::clamp(static_cast<int>(std::floor(trace_bound.get_src_y() / tile_size)), 0, chamber.extent.y - 1);
		const int max_x = std::clamp(static_cast<int>(std::ceil(trace_bound.get_end_x() / tile_size)) - 1, 0, chamber.extent.x - 1);
		const int max_y = std::clamp(static_cast<int>(std::ceil(trace_bound.get_end_y() / tile_size)) - 1, 0, chamber.extent.y - 1);
		if(max_x < min_x || max_y < min_y){
			return {};
		}

		const math::vec2 direction = chamber::trace_direction(from, to);
		const physics::collision_shape_record tile_shape = physics::make_box_collision_shape({tile_size * 0.5f, tile_size * 0.5f});
		std::vector<projectile_tile_hit> hits{};

		for(int y = min_y; y <= max_y; ++y){
			for(int x = min_x; x <= max_x; ++x){
				const math::point2 coord{x, y};
				if(chamber.building_at(coord) == nullptr){
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
		return hits;
	}

	export
	inline projectile_hit_result apply_projectile_hit(chamber_manifold& chamber, projectile_hit_context& context){
		projectile_hit_result result{};
		for(const projectile_tile_hit& hit : chamber::collect_projectile_tile_hits(chamber, context)){
			if(context.damage.exhausted()){
				break;
			}

			building_data* building = chamber.building_at(hit.coord);
			if(!building){
				continue;
			}

			const float actual = building->consume_damage(hit.coord, context.damage);
			if(actual <= 0.f){
				continue;
			}
			result.hit_any_tile = true;
			result.actual_damage += actual;
		}
		result.damage_exhausted = context.damage.exhausted();
		return result;
	}
}
