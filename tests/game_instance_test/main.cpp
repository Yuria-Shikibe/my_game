#include <gtest/gtest.h>

import std;
import mo_yanxi.game.instance;
import mo_yanxi.game.physics;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.renderer.frontend;

namespace{
	using namespace mo_yanxi::game;
	using namespace mo_yanxi::game::ecs;
	namespace math = mo_yanxi::math;
	namespace gui = mo_yanxi::gui;
	namespace instr = mo_yanxi::graphic::draw::instruction;

	using drawable_body_desc = std::tuple<
		chunk_meta,
		mech_motion,
		collider,
		physics_body,
		collision_shape_drawer>;
	using invalid_drawable_desc = std::tuple<chunk_meta, collision_shape_drawer>;

	struct counting_draw_frontend_host{
		mo_yanxi::graphic::draw::data_layout_table<> vertex_table{};
		mo_yanxi::graphic::draw::data_layout_table<> general_table{};
		game_render_frame_state frame{};
		std::size_t line_count{};
		std::size_t closed_polyline_count{};
		std::size_t filled_triangle_count{};
		std::size_t filled_quad_count{};
		std::size_t ring_count{};
		std::size_t rectangle_count{};
		std::size_t non_clip_depth_count{};
		float max_depth{};

		void push(const instr::instruction_head head, const std::byte* payload){
			auto track_depth = [](counting_draw_frontend_host& self, const std::byte* payload){
				if(payload == nullptr){
					return;
				}
				const auto& generic = *std::launder(reinterpret_cast<const instr::primitive_generic*>(payload));
				self.max_depth = std::max(self.max_depth, generic.depth);
				if(generic.depth < 0.f || generic.depth > 1.f){
					++self.non_clip_depth_count;
				}
			};

			switch(head.type){
			case instr::instr_type::line:
				track_depth(*this, payload);
				++line_count;
				break;
			case instr::instr_type::line_segments_closed:
				track_depth(*this, payload);
				++closed_polyline_count;
				break;
			case instr::instr_type::triangle:
				track_depth(*this, payload);
				++filled_triangle_count;
				break;
			case instr::instr_type::quad:
				track_depth(*this, payload);
				++filled_quad_count;
				break;
			case instr::instr_type::poly:
			case instr::instr_type::poly_partial:
				track_depth(*this, payload);
				++ring_count;
				break;
			case instr::instr_type::rectangle:
				track_depth(*this, payload);
				++rectangle_count;
				break;
			default:
				break;
			}
		}

		void push_batch(const std::span<const instr::instruction_head> heads, const std::byte* payload){
			for(const instr::instruction_head head : heads){
				this->push(head, payload);
			}
		}

		void push_state(
			mo_yanxi::graphic::draw::instruction::state_push_config,
			mo_yanxi::graphic::draw::instruction::state_tag,
			std::span<const std::byte>,
			unsigned){
		}

		[[nodiscard]] gui::renderer_frontend create_frontend(){
			return gui::renderer_frontend{
				vertex_table,
				general_table,
				{
					*this,
					[](counting_draw_frontend_host& host, instr::instruction_head head, const std::byte* payload) static{
						host.push(head, payload);
					},
					[](counting_draw_frontend_host& host, std::span<const instr::instruction_head> heads, const std::byte* payload) static{
						host.push_batch(heads, payload);
					},
					[](counting_draw_frontend_host& host, auto config, auto tag, auto payload, auto offset) static{
						host.push_state(config, tag, payload, offset);
					}
				}
			};
		}

		[[nodiscard]] std::size_t primitive_count() const noexcept{
			return line_count
				+ closed_polyline_count
				+ filled_triangle_count
				+ filled_quad_count
				+ ring_count
				+ rectangle_count;
		}
	};

	[[nodiscard]] bool near(const float lhs, const float rhs, const float margin = 0.05f) noexcept{
		return std::abs(lhs - rhs) <= margin;
	}

	[[nodiscard]] constexpr math::vec2 chamber_tile_point(const float x, const float y) noexcept{
		return {
			chamber::tiles_to_world_units(x),
			chamber::tiles_to_world_units(y)
		};
	}

	[[nodiscard]] constexpr math::vec2 chamber_tile_half_extent(const float x, const float y) noexcept{
		return {
			chamber::tiles_to_world_units(x),
			chamber::tiles_to_world_units(y)
		};
	}

	[[nodiscard]] entity_id spawn_drawable_box(
		component_manager& manager,
		const math::vec2 position,
		const math::vec2 half_extent){
		tuple_to_comp_t<drawable_body_desc> components{};
		components.template get<mech_motion>().trans.vec = position;
		components.template get<collider>().shape = physics::make_box_collision_shape(half_extent).to_record();
		components.template get<physics_body>() = physics_body::make_static();
		return manager.spawn<drawable_body_desc>(std::move(components));
	}
}

