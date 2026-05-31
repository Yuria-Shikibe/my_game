export module mo_yanxi.game.runtime.draw.collision_shape_component;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.component.physics;
export import mo_yanxi.game.runtime.draw.collision_shape_style;

namespace mo_yanxi::game::ecs{
export
struct collision_shape_drawer{
	draw::collision_shape_draw_style style{};
	float screen_clip_margin{8.f};
	bool enabled{true};
};
}
