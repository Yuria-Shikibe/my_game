export module mo_yanxi.game.physics.rigid_body;

export import mo_yanxi.math.vector2;
export import mo_yanxi.math.trans2;

import std;

namespace mo_yanxi::game::physics{
	export
	enum struct body_type : std::uint8_t{
		static_body,
		kinematic_body,
		dynamic_body
	};

	export
	enum struct ccd_mode : std::uint8_t{
		disabled,
		linear_sweep
	};

	export
	struct rigid_body{
		body_type type{body_type::dynamic_body};
		ccd_mode ccd{ccd_mode::disabled};

		float mass{1.f};
		float inverse_mass{1.f};
		float rotational_inertia{1.f};
		float inverse_rotational_inertia{1.f};

		float friction{0.35f};
		float restitution{0.1f};
		float linear_drag{0.025f};
		float angular_drag{0.025f};
		float ccd_threshold{1.f};

		math::vec2 force{};
		float torque{};

		[[nodiscard]] static constexpr rigid_body make_static() noexcept{
			return {
				.type = body_type::static_body,
				.mass = std::numeric_limits<float>::infinity(),
				.inverse_mass = 0.f,
				.rotational_inertia = std::numeric_limits<float>::infinity(),
				.inverse_rotational_inertia = 0.f,
			};
		}

		[[nodiscard]] static constexpr rigid_body make_kinematic() noexcept{
			rigid_body body = make_static();
			body.type = body_type::kinematic_body;
			return body;
		}

		[[nodiscard]] static constexpr rigid_body make_dynamic(
			const float mass,
			const float rotational_inertia = -1.f) noexcept{
			rigid_body body{};
			body.type = body_type::dynamic_body;
			body.set_mass(mass);
			body.set_rotational_inertia(rotational_inertia > 0.f ? rotational_inertia : mass);
			return body;
		}

		constexpr void set_mass(const float value) noexcept{
			mass = value;
			inverse_mass = type == body_type::dynamic_body && value > 0.f && std::isfinite(value)
				? 1.f / value
				: 0.f;
		}

		constexpr void set_rotational_inertia(const float value) noexcept{
			rotational_inertia = value;
			inverse_rotational_inertia = type == body_type::dynamic_body && value > 0.f && std::isfinite(value)
				? 1.f / value
				: 0.f;
		}

		[[nodiscard]] constexpr bool is_dynamic() const noexcept{
			return type == body_type::dynamic_body;
		}

		[[nodiscard]] constexpr bool has_infinite_mass() const noexcept{
			return inverse_mass == 0.f;
		}

		constexpr void clear_forces() noexcept{
			force = {};
			torque = 0.f;
		}

		constexpr void add_force(const math::vec2 value) noexcept{
			force += value;
		}

		constexpr void add_torque(const float value) noexcept{
			torque += value;
		}

		constexpr void apply_impulse(math::uniform_trans2& velocity, const math::vec2 impulse,
		                             const math::vec2 contact_offset) const noexcept{
			if(!is_dynamic()){
				return;
			}

			velocity.vec += impulse * inverse_mass;
			velocity.rot += contact_offset.cross(impulse) * inverse_rotational_inertia;
		}
	};
}
