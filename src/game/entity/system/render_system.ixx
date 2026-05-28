module;


export module mo_yanxi.game.ecs.system.renderer;
export import mo_yanxi.shared_stack;

import mo_yanxi.game.ecs.component.manage;
import mo_yanxi.game.ecs.component.drawer;
import mo_yanxi.game.ecs.component.manifold;
import mo_yanxi.game.world.graphic;
import std;

namespace mo_yanxi::game::ecs::system{
	export
	struct renderer{
	private:
		struct draw_data{
			const drawer::entity_drawer* drawer;
		};
		shared_stack<draw_data> drawers{};
		std::size_t last_draw_size{};
		std::atomic_size_t failed{};

	public:

		void draw(const world::graphic_context& graphic_context) const {
			for (const auto & drawer : drawers.locked_range()){
				drawer.drawer->draw(graphic_context);
				drawer.drawer->post_effect(graphic_context);
			}

		}

		void filter_screen_space(component_manager& manager, math::frect viewport){
			auto cursz = drawers.size();
			if(failed.load(std::memory_order_relaxed) == 0 && (drawers.capacity() - cursz) > 128 && (cursz - last_draw_size < -32)){
				drawers.reset();
			}

			last_draw_size = cursz;
			drawers.clear_and_reserve(std::max<std::size_t>(128, drawers.size() + failed.exchange(0, std::memory_order_relaxed)));

			//TODO parallel
			manager.sliced_each([&](const manifold& manifold, const drawer::entity_drawer& drawer){
				if(drawer.clip.value_or(manifold.hitbox.max_wrap_bound()).expand(drawer.clipspace_margin).overlap_inclusive(viewport)){
					if(auto ptr = drawers.try_push_uninitialized()){
						std::construct_at(ptr, &drawer);
					}else{
						failed.fetch_add(1, std::memory_order_relaxed);
					}
				}
			});

			drawers.release_on_write();
		}
	};
}
