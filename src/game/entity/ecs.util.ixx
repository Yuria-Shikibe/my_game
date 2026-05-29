module;

#include "mo_yanxi/adapted_attributes.hpp"

export module mo_yanxi.game.ecs.util;

import mo_yanxi.game.ecs.component.physical_property;
import mo_yanxi.game.ecs.component.physics;
import std;

namespace mo_yanxi::game::ecs{
	export
	[[nodiscard]] FORCE_INLINE double get_rotational_inertia(const physics_body& body, const collider& collider) noexcept{
		if(body.body.rotational_inertia > 0.f && std::isfinite(body.body.rotational_inertia)){
			return body.body.rotational_inertia;
		}

		const auto radius = collider.shape.radius_bound();
		return body.body.mass * radius * radius;
	}
}
