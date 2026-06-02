//
// Created by Matrix on 2025/11/19.
//

module mo_yanxi.gui.game_examples;

import mo_yanxi.gui.elem.button;


import std;

import mo_yanxi.binary_trace;

import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.elem.group;
import mo_yanxi.gui.global;

import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;

import mo_yanxi.gui.elem.scaling_stack;
import mo_yanxi.gui.elem.sequence;
import mo_yanxi.gui.elem.overflow_sequence;
import mo_yanxi.gui.elem.scroll_pane;
import mo_yanxi.gui.elem.collapser;
import mo_yanxi.gui.elem.table;
import mo_yanxi.gui.elem.grid;
import mo_yanxi.gui.elem.menu;
import mo_yanxi.gui.elem.slider;
import mo_yanxi.gui.elem.progress_bar;

import mo_yanxi.gui.elem.image_frame;
import mo_yanxi.gui.elem.image_frame;
import mo_yanxi.gui.elem.drag_split;
import mo_yanxi.gui.elem.label;
import mo_yanxi.gui.elem.text_edit;
import mo_yanxi.gui.elem.viewport;
import mo_yanxi.gui.elem.check_box;
import mo_yanxi.gui.elem.flipper;

import mo_yanxi.gui.infrastructure;
import mo_yanxi.font;

import mo_yanxi.typesetting.util;
import mo_yanxi.font;
import mo_yanxi.font.manager;
import mo_yanxi.graphic.color;
import mo_yanxi.graphic.image_atlas;
import mo_yanxi.graphic.msdf;
import align;

import mo_yanxi.typesetting;
import mo_yanxi.graphic.draw.instruction.recorder;


import mo_yanxi.gui.compound.color_picker;
import mo_yanxi.gui.compound.named_slider;
import mo_yanxi.gui.compound.file_selector;
import mo_yanxi.gui.compound.data_table;
import mo_yanxi.gui.compound.click_collapser;
import mo_yanxi.gui.compound.numeric_input_area;

import mo_yanxi.gui.default_config.round_styles;
import mo_yanxi.gui.style.progress_bars;
import mo_yanxi.gui.style.palette;

import mo_yanxi.gui.assets.manager;
import mo_yanxi.game.ui.collision_shape_editor;

import mo_yanxi.backend.communicator;
import mo_yanxi.backend.vulkan.context;

import mo_yanxi.graphic.trail;
import mo_yanxi.math.rand;

import mo_yanxi.gui.examples.default_config.constants;
import mo_yanxi.gui.default_config.scene;


namespace mo_yanxi::gui::example{
using namespace gui;

struct test_entry{
	std::string name;
	std::function<elem_ptr(scene&, elem*)> creator;

	[[nodiscard]] test_entry(const std::string& name, const std::function<elem_ptr(scene&, elem*)>& creator)
		: name(name),
		  creator(creator){
	}

	template <invocable_elem_init_func Fn>
	[[nodiscard]] test_entry(const std::string& name, Fn&& fn)
		: name(name),
		  creator([f = std::forward<Fn>(fn)](scene& s, elem* p){
			  return elem_ptr{s, p, f};
		  }){
	}
};

#pragma region ExampleUIStructs
struct csv_file_reader : head_body{
	struct file_listener : react_flow::terminal<std::span<const std::filesystem::path>>{
		gui::elem* overlay{};
		csv_file_reader* carrier;

		[[nodiscard]] explicit file_listener(csv_file_reader* carrier)
			: carrier(carrier){
		}

	protected:
		void on_update(react_flow::data_carrier<std::span<const std::filesystem::path>>& data) override{
			auto sp = data.get();
			if(sp.empty()) return;
			auto& path = sp.front();

			carrier->get_scene().close_overlay(std::exchange(overlay, nullptr));

			util::post_elem_async_task(*carrier, [&](csv_file_reader& r){
				return elem_async_yield_task{
						r, [&](csv_file_reader& r, scene& s){
							return elem_ptr{
									s, &r, [p = path](cpd::data_table& table){
										table.set_style();
										table.get_item() = cpd::data_table_desc::from_csv(p, '|');
										table.get_item().try_update_glyph_layouts();
										table.notify_isolated_layout_changed();
									}
								};
						},
						[](csv_file_reader& r, scene& s, elem_ptr&& ptr){
							util::sync_elem_tree(*ptr, r.get_scene());
							r.set_body_elem(std::move(ptr));
						}
					};
			});

			carrier->create_body([&](progress_bar& prog){
				prog.set_style();
				prog.progress.set_state(progress_state::approach_smooth);
				prog.progress.set_speed(.0001f);
				prog.draw_config.color = {graphic::colors::white, graphic::colors::white};

				prog.set_self_border(gui::border{}.set(32));
				prog.set_style(style::make_ring_progress_style(32));
				prog.set_progress_state(progress_state::rough);
			});
		}
	};

