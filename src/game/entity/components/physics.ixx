module;

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
	private:
		entity_id first_id_{};
		entity_id second_id_{};

	public:
		[[nodiscard]] physics_contact_key() = default;

		[[nodiscard]] physics_contact_key(const entity_id first, const entity_id second) noexcept
			: first_id_(first),
			  second_id_(second){
		}

		[[nodiscard]] static physics_contact_key ordered(entity_id lhs, entity_id rhs) noexcept{
			return std::less<entity_id>{}(rhs, lhs)
				? physics_contact_key{rhs, lhs}
				: physics_contact_key{lhs, rhs};
		}

		[[nodiscard]] entity_id first() const noexcept{
			return first_id_;
		}

		[[nodiscard]] entity_id second() const noexcept{
			return second_id_;
		}

		[[nodiscard]] entity_pin pin_for(const entity_id id) const noexcept{
			if(id == this->first()){
				return entity_pin{first_id_};
			}
			if(id == this->second()){
				return entity_pin{second_id_};
			}
			return {};
		}

		friend bool operator==(const physics_contact_key& lhs, const physics_contact_key& rhs) noexcept{
			return lhs.first() == rhs.first() && lhs.second() == rhs.second();
		}

		friend std::strong_ordering operator<=>(const physics_contact_key& lhs, const physics_contact_key& rhs) noexcept{
			if(std::less<entity_id>{}(lhs.first(), rhs.first())){
				return std::strong_ordering::less;
			}
			if(std::less<entity_id>{}(rhs.first(), lhs.first())){
				return std::strong_ordering::greater;
			}
			if(std::less<entity_id>{}(lhs.second(), rhs.second())){
				return std::strong_ordering::less;
			}
			if(std::less<entity_id>{}(rhs.second(), lhs.second())){
				return std::strong_ordering::greater;
			}
			return std::strong_ordering::equal;
		}
	};

	export
	struct physics_contact_endpoint_snapshot{
		entity_id id_{};
		math::uniform_trans2 previous_motion{};
		math::uniform_trans2 current_motion{};
		math::trans2 previous_shape{};
		math::trans2 current_shape{};
		physics::collision_filter filter{};

		[[nodiscard]] entity_id id() const noexcept{
			return id_;
		}

		[[nodiscard]] bool valid() const noexcept{
			return id_.is_inserted();
		}
	};

	export
	struct physics_contact_event{
		physics_contact_key key{};
		physics_contact_phase phase{physics_contact_phase::begin};
		entity_id subject{};
		entity_id object{};
		physics_contact_endpoint_snapshot subject_endpoint{};
		physics_contact_endpoint_snapshot object_endpoint{};
		bool sensor{};
		float toi{1.f};
		float depth{};
		math::vec2 normal{1.f, 0.f};
		math::vec2 point{};

		[[nodiscard]] entity_id other(entity_id self) const noexcept{
			return subject == self ? object : subject;
		}

		[[nodiscard]] const physics_contact_endpoint_snapshot& endpoint_for(entity_id self) const noexcept{
			return subject == self ? subject_endpoint : object_endpoint;
		}

		[[nodiscard]] const physics_contact_endpoint_snapshot& other_endpoint(entity_id self) const noexcept{
			return subject == self ? object_endpoint : subject_endpoint;
		}
	};

	export
	struct physics_query_result{
		entity_id id_{};
		math::frect aabb{};
		math::vec2 position_snapshot{};
		float radius_snapshot{};

		[[nodiscard]] entity_id id() const noexcept{
			return id_;
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
		const auto lhs = std::hash<mo_yanxi::game::ecs::entity_id>{}(key.first());
		const auto rhs = std::hash<mo_yanxi::game::ecs::entity_id>{}(key.second());
		return lhs ^ (rhs + 0x9e3779b97f4a7c15ull + (lhs << 6u) + (lhs >> 2u));
	}
};