TEST(GameInstanceTest, DefaultSceneDrawsThroughDrawableComponents){
	game_instance instance{};
	instance.initialize();

	counting_draw_frontend_host renderer{};
	gui::renderer_frontend frontend = renderer.create_frontend();
	const auto stats = instance.render_frame(frontend, math::vec2{1280.f, 720.f});

	EXPECT_TRUE(stats.valid_extent());
	EXPECT_EQ(stats.frame.frame_index, 1u);
	EXPECT_EQ(stats.frame.simulation_tick, 0u);
	EXPECT_DOUBLE_EQ(stats.frame.simulation_time_seconds, 0.0);
	EXPECT_GE(stats.drawable_visited, 6u);
	EXPECT_EQ(stats.drawable_visited, stats.drawable_drawn + stats.drawable_culled);
	EXPECT_GT(stats.drawable_drawn, 0u);
	EXPECT_GT(renderer.primitive_count(), 0u);
	EXPECT_GT(renderer.ring_count, 0u);
	EXPECT_GT(renderer.closed_polyline_count, 0u);
	EXPECT_GT(renderer.filled_quad_count + renderer.filled_triangle_count + renderer.rectangle_count, 0u);
	EXPECT_GE(renderer.line_count, 82u);
	EXPECT_EQ(renderer.non_clip_depth_count, 0u);
	EXPECT_LT(renderer.max_depth, 1.f);
}

TEST(GameInstanceTest, ChamberDrawerOwnsChamberRendering){
	game_instance instance{};
	entity_id chamber_entity{};
	game_command_result spawn_result{};
	instance.post_command(spawn_chamber_command{
		.extent = {4, 2},
		.initial_commands = {
			chamber::place_basic_building_command{
				.region = {.src = {1, 0}, .extent = {2, 1}},
				.hit_points = 40.f,
				.structural = true
			}
		},
		.out_entity = &chamber_entity,
		.out_result = &spawn_result
	});
	instance.update(1.f / 60.f);

	ASSERT_TRUE(spawn_result.applied());
	ASSERT_TRUE(chamber_entity.is_inserted());
	EXPECT_NE(chamber_entity.try_get<chamber::chamber_manifold>(), nullptr);
	EXPECT_NE(chamber_entity.try_get<chamber_drawer>(), nullptr);

	counting_draw_frontend_host renderer{};
	gui::renderer_frontend frontend = renderer.create_frontend();
	const auto stats = instance.render_frame(frontend, math::vec2{1280.f, 720.f});
	EXPECT_GE(stats.drawable_visited, 1u);
	EXPECT_GE(stats.drawable_drawn, 1u);
	EXPECT_GE(renderer.filled_quad_count, 2u);
	EXPECT_GE(renderer.closed_polyline_count, 2u);
	EXPECT_EQ(renderer.non_clip_depth_count, 0u);
	EXPECT_LT(renderer.max_depth, 1.f);
}

TEST(GameInstanceTest, DrawableCullingReportsDrawnAndCulledCounts){
	game_instance instance{};
	instance.reset();
	auto& manager = instance.components();
	const entity_id visible = spawn_drawable_box(manager, {0.f, 0.f}, {8.f, 8.f});
	const entity_id hidden = spawn_drawable_box(manager, {5000.f, 0.f}, {8.f, 8.f});
	manager.commit();
	ASSERT_TRUE(visible.is_inserted());
	ASSERT_TRUE(hidden.is_inserted());

	counting_draw_frontend_host renderer{};
	gui::renderer_frontend frontend = renderer.create_frontend();
	const auto stats = instance.render_frame(frontend, math::vec2{1280.f, 720.f});
	EXPECT_EQ(stats.drawable_visited, 2u);
	EXPECT_EQ(stats.drawable_drawn, 1u);
	EXPECT_EQ(stats.drawable_culled, 1u);
	EXPECT_GT(renderer.primitive_count(), 0u);
}

TEST(GameInstanceTest, DrawableMissingRequiredComponentsFails){
	game_instance instance{};
	instance.reset();
	auto& manager = instance.components();
	tuple_to_comp_t<invalid_drawable_desc> components{};
	(void)manager.spawn<invalid_drawable_desc>(std::move(components));
	manager.commit();

	counting_draw_frontend_host renderer{};
	gui::renderer_frontend frontend = renderer.create_frontend();
	EXPECT_THROW((void)instance.render_frame(frontend, math::vec2{1280.f, 720.f}), std::logic_error);
}

