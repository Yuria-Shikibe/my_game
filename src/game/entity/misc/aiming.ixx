module;

#include <cassert>
#include "mo_yanxi/adapted_attributes.hpp"

export module mo_yanxi.game.aiming;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.component.physical_property;
export import mo_yanxi.game.ecs.system.physics;
export import mo_yanxi.math;
export import mo_yanxi.math.trans2;
export import mo_yanxi.array_stack;

import mo_yanxi.meta_programming;

import std;

namespace mo_yanxi::game{

	export
	struct targeting_queue{
		struct weighted_entity{
			float distance{};
			float preference{};
			ecs::entity_ref ref{};
		};
	private:
		std::vector<weighted_entity> candidates{};

	public:
		[[nodiscard]] std::span<const weighted_entity> get_candidates() const noexcept{
			return candidates;
		}

		[[nodiscard]] ecs::entity_id get_optimal() noexcept{
			if(candidates.empty())return nullptr;

			auto itr = candidates.begin();
			while(itr != candidates.end()){
				if(itr->ref.is_expired()){
					++itr;
				}else{
					auto eid = itr->ref.id();
					candidates.erase(candidates.begin(), itr);
					return eid;
				}
			}

			candidates.clear();
			return nullptr;
		}

		[[nodiscard]] ecs::entity_id get_optimal() const noexcept{
			if(const auto itr = std::ranges::find_if_not(candidates, &ecs::entity_ref::is_expired, &weighted_entity::ref); itr != candidates.end()){
				return itr->ref.id();
			}

			return nullptr;
		}

		void clear() noexcept{
			candidates.clear();
		}

		auto size() const noexcept{
			return candidates.size();
		}

		template <
			std::predicate<const ecs::physics_query_result&> Filter = pred_always,
			std::strict_weak_order<float, float> SortPred = std::ranges::less>
		void index_candidates_by_distance(
			const ecs::system::physics_system& physics_system,
			const ecs::entity_id self,
			const math::vec2 position,
			const math::range valid_distance,
			Filter filter = {},
			SortPred sort_pred = {}){

			candidates.clear();
			const math::frect max_bound{position, valid_distance.to * 2};
			physics_system.spatial_query(max_bound, [&](const ecs::physics_query_result& obj){
				if(obj.id == self || obj.id->is_expired())return;
				const auto obj_trs = obj.position();
				const auto dst = obj_trs.dst(position);

				auto rng = valid_distance;
				if(!rng.expand(obj.radius_bound()).within_closed(dst))return;
				if(!std::invoke(filter, obj))return;

				candidates.push_back(weighted_entity{
					dst, 0.f, obj.id
				});
			});

			std::ranges::sort(candidates, std::move(sort_pred), &weighted_entity::distance);
		}

		template <
			std::predicate<const ecs::physics_query_result&> Filter = pred_always,
			std::invocable<const weighted_entity&> SortPredProj = decltype(&weighted_entity::preference),
			std::strict_weak_order<std::invoke_result_t<SortPredProj, const weighted_entity&>, std::invoke_result_t<SortPredProj, const weighted_entity&>> SortPred = std::ranges::less,
			std::invocable<const ecs::physics_query_result&> PrefProj
		>
			requires (std::convertible_to<std::invoke_result_t<PrefProj, const ecs::physics_query_result&>, float>)
		void index_candidates_by_distance(
			const ecs::system::physics_system& physics_system,
			const ecs::entity_id self,
			const math::vec2 position,
			const math::range valid_distance,
			Filter filter,
			SortPred sort_pred, PrefProj preference_proj, SortPredProj sort_pred_proj = &weighted_entity::preference){

			candidates.clear();
			const math::frect max_bound{position, valid_distance.to * 2};
			physics_system.spatial_query(max_bound, [&](const ecs::physics_query_result& obj){
				if(obj.id == self || obj.id->is_expired())return;

				const auto obj_trs = obj.position();
				const auto dst = obj_trs.dst(position);

				auto rng = valid_distance;
				if(!rng.expand(obj.radius_bound()).within_closed(dst))return;
				if(!std::invoke(filter, obj))return;

				candidates.push_back(weighted_entity{
					dst, std::invoke_r<float>(preference_proj, obj), obj.id
				});
			});

			std::ranges::sort(candidates, std::move(sort_pred), std::move(sort_pred_proj));
		}

	};

