module mo_yanxi.game.runtime.draw.chamber_component;

import std;

namespace mo_yanxi::game::ecs{
namespace{
	constexpr int max_chamber_tile_grid_overlay_area = 40 * 40;
	constexpr float chamber_tile_grid_overlay_stroke = 0.18f;
	constexpr float chamber_tile_damage_overlay_stroke = 0.24f;

	enum class chamber_building_visual_kind : std::uint8_t{
		basic,
		armor,
		energy_generator,
		thruster,
		radar,
		turret,
		structural_joint,
		custom
	};

	[[nodiscard]] const chamber::chamber_manifold& require_chamber(const game::game_draw_context& context){
		const auto* chamber_ptr = context.entity.try_get<::mo_yanxi::game::ecs::chamber::chamber_manifold>();
		if(chamber_ptr == nullptr){
			throw std::logic_error{"chamber_drawer requires chamber_manifold"};
		}
		return *chamber_ptr;
	}

	[[nodiscard]] const mech_motion& require_motion(const game::game_draw_context& context){
		const auto* motion_ptr = context.entity.try_get<::mo_yanxi::game::ecs::mech_motion>();
		if(motion_ptr == nullptr){
			throw std::logic_error{"chamber_drawer requires mech_motion"};
		}
		return *motion_ptr;
	}

	[[nodiscard]] chamber_building_visual_kind classify_chamber_building_visual(
		const chamber::chamber_manifold& chamber_data,
		const chamber::building_common& building) noexcept{
		const auto& buildings = chamber_data.buildings();
		if(building.structural
			|| buildings.template try_get<chamber::structural_joint_building>(building.handle) != nullptr){
			return chamber_building_visual_kind::structural_joint;
		}
		if(buildings.template try_get<chamber::armor_building>(building.handle) != nullptr){
			return chamber_building_visual_kind::armor;
		}
		if(buildings.template try_get<chamber::energy_generator_building>(building.handle) != nullptr){
			return chamber_building_visual_kind::energy_generator;
		}
		if(buildings.template try_get<chamber::thruster_building>(building.handle) != nullptr){
			return chamber_building_visual_kind::thruster;
		}
		if(buildings.template try_get<chamber::radar_building>(building.handle) != nullptr){
			return chamber_building_visual_kind::radar;
		}
		if(buildings.template try_get<chamber::turret_building>(building.handle) != nullptr){
			return chamber_building_visual_kind::turret;
		}
		if(buildings.template try_get<chamber::basic_building>(building.handle) != nullptr){
			return chamber_building_visual_kind::basic;
		}
		return chamber_building_visual_kind::custom;
	}

	[[nodiscard]] draw::collision_shape_draw_style make_chamber_hull_draw_style() noexcept{
		return draw::collision_shape_draw_style{
			.color = {0.10f, 0.14f, 0.18f, 0.34f},
			.stroke = 1.5f,
			.depth = 4.25f
		};
	}

	[[nodiscard]] draw::collision_shape_draw_style make_chamber_building_draw_style(
		const chamber::chamber_manifold& chamber_data,
		const chamber::building_common& building) noexcept{
		const float ratio = building.hit_points.max > 0.f
			? std::clamp(building.hit_points.current / building.hit_points.max, 0.f, 1.f)
			: 0.f;
		draw::collision_shape_draw_style style{};
		switch(classify_chamber_building_visual(chamber_data, building)){
		case chamber_building_visual_kind::basic:
			style.color = {0.42f, 0.72f, 1.10f, 0.88f};
			style.stroke = 2.25f;
			style.depth = 6.f;
			break;
		case chamber_building_visual_kind::armor:
			style.color = {0.60f, 0.72f, 0.86f, 0.90f};
			style.stroke = 2.5f;
			style.depth = 6.5f;
			break;
		case chamber_building_visual_kind::energy_generator:
			style.color = {2.25f, 1.38f, 0.32f, 0.94f};
			style.stroke = 2.75f;
			style.depth = 7.f;
			break;
		case chamber_building_visual_kind::thruster:
			style.color = {1.00f, 0.50f, 1.80f, 0.92f};
			style.stroke = 2.5f;
			style.depth = 6.75f;
			break;
		case chamber_building_visual_kind::radar:
			style.color = {0.24f, 1.50f, 2.30f, 0.94f};
			style.stroke = 2.75f;
			style.depth = 7.25f;
			break;
		case chamber_building_visual_kind::turret:
			style.color = {2.20f, 0.62f, 0.34f, 0.95f};
			style.stroke = 3.f;
			style.depth = 7.5f;
			break;
		case chamber_building_visual_kind::structural_joint:
			style.color = {0.18f, 1.22f, 0.78f, 0.95f};
			style.stroke = 3.f;
			style.depth = 7.75f;
			break;
		case chamber_building_visual_kind::custom:
			style.color = {1.80f, 0.30f, 2.10f, 0.92f};
			style.stroke = 2.75f;
			style.depth = 7.25f;
			break;
		}
		const float damage_luma = 0.55f + 0.45f * ratio;
		style.color.r *= damage_luma;
		style.color.g *= damage_luma;
		style.color.b *= damage_luma;
		style.color.a = std::clamp(style.color.a * (0.70f + 0.30f * ratio), 0.55f, 1.f);
		return style;
	}

