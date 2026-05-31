#include <cstdlib>

import std;
import mo_yanxi.game.ecs.component.manage;
import mo_yanxi.game.ecs.util;

namespace ecs_entity_test{
	struct position{
		float x{};
		float y{};
	};

	struct velocity{
		float x{};
		float y{};
	};
}

int main(){
	using namespace mo_yanxi::game::ecs;
	using desc = std::tuple<
		chunk_meta,
		ecs_entity_test::position,
		ecs_entity_test::velocity>;

	component_manager manager;
	manager.add_archetype<desc>();
	entity_id created = manager.spawn<desc>(
		ecs_entity_test::position{1.0f, 2.0f},
		ecs_entity_test::velocity{3.0f, 4.0f});
	if(!created.is_staging() || created.try_get<ecs_entity_test::position>() != nullptr){
		return EXIT_FAILURE;
	}
	manager.commit();

	std::size_t visited{};
	manager.each([&](
		const chunk_meta& meta,
		ecs_entity_test::position& pos,
		const ecs_entity_test::velocity& vel){
		if(meta.id() != created){
			return;
		}
		pos.x += vel.x;
		pos.y += vel.y;
		++visited;
	});

	if(visited != 1 || !created.is_inserted()){
		return EXIT_FAILURE;
	}

	bool updated{};
	manager.each([&](const ecs_entity_test::position& pos){
		updated = pos.x == 4.0f && pos.y == 6.0f;
	});

	if(!updated){
		return EXIT_FAILURE;
	}

	bool try_access_called{};
	util::try_access(created, [&](ecs_entity_test::position& pos, const ecs_entity_test::velocity& vel){
		pos.x += vel.x;
		pos.y += vel.y;
		try_access_called = true;
	});
	if(!try_access_called || created.at<ecs_entity_test::position>().x != 7.0f || created.at<ecs_entity_test::position>().y != 10.0f){
		return EXIT_FAILURE;
	}

	bool try_access_pointer_called{};
	entity_ref created_ref{created};
	util::try_access(&created_ref, [&](const ecs_entity_test::position& pos){
		try_access_pointer_called = pos.x == 7.0f && pos.y == 10.0f;
	});
	if(!try_access_pointer_called){
		return EXIT_FAILURE;
	}

	bool try_access_pin_called{};
	entity_pin created_pin{created};
	util::try_access(created_pin, [&](const ecs_entity_test::velocity& vel){
		try_access_pin_called = vel.x == 3.0f && vel.y == 4.0f;
	});
	if(!try_access_pin_called){
		return EXIT_FAILURE;
	}

	struct missing_component{};
	bool missing_component_called{};
	util::try_access(created, [&](missing_component&){
		missing_component_called = true;
	});
	if(missing_component_called){
		return EXIT_FAILURE;
	}

	{
		component_manager filter_manager;
		filter_manager.add_archetype<desc>();
		const entity_id active = filter_manager.spawn<desc>(
			ecs_entity_test::position{1.0f, 0.0f},
			ecs_entity_test::velocity{0.0f, 0.0f});
		const entity_id expired = filter_manager.spawn<desc>(
			ecs_entity_test::position{2.0f, 0.0f},
			ecs_entity_test::velocity{0.0f, 0.0f});
		filter_manager.commit();
		if(!filter_manager.destroy(expired)){
			return EXIT_FAILURE;
		}

		std::size_t filtered_count{};
		bool filtered_saw_expired{};
		filter_manager.each([&](
			const chunk_meta& meta,
			const ecs_entity_test::position&){
			++filtered_count;
			filtered_saw_expired = filtered_saw_expired || meta.id() == expired;
		});

		std::size_t unfiltered_count{};
		bool unfiltered_saw_expired{};
		filter_manager.each_unfiltered([&](
			const chunk_meta& meta,
			const ecs_entity_test::position&){
			++unfiltered_count;
			unfiltered_saw_expired = unfiltered_saw_expired || meta.id() == expired;
		});

		std::size_t no_meta_count{};
		filter_manager.each([&](const ecs_entity_test::position&){
			++no_meta_count;
		});

		std::size_t manager_arg_count{};
		bool manager_arg_ok{true};
		filter_manager.each([&](
			component_manager& self,
			const chunk_meta& meta,
			const ecs_entity_test::position&){
			++manager_arg_count;
			manager_arg_ok = manager_arg_ok && std::addressof(self) == std::addressof(filter_manager) && meta.id() == active;
		});

		std::size_t sliced_count{};
		filter_manager.sliced_each([&](const ecs_entity_test::position&){
			++sliced_count;
		});

		if(filtered_count != 1
			|| filtered_saw_expired
			|| unfiltered_count != 2
			|| !unfiltered_saw_expired
			|| no_meta_count != 1
			|| manager_arg_count != 1
			|| !manager_arg_ok
			|| sliced_count != 1){
			return EXIT_FAILURE;
		}
	}

	const entity_id stale = created;
	if(!manager.destroy(created)){
		return EXIT_FAILURE;
	}
	bool expired_access_called{};
	util::try_access(created, [&](ecs_entity_test::position&){
		expired_access_called = true;
	});
	if(expired_access_called){
		return EXIT_FAILURE;
	}
	if(!created.is_expired() || created.try_get<ecs_entity_test::position>() != nullptr){
		return EXIT_FAILURE;
	}

	manager.commit();
	if(stale.try_get<ecs_entity_test::position>() != nullptr){
		return EXIT_FAILURE;
	}

	const entity_id reused = manager.spawn<desc>(
		ecs_entity_test::position{7.0f, 8.0f},
		ecs_entity_test::velocity{0.0f, 0.0f});
	if(reused.slot() == stale.slot() && reused.generation() == stale.generation()){
		return EXIT_FAILURE;
	}
	manager.commit();
	if(!reused.is_inserted() || reused.at<ecs_entity_test::position>().x != 7.0f){
		return EXIT_FAILURE;
	}
	manager.destroy(reused);
	manager.commit();

	entity_id staged = manager.spawn<desc>(
		ecs_entity_test::position{1.0f, 2.0f},
		ecs_entity_test::velocity{3.0f, 4.0f});
	if(staged.get_state() != entity_state::staging){
		return EXIT_FAILURE;
	}
	manager.destroy(staged);
	if(staged.get_state() != entity_state::expired){
		return EXIT_FAILURE;
	}
	manager.commit();
	if(staged.try_get<ecs_entity_test::position>() != nullptr){
		return EXIT_FAILURE;
	}

	for(int i = 0; i != 16; ++i){
		manager.spawn<desc>(
			ecs_entity_test::position{static_cast<float>(i), 0.0f},
			ecs_entity_test::velocity{0.0f, 0.0f});
	}
	manager.commit();
	manager.destroy_all();
	manager.commit();

	visited = 0;
	manager.each([&](const ecs_entity_test::position&){
		++visited;
	});
	if(visited != 0){
		return EXIT_FAILURE;
	}

	component_manager concurrent_manager;
	static constexpr int thread_count = 4;
	static constexpr int per_thread_count = 32;
	std::vector<std::jthread> threads{};
	threads.reserve(thread_count);
	for(int t = 0; t != thread_count; ++t){
		threads.emplace_back([&, t]{
			entity_command_buffer buffer{};
			for(int i = 0; i != per_thread_count; ++i){
				(void)buffer.spawn<desc>(
					concurrent_manager,
					ecs_entity_test::position{static_cast<float>(t * per_thread_count + i), 0.0f},
					ecs_entity_test::velocity{1.0f, 0.0f});
			}
			concurrent_manager.submit(std::move(buffer));
		});
	}
	threads.clear();
	concurrent_manager.commit();

	visited = 0;
	concurrent_manager.each([&](const ecs_entity_test::position&){
		++visited;
	});
	if(visited != static_cast<std::size_t>(thread_count * per_thread_count)){
		return EXIT_FAILURE;
	}

	entity_id duplicate_destroy = concurrent_manager.spawn<desc>(
		ecs_entity_test::position{},
		ecs_entity_test::velocity{});
	concurrent_manager.commit();
	std::atomic_int destroy_successes{};
	threads.clear();
	for(int t = 0; t != thread_count; ++t){
		threads.emplace_back([&]{
			if(concurrent_manager.destroy(duplicate_destroy)){
				destroy_successes.fetch_add(1, std::memory_order_relaxed);
			}
		});
	}
	threads.clear();
	concurrent_manager.commit();
	if(destroy_successes.load(std::memory_order_relaxed) != 1 || !duplicate_destroy.is_expired()){
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
