export module mo_yanxi.game.editor.grid_editor_model;

export import mo_yanxi.game.ecs.component.chamber.damage_grid;

import std;

namespace mo_yanxi::game::editor{
export
enum class grid_editor_layer{
	placeable,
	corridor
};

export
enum class grid_editor_status{
	applied,
	no_change,
	invalid_extent,
	invalid_region,
	out_of_bounds,
	not_placeable,
	occupied,
	unsupported_structure,
	invalid_energy,
	invalid_maneuver,
	expired_target,
	no_building,
	no_undo,
	no_redo
};

export
struct grid_editor_result{
	grid_editor_status status{grid_editor_status::applied};
	ecs::chamber::chamber_command_status chamber_status{ecs::chamber::chamber_command_status::applied};
	ecs::object_handle target{};
	std::vector<ecs::object_handle> cascaded_targets{};

	[[nodiscard]] constexpr bool applied() const noexcept{
		return status == grid_editor_status::applied;
	}
};

export
struct grid_editor_mirror_axis{
	int coord{};
	bool through_tile_center{};
};

export
struct grid_editor_mirror{
	std::optional<grid_editor_mirror_axis> vertical{};
	std::optional<grid_editor_mirror_axis> horizontal{};

	[[nodiscard]] constexpr bool active() const noexcept{
		return vertical.has_value() || horizontal.has_value();
	}

	constexpr void clear() noexcept{
		vertical.reset();
		horizontal.reset();
	}
};

export
struct grid_editor_standard_building_brush{
	ecs::chamber::standard_building_type type{ecs::chamber::standard_building_type::structural_joint};
	math::point2 extent{1, 1};
	float hit_points{100.f};
	std::uint32_t structural_support_radius{1u};
	ecs::chamber::energy_status energy{};
	ecs::chamber::energy_acquisition energy_acquisition{};
	ecs::chamber::maneuver_component maneuver{};
	ecs::chamber::radar_building radar{};
	ecs::chamber::turret_building turret{};

	[[nodiscard]] constexpr bool valid() const noexcept{
		return extent.x > 0 && extent.y > 0 && hit_points > 0.f;
	}