	namespace vel_model{
		export struct constant{};

		export struct inf{};

		export struct geometric{
			float value;
		};
		export struct arithmetic{
			float value;
		};

		export struct analyser{
			std::variant<
				constant, inf//, geometric, arithmetic
			> type;

			float initial;
			float lifetime;
		};
	}

	struct trivial_range : math::range{
		explicit operator bool() const noexcept{
			return !std::isnan(from) && !std::isnan(to);
		}
	};

	struct trivial_optional_float{
		float value;

		explicit(false) operator float() const noexcept{
			return value;
		}

		explicit operator bool() const noexcept{
			return std::isfinite(value);
		}
	};

	FORCE_INLINE trivial_optional_float resolve(const float a, const float b, const float c) noexcept{
		static constexpr auto INF = std::numeric_limits<float>::infinity();
		if(std::abs(a) < std::numeric_limits<decltype(a)>::epsilon()){
			const auto val = -c / b;
			return {
					std::abs(b) > std::numeric_limits<decltype(a)>::epsilon() && val >= 0
						? val
						: INF
				};
		}else{
			const auto delta = b * b - 4 * a * c;
			if(delta < 0){
				return {INF};
			}

			const auto sqrt_delta = std::sqrt(delta);
			const auto t1 = (-b - sqrt_delta) / (2 * a);
			const auto t2 = (-b + sqrt_delta) / (2 * a);

			const auto [min, max] = math::minmax(t1, t2);
			if(min > 0){
				return {min};
			}

			if(max > 0){
				return {max};
			}

			return {INF};
		}
	}

	export
	[[nodiscard]] FORCE_INLINE trivial_optional_float estimate_shoot_hit_delay(
		const math::vec2 shoot_pos,
		const math::vec2 target,
		const math::vec2 target_vel,
		const float bullet_speed) noexcept{
		if(std::isinf(bullet_speed)){
			return {bullet_speed > 0 ? 0 : std::numeric_limits<float>::infinity()};
		}

		const auto local = target - shoot_pos;

		const auto a = target_vel.length2() - bullet_speed * bullet_speed;
		const auto b = 2 * local.dot(target_vel);
		const auto c = local.length2();

		return resolve(a, b, c);
	}

	export
	[[nodiscard]] FORCE_INLINE trivial_optional_float estimate_shoot_hit_delay(
		const math::vec2 approach_delta,
		const math::vec2 target_vel,
		const float bullet_speed,
		const float bullet_initial_displacement) noexcept{
		if(math::isinf(bullet_speed)){
			return {bullet_speed > 0 ? 0 : std::numeric_limits<float>::infinity()};
		}

		const auto a = target_vel.length2() - bullet_speed * bullet_speed;
		const auto b = 2 * (approach_delta.dot(target_vel) - bullet_speed * bullet_initial_displacement);
		const auto c = approach_delta.length2() - bullet_initial_displacement * bullet_initial_displacement;

		return resolve(a, b, c);
	}


	export
	struct target{
		ecs::entity_ref entity{};
		math::vec2 target_local_offset{};

	protected:
		math::vec2 local_pos_{};

	public:
		[[nodiscard]] target() = default;

		[[nodiscard]] explicit(false) target(const ecs::entity_ref& entity) noexcept
			: entity(entity){
		}

		[[nodiscard]] explicit(false) target(ecs::entity_ref&& entity) noexcept
			: entity(std::move(entity)){
		}

		[[nodiscard]] PURE_FN FORCE_INLINE math::vec2 local_pos() const noexcept{
			return local_pos_;
		}

		template <typename T>
		auto& operator=(this T& self, const ecs::entity_id eid) noexcept{
			return self.operator=(T{ecs::entity_ref{eid}});
		}

		explicit operator bool() const noexcept{
			return static_cast<bool>(entity);
		}

		bool check_or_drop() noexcept{
			return entity.check_or_drop();
		}

		bool update(math::trans2 self_transform) noexcept{
			if(!entity.check_or_drop()){
				return false;
			}

			local_pos_ = (target_local_offset >> entity->at<ecs::mech_motion>().trans) << self_transform;
			return true;
		}
	};

	export
	struct traced_target : target{
		math::vec2 last{};

		using target::operator=;

		bool update(math::trans2 self_transform) noexcept{
			last = local_pos_;
			return target::update(self_transform);
		}
	};
}