	react_flow::node_holder_pinned<file_listener> path_node_{this};

	[[nodiscard]] csv_file_reader(scene& scene, elem* parent)
		: head_body(scene, parent){
		set_style();
		set_expand_policy(layout::expand_policy::passive);

		create_head([this](button<direct_label>& b){
			b.set_tokenized_text({"Select File"});
			b.set_button_callback([this](direct_label& e){
				auto& p = e.parent_ref<csv_file_reader>();
				auto selector_overlay = e.get_scene().create_overlay(
					{
						.extent = {
							{layout::size_category::passive, .95f},
							{layout::size_category::passive, .95f}
						},
						.align = align::pos::center,
					}, [this](cpd::file_selector& e){
						e.set_cared_suffix({".csv"});
						e.get_prov().connect_successor(this->path_node_.node);
					});
				this->path_node_.node.overlay = std::addressof(selector_overlay.elem());
			});
		});
		create_body([](cpd::data_table& data_table){
		});

		set_head_size(80);
		set_pad(8);
	}
};

#pragma endregion

ui_outputs build_main_ui(backend::vulkan::context& ctx, renderer_frontend renderer, graphic::image_atlas& image_atlas){
	game::ui::configure_collision_shape_editor_reference_images(image_atlas);

	auto& ui_root = global::manager;
	auto& res = ui_root.add_scene_resources("main");
	auto style_pal_prov = gui::example::make_styles(res);

	const auto scene_add_rst = ui_root.add_scene<gui::example::example_scene, loose_group>("main", res, true, std::move(renderer));

	// scene_add_rst.scene.resize(math::rect_ortho{tags::from_extent, {}, ctx.get_extent().width, ctx.get_extent().height}.as<float>());
	auto& scene = scene_add_rst.scene;
	auto& root = scene_add_rst.root_group;
	style_pal_prov.add_to_scene(scene);

	scene.enable_elem_async_task_post(true);
	scene.drop_and_reset_communicate_async_task_queue_size(1);

	gui::example::set_cursors(scene);

	scene.pass_config = {
			{
				fx::scene_render_pass_config::value_type{
					.begin_config = {
						.draw_targets = 0b1,
					},
					.end_config = std::nullopt
				},
				{
					.begin_config = {
						.draw_targets = 0b10,
					},
					.end_config = std::nullopt
				}
			},
			fx::blit_pipeline_config{}
		};

	scene.resources().set_native_communicator<backend::glfw::communicator>(ctx.window().get_handle());
	scene.get_communicator()->set_native_cursor_visibility(false);

	auto e = scene.create<scaling_stack>();
	// e->set_scaling({.5f, .5f});
	e->set_fill_parent({true, true});
	auto& mroot = static_cast<scaling_stack&>(root.insert(0, std::move(e)));

	ui_outputs result{&scene};


	auto make_create_table = [&] -> std::vector<test_entry>{
		std::vector<test_entry> tests{
				test_entry{
					"collision shapes", [](mo_yanxi::game::ui::collision_shape_editor& editor){
					}
				},
				test_entry{
					"layout test", [](scroll_adaptor<table>& pane){
						pane.set_style();
						pane.set_overlay_bar(true);
						auto& t = pane.get_elem();
						t.set_expand_policy(layout::expand_policy::prefer);
						t.set_style();
						t.template_cell.set_pad(4);

						t.create_back([](gui::elem& e){
						}).cell().set_size({60, 60});

						t.create_back([](gui::sequence& e){
							e.set_expand_policy(layout::expand_policy::passive);
							e.template_cell.set_size({layout::size_category::scaling});
							e.template_cell.set_pad({4, 4});
							e.emplace_back<elem>();
							e.emplace_back<elem>();
							e.emplace_back<elem>();
							e.emplace_back<elem>();
						}, layout::layout_policy::vert_major).cell().set_width(400);
						t.end_line();

						t.emplace_back<elem>().cell().set_height(120);
						t.emplace_back<elem>().cell().set_width(200).unsaturate_cell_align = align::pos::right;
						t.end_line();

						{
							auto sep = t.emplace_back<row_separator>();
							sep.cell().set_height(20).set_width_passive(.85f).saturate = true;
							sep.cell().margin.set_vert(4);
							sep.cell().set_end_line();
						}

						t.emplace_back<elem>();
						t.create_back([](gui::label& e){
							e.set_fit_type(label_fit_type::fix);
							e.set_text("Test Test Test Test Test Test Test Test Test Test ");
						}).cell().set_pending({false, true});
					}
				},
				test_entry{
					"csv", [](cpd::data_table& table){
						table._debug_identity = 114;
						table.get_item() = cpd::data_table_desc::from_csv(LR"(assets/test.csv)");
						table.notify_isolated_layout_changed();
					}
				},
				test_entry{
					"text input", [&](scroll_pane& pane){
						pane.create([&](sequence& sequence){
							sequence.template_cell.set_pad({4, 4});

							auto slider = sequence.emplace_back<slider1d_with_output>();
						slider->set_smooth_scroll(true);
						slider->set_smooth_jump(false);
						slider->set_smooth_drag(true);
						slider->bar_handle_extent = {40};
						slider->set_drawer(style::spec::make_round_slider_style({
							.handle_shape = assets::builtin::default_round_square_base,
							.bar_shape = assets::builtin::default_round_square_base,
							.handle_palette = style::pal::white,
							.bar_palette = style::pal::pastel_gray.copy().mul_rgb(.7f),
							.bar_back_palette = style::pal::pastel_gray.copy().mul_rgb(.2f),
							.bar_margin = 4.f,
							.vert_margin = 5.f,
						}));
						slider.cell().set_size(60);

							auto& progNode = slider->get_provider();

							sequence.create_back([&](progress_bar& prog){
								prog.progress.set_state(progress_state::approach_smooth);
								prog.progress.set_speed(.0001f);
								auto& t = prog.request_receiver();
								react_flow::connect_chain(progNode, t);
							}).cell().set_size(60);

							sequence.create_back([&](cpd::numeric_input_area<int>& area){
							}).cell().set_size(80);

							{
								auto label = sequence.create_back([&](direct_label& l){
								});
								label.cell().set_pending();

								auto& ln = label->request_react_node<direct_label_text_prov>();
								auto& trans = label->request_embedded_react_node(react_flow::make_transformer(
									[](std::u32string_view sv){
										return typesetting::tokenized_text{sv};
									}));

								sequence.create_back([&](text_edit_prov& area){
									area.set_on_changed_interval(30.f);
									react_flow::connect_chain(area.get_provider(), trans, ln);
								}).cell().set_pending();
							}
						});
					}
				},
				test_entry{
					"file reader", [](csv_file_reader& table){
					}
				},
				test_entry{
					"sliders", [&](scroll_adaptor<sequence>& pane){
						sequence& s = pane.get_elem();
						pane.set_style();

						// util::post_elem_async_task(s, [](gui::sequence& seq){
						// 	return elem_async_yield_task{
						// 			seq,
						// 			[](elem& e){
						// 				log::debug({"Task"}, "begin: current thread: {}",
						// 				             std::this_thread::get_id());
						// 				std::this_thread::sleep_for(std::chrono::milliseconds(500));
						// 				log::debug({"Task"}, "end: current thread: {}",
						// 				             std::this_thread::get_id());
						// 				return 114;
						// 			},
						// 			[](elem& e, int val){
						// 				log::debug({"Task"}, "done: current thread: {} - {}",
						// 				             std::this_thread::get_id(), val);
						// 			}
						// 		};
						// });

						s.set_expand_policy(layout::expand_policy::prefer);
						s.template_cell.set_pending();
						s.template_cell.pad = {16, 4};
						s.set_has_smooth_pos_animation(false);
						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "Bloom Sample Scale", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.25f);

							auto& trans = hdl->add_relay_func([](float val){
								return math::lerp(0.25f, 4.f, val);
							});
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.shader_bloom_scale = &trans;
						}

						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "BloomSrcFactor", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.5f);

							auto& trans = hdl->add_relay(react_flow::make_transformer([](float val){
								return math::lerp(0.f, 2.f, val);
							}));
							auto& formatter = hdl->request_embedded_react_node(react_flow::make_transformer(
								[](float val){
									return std::format("{:.2f}", val);
								}));
							react_flow::connect_chain(trans, formatter, hdl->get_display_text_receiver());

							result.shader_bloom_src_factor = &trans;
						}

						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "BloomDstFactor", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.5f);

