module;

#include <gch/small_vector.hpp>
#include "mo_yanxi/adapted_attributes.hpp"

export module mo_yanxi.game.ecs.component.hitbox;

export import mo_yanxi.math.quad;
export import mo_yanxi.math.vector2;
export import mo_yanxi.math.trans2;
export import mo_yanxi.math.rect_ortho;
export import mo_yanxi.math.matrix3;
export import mo_yanxi.math;

export import mo_yanxi.game.meta.hitbox;

import std;
import mo_yanxi.array_stack;

using hitbox_index = unsigned;
using ccd_trace_index = unsigned;

constexpr std::memory_order drop_release(const std::memory_order m) noexcept{
	return m == std::memory_order_release
		       ? std::memory_order_relaxed
		       : m == std::memory_order_acq_rel || m == std::memory_order_seq_cst
		       ? std::memory_order_acquire
		       : m;
}

template <typename T>
T atomic_fetch_max_explicit(std::atomic<T>& pv,
                            typename std::atomic<T>::value_type v,
                            std::memory_order m) noexcept{
	auto const mr = drop_release(m);
	auto t = (mr != m) ? pv.fetch_add(0, m) : pv.load(mr);
	while(std::max(v, t) != t){
		if(pv.compare_exchange_weak(t, v, m, mr)) return t;
	}
	return t;
}

template <typename T>
T atomic_fetch_min_explicit(std::atomic<T>& pv, typename std::atomic<T>::value_type v,
                            const std::memory_order m) noexcept{
	auto const mr = drop_release(m);
	auto t = (mr != m) ? pv.fetch_add(0, m) : pv.load(mr);
	while(std::min(v, t) != t){
		if(pv.compare_exchange_weak(t, v, m, mr)) return t;
	}
	return t;
}


namespace mo_yanxi::game{
	/**
	 * \brief Used of CCD precision, the larger - the slower and more accurate
	 */
	constexpr float ContinuousTestScl = 1.33f;

	/**
	 * @brief limit index under @link std::numeric_limits<unsigned short>::max() @endlink
	 */
	export
	struct hitbox_collision_indices{
		hitbox_index sbj_idx;
		hitbox_index obj_idx;

		constexpr friend bool operator==(const hitbox_collision_indices& lhs, const hitbox_collision_indices& rhs) noexcept = default;
	};

	export
	struct collision_data{
		static constexpr hitbox_index max_hit_count = 2;
		hitbox_index sbj_transition{};
		hitbox_index obj_transition{};
		// Index
		array_stack<hitbox_collision_indices, max_hit_count, hitbox_index> indices{};

		[[nodiscard]] constexpr collision_data() noexcept = default;

		[[nodiscard]] constexpr collision_data(const hitbox_index transition_sbj, const hitbox_index transition_obj) noexcept
			: sbj_transition(transition_sbj), obj_transition{transition_obj}{
		}

		[[nodiscard]] constexpr bool empty() const noexcept{
			return indices.empty();
		}
	};

	using rect_comp = math::rect_box<float>;

	//TODO maybe [trans local, trans global], [hitbox comp] is better?

	export
	struct hitbox_comp{
		/** @brief Raw Box Data */
		math::rect_box_posed box{};

		/** @brief Local Transform */
		math::trans2 trans{};

		explicit(false) constexpr operator const math::rect_box_posed&() const noexcept{
			return box;
		}

		explicit(false) constexpr operator math::rect_box_posed&() noexcept{
			return box;
		}

		friend bool operator==(const hitbox_comp& lhs, const hitbox_comp& rhs) noexcept{
			return lhs.box.get_identity() == rhs.box.get_identity() && lhs.trans == rhs.trans;
		}

		void update() noexcept{
			box.update(trans);
		}
	};

	union hitbox_span{
		std::span<rect_comp> temporary;
		std::span<const hitbox_comp> components;
	};

