#include <gtest/gtest.h>

import std;
import mo_yanxi.game.ecs.object_storage;

namespace{
	using namespace mo_yanxi::game::ecs;

	struct test_context{
		int turret_updates{};
		int generator_updates{};
		int delivered_energy{};
		int update_delta{3};
	};

	struct building_common{
		object_handle handle{};
		std::uint32_t flags{};
		std::uint32_t tile_status_offset{};
		std::uint32_t tile_status_count{};
	};

	struct turret_building{
		int charge{};
		int received_energy{};
		int received_immovable{};
	};

	struct generator_building{
		int generated{};
	};

	struct sensor_building{
		int updates{};
	};

	struct plain_object{
		int value{};
	};

	struct energy_event{
		int amount{};
	};

	struct unsupported_event{
		int value{};
	};

	struct immovable_event{
		int* destroyed{};
		int amount{};

		immovable_event(int& destroyed_, const int amount_) noexcept
			: destroyed(std::addressof(destroyed_)),
			  amount(amount_){
		}

		immovable_event(const immovable_event&) = delete;
		immovable_event& operator=(const immovable_event&) = delete;
		immovable_event(immovable_event&&) = delete;
		immovable_event& operator=(immovable_event&&) = delete;

		~immovable_event(){
			if(destroyed != nullptr){
				++*destroyed;
			}
		}
	};

	struct turret_system{
		using object_events = object_event_set<energy_event, immovable_event>;

		void update(test_context& context, building_common& common, turret_building& building) const{
			++context.turret_updates;
			++common.flags;
			building.charge += context.update_delta;
		}

		[[nodiscard]] object_event_delivery_result operator()(
			test_context& context,
			building_common&,
			turret_building& building,
			const energy_event& energy) const{
			building.received_energy += energy.amount;
			context.delivered_energy += energy.amount;
			return object_event_delivery_result::delivered;
		}

		[[nodiscard]] object_event_delivery_result operator()(
			test_context&,
			building_common&,
			turret_building& building,
			const immovable_event& event) const{
			building.received_immovable += event.amount;
			return object_event_delivery_result::delivered;
		}
	};

	struct generator_system{
		void update(test_context& context, building_common& common, generator_building& building) const{
			++context.generator_updates;
			building.generated += static_cast<int>(common.tile_status_count);
		}
	};

	struct sensor_system{
		void update(test_context&, building_common&, sensor_building& building) const{
			++building.updates;
		}
	};

	struct plain_system{
		void update(test_context&, object_common&, plain_object& object) const{
			++object.value;
		}
	};

	[[nodiscard]] bool test_default_common_collection(){
		object_collection<test_context> collection{};
		collection.register_channel<plain_object>(plain_system{});

		const object_handle object = collection.emplace<plain_object>(object_common{}, 41);
		test_context context{};
		collection.update_all(context);

		const auto* common = collection.try_common(object);
		const auto* value = collection.try_get<plain_object>(object);
		return common != nullptr
			&& value != nullptr
			&& common->handle == object
			&& value->value == 42;
	}

	[[nodiscard]] bool test_register_channel_accepts_lvalue_system(){
		object_collection<test_context, building_common> collection{};
		sensor_system system{};
		auto& sensors = collection.register_channel<sensor_building>(system);
		const object_handle sensor = collection.emplace<sensor_building>(building_common{}, 0);

		test_context context{};
		collection.update_all(context);

		const auto* sensor_state = collection.try_get<sensor_building>(sensor);
		return sensors.object_type() == mo_yanxi::unstable_type_identity_of<sensor_building>()
			&& sensor_state != nullptr
			&& sensor_state->updates == 1;
	}

