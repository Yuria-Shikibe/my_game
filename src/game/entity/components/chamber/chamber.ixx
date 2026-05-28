module;

#include <cassert>

export module mo_yanxi.game.ecs.component.chamber;

export import :grid_meta;
export import :chamber;
export import :grid;
export import :chamber_meta;

export import :energy_allocation;
export import :grid_maneuver;

namespace mo_yanxi::game::ecs::chamber{

	const meta::chamber::grid_building* building_data::get_meta() const noexcept{
		assert(grid().meta_grid != nullptr);
		auto& g = *this->grid().meta_grid;
		auto index = g.coord_to_index(region_.src);

		return g[index.value()].building;
	}

	void maneuver_subsystem::update(const chamber_manifold& manifold) noexcept{
		force_longitudinal_ = {};
		force_transverse_ = {};
		torque_ = {};
		boost_ = {};
		manifold.manager.sliced_each([this](const maneuver_component& component, const building_data& data){
			append(component, data.region().as<float>().get_center());
		});
	}

}