	[[nodiscard]] draw::collision_shape_draw_style make_chamber_tile_grid_overlay_style() noexcept{
		return draw::collision_shape_draw_style{
			.color = {0.02f, 0.06f, 0.08f, 0.72f},
			.stroke = chamber_tile_grid_overlay_stroke,
			.depth = 8.5f
		};
	}

	[[nodiscard]] draw::collision_shape_draw_style make_chamber_tile_damage_overlay_style(
		const float damage_ratio) noexcept{
		const float ratio = std::clamp(damage_ratio, 0.f, 1.f);
		return draw::collision_shape_draw_style{
			.color = {2.40f, 0.26f + 0.45f * (1.f - ratio), 0.10f, 0.35f + 0.55f * ratio},
			.stroke = chamber_tile_damage_overlay_stroke,
			.depth = 9.25f
		};
	}

	[[nodiscard]] bool should_draw_chamber_tile_grid_overlay(
		const chamber::chamber_manifold& chamber_data,
		const chamber::building_common& building) noexcept{
		const math::point2 chamber_extent = chamber_data.extent();
		return building.region.src.x == 0
			&& building.region.src.y == 0
			&& building.region.extent.x == chamber_extent.x
			&& building.region.extent.y == chamber_extent.y
			&& building.region.area() > 0
			&& building.region.area() <= max_chamber_tile_grid_overlay_area;
	}

	void draw_chamber_overlay_line(
		gui::renderer_frontend& renderer,
		const math::trans2 chamber_transform,
		const math::vec2 local_src,
		const math::vec2 local_dst,
		const draw::collision_shape_draw_style& style){
		draw::push_line(renderer, local_src >> chamber_transform, local_dst >> chamber_transform, style);
	}

	void draw_chamber_tile_grid_overlay(
		gui::renderer_frontend& renderer,
		const chamber::building_common& building,
		const math::trans2 chamber_transform){
		const auto style = make_chamber_tile_grid_overlay_style();
		const float tile_size = chamber::tile_size;
		const float left = static_cast<float>(building.region.src.x) * tile_size;
		const float top = static_cast<float>(building.region.src.y) * tile_size;
		const float right = static_cast<float>(building.region.src.x + building.region.width()) * tile_size;
		const float bottom = static_cast<float>(building.region.src.y + building.region.height()) * tile_size;

		for(int x = 0; x <= building.region.width(); ++x){
			const float local_x = static_cast<float>(building.region.src.x + x) * tile_size;
			draw_chamber_overlay_line(renderer, chamber_transform, {local_x, top}, {local_x, bottom}, style);
		}

		for(int y = 0; y <= building.region.height(); ++y){
			const float local_y = static_cast<float>(building.region.src.y + y) * tile_size;
			draw_chamber_overlay_line(renderer, chamber_transform, {left, local_y}, {right, local_y}, style);
		}
	}

	void draw_chamber_tile_damage_overlay(
		gui::renderer_frontend& renderer,
		const chamber::chamber_manifold& chamber_data,
		const chamber::building_common& building,
		const math::trans2 chamber_transform){
		if(building.region.area() <= 0 || building.hit_points.max <= 0.f || !std::isfinite(building.hit_points.max)){
			return;
		}

		const float tile_size = chamber::tile_size;
		const float tile_hit_points = building.hit_points.max / static_cast<float>(building.region.area()) * 2.f;
		if(tile_hit_points <= 0.f || !std::isfinite(tile_hit_points)){
			return;
		}

		const float inset = tile_size * 0.18f;
		for(int y = building.region.src.y; y != building.region.src.y + building.region.extent.y; ++y){
			for(int x = building.region.src.x; x != building.region.src.x + building.region.extent.x; ++x){
				const chamber::tile_status& status = chamber_data.tile_status_at(building, {x, y});
				if(status.hit_point >= tile_hit_points - 0.001f){
					continue;
				}

				const float damage_ratio = 1.f - std::clamp(status.hit_point / tile_hit_points, 0.f, 1.f);
				const auto style = make_chamber_tile_damage_overlay_style(damage_ratio);
				const float left = static_cast<float>(x) * tile_size + inset;
				const float top = static_cast<float>(y) * tile_size + inset;
				const float right = static_cast<float>(x + 1) * tile_size - inset;
				const float bottom = static_cast<float>(y + 1) * tile_size - inset;

				draw_chamber_overlay_line(renderer, chamber_transform, {left, top}, {right, bottom}, style);
				draw_chamber_overlay_line(renderer, chamber_transform, {left, bottom}, {right, top}, style);
			}
		}
	}