	export
	template <typename T>
	concept collision_collector = requires{
		requires std::is_invocable_r_v<collision_data*, T, hitbox_index, hitbox_index>;
	};

	struct collision_collect_ignore{
		static constexpr collision_data* operator()(hitbox_index, hitbox_index) noexcept{
			assert(false);
			return nullptr;
		}
	};

	using small_vector_of_hitbox_comp = gch::small_vector<hitbox_comp, 4>;


	export
	struct hitbox_identity{
		small_vector_of_hitbox_comp components{};

		[[nodiscard]] hitbox_identity() = default;

		template <std::ranges::input_range Rng>
			requires std::convertible_to<std::ranges::range_value_t<Rng>, hitbox_comp>
		[[nodiscard]] hitbox_identity(std::from_range_t, Rng&& comps)
			: components(std::ranges::begin(comps), std::ranges::end(comps)){
		}

		[[nodiscard]] explicit(false) hitbox_identity(const meta::hitbox& comps)
			: components(comps.size()){
			for(const auto& [idx, meta] : comps | std::views::enumerate){
				components[idx] = hitbox_comp{meta.box, meta.trans};
			}
		}

		[[nodiscard]] explicit(false) hitbox_identity(const std::vector<hitbox_comp>& comps)
			: components(comps.begin(), comps.end()){
		}

		[[nodiscard]] explicit(false) hitbox_identity(const hitbox_comp& comps)
			: hitbox_identity(std::vector{comps}){
		}

		constexpr auto& operator[](const std::size_t index) noexcept{
			return components[index];
		}

		constexpr auto& operator[](const std::size_t index) const noexcept{
			return components[index];
		}

		[[nodiscard]] constexpr std::size_t size() const noexcept{
			return components.size();
		}

		[[nodiscard]] float get_wrap_radius() const noexcept{
			float max{};
			for(const auto& comp : components){
				math::rect_box_posed box{comp.box.get_identity(), comp.trans};
				max = std::ranges::max({box[0].length2(), box[1].length2(), box[2].length2(), box[3].length2()});
			}
			return std::sqrt(max);
		}

		explicit operator meta::hitbox() const{
			meta::hitbox hitbox{};
			hitbox.components.resize(size());
			for(const auto& [idx, comp] : components | std::views::enumerate){
				hitbox.components[idx] = {comp.box.get_identity(), comp.trans};
			}
			return hitbox;
		}
	};

	export
	class ccd_hitbox : /*protected*/public hitbox_identity{
	protected:

		//TODO Should this member move to ccd hitbox?
		math::trans2 trans{};
		//wrap box
		rect_comp wrap_bound_ccd_{};
		math::frect wrap_bound_{};

	public:
		constexpr ccd_hitbox() = default;

		using hitbox_identity::size;
		using hitbox_identity::get_wrap_radius;
		using hitbox_identity::operator meta::hitbox;

		[[nodiscard]] explicit(false) ccd_hitbox(const hitbox_identity& identity, const math::trans2 transform = {})
			: hitbox_identity(identity){
			if(!components.empty())update(transform);
		}

		[[nodiscard]] explicit ccd_hitbox(const std::vector<hitbox_comp>& comps, const math::trans2 transform = {})
			: hitbox_identity(comps){
			if(!components.empty())update(transform);
		}

		[[nodiscard]] explicit ccd_hitbox(const hitbox_comp& comps, const math::trans2 transform = {})
			: ccd_hitbox(std::vector{comps}, transform){
		}

		constexpr const hitbox_comp& operator[](const std::size_t index) const noexcept{
			return components[index];
		}

		void set_trans_unchecked(math::trans2 trs) noexcept{
			trans = trs;
		}

		[[nodiscard]] std::span<const hitbox_comp> get_comps() const noexcept{
			return {components.data(), components.size()};
		}

		[[nodiscard]] auto get_trans() const noexcept{
			return trans;
		}

