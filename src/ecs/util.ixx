export module mo_yanxi.game.ecs.util;

export import mo_yanxi.game.ecs.component.manage;

import mo_yanxi.meta_programming;
import std;

namespace mo_yanxi::game::ecs::util{
	template <typename T>
	struct try_access_component : std::type_identity<std::remove_reference_t<T>>{
	};

	template <typename T>
	using try_access_component_t = typename try_access_component<T>::type;

	template <typename Tuple, typename Entity, std::size_t... Idx>
	[[nodiscard]] auto get_components(Entity& entity, std::index_sequence<Idx...>) noexcept{
		return std::tuple<std::tuple_element_t<Idx, Tuple>*...>{
			entity.template try_get<std::tuple_element_t<Idx, Tuple>>()...
		};
	}

	template <typename Tuple>
	[[nodiscard]] bool all_components_present(const Tuple& components) noexcept{
		return [&]<std::size_t... Idx>(std::index_sequence<Idx...>) noexcept{
			return ((std::get<Idx>(components) != nullptr) && ...);
		}(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
	}

	template <typename Fn, typename Tuple, std::size_t... Idx>
	void invoke_with_components(Fn&& fn, Tuple components, std::index_sequence<Idx...>){
		std::invoke(std::forward<Fn>(fn), *std::get<Idx>(components)...);
	}

	template <typename Entity>
	concept has_entity_valid_id = requires(Entity&& entity){
		{ std::forward<Entity>(entity).valid_id() } -> std::same_as<entity_id>;
	};

	template <typename Entity>
	concept has_entity_id = requires(Entity&& entity){
		{ std::forward<Entity>(entity).id() } -> std::same_as<entity_id>;
	};

	template <typename Entity>
	concept entity_id_resolvable = std::same_as<std::remove_cvref_t<Entity>, entity_id>
		|| ::mo_yanxi::game::ecs::util::has_entity_valid_id<Entity>
		|| ::mo_yanxi::game::ecs::util::has_entity_id<Entity>
		|| std::convertible_to<Entity&&, entity_id>;

	template <typename Entity>
		requires ::mo_yanxi::game::ecs::util::entity_id_resolvable<Entity&&>
	[[nodiscard]] entity_id to_entity_id(Entity&& entity) noexcept{
		if constexpr (std::same_as<std::remove_cvref_t<Entity>, entity_id>){
			return entity;
		}else if constexpr (requires{ std::forward<Entity>(entity).valid_id(); }){
			return std::forward<Entity>(entity).valid_id();
		}else if constexpr (requires{ std::forward<Entity>(entity).id(); }){
			return std::forward<Entity>(entity).id();
		}else if constexpr (std::convertible_to<Entity&&, entity_id>){
			return static_cast<entity_id>(std::forward<Entity>(entity));
		}
	}
}

export namespace mo_yanxi::game::ecs::util{
	template <typename Fn>
	void try_access(const entity_id& entity, Fn&& fn){
		if(!entity.is_inserted()){
			return;
		}

		using fn_params = ::mo_yanxi::remove_mfptr_this_args<std::remove_cvref_t<Fn>>;
		using components = ::mo_yanxi::unary_apply_to_tuple_t<try_access_component_t, fn_params>;
		static_assert([]<std::size_t... Idx>(std::index_sequence<Idx...>) consteval{
			return (std::is_object_v<std::tuple_element_t<Idx, components>> && ...);
		}(std::make_index_sequence<std::tuple_size_v<components>>{}));

		auto component_ptrs = ::mo_yanxi::game::ecs::util::get_components<components>(
			entity,
			std::make_index_sequence<std::tuple_size_v<components>>{});

		if(!::mo_yanxi::game::ecs::util::all_components_present(component_ptrs)){
			return;
		}

		::mo_yanxi::game::ecs::util::invoke_with_components(
			std::forward<Fn>(fn),
			component_ptrs,
			std::make_index_sequence<std::tuple_size_v<components>>{});
	}

	template <typename Entity, typename Fn>
		requires (!std::same_as<std::remove_cvref_t<Entity>, entity_id>
			&& ::mo_yanxi::game::ecs::util::entity_id_resolvable<Entity&&>)
	void try_access(Entity&& entity, Fn&& fn){
		entity_id id = ::mo_yanxi::game::ecs::util::to_entity_id(std::forward<Entity>(entity));
		::mo_yanxi::game::ecs::util::try_access(id, std::forward<Fn>(fn));
	}

	template <typename Entity, typename Fn>
	void try_access(Entity* entity, Fn&& fn){
		if(entity == nullptr){
			return;
		}

		::mo_yanxi::game::ecs::util::try_access(*entity, std::forward<Fn>(fn));
	}
}
