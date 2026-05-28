module;

#include <gch/small_vector.hpp>
#include "mo_yanxi/adapted_attributes.hpp"

export module mo_yanxi.game.ecs.component_operation_task_graph;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.ecs.dependency_generator;
export import mo_yanxi.game.ecs.task_graph;

import mo_yanxi.concepts;
import std;


namespace mo_yanxi::game::ecs{
	// export
	// struct component_operation_task_graph{
	// 	struct resource_idt{
	// 		archetype_slice archetype;
	//
	// 		[[nodiscard]] std::size_t hash_code() const noexcept{
	// 			static constexpr std::hash<std::string_view> hash{};
	// 			const auto bits = std::bit_cast<std::array<char, sizeof(archetype_slice)>>(archetype);
	// 			return hash(std::string_view{bits.data(), bits.size()});
	// 		}
	//
	// 		friend bool operator==(const resource_idt& lhs, const resource_idt& rhs) noexcept {
	// 			return lhs.archetype == rhs.archetype && lhs.archetype.get_slice_generator() == rhs.archetype.get_slice_generator();
	// 		}
	//
	// 		constexpr auto operator<=>(const resource_idt& o) const noexcept{
	// 			auto rst = archetype <=> o.archetype;
	// 			if(rst == std::strong_ordering::equal){
	// 				return std::compare_three_way{}(
	// 					reinterpret_cast<const void*>(archetype.get_slice_generator()),
	// 					reinterpret_cast<const void*>(o.archetype.get_slice_generator())
	// 					);
	// 			}
	// 			return rst;
	// 		}
	// 	};
	//
	// 	using res_idt = concurrent::resource_atomic_capture<resource_idt>;
	//
	// private:
	// 	component_manager* component_manager{};
	// 	concurrent::operation_dependencies_generator<resource_idt> dependencies_generator{};
	// 	concurrent::no_fail_task_graph task_graph{};
	//
	// 	template <typename Fn>
	// 	FORCE_INLINE static void attach_to_slice_func(
	// 		concurrent::task_context& ctx,
	// 		ecs::component_manager& comp_manager,
	// 		Fn&& fn){
	//
	// 		using fn_params = mo_yanxi::remove_mfptr_this_args<std::decay_t<Fn>>;
	// 		constexpr bool has_task_context = std::same_as<concurrent::task_context, std::remove_cvref_t<std::tuple_element_t<0, fn_params>>>;
	//
	//
	// 		if constexpr(has_task_context){
	// 			comp_manager.sliced_each(std::bind_front(fn, ctx));
	// 		} else{
	// 			comp_manager.sliced_each(fn);
	// 		}
	// 	}
	// public:
	// 	[[nodiscard]] component_operation_task_graph() = default;
	//
	// 	[[nodiscard]] explicit(false) component_operation_task_graph(ecs::component_manager& component_manager)
	// 		: component_manager(&component_manager){
	// 	}
	//
	// 	template <typename T>
	// 		requires std::is_enum_v<T> || std::integral<T>
	// 	void reserve_event_id(T id_seq_begin){
	// 		dependencies_generator.set_initial_auto_event_id(static_cast<concurrent::event_id>(id_seq_begin));
	// 	}
	//
	// 	template <typename ComponentTuple>
	// 	void get_captures_of(std::vector<res_idt>& rst) const{
	// 		using raw_params = ComponentTuple;
	// 		using decayed_params = unary_apply_to_tuple_t<std::remove_cvref_t, raw_params>;
	// 		using operation_type_params = unary_apply_to_tuple_t<unwrap_first_element_t, decayed_params>;
	// 		using op_extracted_params =
	// 			unary_apply_to_tuple_t<readonly_const_decay_t,
	// 				binary_apply_to_tuple_t<copy_qualifier_t,
	// 				raw_params,
	// 				operation_type_params
	// 			>>;
	//
	// 		op_extracted_params tuple{};
	//
	// 		component_manager->slice_and_then<operation_type_params>([&rst](
	// 			const std::array<archetype_slice, std::tuple_size_v<op_extracted_params>>& idts){
	//
	// 				auto push = [&]<std::size_t idx>(const archetype_slice& slice){
	// 					rst.push_back({
	// 							slice,
	// 							concurrent::resource_usage::read |
	// 							(std::is_const_v<std::tuple_element_t<idx, op_extracted_params>>
	// 								? concurrent::resource_usage::none
	// 								: concurrent::resource_usage::write)
	// 						});
	// 				};
	//
	// 				[&]<std::size_t ...Idx>(std::index_sequence<Idx...>){
	// 					(push.template operator()<Idx>(idts[Idx]), ...);
	// 				}(std::make_index_sequence<std::tuple_size_v<op_extracted_params>>{});
	// 		});
	// 	}
	//
	// 	template <typename ComponentTuple>
	// 	[[nodiscard]] std::vector<res_idt> get_captures_of() const{
	// 		std::vector<res_idt> rst{};
	// 		rst.reserve(16);
	//
	// 		get_captures_of<ComponentTuple>(rst);
	//
	// 		return rst;
	// 	}
	//
	// 	template <
	// 		std::ranges::input_range DepRng = std::initializer_list<concurrent::event_id>,
	// 		typename Fn>
	// 	concurrent::event_id run(std::optional<concurrent::event_id> id, DepRng&& depRng, Fn&& fn){
	// 		using fn_params = remove_mfptr_this_args<Fn>;
	//
	// 		//TODO replace this: pick all used params
	// 		using comps = std::tuple<>;//get_used_components<fn_params>;
	//
	// 		constexpr bool has_task_context = std::same_as<concurrent::task_context, std::remove_cvref_t<std::tuple_element_t<0, fn_params>>>;
	//
	// 		auto res_cap = this->get_captures_of<comps>();
	// 		auto eid = dependencies_generator.add_operation(id, std::move(res_cap), std::forward<DepRng>(depRng));
	//
	// 		task_graph.add_task(eid, {}, [&comp_manager = *component_manager, fn = std::forward<Fn>(fn)](concurrent::task_context& ctx){
	// 			component_operation_task_graph::attach_to_slice_func(ctx, comp_manager, fn);
	// 		}, !has_task_context);
	//
	// 		return eid;
	// 	}
	//
	// 	template <
	// 		std::ranges::input_range DepRng = std::initializer_list<concurrent::event_id>,
	// 		typename ...Fn>
	// 		requires (sizeof...(Fn) > 1)
	// 	concurrent::event_id run(std::optional<concurrent::event_id> id, DepRng&& depRng, Fn&& ...fn){
	// 		std::vector<res_idt> resource_atomic_captures{};
	//
	// 		bool has_manually_context = false;
	//
	// 		[&] <std::size_t ...Idx>(std::index_sequence<Idx...>){
	// 			([&]<typename F>(){
	// 				using fn_params = remove_mfptr_this_args<Fn>;
	// 				//TODO replace this: pick all used params
	// 				using comps = std::tuple<>;//get_used_components<fn_params>;
	// 				constexpr bool has_task_context = std::same_as<concurrent::task_context, std::remove_cvref_t<std::tuple_element_t<0, fn_params>>>;
	// 				has_manually_context |= has_task_context;
	//
	// 				const auto sz = resource_atomic_captures.size();
	// 				this->get_captures_of<comps>(resource_atomic_captures);
	// 				const auto rng_partial = resource_atomic_captures.begin() + sz;
	// 				std::ranges::sort(rng_partial, resource_atomic_captures.end(), std::ranges::less{}, &res_idt::id_base);
	// 				std::ranges::inplace_merge(resource_atomic_captures.begin(), rng_partial, resource_atomic_captures.end(), std::ranges::less{}, &res_idt::id_base);
	// 			}.template operator()<Fn>(), ...);
	// 		}(std::index_sequence_for<Fn...>{});
	//
	// 		auto eid = dependencies_generator.add_operation(id, std::move(resource_atomic_captures), std::forward<DepRng>(depRng));
	//
	// 		task_graph.add_task(eid, {}, [&comp_manager = *component_manager, ...fn = std::forward<Fn>(fn)](concurrent::task_context& ctx){
	// 			(component_operation_task_graph::attach_to_slice_func(ctx, comp_manager, fn), ...);
	// 		}, !has_manually_context);
	//
	// 		return eid;
	// 	}
	//
	// 	template <
	// 		typename EID,
	// 		std::ranges::input_range DepRng = std::initializer_list<concurrent::event_id>,
	// 		typename Fn>
	// 		requires (std::is_scoped_enum_v<EID>)
	// 	concurrent::event_id run(EID id, DepRng&& depRng, Fn&& fn){
	// 		return this->run(static_cast<concurrent::event_id>(id), std::forward<DepRng>(depRng), std::forward<Fn>(fn));
	// 	}
	//
	// 	template <
	// 		typename EID,
	// 		std::ranges::input_range DepRng = std::initializer_list<concurrent::event_id>,
	// 		typename ...Fn>
	// 		requires (std::is_scoped_enum_v<EID> && (sizeof...(Fn) > 1))
	// 	concurrent::event_id run(EID id, DepRng&& depRng, Fn&& ...fn){
	// 		return this->run(static_cast<concurrent::event_id>(id), std::forward<DepRng>(depRng), std::forward<Fn>(fn) ...);
	// 	}
	//
	// 	template <typename Fn>
	// 	concurrent::event_id run(Fn&& fn){
	// 		return this->run({}, std::forward<Fn>(fn));
	// 	}
	//
	// 	void generate_dependencies(){
	// 		auto deps = dependencies_generator.get_dependencies();
	// 		for (auto&& [task, pre] : deps){
	// 			task_graph.set_dependency(task, pre | std::views::transform([this](concurrent::event_id eid){
	// 				return &task_graph.at(eid);
	// 			}));
	// 		}
	// 	}
	//
	// 	[[nodiscard]] auto get_sorted_task_group() const{
	// 		return task_graph.to_sorted_group();
	// 	}
	// };
}