		/*constexpr*/ void update(const math::trans2 nextTrans) noexcept{
			assert(!components.empty());

			const auto move = this->trans.vec - nextTrans.vec;

			this->trans = nextTrans;
			const float ang = move.equals({}, .2f) ? trans.rot : move.angle_rad();

			const auto [cos, sin] = math::cos_sin(-ang);

			float minX = std::numeric_limits<float>::max();
			float minY = std::numeric_limits<float>::max();
			float maxX = std::numeric_limits<float>::lowest();
			float maxY = std::numeric_limits<float>::lowest();

			//TODO Simple object [box == 1] Optimization (for bullets mainly)

			for(auto& boxData : components){
				boxData.box.update(boxData.trans >> trans);

				const std::array verts{
					boxData.box[0].rotate(cos, sin),
					boxData.box[1].rotate(cos, sin),
					boxData.box[2].rotate(cos, sin),
					boxData.box[3].rotate(cos, sin)
				};

				for(auto [x, y] : verts){
					minX = math::min(minX, x);
					minY = math::min(minY, y);
					maxX = math::max(maxX, x);
					maxY = math::max(maxY, y);
				}
			}

			CHECKED_ASSUME(minX <= maxX);
			CHECKED_ASSUME(minY <= maxY);

			maxX += move.length();

			wrap_bound_ccd_ = rect_comp{
				math::vec2{minX, minY}.rotate(cos, -sin),
				math::vec2{maxX, minY}.rotate(cos, -sin),
				math::vec2{maxX, maxY}.rotate(cos, -sin),
				math::vec2{minX, maxY}.rotate(cos, -sin)
			};

			wrap_bound_ = wrap_bound_ccd_.get_bound().shrink_by(-move);
		}

		[[nodiscard]] constexpr float estimate_max_length2() const noexcept{
			return wrap_bound_.diagonal_len2() * 0.35f;
		}

		[[nodiscard]] constexpr math::frect max_wrap_bound() const noexcept{
			return wrap_bound_ccd_.get_bound();
		}

		[[nodiscard]] constexpr math::frect min_wrap_bound() const noexcept{
			return wrap_bound_;
		}

		[[nodiscard]] constexpr const rect_comp& ccd_wrap_box() const noexcept{
			return wrap_bound_ccd_;
		}

		[[nodiscard]] bool collide_with_rough(const ccd_hitbox& other) const noexcept{
			return wrap_bound_ccd_.overlap_rough(other.wrap_bound_ccd_) && wrap_bound_ccd_.overlap_exact(other.wrap_bound_ccd_);
		}



		//TODO move this to other place?
		// void scl(const math::vec2 scl){
		// 	for(auto& data : components){
		// 		data.box.offset *= scl;
		// 		data.box.size *= scl;
		// 	}
		// }

	};

	export
	class hitbox : public ccd_hitbox{
		/** @brief CCD-traces size., should never be 0*/
		ccd_trace_index size_CCD{1};

		/**
		 * @brief CCD-traces clamp size. shrink only!
		 * [0, size_CCD]
		 */
		mutable std::atomic<ccd_trace_index> indexClamped_CCD{};

		/** @brief CCD-traces spacing*/
		math::vec2 backtraceUnitMove{};

	public:
		using ccd_hitbox::ccd_hitbox;

		void update_hitbox_with_ccd(const math::trans2 current_trans){
			// const auto lastTrans = trans;
			const auto move = current_trans.vec - this->trans.vec;

			gen_continuous_rect_box(move);
			update(current_trans);
		}

		void update_hitbox_with_ccd(const math::vec2 movement) noexcept{
			update({movement + trans.vec, trans.rot});
			gen_continuous_rect_box(movement);
		}

		[[nodiscard]] ccd_trace_index get_intersect_move_index() const noexcept{
			return indexClamped_CCD.load(std::memory_order::acquire);
		}

		[[nodiscard]] constexpr math::vec2 get_move_snapshot_at(const ccd_trace_index transIndex) const noexcept{
			if(size_CCD == transIndex)return {};
			assert(transIndex < size_CCD);
			return backtraceUnitMove * (size_CCD - transIndex - 1);
		}

