module mo_yanxi.gui.game_examples.loop_exec;

import mo_yanxi.game.instance;
import mo_yanxi.gui.examples.default_config.constants;
import mo_yanxi.gui.global;
import std;

import mo_yanxi.gui.fx.instruction_extension;

void mo_yanxi::gui::example::main_loop_fn(struct main_loop<main_loop_payload>& main_loop){
	auto& current_focus = main_loop.get_scene();
	auto deltatime = global::consume_current_input(current_focus, [&](input_handle::input_event_variant e){
		main_loop.payload.game->handle_event(e);
		main_loop.unhandled_events.push(e);
	});

	main_loop.payload.game->update(deltatime.count());

	current_focus.layout();

	auto& renderer = main_loop.get_renderer();
	auto& r = current_focus.renderer();

	renderer.batch_host.begin_rendering();
	renderer.batch_host.get_data_group_non_vertex_info().push_default(fx::ui_state(
		r.get_region().extent(),
		static_cast<float>(current_focus.get_current_time() / 60.f)
	));
	renderer.batch_host.get_data_group_non_vertex_info().push_default(fx::slide_line_config{});

	r.init_timeline_variable();

	r.update_state(r.get_full_screen_scissor());
	r.update_state(r.get_full_screen_viewport());
	r.update_state(fx::pipeline_config{.pipeline_index = gpip::idx::def});
	r.update_state(fx::push_constant{gpip::default_draw_constants{}});

	r.update_state(fx::blend::pma::standard);
	r.update_state(fx::make_blend_write_mask(true), 0);

	mo_yanxi::game::game_render_context render_context{
		renderer,
		r,
		deltatime.count()
	};
	main_loop.payload.game->render(render_context);

	current_focus.draw();
	renderer.batch_host.end_rendering();
	renderer.upload();
	renderer.create_command();
}