	[[nodiscard]] math::trans2 chamber_transform_from_motion(const mech_motion& motion) noexcept{
		return static_cast<math::trans2>(motion.trans);
	}

	[[nodiscard]] physics::collision_shape_record make_chamber_hull_shape(
		const chamber::chamber_manifold& chamber_data){
		const math::point2 chamber_extent = chamber_data.extent();
		return physics::make_box_collision_shape({
			static_cast<float>(chamber_extent.x) * chamber::tile_size * 0.5f,
			static_cast<float>(chamber_extent.y) * chamber::tile_size * 0.5f
		}).to_record();
	}

	[[nodiscard]] math::trans2 make_chamber_hull_transform(
		const chamber::chamber_manifold& chamber_data,
		const math::trans2 chamber_transform) noexcept{
		const math::point2 chamber_extent = chamber_data.extent();
		return math::trans2{
			{
				static_cast<float>(chamber_extent.x) * chamber::tile_size * 0.5f,
				static_cast<float>(chamber_extent.y) * chamber::tile_size * 0.5f
			},
			0.f
		} >> chamber_transform;
	}
}

game::game_draw_cull_bounds chamber_drawer::cull_bounds(
	const game::game_draw_context& context) const{
	const chamber::chamber_manifold& chamber_data = require_chamber(context);
	const mech_motion& motion = require_motion(context);
	const math::point2 chamber_extent = chamber_data.extent();
	if(!enabled || chamber_extent.x <= 0 || chamber_extent.y <= 0){
		return {.enabled = false};
	}

	const physics::collision_shape_record hull_shape = make_chamber_hull_shape(chamber_data);
	const math::trans2 transform = make_chamber_hull_transform(chamber_data, chamber_transform_from_motion(motion));
	return {
		.world_aabb = hull_shape.aabb(transform),
		.screen_clip_margin = screen_clip_margin,
		.enabled = true
	};
}

void chamber_drawer::draw(
	gui::renderer_frontend& renderer,
	const game::game_draw_context& context) const{
	const chamber::chamber_manifold& chamber_data = require_chamber(context);
	const mech_motion& motion = require_motion(context);
	if(!enabled){
		return;
	}

	const math::trans2 chamber_transform = chamber_transform_from_motion(motion);
	const math::point2 chamber_extent = chamber_data.extent();
	if(chamber_extent.x > 0 && chamber_extent.y > 0){
		const physics::collision_shape_record hull_shape = make_chamber_hull_shape(chamber_data);
		draw::draw_render_shape(
			renderer,
			hull_shape,
			make_chamber_hull_transform(chamber_data, chamber_transform),
			make_chamber_hull_draw_style(),
			game::game_render_shape_surface::fill_outline);
	}

	(void)chamber_data.buildings().for_each_common([&](const chamber::building_common& building){
		if(building.region.area() <= 0 || building.hit_points.is_killed()){
			return;
		}

		const auto half_extent = math::vec2{
			static_cast<float>(building.region.width()) * chamber::tile_size * 0.5f,
			static_cast<float>(building.region.height()) * chamber::tile_size * 0.5f
		};
		if(half_extent.x <= 0.f || half_extent.y <= 0.f){
			return;
		}

		const math::trans2 local_transform{
			{
				(static_cast<float>(building.region.src.x) + static_cast<float>(building.region.width()) * 0.5f)
					* chamber::tile_size,
				(static_cast<float>(building.region.src.y) + static_cast<float>(building.region.height()) * 0.5f)
					* chamber::tile_size
			},
			0.f
		};
		draw::draw_render_shape(
			renderer,
			physics::box_shape{half_extent},
			local_transform >> chamber_transform,
			make_chamber_building_draw_style(chamber_data, building),
			game::game_render_shape_surface::fill_outline);

		if(should_draw_chamber_tile_grid_overlay(chamber_data, building)){
			draw_chamber_tile_grid_overlay(renderer, building, chamber_transform);
			draw_chamber_tile_damage_overlay(renderer, chamber_data, building, chamber_transform);
		}
	});
}
}
