module;

#include <gch/small_vector.hpp>
#include "mo_yanxi/adapted_attributes.hpp"

export module mo_yanxi.game.ecs.system.collision;

export import mo_yanxi.game.ecs.component.manage;

export import mo_yanxi.game.quad_tree;
export import mo_yanxi.game.ecs.component.manifold;
export import mo_yanxi.game.ecs.component.physical_property;
export import mo_yanxi.math.intersection;

export import mo_yanxi.type_register;

import mo_yanxi.shared_stack;
import mo_yanxi.meta_programming;

import std;


namespace mo_yanxi::game::ecs::system{

	constexpr float MinimumSuccessAccuracy = 1. / 16.;
	constexpr float MinimumFailedAccuracy = 1. / 16.;

	math::vec2 approachTest(
		math::rect_box<float>& sbjBox, const math::vec2 sbjMove,
		math::rect_box<float>& objBox, const math::vec2 objMove
	){
		const math::vec2 beginning = sbjBox[0];
		float currentTestStep = .5f;
		float failedSum = .0f;

		bool lastTestResult = sbjBox.overlap_rough(objBox) && sbjBox.overlap_exact(objBox);

		while(true){
			sbjBox.move(sbjMove * currentTestStep);
			objBox.move(objMove * currentTestStep);

			if(sbjBox.overlap_rough(objBox) && sbjBox.overlap_exact(objBox)){
				if(std::abs(currentTestStep) < MinimumSuccessAccuracy){
					break;
				}

				//continue backtrace
				currentTestStep /= lastTestResult ? 2.f : -2.f;
				failedSum = .0f;

				lastTestResult = true;
			}else{
				if(std::abs(currentTestStep) < MinimumFailedAccuracy){
					if(lastTestResult){
						failedSum = -currentTestStep;
					}
					//backtrace to last succeed position
					sbjBox.move(sbjMove * failedSum);
					objBox.move(objMove * failedSum);

					break;
				}

				//failed, perform forward trace
				failedSum -= currentTestStep;
				currentTestStep /= lastTestResult ? -2.f : 2.f;

				lastTestResult = false;
			}
		}

		return sbjBox[0] - beginning;
	}


	export
	struct collision_system{
	private:
		std::vector<manifold*> pre_passed_manifolds{};
		using collision_test_tuple = std::tuple<const chunk_meta&, manifold&, mech_motion&, physical_rigid&>;
		using collision_test_tuple_store = std::tuple<const chunk_meta*, manifold*, mech_motion*, physical_rigid*>;

		quad_tree<collision_object> tree{{{0, 0}, 100000}};

		shared_stack<collision_test_tuple_store> passed_entites{};

