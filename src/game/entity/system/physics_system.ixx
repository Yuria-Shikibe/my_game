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
			math::frect broadphase_aabb{};
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
			physics::contact_manifold manifold{};
		};

		struct contact_constraint_point{
			math::vec2 point{};
			math::vec2 lhs_offset{};
			math::vec2 rhs_offset{};
			float depth{};
			float normal_mass{};
			float tangent_mass{};
			float normal_impulse{};
			float tangent_impulse{};
			float velocity_bias{};
		};

		struct contact_constraint{
			std::size_t lhs{};
			std::size_t rhs{};
			physics_contact_key key{};
			math::vec2 normal{1.f, 0.f};
			math::vec2 tangent{0.f, 1.f};
			math::vec2 lhs_initial_position{};
			math::vec2 rhs_initial_position{};
			float friction{};
			std::array<contact_constraint_point, 2> points{};
			std::uint8_t point_count{};
		};

		struct cached_contact_point{
			math::vec2 lhs_local{};
			math::vec2 rhs_local{};
			float normal_impulse{};
			float tangent_impulse{};
		};

		struct contact_cache_entry{
			math::vec2 normal{1.f, 0.f};
			std::array<cached_contact_point, 2> points{};
			std::uint8_t point_count{};
		};

		struct spatial_entry{
			entity_pin id_pin{};
			math::frect aabb{};
			math::vec2 position{};
			float radius{};

			[[nodiscard]] bool valid() const noexcept{
				return id_pin.is_inserted();
			}
		};

		physics::dynamic_bvh<std::size_t> broadphase_{0.1f};
		std::vector<physics_proxy> proxies_{};
		std::vector<candidate_pair> candidate_pairs_{};
		std::vector<contact_constraint> contact_constraints_{};
		std::vector<spatial_entry> spatial_entries_{};
		std::vector<physics_contact_event> contact_events_{};
		std::flat_set<physics_contact_key> previous_contacts_{};
		std::flat_set<physics_contact_key> current_contacts_{};
		std::unordered_map<physics_contact_key, contact_cache_entry> contact_cache_{};
		std::unordered_map<physics_contact_key, contact_cache_entry> next_contact_cache_{};

		static constexpr float drag_linear_stop_speed{0.005f};
		static constexpr float drag_angular_stop_speed{0.005f};
		static constexpr float restitution_velocity_threshold{1.f};
		static constexpr float contact_cache_match_distance{0.25f};
		static constexpr float contact_cache_normal_dot{0.85f};
		static constexpr float contact_normal_reuse_dot{0.98f};
		static constexpr std::size_t parallel_proxy_threshold{512};
		static constexpr std::size_t parallel_pair_threshold{128};

		[[nodiscard]] static math::uniform_trans2 to_transform(const mech_motion& motion) noexcept{
			return motion.trans;
		}

		[[nodiscard]] static math::trans2 shape_transform(const collider& collider, const math::trans2 motion_transform) noexcept{
			return collider.local_transform >> motion_transform;
		}

		[[nodiscard]] static math::frect swept_aabb(math::frect aabb, const math::vec2 displacement) noexcept{
			if(!displacement.is_zero()){
				auto swept = aabb;
				swept.move(displacement);
				aabb.expand_by(swept);
			}
			return aabb;
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

			if(proxies_.size() >= parallel_proxy_threshold){
				//TODO replace this with reusable thread pool
				std::for_each(std::execution::par, proxies_.begin(), proxies_.end(), [dt](physics_proxy& proxy){
					physics_system::integrate_proxy(proxy, dt);
				});
			}else{
				std::ranges::for_each(proxies_, [dt](physics_proxy& proxy){
					physics_system::integrate_proxy(proxy, dt);
				});
			}
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
				spatial_entries_.push_back({
					.id_pin = proxy.id,
					.aabb = aabb,
					.position = proxy.motion->pos(),
					.radius = proxy.collider->shape.radius_bound()
				});
				(void)broadphase_.create_proxy(aabb, index);
			}
		}

		void rebuild_broadphase(){
			broadphase_.clear();
			for(std::size_t i = 0; i != proxies_.size(); ++i){
				auto& proxy = proxies_[i];
				const auto displacement = proxy.previous_motion_transform.vec - proxy.current_motion_transform.vec;
				proxy.broadphase_aabb = physics_system::swept_aabb(proxy.aabb, displacement);
				(void)broadphase_.create_proxy(proxy.broadphase_aabb, i);
			}
		}

		[[nodiscard]] static bool candidate_needs_ccd(const physics_proxy& lhs, const physics_proxy& rhs) noexcept{
			return lhs.collider->wants_ccd(*lhs.body, *lhs.motion) || rhs.collider->wants_ccd(*rhs.body, *rhs.motion);
		}

		[[nodiscard]] static bool candidate_can_solve(const physics_proxy& lhs, const physics_proxy& rhs) noexcept{
			return lhs.body->body.is_dynamic() || rhs.body->body.is_dynamic();
		}

		[[nodiscard]] physics::contact_result stabilize_contact_normal(
			const physics_proxy& lhs,
			const physics_proxy& rhs,
			physics::contact_result contact) const{
			if(!contact.hit){
				return contact;
			}

			const auto key = physics_contact_key::ordered(lhs.id, rhs.id);
			const auto cached = contact_cache_.find(key);
			if(cached == contact_cache_.end() || cached->second.point_count == 0){
				return contact;
			}

			auto normal = contact.normal;
			if(normal.length2() > 0.f){
				normal.normalize();
			}else{
				return contact;
			}

			const auto cached_normal = cached->second.normal;
			if(normal.dot(cached_normal) >= contact_normal_reuse_dot){
				contact.normal = cached_normal;
			}
			return contact;
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
				if(!lhs.broadphase_aabb.overlap_exclusive(rhs.broadphase_aabb)){
					return;
				}

				candidate_pairs_.push_back({
					.lhs = lhs_index,
					.rhs = rhs_index,
					.sensor = lhs.collider->filter.sensor || rhs.collider->filter.sensor,
					.solve = lhs.collider->filter.should_solve_with(rhs.collider->filter)
						&& physics_system::candidate_can_solve(lhs, rhs),
					.ccd = candidate_needs_ccd(lhs, rhs)
				});
			});
		}

		void run_narrowphase(){
			const auto solve_pair = [this](candidate_pair& pair){
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
					if(pair.hit){
						const auto contact = this->stabilize_contact_normal(lhs, rhs, toi.contact);
						pair.manifold = physics::build_contact_manifold(
							lhs.collider->shape,
							math::lerp(lhs.previous_shape_transform, lhs.current_shape_transform, pair.toi),
							rhs.collider->shape,
							math::lerp(rhs.previous_shape_transform, rhs.current_shape_transform, pair.toi),
							contact);
						pair.hit = pair.manifold.hit;
					}
					return;
				}

				const auto contact = this->stabilize_contact_normal(lhs, rhs, physics::collide(
					lhs.collider->shape,
					lhs.current_shape_transform,
					rhs.collider->shape,
					rhs.current_shape_transform));
				pair.manifold = physics::build_contact_manifold(
					lhs.collider->shape,
					lhs.current_shape_transform,
					rhs.collider->shape,
					rhs.current_shape_transform,
					contact);
				pair.hit = pair.manifold.hit;
			};

			if(candidate_pairs_.size() >= parallel_pair_threshold){
				std::for_each(std::execution::par, candidate_pairs_.begin(), candidate_pairs_.end(), solve_pair);
			}else{
				std::for_each(candidate_pairs_.begin(), candidate_pairs_.end(), solve_pair);
			}
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

		[[nodiscard]] static math::vec2 local_contact_point(const physics_proxy& proxy, const math::vec2 point) noexcept{
			return point << static_cast<math::trans2>(proxy.motion->trans);
		}

		static void apply_velocity_impulse(
			const physics_proxy& lhs,
			const physics_proxy& rhs,
			const contact_constraint_point& point,
			const math::vec2 impulse) noexcept{
			lhs.body->body.apply_impulse(lhs.motion->vel, -impulse, point.lhs_offset);
			rhs.body->body.apply_impulse(rhs.motion->vel, impulse, point.rhs_offset);
		}

		static void apply_position_impulse(
			const physics_proxy& proxy,
			const math::vec2 impulse,
			const math::vec2 contact_offset) noexcept{
			const auto& body = proxy.body->body;
			if(!body.is_dynamic()){
				return;
			}

			proxy.motion->trans.vec += impulse * body.inverse_mass;
			proxy.motion->trans.rot += contact_offset.cross(impulse) * body.inverse_rotational_inertia;
		}

		void transfer_cached_impulses(contact_constraint& constraint) const{
			const auto cached = contact_cache_.find(constraint.key);
			if(cached == contact_cache_.end() || cached->second.point_count == 0){
				return;
			}
			if(constraint.normal.dot(cached->second.normal) < contact_cache_normal_dot){
				return;
			}

			const auto& lhs = proxies_[constraint.lhs];
			const auto& rhs = proxies_[constraint.rhs];
			const float match_distance2 = contact_cache_match_distance * contact_cache_match_distance;
			std::array<bool, 2> used{};
			for(std::uint8_t point_index = 0; point_index != constraint.point_count; ++point_index){
				auto& point = constraint.points[point_index];
				const auto lhs_local = physics_system::local_contact_point(lhs, point.point);
				const auto rhs_local = physics_system::local_contact_point(rhs, point.point);
				std::uint8_t best_index{};
				float best_distance = std::numeric_limits<float>::infinity();
				bool matched{};
				for(std::uint8_t cached_index = 0; cached_index != cached->second.point_count; ++cached_index){
					if(used[cached_index]){
						continue;
					}
					const auto& cached_point = cached->second.points[cached_index];
					const float lhs_distance = (lhs_local - cached_point.lhs_local).length2();
					const float rhs_distance = (rhs_local - cached_point.rhs_local).length2();
					if(lhs_distance > match_distance2 || rhs_distance > match_distance2){
						continue;
					}
					const float distance = lhs_distance + rhs_distance;
					if(distance < best_distance){
						best_distance = distance;
						best_index = cached_index;
						matched = true;
					}
				}
				if(!matched){
					continue;
				}

				used[best_index] = true;
				point.normal_impulse = cached->second.points[best_index].normal_impulse;
				point.tangent_impulse = cached->second.points[best_index].tangent_impulse;
			}
		}

		[[nodiscard]] contact_constraint make_contact_constraint(const candidate_pair& pair){
			const auto& lhs = proxies_[pair.lhs];
			const auto& rhs = proxies_[pair.rhs];
			const auto& lhs_body = lhs.body->body;
			const auto& rhs_body = rhs.body->body;
			auto normal = pair.manifold.normal;
			if(normal.length2() > 0.f){
				normal.normalize();
			}else{
				normal = {1.f, 0.f};
			}

			contact_constraint constraint{
				.lhs = pair.lhs,
				.rhs = pair.rhs,
				.key = physics_contact_key::ordered(lhs.id, rhs.id),
				.normal = normal,
				.tangent = {-normal.y, normal.x},
				.lhs_initial_position = lhs.motion->trans.vec,
				.rhs_initial_position = rhs.motion->trans.vec,
				.friction = std::sqrt(std::max(lhs_body.friction * rhs_body.friction, 0.f)),
				.point_count = pair.manifold.point_count
			};

			const float restitution = std::min(lhs_body.restitution, rhs_body.restitution);
			for(std::uint8_t point_index = 0; point_index != constraint.point_count; ++point_index){
				const auto& manifold_point = pair.manifold.points[point_index];
				auto& point = constraint.points[point_index];
				point.point = manifold_point.point;
				point.depth = std::max(manifold_point.depth, 0.f);
				point.lhs_offset = point.point - lhs.motion->trans.vec;
				point.rhs_offset = point.point - rhs.motion->trans.vec;

				const auto normal_inverse_mass = physics_system::inverse_effective_mass(
					lhs_body,
					rhs_body,
					point.lhs_offset,
					point.rhs_offset,
					constraint.normal);
				if(normal_inverse_mass > 0.f && std::isfinite(normal_inverse_mass)){
					point.normal_mass = 1.f / normal_inverse_mass;
				}

				const auto tangent_inverse_mass = physics_system::inverse_effective_mass(
					lhs_body,
					rhs_body,
					point.lhs_offset,
					point.rhs_offset,
					constraint.tangent);
				if(tangent_inverse_mass > 0.f && std::isfinite(tangent_inverse_mass)){
					point.tangent_mass = 1.f / tangent_inverse_mass;
				}

				const auto relative_velocity = rhs.motion->vel_at(point.rhs_offset) - lhs.motion->vel_at(point.lhs_offset);
				const float velocity_along_normal = relative_velocity.dot(constraint.normal);
				if(velocity_along_normal < -restitution_velocity_threshold){
					point.velocity_bias = -restitution * velocity_along_normal;
				}
			}

			this->transfer_cached_impulses(constraint);
			return constraint;
		}

		void build_contact_constraints(){
			contact_constraints_.clear();
			contact_constraints_.reserve(candidate_pairs_.size());
			for(const auto& pair : candidate_pairs_){
				if(!pair.hit || !pair.solve || pair.manifold.point_count == 0){
					continue;
				}

				contact_constraints_.push_back(this->make_contact_constraint(pair));
			}
		}

		void warm_start_contact_constraints(){
			for(auto& constraint : contact_constraints_){
				const auto& lhs = proxies_[constraint.lhs];
				const auto& rhs = proxies_[constraint.rhs];
				for(std::uint8_t point_index = 0; point_index != constraint.point_count; ++point_index){
					const auto& point = constraint.points[point_index];
					const auto impulse = constraint.normal * point.normal_impulse
						+ constraint.tangent * point.tangent_impulse;
					physics_system::apply_velocity_impulse(lhs, rhs, point, impulse);
				}
			}
		}

		static void solve_velocity_constraint(
			const physics_proxy& lhs,
			const physics_proxy& rhs,
			contact_constraint& constraint) noexcept{
			for(std::uint8_t point_index = 0; point_index != constraint.point_count; ++point_index){
				auto& point = constraint.points[point_index];
				auto relative_velocity = rhs.motion->vel_at(point.rhs_offset) - lhs.motion->vel_at(point.lhs_offset);
				const float normal_speed = relative_velocity.dot(constraint.normal);
				const float normal_delta = point.normal_mass * (-normal_speed + point.velocity_bias);
				const float old_normal_impulse = point.normal_impulse;
				point.normal_impulse = std::max(old_normal_impulse + normal_delta, 0.f);
				const auto normal_impulse = constraint.normal * (point.normal_impulse - old_normal_impulse);
				physics_system::apply_velocity_impulse(lhs, rhs, point, normal_impulse);

				relative_velocity = rhs.motion->vel_at(point.rhs_offset) - lhs.motion->vel_at(point.lhs_offset);
				const float tangent_speed = relative_velocity.dot(constraint.tangent);
				const float tangent_delta = point.tangent_mass * -tangent_speed;
				const float max_tangent_impulse = constraint.friction * point.normal_impulse;
				const float old_tangent_impulse = point.tangent_impulse;
				point.tangent_impulse = std::clamp(
					old_tangent_impulse + tangent_delta,
					-max_tangent_impulse,
					max_tangent_impulse);
				const auto tangent_impulse = constraint.tangent * (point.tangent_impulse - old_tangent_impulse);
				physics_system::apply_velocity_impulse(lhs, rhs, point, tangent_impulse);
			}
		}

		void solve_velocity_constraints(){
			for(auto& constraint : contact_constraints_){
				physics_system::solve_velocity_constraint(
					proxies_[constraint.lhs],
					proxies_[constraint.rhs],
					constraint);
			}
		}

		static void solve_position_constraint(
			const physics_proxy& lhs,
			const physics_proxy& rhs,
			const contact_constraint& constraint) noexcept{
			constexpr float slop = 0.01f;
			constexpr float percent = 0.2f;
			constexpr float max_correction = 2.f;

			for(std::uint8_t point_index = 0; point_index != constraint.point_count; ++point_index){
				const auto& point = constraint.points[point_index];
				const float current_depth = point.depth
					+ (lhs.motion->trans.vec - constraint.lhs_initial_position).dot(constraint.normal)
					- (rhs.motion->trans.vec - constraint.rhs_initial_position).dot(constraint.normal);
				const float correction_depth = std::max(current_depth - slop, 0.f);
				if(correction_depth <= 0.f){
					continue;
				}

				const auto lhs_offset = point.point - lhs.motion->trans.vec;
				const auto rhs_offset = point.point - rhs.motion->trans.vec;
				const auto inverse_mass = physics_system::inverse_effective_mass(
					lhs.body->body,
					rhs.body->body,
					lhs_offset,
					rhs_offset,
					constraint.normal);
				if(inverse_mass <= 0.f || !std::isfinite(inverse_mass)){
					continue;
				}

				const float correction = std::min(correction_depth * percent, max_correction);
				const auto impulse = constraint.normal * (correction / inverse_mass);
				physics_system::apply_position_impulse(lhs, -impulse, lhs_offset);
				physics_system::apply_position_impulse(rhs, impulse, rhs_offset);
			}
		}

		void solve_position_constraints(){
			for(const auto& constraint : contact_constraints_){
				physics_system::solve_position_constraint(
					proxies_[constraint.lhs],
					proxies_[constraint.rhs],
					constraint);
			}
		}

		void store_contact_cache(){
			next_contact_cache_.clear();
			next_contact_cache_.reserve(contact_constraints_.size());
			for(const auto& constraint : contact_constraints_){
				const auto& lhs = proxies_[constraint.lhs];
				const auto& rhs = proxies_[constraint.rhs];
				if(lhs.solved_toi < 1.f || rhs.solved_toi < 1.f){
					continue;
				}
				contact_cache_entry entry{
					.normal = constraint.normal,
					.point_count = constraint.point_count
				};
				for(std::uint8_t point_index = 0; point_index != constraint.point_count; ++point_index){
					const auto& point = constraint.points[point_index];
					entry.points[point_index] = {
						.lhs_local = physics_system::local_contact_point(lhs, point.point),
						.rhs_local = physics_system::local_contact_point(rhs, point.point),
						.normal_impulse = point.normal_impulse,
						.tangent_impulse = point.tangent_impulse
					};
				}
				next_contact_cache_.insert_or_assign(constraint.key, entry);
			}
			contact_cache_.swap(next_contact_cache_);
		}

		[[nodiscard]] static physics_contact_endpoint_snapshot make_endpoint_snapshot(const physics_proxy& proxy) noexcept{
			return {
				.id_pin = proxy.id,
				.previous_motion = proxy.previous_motion_transform,
				.current_motion = proxy.current_motion_transform,
				.previous_shape = proxy.previous_shape_transform,
				.current_shape = proxy.current_shape_transform,
				.filter = proxy.collider->filter
			};
		}

		void emit_event(const candidate_pair& pair, const physics_contact_phase phase){
			const auto& lhs = proxies_[pair.lhs];
			const auto& rhs = proxies_[pair.rhs];
			const auto key = physics_contact_key::ordered(lhs.id, rhs.id);
			const auto contact = pair.manifold.representative();
			contact_events_.push_back({
				.key = key,
				.phase = phase,
				.subject = lhs.id,
				.object = rhs.id,
				.subject_endpoint = physics_system::make_endpoint_snapshot(lhs),
				.object_endpoint = physics_system::make_endpoint_snapshot(rhs),
				.sensor = pair.sensor,
				.toi = pair.toi,
				.depth = contact.depth,
				.normal = contact.normal,
				.point = contact.point
			});
		}

		void solve_and_emit_contacts(const float dt){
			constexpr unsigned velocity_iterations = 6;
			constexpr unsigned position_iterations = 3;

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

			this->build_contact_constraints();
			this->warm_start_contact_constraints();
			for(unsigned i = 0; i != velocity_iterations; ++i){
				this->solve_velocity_constraints();
			}

			for(unsigned i = 0; i != position_iterations; ++i){
				this->solve_position_constraints();
			}
			this->store_contact_cache();

			for(const auto& proxy : proxies_){
				physics_system::advance_dynamic_after_toi(proxy, dt);
			}

			for(const auto key : previous_contacts_){
				if(current_contacts_.contains(key)){
					continue;
				}
				contact_events_.push_back({
					.key = key,
					.phase = physics_contact_phase::end,
					.subject = key.first(),
					.object = key.second()
				});
			}

			previous_contacts_.swap(current_contacts_);
		}

	public:
		void clear(){
			broadphase_.clear();
			proxies_.clear();
			candidate_pairs_.clear();
			contact_constraints_.clear();
			spatial_entries_.clear();
			contact_events_.clear();
			previous_contacts_.clear();
			current_contacts_.clear();
			contact_cache_.clear();
			next_contact_cache_.clear();
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
			contact_constraints_.clear();
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
					.id_pin = proxy.id_pin,
					.aabb = proxy.aabb,
					.position_snapshot = proxy.position,
					.radius_snapshot = proxy.radius
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
