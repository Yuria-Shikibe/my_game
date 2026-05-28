//
// Created by Matrix on 2026/5/1.
//

export module mo_yanxi.gui.game_examples.loop_exec;

import mo_yanxi.gui.default_config.main_loop;
import mo_yanxi.graphic.trail;
import mo_yanxi.math.rect_ortho;
import mo_yanxi.math.vector2;

namespace mo_yanxi::gui::example{
export
struct main_loop_payload{
	graphic::uniformed_trail trail{60, .75f};

	[[nodiscard]] main_loop_payload(){
		trail.shrink_interval *= 2.f;
	}
};

export using main_loop_type = main_loop<main_loop_payload>;

export
void main_loop_fn(main_loop<main_loop_payload>& main_loop);
}
