module;

#include <cassert>

export module mo_yanxi.game.ecs.system.physics;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.component.physics;

import std;

namespace mo_yanxi::game::ecs::system{
	export
	struct physics_system{
	private:
		struct physics_proxy{
			entity_id id{};
			collider* collider{};
			mech_motion* motion{};
			physics_body* body{};
			math::uniform_trans2 previous_motion_transform{};
			math::uniform_trans2 current_motion_transform{};
			math::trans2 previous_shape_transform{};
			math::trans2 current_shape_transform{};
			math::frect aabb{};
			float solved_toi{1.f};
		};

		struct candidate_pair{
			std::size_t lhs{};
			std::size_t rhs{};
			bool sensor{};
			bool solve{};
			bool ccd{};
			bool hit{};
			float toi{1.f};
			physics::contact_result contact{};
		};

		struct spatial_entry{
			entity_pin id_pin{};
			entity_id id{};
			math::frect aabb{};
			math::vec2 position{};
			float radius{};

			[[nodiscard]] spatial_entry() = default;

			[[nodiscard]] spatial_entry(
				const entity_id id,
				const math::frect aabb,
				const math::vec2 position,
				const float radius) noexcept
				: id_pin(id),
				  id(id_pin.raw_id()),
				  aabb(aabb),
				  position(position),
				  radius(radius){
			}

			[[nodiscard]] bool valid() const noexcept{
				return id_pin.is_inserted();
			}
		};

		physics::dynamic_bvh<std::size_t> broadphase_{0.1f};
		std::vector<physics_proxy> proxies_{};
		std::vector<candidate_pair> candidate_pairs_{};
		std::vector<spatial_entry> spatial_entries_{};
		std::vector<physics_contact_event> contact_events_{};
		std::flat_set<physics_contact_key> previous_contacts_{};
		std::flat_set<physics_contact_key> current_contacts_{};
		static constexpr float drag_linear_stop_speed{0.005f};
		static constexpr float drag_angular_stop_speed{0.005f};

		[[nodiscard]] static math::uniform_trans2 to_transform(const mech_motion& motion) noexcept{
			return motion.trans;
		}

		[[nodiscard]] static math::trans2 shape_transform(const collider& collider, const math::trans2 motion_transform) noexcept{
			return collider.local_transform >> motion_transform;
		}

		static void integrate_proxy(physics_proxy& proxy, const float dt) noexcept{
			auto& motion = *proxy.motion;
			auto& body = proxy.body->body;
			proxy.previous_motion_transform = to_transform(motion);

			if(body.type == physics::body_type::static_body){
				motion.accel = {};
			}else{
				if(body.is_dynamic()){
					motion.vel += motion.accel * dt;
					motion.vel.vec += body.force * body.inverse_mass * dt;
					motion.vel.rot += body.torque * body.inverse_rotational_inertia * dt;
				}
				motion.trans += motion.vel * dt;
				if(body.is_dynamic()){
					motion.vel.vec.lerp_inplace({}, std::clamp(body.linear_drag * dt, 0.f, 1.f));
					motion.vel.rot.slerp(0, std::clamp(body.angular_drag * dt, 0.f, 1.f));
					if(body.linear_drag > 0.f && motion.vel.vec.length2() <= drag_linear_stop_speed * drag_linear_stop_speed){
						motion.vel.vec = {};
					}
					if(body.angular_drag > 0.f && std::abs(static_cast<float>(motion.vel.rot)) <= drag_angular_stop_speed){
						motion.vel.rot = 0.f;
					}
				}
				motion.accel = {};
			}

			body.clear_forces();
			proxy.current_motion_transform = to_transform(motion);
			proxy.previous_shape_transform = shape_transform(*proxy.collider, static_cast<math::trans2>(proxy.previous_motion_transform));
			proxy.current_shape_transform = shape_transform(*proxy.collider, static_cast<math::trans2>(proxy.current_motion_transform));
			proxy.aabb = proxy.collider->shape.aabb(proxy.current_shape_transform);
		}

		void collect_proxies(component_manager& manager, const float dt){
			proxies_.clear();
			manager.sliced_each([&](
				const chunk_meta& meta,
				collider& collider,
				mech_motion& motion,
				physics_body& body){
				if(!meta.id() || !meta.id()->is_inserted() || !collider.enabled || collider.shape.empty()){
					return;
				}

				proxies_.push_back({
					.id = meta.id(),
					.collider = &collider,
					.motion = &motion,
					.body = &body
				});
			});

			//TODO replace this with reusable thread pool
			std::for_each(std::execution::par, proxies_.begin(), proxies_.end(), [dt](physics_proxy& proxy){
				physics_system::integrate_proxy(proxy, dt);
			});
		}