							auto& trans = hdl->add_relay_func([](float val){
								return math::lerp(0.f, 2.f, val);
							});
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.shader_bloom_dst_factor = &trans;
						}

						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "BloomMixFactor", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.5f);
							result.shader_bloom_mix_factor = &hdl.elem().get_slider_provider();
						}

						{
							s.emplace_back<row_separator>().cell().set_size(8);
						}

						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "HighlightThres", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.25f);
							auto& trans = hdl->add_relay_func([](float val){
								return math::lerp(0.5f, 2.5f, val);
							});
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.highlight_filter_threshold = &trans;
						}

						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "HighlightSmooth", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.5f);
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.highlight_filter_smooth = &hdl.elem().get_slider_provider();
						}

						{
							s.emplace_back<row_separator>().cell().set_size(8);
						}

						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "Contrast", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(1.f);
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.tonemap_contrast = &hdl->get_slider_provider();
						}
						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "Exposure", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.5f);
							auto& trans = hdl->add_relay_func([](float val){
								return math::lerp(0.f, 2.f, val);
							});
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.tonemap_exposure = &hdl->get_slider_provider();
						}
						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "Saturation", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(1.f);
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.tonemap_saturation = &hdl->get_slider_provider();
						}
						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "Gamma", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(math::map(1.2f, 0.5f, 3.f, 0.f, 1.f));
							auto& trans = hdl->add_relay_func([](float val){
								return math::lerp(0.5f, 3.f, val);
							});
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.tonemap_gamma = &trans;
						}
					}
				},
				test_entry{
					"collapsers", [&](scroll_adaptor<sequence>& pane){
						pane.set_layout_spec(layout::layout_specifier::fixed(layout::layout_policy::vert_major));

						sequence& s = pane.get_elem();
						s.set_layout_spec(
							layout::directional_layout_specifier::fixed(layout::layout_policy::vert_major));

						s.set_style();
						s.set_expand_policy(layout::expand_policy::prefer);
						s.template_cell.set_pending();
						s.template_cell.set_pad({6.f});

						for(int i = 0; i < 14; ++i){
							s.create_back([&](collapser& c){
								c.set_update_opacity_during_expand(true);
								c.set_expand_cond(collapser_expand_cond::inbound);

								c.emplace_head<elem>().set_style();
								c.set_head_size(50);
								c.create_body([](table& e){
									e.set_style(style::family_variant::base_only);
									e.interactivity = interactivity_flag::enabled;
									e.template_cell.pad.set_vert(4);
									e.set_tooltip_state(
										{
											.layout_info = tooltip::align_meta{
												.follow = tooltip::anchor_type::owner,
												.attach_point_spawner = align::pos::top_left,
												.attach_point_tooltip = align::pos::top_right,
											},
										}, [](table& tooltip){
											using namespace gui;
											struct dialog_creator : elem{
												[[nodiscard]] dialog_creator(
													gui::scene& scene, elem* parent)
													: elem(scene, parent){
													interactivity = interactivity_flag::enabled;
												}

												events::op_afterwards on_click(
													const events::click event,
													std::span<elem* const> aboves) override{
													if(event.key.on_release()){
														get_scene().create_overlay({
																.extent = {
																	{
																		layout::size_category::passive,
																		.4f
																	},
																	{
																		layout::size_category::scaling,
																		1.f
																	}
																},
																.align = align::pos::center,
															}, [](table& e){
																e.end_line().emplace_back<elem>();
																e.end_line().emplace_back<elem>();
																e.end_line().emplace_back<elem>();
																e.end_line().emplace_back<elem>();
																e.end_line().emplace_back<elem>();
															});
													}
													return events::op_afterwards::intercepted;
												}
											};
											tooltip.emplace_back<dialog_creator>().cell().set_size({
													160, 60
												});
										});
									e.set_entire_align(align::pos::top_left);
									for(int k = 0; k < 5; ++k){
										e.emplace_back<elem>().cell().set_size({250, 60});
									}
								});

								c.set_head_body_transpose(i & 1);
							});
						}
					}
				},
				test_entry{
					"menu", [](menu& menu){
						menu.set_expand_policy(layout::expand_policy::passive);
						menu.set_head_size(90);
						menu.set_style();
						menu.get_head_template_cell().set_pending().set_pad({4, 4});

						for(int i = 0; i < 4; ++i){
							auto hdl = menu.create_back(
								[&](label& e){
									e.set_fit_type(label_fit_type::scl);
									e.set_style(style::family_variant::base_only);
									e.set_text(std::format("chunk by {}", i));
								}, [&](sequence& e){
									e.set_has_smooth_pos_animation(true);
									e.set_expand_policy(layout::expand_policy::passive);
									e.template_cell.set_pad({4, 4});
									for(int j = 0; j < i + 1; ++j){
										e.emplace_back<elem>();
									}
								});
						}
					}
				},
				test_entry{
					"table/check box", [](scroll_pane& pane){
						pane.create([](table& table){
							table.set_expand_policy(layout::expand_policy::prefer);
							table.set_entire_align(align::pos::center);
							table.template_cell.pad.set(4);

							style::family_variant family_variants[]{
									style::family_variant::general,
									style::family_variant::general_static,
									style::family_variant::solid,
									style::family_variant::base_only,
									style::family_variant::edge_only,
									style::family_variant::accent,
									style::family_variant::accepted,
									style::family_variant::warning,
									style::family_variant::invalid,
								};

							for(auto family_variant : family_variants){
								auto check_box = table.emplace_back<gui::check_box>(std::in_place);
								check_box->icons[1].components.color = {graphic::colors::pale_green};
								check_box.cell().set_size({60, 60});
								check_box.cell().unsaturate_cell_align = align::pos::none;

								auto receiver = table.emplace_back<label>();
								receiver->set_fit();
								receiver->set_style(family_variant);
								receiver->interactivity = interactivity_flag::enabled;

								auto& listener = receiver->request_embedded_react_node(react_flow::make_listener(
									[&e = receiver.elem()](bool i){
										e.set_toggled(i);
										if(i){
											e.set_text("Toggled");
										} else{
											e.set_text("");
										}
									}));
								listener.connect_predecessor(check_box->get_prov());

								receiver.cell().set_end_line();
							}

							{
								auto sep = table.emplace_back<row_separator>();
								sep.cell().set_height(20).set_width_passive(.85f).saturate = true;
								sep.cell().margin.set_vert(4);
								sep.cell().set_end_line();
							}

							{
								auto sep = table.create_back([](overflow_sequence& seq){
									seq.set_layout_spec(
										layout::directional_layout_specifier::fixed(layout::layout_policy::vert_major));
									seq.template_cell.set_size(120).set_pad({2, 2});
									auto [_, cell] = seq.create_overflow_elem([](icon_frame& i){
										i.set_style(style::family_variant::base_only);
										i.interactivity = interactivity_flag::enabled;
									}, assets::builtin::shape_id::more);
									cell.set_size({layout::size_category::scaling});

									for(unsigned i = 0; i < 12; ++i){
										seq.create_back(
											[](cpd::click_collapser& c){
												c.set_style();
												c.head().set_pad(8);
												c.set_pad(8);
												c.set_body_size(200);
												c.set_head_size({layout::size_category::scaling});
											},
											layout::layout_policy::vert_major,
											element_create_pacakge{
												[](elem& l){
													l.set_style();
												}
											},
											element_create_pacakge{
												[i](label& l){
													l.set_style();
													l.set_fit_type(label_fit_type::fix);
													l.set_text(std::format("{}", i));
												}
											}).cell().set_pending();
									}
									seq.set_split_index(2);
								});
								sep.cell().set_height(80).saturate = true;
								sep.cell().margin.set_vert(4);
								sep.cell().set_end_line();
							}

							table.emplace_back<elem>().cell().set_width(120).set_pending({false, true});
							table.create_back([](cpd::click_collapser& c){
								                  c.head().set_pad(8);
								                  c.set_pad(8);
								                  c.set_body_size({layout::size_category::pending});
								                  c.set_head_size(80);
							                  },
							                  layout::layout_policy::hori_major,
							                  element_create_pacakge{
								                  [](label& l){
									                  l.set_style();
									                  l.text_entire_align = align::pos::center;
									                  l.set_text("Collapser");
								                  }
							                  },
							                  element_create_pacakge{
								                  [](label& l){
									                  l.set_style();
									                  l.set_text("Collapser Showcase");
								                  }
							                  })
							     .cell().set_pending({false, true}).set_width_passive();

							table.end_line();

							for(int i = 0; i < 4; ++i){
								table.emplace_back<elem>().cell().set_size({120, 120});
								table.emplace_back<elem>();
								table.end_line();
							}
						});
					}
				},
				test_entry{
					"grid", [](scroll_pane& pane){
						pane.create(
							[](grid& table){
								table.set_has_smooth_pos_animation(true);
								table.set_expand_policy(layout::expand_policy::prefer);
								table.emplace_back<elem>().cell().extent = {
										{.type = grid_extent_type::src_extent, .desc = {0, 2},},
										{.type = grid_extent_type::src_extent, .desc = {0, 1},},
									};
								table.emplace_back<elem>().cell().extent = {
										{.type = grid_extent_type::src_extent, .desc = {1, 2},},
										{.type = grid_extent_type::src_extent, .desc = {1, 1},},
									};
								table.emplace_back<elem>().cell().extent = {
										{.type = grid_extent_type::src_extent, .desc = {2, 2},},
										{.type = grid_extent_type::src_extent, .desc = {2, 1},},
									};
								table.emplace_back<elem>().cell().extent = {
										{.type = grid_extent_type::src_extent, .desc = {0, 4},},
										{.type = grid_extent_type::src_extent, .desc = {2, 2},},
									};
								table.emplace_back<elem>().cell().extent = {
										{.type = grid_extent_type::margin, .desc = {1, 1},},
										{.type = grid_extent_type::src_extent, .desc = {5, 1},},
									};
								table.emplace_back<elem>().cell().extent = {
										{.type = grid_extent_type::margin, .desc = {4, 1},},
										{.type = grid_extent_type::src_extent, .desc = {7, 1},},
									};
								table.emplace_back<elem>().cell().extent = {
										{.type = grid_extent_type::src_extent, .desc = {5, 6},},
										{.type = grid_extent_type::margin, .desc = {0, 0},},
									};
							}, math::vector2<grid_dim_spec>{
								grid_uniformed_mastering{6, 300.f, {4, 4}},
								grid_uniformed_passive{8, {4, 4}}
							});
						pane.set_layout_spec(layout::layout_specifier::fixed(layout::layout_policy::vert_major));
					}
				},
				test_entry{
					"drag/label", [](split_pane& table){
						constexpr static auto test_text =
							R"({s:*.5}Basic{size:64} Token {size:128}Test{//}
{u}AVasdfdjknfhvbawhboozx{/}cgiuTeWaVoT.P.àáâã ä åx̂̃ñ
{color:#FF0000}Red Text{/} and {font:gui}Font Change{/}

Escapes Test:
1. Backslash: \\ {_}(Should see single backslash){/}
2. Braces {size:128}with{/} slash: \{ and \} (Should see literal { and })
3. Braces with double: {{ and }} (Should see literal { and })

Line Continuation Test:
This is a very long line that \
{font:gui}should be joined together{/} \
without newlines.

{feature:liga}0 ff {feature:-liga}1 ff {feature:liga} 2 ff{feature} 3 ff{feature} 4 ff

O{ftr:liga}off file flaff{/} ff

Edge Cases:
1. Token without arg: {bold}Bold Text{/bold}
2. {u}Unclosed brace{/}: { This is just text because no closing bracket
3. Unknown escape: \z (Should show 'z')
4. Colon in arg: {log:Time:12:00} (Name="log", Arg="Time:12:00")
)";

						table.set_expand_policy(layout::expand_policy::passive);
						using namespace std::literals;
						table.create_head([](split_pane& inner){
							inner.set_expand_policy(layout::expand_policy::passive);
							inner.create_head([](scroll_adaptor<label>& p){
								auto& l = p.get_elem();
								l.set_style();
								l.set_expand_policy(layout::expand_policy::prefer);
								l.set_fit(false);
								l.set_text(test_text);
							});
							inner.create_body([](scroll_adaptor<label>& p){
								p.set_overlay_bar(true);
								auto& l = p.get_elem();
								l.set_style();
								l.set_tokenizer_tag(typesetting::tokenize_tag::raw);
								l.set_expand_policy(layout::expand_policy::prefer);
								l.set_fit(false);
								l.set_text(test_text);
								p.set_layout_spec(layout::layout_specifier::fixed(layout::layout_policy::vert_major));
							});
							inner.set_layout_spec(layout::layout_specifier::fixed(layout::layout_policy::hori_major));
						});


						table.create_body([](split_pane& inner){
							inner.set_expand_policy(layout::expand_policy::passive);
							inner.create_head([](scroll_pane& label){
								label.create([](gui::label& l){
									l.set_style();
									l.set_expand_policy(layout::expand_policy::prefer);
									l.set_fit(false);
									l.set_typesetting_config(typesetting::layout_config{
											.direction = typesetting::layout_direction::rtl,
										});
									l.set_text(test_text);
								});
							});
							inner.create_body([](scroll_pane& label){
								label.set_overlay_bar(true);
								label.create([&](gui::label& l){
									l.set_style();
									l.set_expand_policy(layout::expand_policy::prefer);
									l.set_fit(false);
									l.set_typesetting_config(typesetting::layout_config{
											.direction = typesetting::layout_direction::btt
										});
									l.set_text(test_text);
								});
								label.set_layout_spec(
									layout::layout_specifier::fixed(layout::layout_policy::vert_major));
							});
							inner.set_layout_spec(layout::layout_specifier::fixed(layout::layout_policy::hori_major));
						});
					}
				},
				test_entry{
					"color picker", [&](sequence& table){
						table.set_style();
						table.set_self_border(gui::border{}.set(16));
						table.set_layout_spec(
							layout::directional_layout_specifier::fixed(layout::layout_policy::vert_major));
						table.template_cell.set_pad({16, 16});
						table.set_expand_policy(layout::expand_policy::passive);
						struct picker : cpd::precise_color_picker{
							std::add_pointer_t<gui::example::make_style_result::node_type> prov;
							using precise_color_picker::precise_color_picker;

						protected:
							void on_color_changed(graphic::color color) override{
								prov->update_value(style::make_theme_palette(color));
							}
						};
						table.create_back([&](picker& p){
							p.prov = &style_pal_prov.front();
						}, layout::layout_policy::hori_major, 120);

						table.create_back([&](picker& p){
							p.prov = &style_pal_prov.back();
						}, layout::layout_policy::hori_major, 120);
					}
				}
			};

		return tests;
	};


	action::push_runnable_action(root, [](elem& e){
		e.set_style();
	});
	mroot.set_style();

	const auto menu_hdl = mroot.emplace_back<menu>(layout::layout_policy::vert_major, [](elem& m){
		m.set_style();
	});
	menu_hdl->set_style();
	menu_hdl->set_expand_policy(layout::expand_policy::passive);
	menu_hdl->set_head_size({layout::size_category::mastering, 100});
	menu_hdl.cell().region_scale = {.0f, .0f, .8f, 1.f};
	menu_hdl.cell().region_align = align::pos::left;

	menu_hdl->get_head_template_cell().set_pending();
	menu_hdl->get_head_template_cell().set_pad({4, 4});
	menu_hdl->get_button_pane().set_scroll_mode(scroll_pane_mode::proportional);

	for(const auto& [idx, creator] : make_create_table() | std::views::enumerate){
		menu_hdl->push_back(
			elem_ptr{
				menu_hdl->get_scene(), &menu_hdl.elem(), [&](label& label){
					label.set_self_border(border{}.set_vert(6));
					label.set_style(style::family_variant::base_only);
					label.set_fit_type(label_fit_type::scl);
					label.set_text(std::format("[{}]-{}", idx, creator.name));
					label.text_entire_align = align::pos::center;
					label.interactivity = interactivity_flag::enabled;
					label.set_transform_config({
							.rotation = text_rotation::deg_270
						});
				}
			}, creator.creator(menu_hdl->get_scene(), &menu_hdl.elem()));
	}

	return result;
}

void clear_main_ui(){
	game::ui::clear_collision_shape_editor_reference_images();
	auto& ui_root = gui::global::manager;
	ui_root.erase_scene("main");
	ui_root.erase_resource("main");
}
}
