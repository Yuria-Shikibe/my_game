export module mo_yanxi.game.ecs.system.motion_system;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.component.physical_property;

import std;

namespace mo_yanxi::game::ecs::system{
	export
	struct motion_system{
		void run(component_manager& manager){
			manager.sliced_each([](
				const component_manager& m,
				const chunk_meta& meta,
				mech_motion& motion
				// component<physical_rigid>& rigid
			){

				motion.apply_and_reset(m.get_update_delta());
				if(auto rigid = meta.id()->try_get<physical_rigid>()){
					motion.apply_drag(m.get_update_delta(), rigid->drag);
				}
			});
		}
	};
}