		void rebuild_spatial_cache(){
			spatial_entries_.clear();
			spatial_entries_.reserve(proxies_.size());
			broadphase_.clear();

			for(const auto& proxy : proxies_){
				if(!proxy.id || !proxy.id->is_inserted()){
					continue;
				}

				const auto motion_transform = to_transform(*proxy.motion);
				const auto current_shape_transform = shape_transform(*proxy.collider, static_cast<math::trans2>(motion_transform));
				const auto aabb = proxy.collider->shape.aabb(current_shape_transform);
				const auto index = spatial_entries_.size();
				spatial_entries_.emplace_back(
					proxy.id,
					aabb,
					proxy.motion->pos(),
					proxy.collider->shape.radius_bound());
				(void)broadphase_.create_proxy(aabb, index);
			}
		}

		void rebuild_broadphase(){
			broadphase_.clear();
			for(std::size_t i = 0; i != proxies_.size(); ++i){
				const auto& proxy = proxies_[i];
				const auto displacement = proxy.previous_motion_transform.vec - proxy.current_motion_transform.vec;
				(void)broadphase_.create_proxy(proxy.aabb, i, displacement);
			}
		}

		[[nodiscard]] static bool candidate_needs_ccd(const physics_proxy& lhs, const physics_proxy& rhs) noexcept{
			return lhs.collider->wants_ccd(*lhs.body, *lhs.motion) || rhs.collider->wants_ccd(*rhs.body, *rhs.motion);
		}

		void collect_candidate_pairs(){
			candidate_pairs_.clear();
			broadphase_.collect_pairs([&](physics::bvh_proxy_id, const std::size_t lhs_index,
			                              physics::bvh_proxy_id, const std::size_t rhs_index){
				const auto& lhs = proxies_[lhs_index];
				const auto& rhs = proxies_[rhs_index];
				if(lhs.id == rhs.id || !lhs.collider->filter.can_collide_with(rhs.collider->filter)){
					return;
				}

				candidate_pairs_.push_back({
					.lhs = lhs_index,
					.rhs = rhs_index,
					.sensor = lhs.collider->filter.sensor || rhs.collider->filter.sensor,
					.solve = lhs.collider->filter.should_solve_with(rhs.collider->filter),
					.ccd = candidate_needs_ccd(lhs, rhs)
				});
			});
		}

		void run_narrowphase(){
			std::for_each(std::execution::par, candidate_pairs_.begin(), candidate_pairs_.end(), [this](candidate_pair& pair){
				const auto& lhs = proxies_[pair.lhs];
				const auto& rhs = proxies_[pair.rhs];

				if(pair.ccd){
					const auto toi = physics::linear_time_of_impact(
						lhs.collider->shape,
						lhs.previous_shape_transform,
						lhs.current_shape_transform,
						rhs.collider->shape,
						rhs.previous_shape_transform,
						rhs.current_shape_transform);
					pair.hit = toi.hit;
					pair.toi = toi.fraction;
					pair.contact = toi.contact;
					return;
				}

				pair.contact = physics::collide(
					lhs.collider->shape,
					lhs.current_shape_transform,
					rhs.collider->shape,
					rhs.current_shape_transform);
				pair.hit = pair.contact.hit;
			});
		}

		static void rewind_dynamic_to_toi(physics_proxy& proxy, const float toi) noexcept{
			if(toi >= proxy.solved_toi || !proxy.body->body.is_dynamic()){
				return;
			}

			proxy.solved_toi = std::clamp(toi, 0.f, 1.f);
			proxy.motion->trans = math::lerp(
				proxy.previous_motion_transform,
				proxy.current_motion_transform,
				proxy.solved_toi);
		}

		static void advance_dynamic_after_toi(const physics_proxy& proxy, const float dt) noexcept{
			if(proxy.solved_toi >= 1.f || !proxy.body->body.is_dynamic()){
				return;
			}

			const float remaining_dt = dt * std::clamp(1.f - proxy.solved_toi, 0.f, 1.f);
			proxy.motion->trans += proxy.motion->vel * remaining_dt;
		}

