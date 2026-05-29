module;

#include <cstddef>

export module mo_yanxi.game.ecs.component.physics;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.component.physical_property;
export import mo_yanxi.game.physics;

import std;

namespace mo_yanxi::game::ecs{
	export
	enum struct physics_contact_phase : std::uint8_t{
		begin,
		stay,
		end
	};

	export
	struct collider{
		physics::collision_shape_record shape{};
		physics::collision_filter filter{};
		math::trans2 local_transform{};

		float depth{/* UNUSED */}; //TODO should this game has depth collider?

		float ccd_threshold{1.f};
		bool enabled{true};
		physics::ccd_mode ccd{physics::ccd_mode::disabled};

		[[nodiscard]] math::trans2 world_transform(const mech_motion& motion) const noexcept{
			return local_transform >> static_cast<math::trans2>(motion.trans);
		}

		[[nodiscard]] math::frect world_aabb(const mech_motion& motion) const noexcept{
			return shape.aabb(world_transform(motion));
		}

		[[nodiscard]] bool wants_ccd(const physics_body& body, const mech_motion& motion) const noexcept{
			const auto mode = ccd == physics::ccd_mode::disabled ? body.body.ccd : ccd;
			const auto threshold = std::max(ccd_threshold, body.body.ccd_threshold);
			return mode != physics::ccd_mode::disabled && motion.vel.vec.length2() > threshold * threshold;
		}
	};

	export
	struct physics_contact_key{
		entity_id first{};
		entity_id second{};

	private:
		entity_pin first_pin_{};
		entity_pin second_pin_{};

	public:
		[[nodiscard]] physics_contact_key() = default;

		[[nodiscard]] physics_contact_key(const entity_id first, const entity_id second) noexcept
			: first(first), second(second), first_pin_(first), second_pin_(second){
		}

		[[nodiscard]] static physics_contact_key ordered(entity_id lhs, entity_id rhs) noexcept{
			return std::less<entity_id>{}(rhs, lhs)
				? physics_contact_key{rhs, lhs}
				: physics_contact_key{lhs, rhs};
		}

		[[nodiscard]] entity_pin pin_for(const entity_id id) const noexcept{
			if(id == first){
				return first_pin_;
			}
			if(id == second){
				return second_pin_;
			}
			return {};
		}

		friend bool operator==(const physics_contact_key& lhs, const physics_contact_key& rhs) noexcept{
			return lhs.first == rhs.first && lhs.second == rhs.second;
		}

		friend bool operator<(const physics_contact_key& lhs, const physics_contact_key& rhs) noexcept{
			if(std::less<entity_id>{}(lhs.first, rhs.first)){
				return true;
			}
			if(std::less<entity_id>{}(rhs.first, lhs.first)){
				return false;
			}
			return std::less<entity_id>{}(lhs.second, rhs.second);
		}
	};

	export
	struct physics_contact_event{
		physics_contact_key key{};
		physics_contact_phase phase{physics_contact_phase::begin};
		entity_id subject{};
		entity_id object{};
		bool sensor{};
		float toi{1.f};
		float depth{};
		math::vec2 normal{1.f, 0.f};
		math::vec2 point{};

	private:
		entity_pin subject_pin_{};
		entity_pin object_pin_{};

	public:
		[[nodiscard]] physics_contact_event() = default;

		[[nodiscard]] physics_contact_event(
			physics_contact_key key,
			const physics_contact_phase phase,
			const entity_id subject,
			const entity_id object,
			const bool sensor = false,
			const float toi = 1.f,
			const float depth = 0.f,
			const math::vec2 normal = {1.f, 0.f},
			const math::vec2 point = {}) noexcept
			: key(std::move(key)),
			  phase(phase),
			  subject(subject),
			  object(object),
			  sensor(sensor),
			  toi(toi),
			  depth(depth),
			  normal(normal),
			  point(point),
			  subject_pin_(this->key.pin_for(subject)),
			  object_pin_(this->key.pin_for(object)){
		}

		[[nodiscard]] entity_id other(entity_id self) const noexcept{
			return subject == self ? object : subject;
		}
	};

	export
	struct physics_query_result{
		entity_id id{};
		math::frect aabb{};
		math::vec2 position_snapshot{};
		float radius_snapshot{};

	private:
		entity_pin id_pin_{};

	public:
		[[nodiscard]] physics_query_result() = default;

		[[nodiscard]] physics_query_result(
			const entity_pin& id_pin,
			const math::frect aabb,
			const math::vec2 position,
			const float radius) noexcept
			: id(id_pin.raw_id()),
			  aabb(aabb),
			  position_snapshot(position),
			  radius_snapshot(radius),
			  id_pin_(id_pin){
		}

		[[nodiscard]] math::vec2 position() const noexcept{
			return position_snapshot;
		}

		[[nodiscard]] float radius_bound() const noexcept{
			return radius_snapshot;
		}
	};
}

template <>
struct std::hash<mo_yanxi::game::ecs::physics_contact_key>{
	[[nodiscard]] std::size_t operator()(const mo_yanxi::game::ecs::physics_contact_key& key) const noexcept{
		const auto lhs = std::hash<const void*>{}(key.first);
		const auto rhs = std::hash<const void*>{}(key.second);
		return lhs ^ (rhs + 0x9e3779b97f4a7c15ull + (lhs << 6u) + (lhs >> 2u));
	}
};