		[[nodiscard]] constexpr math::vec2 get_back_trace_unit_move() const noexcept{
			return backtraceUnitMove;
		}

		[[nodiscard]] math::vec2 get_back_trace_move() const noexcept{
			return get_move_snapshot_at(get_intersect_move_index());
		}

		[[nodiscard]] math::vec2 get_back_trace_move_full() const noexcept{
			return backtraceUnitMove * (size_CCD - 1);
		}

		template <collision_collector Col = collision_collect_ignore>
		bool collide_with_exact(
			const hitbox& other,
			Col collector = {},
			const bool soundful = false
		) const noexcept{

			if constexpr (std::same_as<Col, collision_collect_ignore>){
				if(is_clamped() || other.is_clamped()){
					return true;
				}
			}

			math::frect bound_subject = this->wrap_bound_;
			math::frect bound_object = other.wrap_bound_;

			const auto trans_subject = -this->backtraceUnitMove;
			const auto trans_object = -other.backtraceUnitMove;

			const bool sbj_is_static = backtraceUnitMove.is_zero();
			const bool obj_is_static = other.backtraceUnitMove.is_zero();

			gch::small_vector<rect_comp, 8> tempHitboxes{};
			hitbox_span rangeSubject{};
			hitbox_span rangeObject{};
			using spn = std::span<rect_comp>;
			//add comp to temp array or directly observe it
			{
				if(sbj_is_static && obj_is_static){
					std::construct_at(&rangeSubject.components, get_comps());
					std::construct_at(&rangeObject.components, other.get_comps());
				}else if(!sbj_is_static && obj_is_static){
					tempHitboxes.reserve(components.size());

					for (const auto& component : components){
						tempHitboxes.push_back(component.box);
					}

					std::construct_at(&rangeSubject.temporary, spn{tempHitboxes.data(), tempHitboxes.size()});
					std::construct_at(&rangeObject.components, other.get_comps());
				}else if(sbj_is_static && !obj_is_static){
					tempHitboxes.reserve(other.components.size());

					for (const auto& component : other.components){
						tempHitboxes.push_back(component.box);
					}

					std::construct_at(&rangeSubject.components, get_comps());
					std::construct_at(&rangeObject.temporary, spn{tempHitboxes.data(), tempHitboxes.size()});
				}else{
					tempHitboxes.reserve(components.size() + other.components.size());

					for (const auto& component : components){
						tempHitboxes.push_back(component.box);
					}

					for (const auto& component : other.components){
						tempHitboxes.push_back(component.box);
					}

					std::construct_at(&rangeSubject.temporary, spn{tempHitboxes.data(), size()});
					std::construct_at(&rangeObject.temporary, spn{tempHitboxes.data() + size(), tempHitboxes.size() - size()});
				}
			}


			//initialize collision state: move all object to the pos before last move
			{
				if(!sbj_is_static){
					const auto maxTrans_subject = this->get_back_trace_move_full();

					//Move to initial stage
					for(auto& box : rangeSubject.temporary){
						box.move(maxTrans_subject);
					}

					bound_subject.move(maxTrans_subject);
				}

				if(!obj_is_static){
					const auto maxTrans_object = other.get_back_trace_move_full();

					for(auto& box : rangeObject.temporary){
						box.move(maxTrans_object);
					}

					bound_object.move(maxTrans_object);
				}
			}

			assert(size_CCD > 0);
			assert(other.size_CCD > 0);



			auto sbjMoveFunc = [&]{
				if(sbj_is_static)return;
				for(auto& box : rangeSubject.temporary){
					box.move(trans_subject);
				}
				bound_subject.move(trans_subject);
			};

			auto objMoveFunc = [&]{
				if(obj_is_static)return;
				for(auto& box : rangeObject.temporary){
					box.move(trans_object);
				}
				bound_object.move(trans_object);
			};

			hitbox_index index_subject{};
			auto test = [&, this](const hitbox_index curCeilIdx){
				bool ignoreGeneralTest = this->size() == 1 && other.size() == 1;
				hitbox_index index_object{};

				for(; index_object <= curCeilIdx; ++index_object){
					if(ignoreGeneralTest || bound_subject.overlap_exclusive(bound_object)){
						if(this->load_to<Col>(collector, soundful, other,
							rangeSubject, index_subject, rangeObject, index_object
						)){
							return true;
						}
					}

					objMoveFunc();
				}

				if(ignoreGeneralTest || bound_subject.overlap_exclusive(bound_object)){
					if(this->load_to<Col>(collector, soundful, other,
						rangeSubject, index_subject, rangeObject, index_object
					)){
						return true;
					}
				}

				return false;
			};

			//OPTM this is horrible..., fucking O(n*m*p*q)

			for(; index_subject <= get_intersect_move_index(); ++index_subject){
				const auto curIdx = other.get_intersect_move_index();
				if(test(curIdx))return true;

				sbjMoveFunc();

				if(!obj_is_static){
					for(auto& box : rangeObject.temporary){
						box.move(-trans_object * (curIdx + 1));
					}

					bound_object.move(-trans_object * (curIdx + 1));
				}
			}

			if(test(other.get_intersect_move_index()))return true;

			return false;
		}



