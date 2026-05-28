module;


export module mo_yanxi.game.meta.instancing;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.meta.projectile;

import mo_yanxi.game.ecs.entitiy_decleration;

import std;

namespace mo_yanxi::game::meta{
	template <typename Derive, typename Meta, typename EntityDesc>
	struct entity_create_handle{
		struct promise_type;
		using component_chunk_type = ecs::tuple_to_comp_t<EntityDesc>;
		using meta_info = Meta;
		using handle = std::coroutine_handle<promise_type>;


		[[nodiscard]] entity_create_handle() noexcept = default;

		[[nodiscard]] explicit entity_create_handle(handle&& hdl)
			: hdl{std::move(hdl)}{}

		struct promise_type{
			ecs::component_manager& manager;
			const meta_info& meta;
			component_chunk_type* components{};

			[[nodiscard]] promise_type() = default;

			[[nodiscard]] promise_type(ecs::component_manager& manager, const meta_info& metainfo)
				: manager{manager}, meta{metainfo}
			{

			}

			Derive get_return_object() noexcept{
				return Derive{entity_create_handle{handle::from_promise(*this)}};
			}

			[[nodiscard]] static auto initial_suspend() noexcept{ return std::suspend_never{}; }

			[[nodiscard]] static auto final_suspend() noexcept{ return std::suspend_always{}; }

			std::suspend_always yield_value(component_chunk_type& comp) noexcept{
				components = std::addressof(comp);
				return {};
			}

			static void return_void() noexcept{

			}

			[[noreturn]] static void unhandled_exception() noexcept{
				std::terminate();
			}

		private:
			friend entity_create_handle;

		};

		void resume() const{
			hdl.resume();
		}

		[[nodiscard]] bool done() const noexcept{
			return hdl.done();
		}

		entity_create_handle(const entity_create_handle& other) = delete;

		entity_create_handle(entity_create_handle&& other) noexcept
			: hdl{std::exchange(other.hdl, {})}{
		}

		entity_create_handle& operator=(const entity_create_handle& other) = delete;

		entity_create_handle& operator=(entity_create_handle&& other) noexcept{
			if(this == &other) return *this;
			dstry();
			hdl = std::exchange(other.hdl, {});
			return *this;
		}

		~entity_create_handle(){
			dstry();
		}

		[[nodiscard]] ecs::component_manager& get_target_manager() const noexcept{
			return hdl.promise().manager;
		}

		[[nodiscard]] component_chunk_type& get_components() const noexcept{
			return *hdl.promise().components;
		}

		[[nodiscard]] const meta_info& get_meta() const noexcept{
			return hdl.promise().meta;
		}

	private:
		void dstry() noexcept{
			if(hdl){
				if(!done()){
					resume();
				}
				hdl.destroy();
			}
		}
		handle hdl{};
	};


	struct projectile_create_handle : entity_create_handle<projectile_create_handle, meta::projectile, ecs::desc::projectile>{
		// using entity_create_handle::entity_create_handle;

		// [[nodiscard]] explicit projectile_create_handle(handle&& hdl)
		// 	: entity_create_handle(std::move(hdl)){
		// }
		//
		// [[nodiscard]] projectile_create_handle() = default;

		void set_initial_trans(math::trans2 where) const noexcept{
			get_components().get<ecs::mech_motion>().trans = where;
		}

		void set_initial_vel(float direction_angle_rad, const float vel_override, const float angular_override) const noexcept{
			get_components().get<ecs::mech_motion>().vel = {math::vec2::from_polar_rad(direction_angle_rad, vel_override), angular_override};
		}

		void set_initial_vel(const math::uniform_trans2& vel) const noexcept{
			get_components().get<ecs::mech_motion>().vel = vel;
		}

		void set_initial_vel(const float direction_angle_rad, const float angular_override = 0) const noexcept{
			set_initial_vel(direction_angle_rad, get_meta().initial_speed, angular_override);
		}

		void set_faction(const faction_ref faction) const noexcept{
			get_components().faction = faction;
		}

		void set_owner(const ecs::entity_id owner) const noexcept{
			get_components().owner = owner;
		}
	};

	export
	inline projectile_create_handle create(ecs::component_manager& manager, const meta::projectile& metainfo){
		using ret_t = projectile_create_handle;
		using comp_t = ret_t::component_chunk_type;
		comp_t comp;

		comp.get<ecs::manifold>().hitbox = game::hitbox{metainfo.hitbox};
		comp.get<ecs::physical_rigid>() = metainfo.rigid;
		comp.set_trail_style(metainfo.trail_style);
		comp.drawer = metainfo.drawer;
		comp.set_damage(metainfo.damage);
		comp.duration.set(metainfo.lifetime);


		co_yield manager.create_entity_deferred<ecs::desc::projectile>(std::move(comp));
	}
}