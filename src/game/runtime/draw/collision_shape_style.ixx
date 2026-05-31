export module mo_yanxi.game.runtime.draw.collision_shape_style;

export import mo_yanxi.graphic.color;

namespace mo_yanxi::game::draw{
export
struct collision_shape_draw_style{
	graphic::color color{0.20f, 0.95f, 0.45f, 0.85f};
	float stroke{1.f};
	float depth{};
};
}