	[[nodiscard]] ecs::chamber::place_standard_building_command make_command(
		const math::point2 src) const noexcept{
		return {
			.type = type,
			.region = {.src = src, .extent = extent},
			.hit_points = hit_points,
			.structural_support_radius = structural_support_radius,
			.energy = energy,
			.energy_acquisition = energy_acquisition,
			.maneuver = maneuver,
			.radar = radar,
			.turret = turret
		};
	}
};

export
[[nodiscard]] constexpr ecs::chamber::tile_region make_tile_region(
	const math::point2 src,
	const math::point2 extent) noexcept{
	return {.src = src, .extent = extent};
}

export
[[nodiscard]] constexpr ecs::chamber::tile_region tile_region_between(
	const math::point2 lhs,
	const math::point2 rhs) noexcept{
	const math::point2 src{
		std::min(lhs.x, rhs.x),
		std::min(lhs.y, rhs.y)
	};
	const math::point2 end{
		std::max(lhs.x, rhs.x) + 1,
		std::max(lhs.y, rhs.y) + 1
	};
	return {.src = src, .extent = end - src};
}

export
[[nodiscard]] inline math::point2 tile_coord_from_world_pos(math::vec2 world_pos) noexcept{
	return world_pos.div(ecs::chamber::tile_size).floor().round<int>();
}

export
[[nodiscard]] inline math::vec2 tile_world_min(const math::point2 coord) noexcept{
	return {
		static_cast<float>(coord.x) * ecs::chamber::tile_size,
		static_cast<float>(coord.y) * ecs::chamber::tile_size
	};
}

export
[[nodiscard]] inline math::vec2 tile_world_max(const math::point2 coord) noexcept{
	return {
		static_cast<float>(coord.x + 1) * ecs::chamber::tile_size,
		static_cast<float>(coord.y + 1) * ecs::chamber::tile_size
	};
}

export
[[nodiscard]] constexpr grid_editor_status map_chamber_status(
	const ecs::chamber::chamber_command_status status) noexcept{
	using enum ecs::chamber::chamber_command_status;
	switch(status){
	case applied:
		return grid_editor_status::applied;
	case invalid_region:
		return grid_editor_status::invalid_region;
	case invalid_energy:
		return grid_editor_status::invalid_energy;
	case invalid_maneuver:
		return grid_editor_status::invalid_maneuver;
	case unsupported_structure:
		return grid_editor_status::unsupported_structure;
	case out_of_bounds:
		return grid_editor_status::out_of_bounds;
	case not_placeable:
		return grid_editor_status::not_placeable;
	case occupied:
		return grid_editor_status::occupied;
	case expired_target:
		return grid_editor_status::expired_target;
	default:
		std::unreachable();
	}
}

export
[[nodiscard]] constexpr std::string_view grid_editor_status_name(
	const grid_editor_status status) noexcept{
	using enum grid_editor_status;
	switch(status){
	case applied:
		return "applied";
	case no_change:
		return "no change";
	case invalid_extent:
		return "invalid extent";
	case invalid_region:
		return "invalid region";
	case out_of_bounds:
		return "out of bounds";
	case not_placeable:
		return "not placeable";
	case occupied:
		return "occupied";
	case unsupported_structure:
		return "unsupported structure";
	case invalid_energy:
		return "invalid energy";
	case invalid_maneuver:
		return "invalid maneuver";
	case expired_target:
		return "expired target";
	case no_building:
		return "no building";
	case no_undo:
		return "no undo";
	case no_redo:
		return "no redo";
	default:
		std::unreachable();
	}
}

namespace grid_editor_model_local{
[[nodiscard]] constexpr bool valid_extent(const math::point2 extent) noexcept{
	return extent.x >= 0 && extent.y >= 0;
}

[[nodiscard]] constexpr bool valid_region(const ecs::chamber::tile_region region) noexcept{
	return region.extent.x > 0 && region.extent.y > 0;
}

[[nodiscard]] constexpr bool same_region(
	const ecs::chamber::tile_region lhs,
	const ecs::chamber::tile_region rhs) noexcept{
	return lhs.src == rhs.src && lhs.extent == rhs.extent;
}

[[nodiscard]] constexpr bool region_in_extent(
	const math::point2 extent,
	const ecs::chamber::tile_region region) noexcept{
	return valid_region(region)
		&& region.src.x >= 0
		&& region.src.y >= 0
		&& region.src.x + region.extent.x <= extent.x
		&& region.src.y + region.extent.y <= extent.y;
}

[[nodiscard]] constexpr int mirror_coord(
	const int coord,
	const grid_editor_mirror_axis axis) noexcept{
	return axis.coord - (coord - axis.coord) + (axis.through_tile_center ? 1 : 0);
}

[[nodiscard]] constexpr ecs::chamber::tile_region mirror_region_vertical(
	const ecs::chamber::tile_region region,
	const grid_editor_mirror_axis axis) noexcept{
	const int begin = mirror_coord(region.src.x, axis);
	const int end = mirror_coord(region.src.x + region.extent.x, axis);
	return {
		.src = {std::min(begin, end), region.src.y},
		.extent = {std::abs(end - begin), region.extent.y}
	};
}

[[nodiscard]] constexpr ecs::chamber::tile_region mirror_region_horizontal(
	const ecs::chamber::tile_region region,
	const grid_editor_mirror_axis axis) noexcept{
	const int begin = mirror_coord(region.src.y, axis);
	const int end = mirror_coord(region.src.y + region.extent.y, axis);
	return {
		.src = {region.src.x, std::min(begin, end)},
		.extent = {region.extent.x, std::abs(end - begin)}
	};
}

void append_unique_region(
	std::vector<ecs::chamber::tile_region>& regions,
	const ecs::chamber::tile_region region){
	if(std::ranges::none_of(regions, [region](const ecs::chamber::tile_region existing) noexcept{
		return grid_editor_model_local::same_region(existing, region);
	})){
		regions.push_back(region);
	}
}

template <typename Function>
void each_anchor(
	const ecs::chamber::tile_region region,
	const math::point2 stride,
	Function&& function){
	for(int y = region.src.y; y < region.src.y + region.extent.y; y += stride.y){
		for(int x = region.src.x; x < region.src.x + region.extent.x; x += stride.x){
			std::invoke(function, math::point2{x, y});
		}
	}
}

[[nodiscard]] ecs::chamber::chamber_manifold make_chamber_from_dump(
	const ecs::chamber::chamber_dump& dump){
	ecs::chamber::chamber_manifold chamber{};
	chamber.load_dump(dump);
	return chamber;
}
}

export
[[nodiscard]] inline std::vector<ecs::chamber::tile_region> mirrored_tile_regions(
	const grid_editor_mirror mirror,
	const ecs::chamber::tile_region region){
	std::vector<ecs::chamber::tile_region> regions{};
	regions.reserve(mirror.vertical && mirror.horizontal ? 4u : (mirror.active() ? 2u : 1u));
	grid_editor_model_local::append_unique_region(regions, region);
	if(mirror.vertical){
		grid_editor_model_local::append_unique_region(
			regions,
			grid_editor_model_local::mirror_region_vertical(region, *mirror.vertical));
	}
	if(mirror.horizontal){
		grid_editor_model_local::append_unique_region(
			regions,
			grid_editor_model_local::mirror_region_horizontal(region, *mirror.horizontal));
	}
	if(mirror.vertical && mirror.horizontal){
		grid_editor_model_local::append_unique_region(
			regions,
			grid_editor_model_local::mirror_region_horizontal(
				grid_editor_model_local::mirror_region_vertical(region, *mirror.vertical),
				*mirror.horizontal));
	}
	return regions;
}

export
struct grid_editor_model{
private:
	ecs::chamber::chamber_manifold chamber_{math::point2{12, 8}};
	grid_editor_mirror mirror_{};
	std::vector<ecs::chamber::chamber_dump> undo_stack_{};
	std::vector<ecs::chamber::chamber_dump> redo_stack_{};

