module;

#include "mo_yanxi/adapted_attributes.hpp"

export module mo_yanxi.game.ecs.util;

import mo_yanxi.game.ecs.component.manifold;
import mo_yanxi.game.ecs.component.physical_property;

namespace mo_yanxi::game::ecs{
	export
	[[nodiscard]] CONST_FN FORCE_INLINE double get_rotational_inertia(const physical_rigid& rigid, const manifold& manifold) noexcept{
		return rigid.rotational_inertia >= 0 ? rigid.rotational_inertia : manifold.hitbox.get_rotational_inertia(rigid.inertial_mass);
	}
}