		[[nodiscard]] static math::vec2 contact_normal(const physics::contact_result& contact) noexcept{
			auto normal = contact.normal;
			if(normal.length2() > 0.f){
				normal.normalize();
			}else{
				normal = {1.f, 0.f};
			}
			return normal;
		}

		[[nodiscard]] static float inverse_effective_mass(
			const physics::rigid_body& lhs_body,
			const physics::rigid_body& rhs_body,
			const math::vec2 lhs_offset,
			const math::vec2 rhs_offset,
			const math::vec2 axis) noexcept{
			const auto lhs_rot = lhs_offset.cross(axis);
			const auto rhs_rot = rhs_offset.cross(axis);
			return lhs_body.inverse_mass + rhs_body.inverse_mass
				+ lhs_rot * lhs_rot * lhs_body.inverse_rotational_inertia
				+ rhs_rot * rhs_rot * rhs_body.inverse_rotational_inertia;
		}

		static void solve_velocity_impulse(const physics_proxy& lhs, const physics_proxy& rhs, const physics::contact_result& contact) noexcept{
			auto& lhs_body = lhs.body->body;
			auto& rhs_body = rhs.body->body;
			const float inverse_mass_sum = lhs_body.inverse_mass + rhs_body.inverse_mass;
			if(inverse_mass_sum <= 0.f){
				return;
			}

			const auto normal = physics_system::contact_normal(contact);
			const auto lhs_offset = contact.point - lhs.motion->trans.vec;
			const auto rhs_offset = contact.point - rhs.motion->trans.vec;
			auto relative_velocity = rhs.motion->vel_at(rhs_offset) - lhs.motion->vel_at(lhs_offset);
			const auto velocity_along_normal = relative_velocity.dot(normal);
			float normal_impulse_scalar{};
			if(velocity_along_normal < 0.f){
				const auto restitution = std::min(lhs_body.restitution, rhs_body.restitution);
				const auto normal_mass = physics_system::inverse_effective_mass(lhs_body, rhs_body, lhs_offset, rhs_offset, normal);
				if(normal_mass <= 0.f || !std::isfinite(normal_mass)){
					return;
				}

				normal_impulse_scalar = -(1.f + restitution) * velocity_along_normal / normal_mass;
				const auto impulse = normal * normal_impulse_scalar;
				lhs_body.apply_impulse(lhs.motion->vel, -impulse, lhs_offset);
				rhs_body.apply_impulse(rhs.motion->vel, impulse, rhs_offset);
			}

			if(normal_impulse_scalar <= 0.f){
				return;
			}

			relative_velocity = rhs.motion->vel_at(rhs_offset) - lhs.motion->vel_at(lhs_offset);
			auto tangent_velocity = relative_velocity - normal * relative_velocity.dot(normal);
			if(tangent_velocity.length2() <= 0.f){
				return;
			}

			tangent_velocity.normalize();
			const auto tangent_mass = physics_system::inverse_effective_mass(lhs_body, rhs_body, lhs_offset, rhs_offset, tangent_velocity);
			if(tangent_mass <= 0.f || !std::isfinite(tangent_mass)){
				return;
			}

			const auto tangent_speed = relative_velocity.dot(tangent_velocity);
			const auto tangent_impulse_scalar = -tangent_speed / tangent_mass;
			const auto friction = std::sqrt(std::max(lhs_body.friction * rhs_body.friction, 0.f));
			const auto tangent_impulse_limit = friction * normal_impulse_scalar;
			const auto tangent_impulse = tangent_velocity * std::clamp(
				tangent_impulse_scalar,
				-tangent_impulse_limit,
				tangent_impulse_limit);
			lhs_body.apply_impulse(lhs.motion->vel, -tangent_impulse, lhs_offset);
			rhs_body.apply_impulse(rhs.motion->vel, tangent_impulse, rhs_offset);
		}

		static void solve_position_correction(const physics_proxy& lhs, const physics_proxy& rhs, const physics::contact_result& contact) noexcept{
			auto& lhs_body = lhs.body->body;
			auto& rhs_body = rhs.body->body;
			const float inverse_mass_sum = lhs_body.inverse_mass + rhs_body.inverse_mass;
			if(inverse_mass_sum <= 0.f){
				return;
			}

			const auto normal = physics_system::contact_normal(contact);
			constexpr float slop = 0.01f;
			constexpr float percent = 0.65f;
			const auto correction_depth = std::max(contact.depth - slop, 0.f);
			if(correction_depth <= 0.f){
				return;
			}

			const auto correction = normal * (correction_depth / inverse_mass_sum * percent);
			if(lhs_body.is_dynamic()){
				lhs.motion->trans.vec -= correction * lhs_body.inverse_mass;
			}
			if(rhs_body.is_dynamic()){
				rhs.motion->trans.vec += correction * rhs_body.inverse_mass;
			}
		}