	[[nodiscard]] grid_editor_result validate_regions(
		const std::span<const ecs::chamber::tile_region> regions) const noexcept{
		for(const ecs::chamber::tile_region region : regions){
			if(!grid_editor_model_local::valid_region(region)){
				return {.status = grid_editor_status::invalid_region};
			}
			if(!grid_editor_model_local::region_in_extent(this->extent(), region)){
				return {.status = grid_editor_status::out_of_bounds};
			}
		}
		return {};
	}

	template <typename Function>
	[[nodiscard]] grid_editor_result transact(Function&& function){
		ecs::chamber::chamber_dump before = this->dump();
		ecs::chamber::chamber_manifold working =
			grid_editor_model_local::make_chamber_from_dump(before);
		grid_editor_result result = std::invoke(std::forward<Function>(function), working);
		if(!result.applied()){
			return result;
		}

		undo_stack_.push_back(std::move(before));
		redo_stack_.clear();
		chamber_ = std::move(working);
		return result;
	}

	[[nodiscard]] bool tile_layer_matches(
		const ecs::chamber::tile_region region,
		const grid_editor_layer layer,
		const bool value) const noexcept{
		for(int y = region.src.y; y != region.src.y + region.extent.y; ++y){
			for(int x = region.src.x; x != region.src.x + region.extent.x; ++x){
				const ecs::chamber::tile_state& tile = chamber_.tile_state_at({x, y});
				const bool current = layer == grid_editor_layer::placeable
					? tile.placeable
					: tile.corridor;
				if(current != value){
					return false;
				}
			}
		}
		return true;
	}

public:
	[[nodiscard]] grid_editor_model() = default;

	[[nodiscard]] explicit grid_editor_model(const math::point2 extent)
		: chamber_{extent}{
		if(!grid_editor_model_local::valid_extent(extent)){
			throw std::invalid_argument{"grid editor extent must not be negative"};
		}
	}

	void reset(const math::point2 extent = {12, 8}){
		if(!grid_editor_model_local::valid_extent(extent)){
			throw std::invalid_argument{"grid editor extent must not be negative"};
		}
		chamber_ = ecs::chamber::chamber_manifold{extent};
		mirror_.clear();
		undo_stack_.clear();
		redo_stack_.clear();
	}

	void load_dump(const ecs::chamber::chamber_dump& dump){
		ecs::chamber::chamber_manifold loaded =
			grid_editor_model_local::make_chamber_from_dump(dump);
		chamber_ = std::move(loaded);
		mirror_.clear();
		undo_stack_.clear();
		redo_stack_.clear();
	}

	[[nodiscard]] ecs::chamber::chamber_dump dump() const{
		return chamber_.dump();
	}

	[[nodiscard]] const ecs::chamber::chamber_manifold& chamber() const noexcept{
		return chamber_;
	}

	[[nodiscard]] math::point2 extent() const noexcept{
		return chamber_.extent();
	}

	[[nodiscard]] const grid_editor_mirror& mirror() const noexcept{
		return mirror_;
	}

	void set_mirror(const grid_editor_mirror mirror) noexcept{
		mirror_ = mirror;
	}

	void clear_mirror() noexcept{
		mirror_.clear();
	}

	[[nodiscard]] std::vector<ecs::chamber::tile_region> mirrored_regions(
		const ecs::chamber::tile_region region) const{
		return ::mo_yanxi::game::editor::mirrored_tile_regions(mirror_, region);
	}