		FORCE_INLINE static void rigidCollideWith(
			const collision_object& sbj,
			const collision_object& obj,
			const intersection& intersection,
			const math::vec2 sbj_correction,
			const float energyScale) noexcept{
			assert(sbj != obj);

			using math::vec2;

			const vec2 dst_to_sbj = intersection.pos - sbj.motion->pos();
			const vec2 dst_to_obj = intersection.pos - obj.motion->pos();

			const vec2 sbj_vel = sbj.motion->vel_at(dst_to_sbj);
			const vec2 obj_vel = obj.motion->vel_at(dst_to_obj);

			const vec2 rel_vel = (obj_vel - sbj_vel) * energyScale;

			const vec2 hit_normal = intersection.normal;
			assert(hit_normal.length2() > 0.75f);

			const auto sbj_mass = sbj.rigid->inertial_mass;
			const auto obj_mass = obj.rigid->inertial_mass;

			const auto scaled_mass = 1. / sbj_mass + 1. / obj_mass;
			const auto sbj_rotational_inertia = sbj.rigid->rotational_inertia >= 0 ? sbj.rigid->rotational_inertia : sbj.manifold->hitbox.get_rotational_inertia(sbj_mass);
			const auto obj_rotational_inertia = obj.rigid->rotational_inertia >= 0 ? obj.rigid->rotational_inertia : obj.manifold->hitbox.get_rotational_inertia(obj_mass);

			const vec2 vert_hit_vel{rel_vel.copy().project_scaled(hit_normal)};
			const vec2 hori_hit_vel{(rel_vel - vert_hit_vel)};

			const vec2 hit_tangent = hori_hit_vel.equals({})
				                                 ? hit_normal.copy().rotate_rt_clockwise()
				                                 : hori_hit_vel.copy().normalize();

			const vec2 impulse_normal = vert_hit_vel * (1 +
				math::min(sbj.rigid->restitution, obj.rigid->restitution)) /
				(scaled_mass +
					math::sqr(dst_to_obj.cross(hit_normal)) / obj_rotational_inertia +
					math::sqr(dst_to_sbj.cross(hit_normal)) / sbj_rotational_inertia
				);

			const vec2 impulse_tangent = (hori_hit_vel * hori_hit_vel.length() /
						(scaled_mass +
							math::sqr(dst_to_obj.cross(hit_tangent)) / obj_rotational_inertia +
							math::sqr(dst_to_sbj.cross(hit_tangent)) / sbj_rotational_inertia))
				.limit_max_length(std::sqrt(sbj.rigid->friction_coefficient * obj.rigid->friction_coefficient) * impulse_normal.length());

			const vec2 impulse = impulse_normal + impulse_tangent;

			sbj.manifold->collision_vel_trans_sum.vec += impulse / sbj.rigid->inertial_mass;
			sbj.manifold->collision_vel_trans_sum.rot += dst_to_sbj.cross(impulse) / sbj_rotational_inertia;

			auto sbj_vel_len2 = sbj.motion->vel.vec.length2();
			auto obj_vel_len2 = obj.motion->vel.vec.length2();
			if(sbj_mass <= obj_mass){
				math::rect_box box{sbj.manifold->hitbox[intersection.index.sbj_idx].box};
				box.move(sbj_correction);
				auto& obj_box = obj.manifold->hitbox[intersection.index.obj_idx].box;

				if(std::ranges::contains(sbj.manifold->last_collided, obj.id, &entity_ref::id)){
					vec2 min_correction{};
					auto dot = (box.avg() - obj_box.avg()).dot(sbj.motion->vel.vec);
					if((sbj.motion->vel.vec - obj.motion->vel.vec).length2() < 25 || math::abs(dot) < 0.65f){
						min_correction = box.depart_vector_to(obj_box).set_length(1.5f);
					}else{
						min_correction = (sbj.motion->vel.vec * (dot > 0 ? 1 : -1)).set_length(1.5f) ;
					}


					sbj.manifold->collision_correction_vec += min_correction;

					sbj.manifold->is_under_correction = true;
				}else{
					vec2 min_correction{};
					auto dot = (box.avg() - obj_box.avg()).dot(sbj.motion->vel.vec);
					if((sbj.motion->vel.vec - obj.motion->vel.vec).length2() < 25 || math::abs(dot) < 0.65f){
						min_correction = box.depart_vector_to(obj_box) * .05f;
					}else{
						min_correction = box.depart_vector_to_on_vel_rough_min(obj_box, sbj.motion->vel.vec * (dot > 0 ? -1 : 1)) * .75f;
					}


					sbj.manifold->collision_correction_vec += min_correction;
				}

			}

			assert(!sbj.manifold->collision_vel_trans_sum.is_NaN());
		}


		[[nodiscard]] FORCE_INLINE static bool roughIntersectWith(
			const collision_object& sbj,
			const collision_object& obj
		) noexcept{
			if(sbj == obj)return false;

			if(!sbj.manifold->hitbox.max_wrap_bound().overlap_exclusive(obj.manifold->hitbox.max_wrap_bound()))return false;

			if(std::abs(sbj.motion->depth - obj.motion->depth) > sbj.manifold->thickness + obj.manifold->thickness) return
				false;

			if(!manifold::is_collideable_between(sbj, obj))return false;

			return true;
		}