TEST(GameInstanceTest, SpawnCommandsCreateWorldComponents){
	game_instance instance{};
	entity_id dynamic_entity{};
	entity_id static_entity{};
	entity_id projectile_entity{};
	game_command_result dynamic_spawn_result{};
	game_command_result static_spawn_result{};
	game_command_result projectile_spawn_result{};

	const auto shape = physics::make_box_collision_shape({0.5f, 0.5f});
	projectile_state projectile{};
	projectile.faction_id = 7;
	projectile.remaining_lifetime = 5.f;
	projectile.set_damage({.material_damage = {.direct = 12.f}});
	instance.post_command(spawn_physics_body_command{
		.position = {-0.25f, 0.f},
		.shape = shape.to_record(),
		.body = physics_body::make_dynamic(1.f),
		.velocity = {1.f, 0.f},
		.out_entity = &dynamic_entity,
		.out_result = &dynamic_spawn_result
	});
	instance.post_command(spawn_physics_body_command{
		.position = {0.25f, 0.f},
		.shape = shape.to_record(),
		.body = physics_body::make_static(),
		.out_entity = &static_entity,
		.out_result = &static_spawn_result
	});
	instance.post_command(spawn_projectile_command{
		.position = {10.f, 0.f},
		.shape = physics::make_circle_collision_shape(0.25f).to_record(),
		.body = physics_body::make_dynamic(1.f),
		.velocity = {20.f, 0.f},
		.ccd = physics::ccd_mode::linear_sweep,
		.projectile = std::move(projectile),
		.out_entity = &projectile_entity,
		.out_result = &projectile_spawn_result
	});

	instance.update(1.f / 30.f);

	EXPECT_TRUE(dynamic_spawn_result.applied());
	EXPECT_TRUE(static_spawn_result.applied());
	EXPECT_TRUE(projectile_spawn_result.applied());
	EXPECT_TRUE(dynamic_entity.is_inserted());
	EXPECT_TRUE(static_entity.is_inserted());
	EXPECT_TRUE(projectile_entity.is_inserted());
	EXPECT_EQ(dynamic_spawn_result.target, dynamic_entity);
	EXPECT_EQ(static_spawn_result.target, static_entity);
	EXPECT_EQ(projectile_spawn_result.target, projectile_entity);

	const auto* spawned_projectile = projectile_entity.try_get<projectile_state>();
	const auto* projectile_motion = projectile_entity.try_get<mech_motion>();
	const auto* projectile_collider = projectile_entity.try_get<collider>();
	ASSERT_NE(spawned_projectile, nullptr);
	ASSERT_NE(projectile_motion, nullptr);
	ASSERT_NE(projectile_collider, nullptr);
	EXPECT_EQ(spawned_projectile->faction_id, 7u);
	EXPECT_TRUE(near(spawned_projectile->damage.material_damage.direct, 12.f));
	EXPECT_TRUE(near(projectile_motion->vel.vec.x, 20.f));
	EXPECT_EQ(projectile_collider->ccd, physics::ccd_mode::linear_sweep);

	const auto events = instance.world().physics().contact_events();
	EXPECT_TRUE(std::ranges::any_of(events, [&](const physics_contact_event& event){
		return event.key == physics_contact_key::ordered(dynamic_entity, static_entity);
	}));

	instance.reset();
	std::size_t visited{};
	instance.components().each([&](const mech_motion&){
		++visited;
	});
	EXPECT_EQ(visited, 0u);
	EXPECT_TRUE(instance.world().physics().contact_events().empty());
}