		[[nodiscard]] bool is_clamped() const noexcept{
			return get_intersect_move_index() != size_CCD;
		}

	private:

		/**
		 * \brief This ignores the rotation of the subject entity!
		 * Record backwards movements
		 */
		void gen_continuous_rect_box(
			math::vec2 move
		) noexcept {
			const float size2 = estimate_max_length2();
			if(size2 < 0.25f){
				size_CCD = 1;
				indexClamped_CCD.store(0, std::memory_order::release);

				return;
			}

			const float dst2 = move.length2();

			const hitbox_index seg = math::min(math::round<hitbox_index>(std::sqrtf(dst2 / size2) * ContinuousTestScl), 64u);

			if(seg > 0){
				move /= static_cast<float>(seg);
				backtraceUnitMove = -move;
			}else{
				backtraceUnitMove = {};
			}

			size_CCD = seg + 1;
			indexClamped_CCD.store(seg, std::memory_order::release);
		}

		void clamp_ccd(const hitbox_index index) const noexcept{
			atomic_fetch_min_explicit(indexClamped_CCD, index, std::memory_order::acq_rel);
		}

		template <collision_collector Col = collision_collect_ignore>
		[[nodiscard]] bool load_to(
			Col collector, const bool soundful, const hitbox& other,
			const hitbox_span rangeSubject, const hitbox_index index_subject,
			const hitbox_span rangeObject, const hitbox_index index_object
		) const noexcept{
			collision_data* collisionData{};
			assert(index_subject <= size_CCD);
			assert(index_object <= other.size_CCD);

			auto tryIntersect = [&](
				const std::span<rect_comp>::size_type subjectBoxIndex, const rect_comp& subject,
				const std::span<rect_comp>::size_type objectBoxIndex, const rect_comp& object
			){
				if(index_subject > this->get_intersect_move_index() + 1 || index_object > other.get_intersect_move_index() + 1) return false;

				if(!subject.overlap_rough(object) || !subject.overlap_exact(object)) return false;

				if constexpr (std::same_as<Col, collision_collect_ignore>){
					this->clamp_ccd(index_subject);
					other.clamp_ccd(index_object);

					return true;
				}else{
					if(!collisionData){
						this->clamp_ccd(index_subject);
						other.clamp_ccd(index_object);
						collisionData = std::invoke(collector, index_subject, index_object);
					}

					collisionData->indices.push({
						static_cast<hitbox_index>(subjectBoxIndex),
						static_cast<hitbox_index>(objectBoxIndex)
						});
					if(!soundful || collisionData->indices.full()){
						return true;
					}
				}

				return false;
			};


			if(backtraceUnitMove.is_zero() && other.backtraceUnitMove.is_zero()){
				for(auto&& [subjectBoxIndex, subject] : rangeSubject.components | std::views::transform(&hitbox_comp::box) | std::views::enumerate){
					for(auto&& [objectBoxIndex, object] : rangeObject.components | std::views::transform(&hitbox_comp::box) | std::views::enumerate){
						if(tryIntersect(subjectBoxIndex, subject, objectBoxIndex, object)){
							return true;
						}
					}
				}
			}else if(!backtraceUnitMove.is_zero() && other.backtraceUnitMove.is_zero()){
				for(auto&& [subjectBoxIndex, subject] : rangeSubject.temporary | std::views::enumerate){
					for(auto&& [objectBoxIndex, object] : rangeObject.components | std::views::transform(&hitbox_comp::box) | std::views::enumerate){
						if(tryIntersect(subjectBoxIndex, subject, objectBoxIndex, object)){
							return true;
						}
					}
				}
			}else if(backtraceUnitMove.is_zero() && !other.backtraceUnitMove.is_zero()){
				for(auto&& [subjectBoxIndex, subject] : rangeSubject.components | std::views::transform(&hitbox_comp::box) | std::views::enumerate){
					for(auto&& [objectBoxIndex, object] : rangeObject.temporary | std::views::enumerate){
						if(tryIntersect(subjectBoxIndex, subject, objectBoxIndex, object)){
							return true;
						}
					}
				}
			}else{
				for(auto&& [subjectBoxIndex, subject] : rangeSubject.temporary | std::views::enumerate){
					for(auto&& [objectBoxIndex, object] : rangeObject.temporary | std::views::enumerate){
						if(tryIntersect(subjectBoxIndex, subject, objectBoxIndex, object)){
							return true;
						}
					}
				}
			}

			if constexpr (std::same_as<Col, collision_collect_ignore>){
				return false;
			}

			assert(soundful || !collisionData);
			if(collisionData){
				assert(!collisionData->empty());
				return true;
			}

			return false;
		}