	[[nodiscard]] bool test_type_safe_channels(){
		object_collection<test_context, building_common> collection{};
		auto& turrets = collection.register_channel<turret_building>(turret_system{});
		auto& generators = collection.register_channel<generator_building>(generator_system{});

		if(collection.channel_count() != 2 || turrets.channel_index() == generators.channel_index()){
			return false;
		}
		if(turrets.object_type() != mo_yanxi::unstable_type_identity_of<turret_building>()){
			return false;
		}
		if(!collection.contains_channel<turret_building>() || !collection.contains_channel<generator_building>()){
			return false;
		}

		const object_handle turret = collection.emplace<turret_building>(
			building_common{.flags = 10, .tile_status_offset = 4, .tile_status_count = 2},
			7,
			0);
		const object_handle generator = collection.emplace<generator_building>(
			building_common{.flags = 20, .tile_status_offset = 8, .tile_status_count = 5},
			11);

		if(!turret || !generator || turret.channel == generator.channel){
			return false;
		}
		if(collection.try_get<generator_building>(turret) != nullptr){
			return false;
		}
		if(collection.try_get<turret_building>(generator) != nullptr){
			return false;
		}

		const auto* turret_common = collection.try_common(turret);
		const auto* generator_common = collection.try_common(generator);
		if(turret_common == nullptr || generator_common == nullptr){
			return false;
		}
		if(turret_common->handle != turret || generator_common->handle != generator){
			return false;
		}
		if(turret_common->tile_status_offset != 4 || generator_common->tile_status_count != 5){
			return false;
		}

		std::size_t turret_count{};
		int charge_sum{};
		const std::size_t visited = collection.for_each<turret_building>(
			[&](const building_common& common, const turret_building& building){
				++turret_count;
				charge_sum += building.charge + static_cast<int>(common.flags);
			});
		if(visited != 1 || turret_count != 1 || charge_sum != 17){
			return false;
		}

		test_context context{};
		collection.update_all(context);
		const auto* updated_turret = collection.try_get<turret_building>(turret);
		const auto* updated_generator = collection.try_get<generator_building>(generator);
		const auto* updated_common = collection.try_common(turret);
		return updated_turret != nullptr
			&& updated_generator != nullptr
			&& updated_common != nullptr
			&& context.turret_updates == 1
			&& context.generator_updates == 1
			&& updated_turret->charge == 10
			&& updated_common->flags == 11
			&& updated_generator->generated == 16;
	}

	[[nodiscard]] bool test_swap_erase_keeps_moved_handle_valid(){
		object_collection<test_context, building_common> collection{};
		collection.register_channel<turret_building>(turret_system{});

		const object_handle first = collection.emplace<turret_building>(
			building_common{.flags = 1, .tile_status_offset = 0, .tile_status_count = 1},
			10,
			0);
		const object_handle second = collection.emplace<turret_building>(
			building_common{.flags = 2, .tile_status_offset = 2, .tile_status_count = 3},
			20,
			0);

		if(!collection.erase(first)){
			return false;
		}
		if(collection.try_get<turret_building>(first) != nullptr || collection.try_common(first) != nullptr){
			return false;
		}

		auto* moved = collection.try_get<turret_building>(second);
		auto* moved_common = collection.try_common(second);
		if(moved == nullptr || moved_common == nullptr){
			return false;
		}
		if(moved->charge != 20 || moved_common->flags != 2 || moved_common->handle != second){
			return false;
		}

		const object_handle replacement = collection.emplace<turret_building>(
			building_common{.flags = 9, .tile_status_offset = 9, .tile_status_count = 9},
			30,
			0);
		if(replacement.slot != first.slot || replacement.generation == first.generation){
			return false;
		}
		if(collection.try_get<turret_building>(replacement) == nullptr){
			return false;
		}
		return collection.channel<turret_building>().size() == 2;
	}

	[[nodiscard]] bool test_direct_events(){
		object_collection<test_context, building_common> collection{};
		collection.register_channel<turret_building>(turret_system{});
		collection.register_channel<generator_building>(generator_system{});

		const object_handle turret = collection.emplace<turret_building>(
			building_common{.flags = 0, .tile_status_offset = 0, .tile_status_count = 1},
			0,
			0);
		const object_handle generator = collection.emplace<generator_building>(
			building_common{.flags = 0, .tile_status_offset = 0, .tile_status_count = 1},
			0);

		if(collection.post(turret, energy_event{7}) != object_event_post_result::queued){
			return false;
		}
		if(collection.post(generator, energy_event{5}) != object_event_post_result::queued){
			return false;
		}
		if(collection.post(turret, unsupported_event{1}) != object_event_post_result::queued){
			return false;
		}

		const object_handle stale = turret;
		if(!collection.erase(turret)){
			return false;
		}
		if(collection.post(stale, energy_event{9}) != object_event_post_result::invalid_target){
			return false;
		}

		test_context context{};
		const object_event_delivery_report report = collection.deliver_events(context);
		return report.size() == 3
			&& report.count(object_event_delivery_result::delivered) == 0
			&& report.count(object_event_delivery_result::expired_target) == 2
			&& report.count(object_event_delivery_result::unsupported_event) == 1
			&& report.count<energy_event>(object_event_delivery_result::expired_target) == 1
			&& report.count<energy_event>(object_event_delivery_result::unsupported_event) == 1
			&& report.count<unsupported_event>(object_event_delivery_result::expired_target) == 1
			&& context.delivered_energy == 0;
	}