TEST(GameInstanceTest, ChamberTargetingAndProjectileCommands){
	game_instance instance{};
	entity_id chamber_entity{};
	entity_id target_entity{};
	game_command_result chamber_spawn_result{};
	game_command_result target_spawn_result{};
	const math::trans2 chamber_transform{chamber_tile_point(10.f, 20.f), math::pi_half};

	instance.post_command(spawn_chamber_command{
		.extent = {4, 1},
		.position = chamber_transform.vec,
		.rotation = static_cast<float>(chamber_transform.rot),
		.faction_id = 7u,
		.initial_commands = {
			chamber::place_standard_building_command{
				.type = chamber::standard_building_type::structural_joint,
				.region = {.src = {0, 0}, .extent = {1, 1}},
				.hit_points = 100.f,
				.structural_support_radius = 3
			},
			chamber::place_standard_building_command{
				.type = chamber::standard_building_type::radar,
				.region = {.src = {1, 0}, .extent = {1, 1}},
				.hit_points = 50.f,
				.radar = {.sensor = {.range = chamber::tiles_to_world_units(5.f)}}
			},
			chamber::place_standard_building_command{
				.type = chamber::standard_building_type::turret,
				.region = {.src = {2, 0}, .extent = {1, 1}},
				.hit_points = 50.f,
				.turret = {.projectile_speed = 20.f}
			}
		},
		.out_entity = &chamber_entity,
		.out_result = &chamber_spawn_result
	});
	instance.post_command(spawn_physics_body_command{
		.position = game_world::chamber_to_world_point(chamber_tile_point(3.8f, 0.5f), chamber_transform),
		.shape = physics::make_box_collision_shape(chamber_tile_half_extent(0.5f, 0.5f)).to_record(),
		.body = physics_body::make_static(),
		.out_entity = &target_entity,
		.out_result = &target_spawn_result
	});
	instance.update(1.f / 60.f);
	ASSERT_TRUE(chamber_spawn_result.applied());
	ASSERT_TRUE(target_spawn_result.applied());
	EXPECT_NE(chamber_entity.try_get<chamber_drawer>(), nullptr);

	const auto* chamber_component = chamber_entity.try_get<chamber::chamber_manifold>();
	ASSERT_NE(chamber_component, nullptr);
	ASSERT_EQ(chamber_component->last_fire_requests().size(), 1u);
	EXPECT_EQ(chamber_component->last_fire_requests().front().target.entity, target_entity);

	std::vector<entity_id> projectile_entities{};
	game_command_result projectile_spawn_result{};
	instance.post_command(spawn_chamber_projectiles_command{
		.target = chamber_entity,
		.shape = physics::make_circle_collision_shape(chamber::tiles_to_world_units(0.25f)).to_record(),
		.body = physics_body::make_dynamic(1.f),
		.lifetime = 5.f,
		.ccd = physics::ccd_mode::linear_sweep,
		.out_entities = &projectile_entities,
		.out_result = &projectile_spawn_result
	});
	instance.update(1.f / 60.f);
	ASSERT_TRUE(projectile_spawn_result.applied());
	ASSERT_EQ(projectile_entities.size(), 1u);
	EXPECT_EQ(chamber_component->last_fire_requests().size(), 1u);

	const entity_id projectile_entity = projectile_entities.front();
	const auto* spawned_projectile = projectile_entity.try_get<projectile_state>();
	const auto* spawned_motion = projectile_entity.try_get<mech_motion>();
	ASSERT_NE(spawned_projectile, nullptr);
	ASSERT_NE(spawned_motion, nullptr);
	EXPECT_EQ(spawned_projectile->owner_id(), chamber_entity);
	EXPECT_EQ(spawned_projectile->faction_id, 7u);
	EXPECT_TRUE(near(spawned_projectile->damage.material_damage.direct, 10.f));
	EXPECT_TRUE(near(spawned_motion->vel.vec.x, 0.f));
	EXPECT_TRUE(near(spawned_motion->vel.vec.y, 20.f));
}

TEST(GameInstanceTest, ProjectileDamagesChamberThroughPhysicsContact){
	game_world world{};
	entity_id chamber_entity{};
	entity_id projectile_entity{};
	const auto chamber_result = world.execute(spawn_chamber_command{
		.extent = {4, 1},
		.faction_id = 3u,
		.initial_commands = {
			chamber::place_basic_building_command{
				.region = {.src = {1, 0}, .extent = {2, 1}},
				.hit_points = 40.f,
				.structural = true
			}
		},
		.out_entity = &chamber_entity
	});
	projectile_state projectile{};
	projectile.faction_id = 9u;
	projectile.remaining_lifetime = 5.f;
	projectile.set_damage({.material_damage = {.direct = 30.f}});
	const auto projectile_result = world.execute(spawn_projectile_command{
		.position = chamber_tile_point(-1.f, 0.5f),
		.shape = physics::make_circle_collision_shape(chamber::tiles_to_world_units(0.25f)).to_record(),
		.body = physics_body::make_kinematic(),
		.velocity = {chamber::tiles_to_world_units(6.f), 0.f},
		.sensor = true,
		.ccd = physics::ccd_mode::linear_sweep,
		.projectile = std::move(projectile),
		.out_entity = &projectile_entity
	});
	world.components().commit();
	world.step(1.f);

	const auto* chamber_component = chamber_entity.try_get<chamber::chamber_manifold>();
	const auto* damaged_building = chamber_component != nullptr
		? chamber_component->building_at({1, 0})
		: nullptr;
	ASSERT_TRUE(chamber_result.applied());
	ASSERT_TRUE(projectile_result.applied());
	ASSERT_NE(chamber_component, nullptr);
	ASSERT_NE(damaged_building, nullptr);
	EXPECT_TRUE(near(damaged_building->hit_points.current, 10.f, 0.001f));
	EXPECT_TRUE(projectile_entity.is_expired());
	EXPECT_TRUE(std::ranges::any_of(world.physics().contact_events(), [&](const physics_contact_event& event){
		return event.phase == physics_contact_phase::begin
			&& event.key == physics_contact_key::ordered(chamber_entity, projectile_entity);
	}));
}