	[[nodiscard]] grid_editor_result set_tile_layer(
		const ecs::chamber::tile_region region,
		const grid_editor_layer layer,
		const bool value){
		const std::vector<ecs::chamber::tile_region> regions = this->mirrored_regions(region);
		if(grid_editor_result validation = this->validate_regions(regions); !validation.applied()){
			return validation;
		}
		if(std::ranges::all_of(regions, [this, layer, value](const ecs::chamber::tile_region item){
			return this->tile_layer_matches(item, layer, value);
		})){
			return {.status = grid_editor_status::no_change};
		}

		return this->transact([regions, layer, value](ecs::chamber::chamber_manifold& chamber){
			for(const ecs::chamber::tile_region item : regions){
				ecs::chamber::chamber_command_result result =
					layer == grid_editor_layer::placeable
						? chamber.execute(ecs::chamber::set_tile_placeable_command{
							.region = item,
							.placeable = value
						})
						: chamber.execute(ecs::chamber::set_tile_corridor_command{
							.region = item,
							.corridor = value
						});
				if(!result.applied()){
					return grid_editor_result{
						.status = ::mo_yanxi::game::editor::map_chamber_status(result.status),
						.chamber_status = result.status,
						.target = result.target,
						.cascaded_targets = result.cascaded_targets
					};
				}
			}
			return grid_editor_result{};
		});
	}

	[[nodiscard]] grid_editor_result place_building(
		const math::point2 src,
		const grid_editor_standard_building_brush& brush){
		return this->place_buildings(make_tile_region(src, brush.extent), brush);
	}

	[[nodiscard]] grid_editor_result place_buildings(
		const ecs::chamber::tile_region anchor_region,
		const grid_editor_standard_building_brush& brush){
		if(!brush.valid()){
			return {.status = grid_editor_status::invalid_extent};
		}
		if(!grid_editor_model_local::valid_region(anchor_region)){
			return {.status = grid_editor_status::invalid_region};
		}

		std::vector<ecs::chamber::tile_region> target_regions{};
		grid_editor_model_local::each_anchor(anchor_region, brush.extent, [this, &brush, &target_regions](const math::point2 anchor){
			for(const ecs::chamber::tile_region target : this->mirrored_regions(make_tile_region(anchor, brush.extent))){
				grid_editor_model_local::append_unique_region(target_regions, target);
			}
		});
		if(grid_editor_result validation = this->validate_regions(target_regions); !validation.applied()){
			return validation;
		}

		return this->transact([target_regions, brush](ecs::chamber::chamber_manifold& chamber){
			grid_editor_result last{};
			for(const ecs::chamber::tile_region item : target_regions){
				ecs::chamber::chamber_command_result result =
					chamber.execute(brush.make_command(item.src));
				if(!result.applied()){
					return grid_editor_result{
						.status = ::mo_yanxi::game::editor::map_chamber_status(result.status),
						.chamber_status = result.status,
						.target = result.target,
						.cascaded_targets = result.cascaded_targets
					};
				}
				last.target = result.target;
				last.cascaded_targets = result.cascaded_targets;
			}
			return last;
		});
	}

	[[nodiscard]] grid_editor_result erase_building_at(const math::point2 coord){
		const ecs::object_handle handle = chamber_.building_handle_at(coord);
		if(!handle){
			return {.status = grid_editor_status::no_building};
		}

		return this->transact([handle](ecs::chamber::chamber_manifold& chamber){
			const ecs::chamber::chamber_command_result result =
				chamber.execute(ecs::chamber::erase_building_command{.target = handle});
			if(!result.applied()){
				return grid_editor_result{
					.status = ::mo_yanxi::game::editor::map_chamber_status(result.status),
					.chamber_status = result.status,
					.target = result.target,
					.cascaded_targets = result.cascaded_targets
				};
			}
			return grid_editor_result{
				.target = result.target,
				.cascaded_targets = result.cascaded_targets
			};
		});
	}

	[[nodiscard]] grid_editor_result undo(){
		if(undo_stack_.empty()){
			return {.status = grid_editor_status::no_undo};
		}

		redo_stack_.push_back(this->dump());
		ecs::chamber::chamber_dump previous = std::move(undo_stack_.back());
		undo_stack_.pop_back();
		chamber_.load_dump(previous);
		return {};
	}

	[[nodiscard]] grid_editor_result redo(){
		if(redo_stack_.empty()){
			return {.status = grid_editor_status::no_redo};
		}

		undo_stack_.push_back(this->dump());
		ecs::chamber::chamber_dump next = std::move(redo_stack_.back());
		redo_stack_.pop_back();
		chamber_.load_dump(next);
		return {};
	}

	[[nodiscard]] bool can_undo() const noexcept{
		return !undo_stack_.empty();
	}

	[[nodiscard]] bool can_redo() const noexcept{
		return !redo_stack_.empty();
	}
};
}