	[[nodiscard]] bool test_event_delivery_success(){
		object_collection<test_context, building_common> collection{};
		collection.register_channel<turret_building>(turret_system{});

		const object_handle turret = collection.emplace<turret_building>(
			building_common{.flags = 0, .tile_status_offset = 0, .tile_status_count = 1},
			0,
			0);
		if(collection.post(turret, energy_event{12}) != object_event_post_result::queued){
			return false;
		}

		test_context context{};
		const object_event_delivery_report report = collection.deliver_events(context);
		const auto* building = collection.try_get<turret_building>(turret);
		return building != nullptr
			&& report.size() == 1
			&& report.count(object_event_delivery_result::delivered) == 1
			&& report.count(object_event_delivery_result::expired_target) == 0
			&& report.count(object_event_delivery_result::unsupported_event) == 0
			&& report.count<energy_event>(object_event_delivery_result::delivered) == 1
			&& building->received_energy == 12
			&& context.delivered_energy == 12;
	}

	[[nodiscard]] bool test_inline_immovable_event_payload(){
		object_collection<test_context, building_common> collection{};
		collection.register_channel<turret_building>(turret_system{});

		const object_handle turret = collection.emplace<turret_building>(
			building_common{.flags = 0, .tile_status_offset = 0, .tile_status_count = 1},
			0,
			0);

		int destroyed{};
		if(collection.emplace_event<immovable_event>(turret, destroyed, 17) != object_event_post_result::queued){
			return false;
		}
		if(destroyed != 0){
			return false;
		}

		test_context context{};
		const object_event_delivery_report report = collection.deliver_events(context);
		const auto* building = collection.try_get<turret_building>(turret);
		return building != nullptr
			&& report.size() == 1
			&& report.count<immovable_event>(object_event_delivery_result::delivered) == 1
			&& building->received_immovable == 17
			&& destroyed == 1;
	}

	[[nodiscard]] bool test_missing_channel_is_explicit_error(){
		object_collection<test_context, building_common> collection{};
		try{
			(void)collection.emplace<sensor_building>(building_common{}, 0);
		}catch(const std::logic_error&){
			return true;
		}
		return false;
	}

	[[nodiscard]] bool test_duplicate_channel_is_explicit_error(){
		object_collection<test_context, building_common> collection{};
		collection.register_channel<turret_building>(turret_system{});
		try{
			collection.register_channel<turret_building>(turret_system{});
		}catch(const std::logic_error&){
			return true;
		}
		return false;
	}
}

TEST(ObjectStorageTest, DefaultCommonCollection){
	EXPECT_TRUE(test_default_common_collection());
}

TEST(ObjectStorageTest, RegisterChannelAcceptsLvalueSystem){
	EXPECT_TRUE(test_register_channel_accepts_lvalue_system());
}

TEST(ObjectStorageTest, TypeSafeChannels){
	EXPECT_TRUE(test_type_safe_channels());
}

TEST(ObjectStorageTest, SwapEraseKeepsMovedHandleValid){
	EXPECT_TRUE(test_swap_erase_keeps_moved_handle_valid());
}

TEST(ObjectStorageTest, DirectEvents){
	EXPECT_TRUE(test_direct_events());
}

TEST(ObjectStorageTest, EventDeliverySuccess){
	EXPECT_TRUE(test_event_delivery_success());
}

TEST(ObjectStorageTest, InlineImmovableEventPayload){
	EXPECT_TRUE(test_inline_immovable_event_payload());
}

TEST(ObjectStorageTest, MissingChannelIsExplicitError){
	EXPECT_TRUE(test_missing_channel_is_explicit_error());
}

TEST(ObjectStorageTest, DuplicateChannelIsExplicitError){
	EXPECT_TRUE(test_duplicate_channel_is_explicit_error());
}