		void emit_event(const candidate_pair& pair, const physics_contact_phase phase){
			const auto& lhs = proxies_[pair.lhs];
			const auto& rhs = proxies_[pair.rhs];
			const auto key = physics_contact_key::ordered(lhs.id, rhs.id);
			contact_events_.emplace_back(
				key,
				phase,
				lhs.id,
				rhs.id,
				pair.sensor,
				pair.toi,
				pair.contact.depth,
				pair.contact.normal,
				pair.contact.point);
		}

		void solve_and_emit_contacts(const float dt){
			constexpr unsigned velocity_iterations = 4;

			current_contacts_.clear();
			contact_events_.clear();

			for(auto& proxy : proxies_){
				proxy.solved_toi = 1.f;
			}

			for(const auto& pair : candidate_pairs_){
				if(!pair.hit){
					continue;
				}

				auto& lhs = proxies_[pair.lhs];
				auto& rhs = proxies_[pair.rhs];
				const auto key = physics_contact_key::ordered(lhs.id, rhs.id);
				current_contacts_.insert(key);
				emit_event(pair, previous_contacts_.contains(key)
					? physics_contact_phase::stay
					: physics_contact_phase::begin);

				if(pair.solve){
					physics_system::rewind_dynamic_to_toi(lhs, pair.toi);
					physics_system::rewind_dynamic_to_toi(rhs, pair.toi);
				}
			}

			for(unsigned i = 0; i != velocity_iterations; ++i){
				for(const auto& pair : candidate_pairs_){
					if(!pair.hit || !pair.solve){
						continue;
					}

					physics_system::solve_velocity_impulse(proxies_[pair.lhs], proxies_[pair.rhs], pair.contact);
				}
			}

			for(const auto& pair : candidate_pairs_){
				if(!pair.hit || !pair.solve){
					continue;
				}

				physics_system::solve_position_correction(proxies_[pair.lhs], proxies_[pair.rhs], pair.contact);
			}

			for(const auto& proxy : proxies_){
				physics_system::advance_dynamic_after_toi(proxy, dt);
			}

			for(const auto key : previous_contacts_){
				if(current_contacts_.contains(key)){
					continue;
				}
				contact_events_.emplace_back(
					key,
					physics_contact_phase::end,
					key.first,
					key.second);
			}

			previous_contacts_ = current_contacts_;
		}

	public:
		void clear(){
			broadphase_.clear();
			proxies_.clear();
			candidate_pairs_.clear();
			spatial_entries_.clear();
			contact_events_.clear();
			previous_contacts_.clear();
			current_contacts_.clear();
		}

		void step(component_manager& manager){
			const auto dt = manager.get_update_delta();
			collect_proxies(manager, dt);
			rebuild_broadphase();
			collect_candidate_pairs();
			run_narrowphase();
			solve_and_emit_contacts(dt);
			rebuild_spatial_cache();
			proxies_.clear();
			candidate_pairs_.clear();
		}

		void run(component_manager& manager){
			step(manager);
		}

		[[nodiscard]] std::span<const physics_contact_event> contact_events() const noexcept{
			return contact_events_;
		}

		template <typename Fn>
		void spatial_query(const math::frect region, Fn&& fn) const{
			physics::bvh_query_workspace workspace{};
			broadphase_.query(region, [&](physics::bvh_proxy_id, const std::size_t proxy_index){
				const auto& proxy = spatial_entries_[proxy_index];
				if(!proxy.valid() || !proxy.aabb.overlap_exclusive(region)){
					return false;
				}

				physics_query_result result{
					proxy.id_pin,
					proxy.aabb,
					proxy.position,
					proxy.radius
				};

				if constexpr(std::is_invocable_r_v<bool, Fn, const physics_query_result&>){
					return std::invoke(fn, result);
				}else{
					std::invoke(fn, result);
					return false;
				}
			}, workspace);
		}
	};
}