	public:

		[[nodiscard]] bool contains(const math::vec2 vec2) const noexcept{
			if(!min_wrap_bound().contains_strict(vec2))return false;

			return std::ranges::any_of(components, [vec2](const hitbox_comp& data){
				return data.box.contains(vec2);
			});
		}

		[[nodiscard]] bool overlaps(const math::frect rect) const noexcept{
			if(!min_wrap_bound().contains_strict(rect))return false;

			return std::ranges::any_of(components, [&](const hitbox_comp& data){
				return data.box.overlap_rough(rect) && data.box.overlap_exact(rect);
			});
		}

		[[nodiscard]] constexpr double get_rotational_inertia(
			const float mass,
			const float scale = 1 / 12.0f,
			const float lengthRadiusRatio = 0.25f
		) const noexcept{
			return std::ranges::fold_left(components, 1.0, [mass, scale, lengthRadiusRatio](const double val, const hitbox_comp& pair){
				return val + pair.box.get_rotational_inertia(mass, scale, lengthRadiusRatio) + mass * pair.trans.vec.length2();
			});
		}

		hitbox(const hitbox& other)
			: ccd_hitbox{other}{}

		hitbox(hitbox&& other) noexcept
			: ccd_hitbox{std::move(other)}{}

		hitbox& operator=(const hitbox& other){
			if(this == &other) return *this;
			ccd_hitbox::operator =(other);
			return *this;
		}

		hitbox& operator=(hitbox&& other) noexcept{
			if(this == &other) return *this;
			ccd_hitbox::operator =(std::move(other));
			return *this;
		}
	};

