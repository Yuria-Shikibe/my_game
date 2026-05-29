export module mo_yanxi.game.ecs.world.top;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.system.physics;
export import mo_yanxi.game.world.graphic;

import std;

namespace mo_yanxi::game::world{
	export
	struct entity_top_world{
		ecs::component_manager component_manager{};
		graphic_context graphic_context{};

		ecs::system::physics_system physics_system{};

		[[nodiscard]] entity_top_world() = default;

		[[nodiscard]] explicit(false) entity_top_world(graphic::renderer_world& renderer)
			: graphic_context(renderer){
		}

		void reset(){
			std::destroy_at(&physics_system);
			std::destroy_at(&component_manager);
			std::construct_at(&component_manager);
			std::construct_at(&physics_system);
		}
	};
}
