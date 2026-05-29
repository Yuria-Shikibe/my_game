export module mo_yanxi.game.ecs.system.motion_system;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.component.physical_property;
export import mo_yanxi.game.ecs.component.physics;

import std;

namespace mo_yanxi::game::ecs::system{
	export
	struct motion_system{
		void run(component_manager& manager){
			manager.sliced_each([](
				const component_manager& m,
				const chunk_meta& meta,
				mech_motion& motion
			){
				if(meta.id()->try_get<physics_body>()){
					return;
				}

				motion.apply_and_reset(m.get_update_delta());
			});
		}
	};
}