		FORCE_INLINE static bool check_pre_collision(manifold& manifold, mech_motion& motion) noexcept{
			//TODO is this necessary?
			const bool rst = manifold.filter();

			if(rst){
				//Push trans to hitpos
				if(!manifold.no_backtrace_correction && !manifold.is_under_correction){
					const auto backTrace = manifold.hitbox.get_back_trace_move();

					motion.trans.vec += backTrace;
				}
			} else{
				manifold.is_under_correction = false;
			}

			return rst;
		}

		FORCE_INLINE static void collapse_possible_collisions(manifold& manifold, mech_motion& motion) noexcept{
			math::vec2 pre_correction_sum{};

			for(const auto& collision : manifold.possible_collision){
				for(const auto index : collision.data.indices){

					const auto& sbj_box = manifold.hitbox;
					const auto& obj_box = collision.other.manifold->hitbox;

					math::rect_box sbj = sbj_box[index.sbj_idx].box;
					math::rect_box obj = obj_box[index.obj_idx].box;

					sbj.move(sbj_box.get_back_trace_move());
					obj.move(obj_box.get_back_trace_move());

					math::vec2 correction{};
					const auto sbjMove = sbj_box.get_back_trace_unit_move();
					const auto objMove = obj_box.get_back_trace_unit_move();
					if(
						manifold.is_under_correction ||
						sbjMove.length2() + objMove.length2() < math::sqr(15) ||
						std::ranges::contains(manifold.last_collided, collision.other.id, &entity_ref::id)
					){
					} else{
						correction = manifold.is_under_correction
							                        ? math::vec2{}
							                        : approachTest(
								                        sbj, sbj_box.get_back_trace_unit_move(),
								                        obj, obj_box.get_back_trace_unit_move()
							                        );
					}




					const auto avg_s = sbj.expand_unchecked(2);
					const auto avg_o = obj.expand_unchecked(2);

					auto avg = rect_rough_avg_intersection(sbj, obj);
					if(!avg){
						avg.pos = (avg_s + avg_o) / 2;
					}

					pre_correction_sum += correction;
					auto& data = manifold.confirmed_collision.emplace_back(
						collision.other,
						correction,
						intersection{
							.pos = avg.pos,
							.normal = avg_edge_normal(obj, avg.pos).normalize(),
							.index = index
						});

					assert(!data.intersection.normal.is_NaN());
				}
			}

			manifold.possible_collision.clear();

			if(!manifold.confirmed_collision.empty()){
				pre_correction_sum /= manifold.confirmed_collision.size();
			}

			motion.trans.vec += pre_correction_sum;
			manifold.collision_correction_vec -= pre_correction_sum;
		}

		FORCE_INLINE static void collisionsPostProcess_3(const collision_object& subject) noexcept{
			for(const auto& data : subject.manifold->confirmed_collision){
				auto rst = manifold::try_collide_to(subject, data.other, data.intersection);
				if((rst & collision_result::obj_passed) == collision_result{}){
					using gch::erase_if;
					erase_if(data.other.manifold->confirmed_collision, [&](const collision_confirmed& d){
						return d.get_other_id() == subject.id;
					});
				}

				if((rst & collision_result::sbj_passed) == collision_result{}){
					//Notice that this is required, or else the other will goto last hit
					using gch::erase_if;
					erase_if(subject.manifold->confirmed_collision, [&](const collision_confirmed& d){
						return d.get_other_id() == data.other.id;
					});
				}else{
					rigidCollideWith(subject, data.other, data.intersection, data.correction, 1);
				}

			}
		}