	//
	// template <std::derived_from<HitBoxComponent> T>
	// void flipX(std::vector<T>& datas){
	// 	const std::size_t size = datas.size();
	//
	// 	std::ranges::copy(datas, std::back_inserter(datas));
	//
	// 	for(int i = 0; i < size; ++i){
	// 		auto& dst = datas.at(i + size);
	// 		dst.trans.vec.y *= -1;
	// 		dst.trans.rot *= -1;
	// 		dst.trans.rot = math::Angle::getAngleInPi2(dst.trans.rot);
	// 		dst.box.offset.y *= -1;
	// 		dst.box.sizeVec2.y *= -1;
	// 	}
	// }
	//
	// void scl(std::vector<HitBoxComponent>& datas, const float scl){
	// 	for (auto& data : datas){
	// 		data.box.offset *= scl;
	// 		data.box.sizeVec2 *= scl;
	// 		data.trans.vec *= scl;
	// 	}
	// }
	//
	// void flipX(HitBox& datas){
	// 	flipX(datas.hitBoxGroup);
	// }
	//
	// void read(const OS::File& file, HitBox& box){
	// 	box.hitBoxGroup.clear();
	// 	int size = 0;
	//
	// 	std::ifstream reader{file.getPath(), std::ios::binary | std::ios::in};
	//
	// 	reader.read(reinterpret_cast<char*>(&size), sizeof(size));
	//
	// 	box.hitBoxGroup.resize(size);
	//
	// 	for (auto& data : box.hitBoxGroup){
	// 		data.read(reader);
	// 	}
	//
	// }
}
//
// export template<>
// struct ext::json::JsonSerializator<Geom::RectBox>{
// 	static void write(ext::json::JsonValue& jsonValue, const Geom::RectBox& data){
// 		ext::json::append(jsonValue, ext::json::keys::Size2D, data.sizeVec2);
// 		ext::json::append(jsonValue, ext::json::keys::Offset, data.offset);
// 		ext::json::append(jsonValue, ext::json::keys::Trans, data.transform);
// 	}
//
// 	static void read(const ext::json::JsonValue& jsonValue, Geom::RectBox& data){
// 		ext::json::read(jsonValue, ext::json::keys::Size2D, data.sizeVec2);
// 		ext::json::read(jsonValue, ext::json::keys::Offset, data.offset);
// 		ext::json::read(jsonValue, ext::json::keys::Trans, data.transform);
// 	}
// };
//
// export template<>
// struct ext::json::JsonSerializator<Game::HitBoxComponent>{
// 	static void write(ext::json::JsonValue& jsonValue, const Game::HitBoxComponent& data){
// 		ext::json::append(jsonValue, ext::json::keys::Trans, data.trans);
// 		ext::json::append(jsonValue, ext::json::keys::Data, data.box);
// 	}
//
// 	static void read(const ext::json::JsonValue& jsonValue, Game::HitBoxComponent& data){
// 		ext::json::read(jsonValue, ext::json::keys::Trans, data.trans);
// 		ext::json::read(jsonValue, ext::json::keys::Data, data.box);
// 	}
// };
//
// export template<>
// struct ext::json::JsonSerializator<Game::HitBox>{
// 	static void write(ext::json::JsonValue& jsonValue, const Game::HitBox& data){
// 		ext::json::JsonValue boxData{};
// 		ext::json::JsonSrlContBase_vector<decltype(data.hitBoxGroup)>::write(boxData, data.hitBoxGroup);
//
// 		ext::json::append(jsonValue, ext::json::keys::Data, std::move(boxData));
// 		ext::json::append(jsonValue, ext::json::keys::Trans, data.trans);
// 	}
//
// 	static void read(const ext::json::JsonValue& jsonValue, Game::HitBox& data){
// 		const auto& map = jsonValue.asObject();
// 		const ext::json::JsonValue boxData = map.at(ext::json::keys::Data);
// 		ext::json::JsonSrlContBase_vector<decltype(data.hitBoxGroup)>::read(boxData, data.hitBoxGroup);
// 		ext::json::read(jsonValue, ext::json::keys::Trans, data.trans);
// 		data.updateHitbox(data.trans);
// 	}
// };
