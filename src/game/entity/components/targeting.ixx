export module mo_yanxi.game.ecs.component.targeting;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.object_storage;

import mo_yanxi.math.vector2;
import std;

namespace mo_yanxi::game::ecs{
export
inline constexpr std::uint32_t targeting_all_channels = std::numeric_limits<std::uint32_t>::max();

export
enum class target_kind : std::uint8_t{
	none,
	entity,
	chamber_building
};

export
struct target_ref{
	target_kind kind{target_kind::none};
	entity_id entity{};
	object_handle chamber_building{};

	[[nodiscard]] static constexpr target_ref entity_target(const entity_id target) noexcept{
		return {
			.kind = target ? target_kind::entity : target_kind::none,
			.entity = target
		};
	}

	[[nodiscard]] static constexpr target_ref chamber_building_target(
		const entity_id target,
		const object_handle building) noexcept{
		return {
			.kind = target && building ? target_kind::chamber_building : target_kind::none,
			.entity = target,
			.chamber_building = building
		};
	}

	[[nodiscard]] constexpr bool has_handle() const noexcept{
		switch(kind){
		case target_kind::entity:
			return static_cast<bool>(entity);
		case target_kind::chamber_building:
			return static_cast<bool>(entity) && static_cast<bool>(chamber_building);
		case target_kind::none:
			return false;
		}
		std::unreachable();
	}

	[[nodiscard]] constexpr explicit operator bool() const noexcept{
		return this->has_handle();
	}

	friend constexpr bool operator==(const target_ref&, const target_ref&) noexcept = default;

	friend constexpr std::strong_ordering operator<=>(const target_ref& lhs, const target_ref& rhs) noexcept{
		if(const auto kind_order = std::to_underlying(lhs.kind) <=> std::to_underlying(rhs.kind);
			kind_order != std::strong_ordering::equal){
			return kind_order;
		}
		if(const auto entity_order = lhs.entity <=> rhs.entity;
			entity_order != std::strong_ordering::equal){
			return entity_order;
		}
		if(lhs.chamber_building.channel < rhs.chamber_building.channel){
			return std::strong_ordering::less;
		}
		if(rhs.chamber_building.channel < lhs.chamber_building.channel){
			return std::strong_ordering::greater;
		}
		if(lhs.chamber_building.slot < rhs.chamber_building.slot){
			return std::strong_ordering::less;
		}
		if(rhs.chamber_building.slot < lhs.chamber_building.slot){
			return std::strong_ordering::greater;
		}
		if(lhs.chamber_building.generation < rhs.chamber_building.generation){
			return std::strong_ordering::less;
		}
		if(rhs.chamber_building.generation < lhs.chamber_building.generation){
			return std::strong_ordering::greater;
		}
		return std::strong_ordering::equal;
	}
};

export
struct targetable_profile{
	bool enabled{true};
	std::uint32_t detectable_channels{targeting_all_channels};
	float signature{1.f};
	float stealth_strength{};
	float priority{};
};

export
struct target_snapshot{
	target_ref target{};
	math::vec2 position{};
	math::vec2 velocity{};
	std::uint32_t faction_id{};
	float distance{};
	float priority{};
	float preference{};
	float signature{1.f};
	float stealth_strength{};
	std::uint32_t detectable_channels{targeting_all_channels};
};

export
struct targeting_sensor{
	bool enabled{true};
	float range{};
	std::uint32_t max_targets{1};
	std::uint32_t detect_channels{targeting_all_channels};
	float detection_strength{};
	float scan_interval{};
	float scan_timer{};
	float preference{};

	[[nodiscard]] constexpr bool can_scan() const noexcept{
		return enabled && range > 0.f && max_targets != 0u;
	}
};

export
struct target_memory{
	std::vector<target_snapshot> targets{};
	std::uint64_t scan_tick{};

	[[nodiscard]] std::span<const target_snapshot> view() const noexcept{
		return targets;
	}

	[[nodiscard]] bool empty() const noexcept{
		return targets.empty();
	}

	[[nodiscard]] const target_snapshot* primary() const noexcept{
		return targets.empty() ? nullptr : std::addressof(targets.front());
	}

	void clear() noexcept{
		targets.clear();
		scan_tick = 0;
	}

	void assign(std::vector<target_snapshot> next, const std::uint64_t tick){
		targets = std::move(next);
		scan_tick = tick;
	}
};

export
[[nodiscard]] constexpr bool target_is_hostile(
	const std::uint32_t self_faction,
	const std::uint32_t target_faction) noexcept{
	return self_faction == 0u || target_faction == 0u || self_faction != target_faction;
}

export
[[nodiscard]] constexpr bool target_is_detectable(
	const targeting_sensor& sensor,
	const target_snapshot& target) noexcept{
	return (sensor.detect_channels & target.detectable_channels) != 0u
		&& sensor.detection_strength + target.signature >= target.stealth_strength;
}

export
[[nodiscard]] constexpr float target_score(const target_snapshot& target) noexcept{
	return target.distance - target.priority - target.preference;
}
}

template <>
struct std::hash<mo_yanxi::game::ecs::target_ref>{
	[[nodiscard]] std::size_t operator()(const mo_yanxi::game::ecs::target_ref& target) const noexcept{
		std::size_t value = std::hash<int>{}(std::to_underlying(target.kind));
		value ^= std::hash<mo_yanxi::game::ecs::entity_id>{}(target.entity)
			+ 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
		value ^= std::hash<std::uint32_t>{}(target.chamber_building.channel)
			+ 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
		value ^= std::hash<std::uint32_t>{}(target.chamber_building.slot)
			+ 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
		value ^= std::hash<std::uint64_t>{}(target.chamber_building.generation)
			+ 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
		return value;
	}
};