		FORCE_INLINE static void collisionsPostProcess_4(manifold& manifold, mech_motion& motion) noexcept{
			math::vec2 pre_correction_sum{};
			if(!manifold.confirmed_collision.empty()){
				for (const auto & confirmed_collision : manifold.confirmed_collision){
					pre_correction_sum += confirmed_collision.correction;
				}

				pre_correction_sum /= manifold.confirmed_collision.size();
			}

			motion.trans.vec += manifold.collision_correction_vec + pre_correction_sum;
			motion.vel += manifold.collision_vel_trans_sum;

			manifold.collision_correction_vec = {};
			manifold.collision_vel_trans_sum = {};

			if(manifold.confirmed_collision.empty()){
				if(!manifold.no_backtrace_correction && !manifold.is_under_correction){
					const auto backTrace = manifold.hitbox.get_back_trace_move();

					motion.trans.vec -= backTrace;
				}
			}

			manifold.markLast();

			//Fetch manifold pos to final trans pos, necessary to make next ccd correct
			manifold.hitbox.set_trans_unchecked(motion.trans);
		}

	public:
		[[nodiscard]] decltype(tree)& quad_tree() noexcept{
			return tree;
		}

		void insert_all(component_manager& component_manager) noexcept{
			tree->reserved_clear();

			component_manager.sliced_each([this](
				const chunk_meta& meta,
				manifold& manifold,
				mech_motion& motion,
				physical_rigid& rigid
			){
				manifold.hitbox.update_hitbox_with_ccd(motion.trans);
				tree->insert({meta.id(), &manifold, &motion, &rigid});
			});
		}

		void run_collision_test_pre(component_manager& component_manager){
			using RangeT = decltype(component_manager.get_slice_of<const chunk_meta, manifold, mech_motion, physical_rigid>());
			auto ranges =
				component_manager.get_slice_of<const chunk_meta, manifold, mech_motion, physical_rigid>() | std::views::transform([](RangeT::const_reference value){
				using T = unary_apply_to_tuple_t<std::remove_reference_t, unary_apply_to_tuple_t<std::ranges::range_reference_t, RangeT::value_type>>;


				return strided_multi_span<T>{value};
			}) | std::ranges::to<std::vector>();

			auto view = ranges | std::views::join;

			auto entity_count = std::ranges::fold_left(ranges | std::views::transform([](auto& v){
				return v.size();
			}), std::size_t{0}, std::plus{});

			std::atomic_size_t possible_counts{};
			std::for_each(std::execution::par, view.begin(), view.end(), [&, this](
				collision_test_tuple data
			){
				auto [meta, mf, motion, rigid] = data;
				tree->intersect_all({meta.id(), &mf, &motion, &rigid}, [&](const collision_object& sbj, const collision_object& obj){
					if(sbj == obj)return;
					if(sbj.manifold->test_intersection_with(obj)){
						possible_counts.fetch_add(1, std::memory_order_relaxed);
					}
				}, roughIntersectWith);
			});

			auto count = possible_counts.load(std::memory_order_relaxed);

			if(count * 2 < passed_entites.capacity() && passed_entites.capacity() > 128){
				passed_entites.reset();
			}
			passed_entites.clear_and_reserve(count);


			std::for_each(std::execution::par, view.begin(), view.end(), [&, this](
				collision_test_tuple data
			){
				auto [meta, mf, motion, rigid] = data;

				if(!check_pre_collision(mf, motion)) return;

				collapse_possible_collisions(mf, motion);

				passed_entites.push({&meta, &mf, &motion, &rigid});
			});
		}

		void run_collision_test(component_manager& component_manager){

			run_collision_test_pre(component_manager);

			auto rng = passed_entites.locked_range();

			//TODO make it resversed: object with high priority firstly try collision, if passed, remove both collision test from itself and the target
			std::ranges::sort(rng, std::ranges::greater{}, [](const collision_test_tuple_store& tlp){
				return std::get<manifold*>(tlp)->collider.index();
			});

		}

		void apply_collision(){
			auto rng = passed_entites.locked_range();

			for (auto [meta, mf, motion, rigid] : rng){
				collisionsPostProcess_3({meta->id(), mf, motion, rigid});
			}

			for (auto [meta, mf, motion, rigid] : rng){
				collisionsPostProcess_4(*mf, *motion);
			}
		}
	};
}
