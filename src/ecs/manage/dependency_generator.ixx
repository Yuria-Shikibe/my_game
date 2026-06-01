module;

#include "mo_yanxi/enum_operator_gen.hpp"
#include "mo_yanxi/adapted_attributes.hpp"

export module mo_yanxi.game.ecs.dependency_generator;

import std;

namespace mo_yanxi::concurrent{
	export
	enum struct resource_usage : unsigned{

		/**
		 * @brief No resource operations
		 */
		none = 0u,

		/**
		 * @brief Resource is read
		 */
		read = 1u << 0,

		/**
		 * @brief Resource is modified
		 */
		write = 1u << 1,

		read_write = read | write,

		/**
		 * @brief Resources could be used thread safely, if not set, resource is exclusive
		 */
		sharable = 1u << 2,
	};

	BITMASK_OPS(export, resource_usage);

	export using event_id = std::size_t;

	export
	struct resource_capture_state{
		resource_usage usage{};

		[[nodiscard]] constexpr bool has_exclusive_write() const noexcept{
			return (usage & resource_usage::write) != resource_usage{} && (usage & resource_usage::sharable) == resource_usage{};
		}

		[[nodiscard]] bool race_with(const resource_capture_state& capture) const noexcept{
			return has_exclusive_write() || capture.has_exclusive_write();
		}
	};

	export
	template <typename T>
	struct resource_identity{
		using id_type = T;
		id_type id{};

		[[nodiscard]] constexpr resource_identity() = default;

		template <typename Raw>
			requires (sizeof(Raw) == sizeof(id_type) && std::is_trivially_copyable_v<Raw>)
		 [[nodiscard]] constexpr explicit(false) resource_identity(const Raw& val) noexcept : id{std::bit_cast<id_type, Raw>(val)} {}

		[[nodiscard]] constexpr explicit(false) resource_identity(id_type id)
			: id(id){
		}

		constexpr friend bool operator==(const resource_identity& lhs, const resource_identity& rhs) noexcept = default;
	};
}


template <typename T>
struct std::hash<mo_yanxi::concurrent::resource_identity<T>>{
	static constexpr std::size_t operator()(const mo_yanxi::concurrent::resource_identity<T>& idt) noexcept{
		if constexpr (requires(const T& id){
			{ id.hash_code() } -> std::convertible_to<std::size_t>;
		}){
			return idt.id.hash_code();
		}else if constexpr (requires(const T& id){
			{ id.hash_value() } -> std::convertible_to<std::size_t>;
		}){
			return idt.id.hash_value();
		}else{
			static constexpr auto hasher = std::hash<typename mo_yanxi::concurrent::resource_identity<T>::id_type>{};
			return hasher(idt.id);
		}
	}
};


namespace mo_yanxi::concurrent{
	export
	template <typename T>
	struct resource_atomic_capture{
		resource_identity<T> resource{};
		resource_capture_state state{};

		constexpr const T& id_base() const noexcept{
			return resource.id;
		}
	};

	template <typename T>
	struct event_capture_stamp{
		std::vector<event_id> pre_events{};
		std::vector<resource_atomic_capture<T>> captures{};
	};

	// using T = std::size_t;
	export
	template <typename T>
	struct operation_dependencies_generator{
	private:
		event_id event_id_counter{};

		event_id get_new_event_id() noexcept{
			auto eid = event_id_counter++;

			while(event_dependencies.contains(eid)){
				eid = event_id_counter++;
			}

			return eid;
		}

		std::unordered_map<event_id, event_capture_stamp<T>> event_dependencies{};

	public:

		void set_initial_auto_event_id(std::size_t initial_inclusive = 0) noexcept{
			event_id_counter = initial_inclusive;
		}


		template <
			std::ranges::forward_range CapRng = std::initializer_list<resource_atomic_capture<T>>,
			std::ranges::input_range DepRng = std::initializer_list<event_id>
		>
		requires requires{
			requires std::convertible_to<std::ranges::range_value_t<CapRng>, resource_atomic_capture<T>>;
			requires std::convertible_to<std::ranges::range_value_t<DepRng>, event_id>;
		}
		event_id add_operation(std::optional<event_id> eid, CapRng&& caps, DepRng&& dependencies = {}){
			auto id = eid.value_or(get_new_event_id());

			auto [rst, suc] = event_dependencies.try_emplace(id,
				event_capture_stamp{
					dependencies | std::ranges::to<std::vector<event_id>>(),
					caps | std::ranges::to<std::vector<resource_atomic_capture<T>>>(),
				});

			if(!suc){
				throw std::runtime_error{"Duplicate event id"};
			}

			return id;
		}

		std::unordered_map<event_id, std::unordered_set<event_id>> get_dependencies() const{
			auto all_events_view = event_dependencies | std::views::keys;

			std::unordered_map<event_id, std::unordered_set<event_id>> result{};

			std::unordered_map<event_id, std::unordered_set<event_id>> reachable{};
			result.reserve(event_dependencies.size());
			reachable.reserve(event_dependencies.size());

			auto add_reachable = [&](this auto&& self, event_id event) -> std::unordered_set<event_id>&{

				auto [itr, suc] = reachable.try_emplace(event);
				if(!suc)return itr->second;

				auto& pres = event_dependencies.at(event).pre_events;
				for (event_id pre_event : pres){
					auto& pre_reachable = self(pre_event);
					itr->second.insert_range(pre_reachable);
				}

				itr->second.insert_range(pres);

				return itr->second;
			};

			for (const auto & [event, dep] : event_dependencies){
				add_reachable(event);
			}

			for (const auto & [event, dep] : event_dependencies){
				auto& cur_node = result[event];
				cur_node.insert_range(dep.pre_events);

				for (const auto& other : all_events_view){
					if(other == event)continue;

					//get resource dependencies
					if(reachable.at(other).contains(event))continue;
					if(auto itr = result.find(other); itr != result.end()){
						if(itr->second.contains(event))continue;
					}

					auto& other_dep = event_dependencies.at(other);

					for (auto && [res, capture] : other_dep.captures){
						if(auto itr = std::ranges::find(dep.captures, res, &resource_atomic_capture<T>::resource); itr != dep.captures.end()){
							if(itr->state.race_with(capture)){
								cur_node.insert(other);
								break;
							}
						}
					}

				}
			}

			return result;
		}
	};
}
