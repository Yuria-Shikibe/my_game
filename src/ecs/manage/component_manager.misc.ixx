//
// Created by Matrix on 2025/7/24.
//

export module mo_yanxi.game.ecs.component.manage:misc;

import :serializer;
import mo_yanxi.type_register;
import mo_yanxi.concepts;
import mo_yanxi.seq_chunk;
import mo_yanxi.strided_span;
import mo_yanxi.utility;
import std;

namespace mo_yanxi::game::ecs{
	struct spin_lock{
	private:
		std::binary_semaphore semaphore{1};

	public:
		void lock() noexcept{
			semaphore.acquire();
		}

		void unlock() noexcept{
			semaphore.release();
		}

		[[nodiscard]] bool try_lock() noexcept{
			return semaphore.try_acquire();
		}
	};

	export struct component_manager;
	template <typename T = spin_lock>
	struct no_propagation_mutex : T{
		[[nodiscard]] no_propagation_mutex() = default;

		no_propagation_mutex(const no_propagation_mutex& other) noexcept{
			mo_yanxi::assert_unlocked(other);
		}

		no_propagation_mutex(no_propagation_mutex&& other) noexcept{
			mo_yanxi::assert_unlocked(other);
		}

		no_propagation_mutex& operator=(const no_propagation_mutex& other) noexcept{
			mo_yanxi::assert_unlocked(other);
			return *this;
		}

		no_propagation_mutex& operator=(no_propagation_mutex&& other) noexcept{
			mo_yanxi::assert_unlocked(other);
			return *this;
		}
	};

	export
	template <typename TupleT>
	struct archetype;

	struct staging_add_base{
		virtual ~staging_add_base() = default;
		[[nodiscard]] virtual type_identity_index get_idx() const noexcept = 0;
		virtual void add_archetype(component_manager& manager) const noexcept = 0;
		virtual void push_entity(component_manager& manager) noexcept = 0;
	};

	export struct entity;
	export using entity_id = entity*;

	export
	struct chunk_meta{

		template <typename TupleT>
		friend struct archetype;
		friend component_manager;

	private:
		entity_id eid_{};

	public:
		[[nodiscard]] constexpr chunk_meta() = default;

		[[nodiscard]] constexpr explicit(false) chunk_meta(ecs::entity_id entity_id)
			noexcept : eid_(entity_id){
		}

		[[nodiscard]] constexpr entity_id id() const noexcept{
			return eid_;
		}
	};



	export
	struct archetype_base{
	protected:
		template <typename T>
		[[nodiscard]] static constexpr chunk_meta& meta_of(T& comp) noexcept{
			return comp.template get<chunk_meta>();
		}

		template <typename T>
		[[nodiscard]] static constexpr const chunk_meta& meta_of(const T& comp) noexcept{
			return comp.template get<chunk_meta>();
		}

	public:
		virtual ~archetype_base() = default;

		virtual void reserve(std::size_t sz){

		}

		virtual std::size_t insert(const entity_id entity);

		virtual void erase(const entity_id entity);

		virtual void erase_at(const entity_id entity, std::size_t idx);

		virtual bool erase_staging(const entity_id entity) noexcept{
			return false;
		}

		[[nodiscard]] virtual entity_id entity_at(std::size_t idx) const noexcept = 0;

	protected:

		virtual void* get_chunk_partial_ptr(type_identity_index type, std::size_t idx) noexcept = 0;
		virtual strided_span<std::byte> get_staging_chunk_partial_slice(type_identity_index type) noexcept = 0;

	public:
		[[nodiscard]] virtual bool has_type(type_identity_index type) const = 0;

		[[nodiscard]] virtual std::unique_ptr<archetype_serializer> dump() const = 0;

		template <typename T>
		[[nodiscard]] bool has_type() const noexcept{
			if constexpr (std::same_as<T, chunk_meta>){
				return true;
			}else{
				return this->has_type(unstable_type_identity_of<T>());
			}
		}

		template <typename T>
		[[nodiscard]] T* try_get_comp(const std::size_t idx) noexcept{
			return static_cast<T*>(this->get_chunk_partial_ptr(unstable_type_identity_of<T>(), idx));
		}

		template <typename T>
		[[nodiscard]] strided_span<T> try_get_slice_of_staging() noexcept{
			auto span = this->get_staging_chunk_partial_slice(unstable_type_identity_of<T>());
			return strided_span<T>{reinterpret_cast<T*>(span.data()), span.size(), span.stride()};
		}

		template <typename T>
		[[nodiscard]] T* try_get_comp(entity_id id) noexcept;

		virtual void dump_staging() = 0;

		[[nodiscard]] virtual std::size_t size() const noexcept = 0;
	};



	template <typename Pair, typename Ty>
	struct find_if_first_equal : std::bool_constant<std::same_as<typename Pair::first_type, Ty>>{};


	export
	template <typename Tuple>
	concept entity_component_seq = is_tuple_v<Tuple> && std::derived_from<std::tuple_element_t<0, Tuple>, chunk_meta> && []{
		return [] <std::size_t ...Idx>(std::index_sequence<Idx...>){
			return (!std::derived_from<std::tuple_element_t<Idx + 1, Tuple>, chunk_meta> && ...);
		}(std::make_index_sequence<std::tuple_size_v<Tuple> - 1>{});
	}();

	export
	template <entity_component_seq Tuple>
	using tuple_to_comp_t = tuple_to_seq_chunk_t<tuple_cat_t<Tuple>>;

	template <typename T, typename D>
	struct dump_base{
		static void dump(D& srl_type, const T& value) requires (std::is_assignable_v<D&, T>){
			srl_type = value;
		}

		static void load(const D& srl_type, T& value) requires (std::is_assignable_v<T&, D>){
			value = srl_type;
		}
	};

	template <typename T>
	struct dump_base<T, void>{

	};

	template <typename T>
	using get_dump_type = decltype([]{
		if constexpr (requires{
			typename T::dump_type;
		}){
			return std::type_identity<typename T::dump_type>{};
		}else if constexpr (std::is_copy_assignable_v<T>){
			return std::type_identity<T>{};
		}else{
			return std::type_identity<void>{};
		}
	}())::type;

	export
	template <typename T, typename DumpTy = get_dump_type<T>>
	struct component_custom_behavior_base : dump_base<T, DumpTy>{
		using value_type = T;
		using dump_type = DumpTy;

		static void on_init(const chunk_meta& meta, value_type& comp) = delete;
		static void on_terminate(const chunk_meta& meta, value_type& comp) = delete;
		static void on_relocate(const chunk_meta& meta, value_type& comp) = delete;

		static void on_init(const chunk_meta& meta, const value_type& comp) = delete;
		static void on_terminate(const chunk_meta& meta, const value_type& comp) = delete;
		static void on_relocate(const chunk_meta& meta, const value_type& comp) = delete;
	};

	export
	template <typename T>
	struct component_custom_behavior : component_custom_behavior_base<T>{};

	template <>
	struct component_custom_behavior<chunk_meta> : component_custom_behavior_base<chunk_meta, void>{

	};

	template <typename T>
	using dump_type_to_tuple_t = std::conditional_t<
		std::same_as<typename component_custom_behavior<T>::dump_type, void>, std::tuple<>, std::tuple<
			typename component_custom_behavior<T>::dump_type>>;

	template <typename T>
	using unwrap_type = T::type;

	// export
	template <typename T>
	struct get_base_types{
		using direct_parent = decltype([]{
			if constexpr (requires{
				typename component_custom_behavior<T>::base_types;
			}){
				if constexpr (is_tuple_v<typename component_custom_behavior<T>::base_types>){
					return std::type_identity<typename component_custom_behavior<T>::base_types>{};
				}else{
					return std::type_identity<std::tuple<typename component_custom_behavior<T>::base_types>>{};
				}
			}else{
				return std::type_identity<std::tuple<>>{};
			}
		}())::type;

	private:
		using type_raw = tuple_cat_t<
			std::tuple<T>,
			all_apply_to<tuple_cat_t,
				unary_apply_to_tuple_t<unwrap_type,
					unary_apply_to_tuple_t<get_base_types, direct_parent>>>>;

		static constexpr bool should_contain_chunk_meta = !contained_in<chunk_meta, type_raw> && std::derived_from<T, chunk_meta>;
	public:

		using type = decltype([]{
			if constexpr (should_contain_chunk_meta){
				return std::type_identity<tuple_cat_t<std::tuple<chunk_meta>, type_raw>>{};
			}else{
				return std::type_identity<type_raw>{};
			}
		}())::type;

	};

	template <typename T>
	struct component_trait{
		using value_type = component_custom_behavior<T>::value_type;
		static_assert(std::same_as<T, value_type>);

		using dump_type = component_custom_behavior<T>::dump_type;

		static constexpr bool is_transient = std::is_void_v<dump_type>;

		using base_types = get_base_types<value_type>::type;

		static constexpr bool is_polymorphic = !requires{
			typename component_custom_behavior<T>::base_types;
		};

		static void on_init(const chunk_meta& meta, T& comp) {
			if constexpr (std::same_as<T, chunk_meta>)return;
			[&]<std::size_t ... I>(std::index_sequence<I...>){
				([&]<std::size_t J>{
					using Cur = std::tuple_element_t<J, base_types>;

					if constexpr (requires{
						component_custom_behavior<Cur>::on_init(meta, comp);
					}){
						component_custom_behavior<Cur>::on_init(meta, comp);
					}
				}.template operator()<I>(), ...);
			}(std::make_index_sequence<std::tuple_size_v<base_types>>());
		}

		static void on_terminate(const chunk_meta& meta, T& comp) {
			if constexpr (std::same_as<T, chunk_meta>)return;
			[&]<std::size_t ... I>(std::index_sequence<I...>){
				([&]<std::size_t J>{
					using Cur = std::tuple_element_t<J, base_types>;

					if constexpr (requires{
						component_custom_behavior<Cur>::on_terminate(meta, comp);
					}){
						component_custom_behavior<Cur>::on_terminate(meta, comp);
					}
				}.template operator()<I>(), ...);
			}(std::make_index_sequence<std::tuple_size_v<base_types>>());
		}

		static void on_relocate(const chunk_meta& meta, T& comp) {
			if constexpr (std::same_as<T, chunk_meta>)return;
			[&]<std::size_t ... I>(std::index_sequence<I...>){
				([&]<std::size_t J>{
					using Cur = std::tuple_element_t<J, base_types>;

					if constexpr (requires{
						component_custom_behavior<Cur>::on_relocate(meta, comp);
					}){
						component_custom_behavior<Cur>::on_relocate(meta, comp);
					}
				}.template operator()<I>(), ...);
			}(std::make_index_sequence<std::tuple_size_v<base_types>>());

		}
	};

	export
	template <typename TypeDesc, typename SrlIdx = int, SrlIdx = SrlIdx{}>
	struct archetype_custom_behavior_base{

		using raw_desc = TypeDesc;
		using value_type = tuple_to_comp_t<TypeDesc>;
		using dump_types = all_apply_to<tuple_cat_t, unary_apply_to_tuple_t<dump_type_to_tuple_t, TypeDesc>>;
		using dump_chunk = tuple_to_seq_chunk_t<dump_types>;

		[[nodiscard]] static dump_chunk dump(const value_type& comp){
			dump_chunk chunk{};
			[&]<std::size_t... I>(std::index_sequence<I...>){
				([&]<std::size_t Idx>(){
					using SrcTy = std::tuple_element_t<I, raw_desc>;
					using DstTy = component_trait<SrcTy>::dump_type;
					if constexpr (!component_trait<SrcTy>::is_transient){
						component_custom_behavior<SrcTy>::dump(get<DstTy>(chunk), get<SrcTy>(comp));
					}
				}.template operator()<I>(), ...);
			}(std::make_index_sequence<std::tuple_size_v<raw_desc>>{});
			return chunk;
		}

		[[nodiscard]] static value_type load(const dump_chunk& comp){
			value_type chunk{};
			[&]<std::size_t... I>(std::index_sequence<I...>){
				([&]<std::size_t Idx>(){
					using SrcTy = std::tuple_element_t<I, raw_desc>;
					using DstTy = component_trait<SrcTy>::dump_type;
					if constexpr (!component_trait<SrcTy>::is_transient){
						component_custom_behavior<SrcTy>::load(get<DstTy>(comp), get<SrcTy>(chunk));
					}
				}.template operator()<I>(), ...);
			}(std::make_index_sequence<std::tuple_size_v<raw_desc>>{});
			return chunk;
		}

		template <typename ...Ts>
		static auto get_unwrap_of(value_type& comp) noexcept{
			return [&] <std::size_t... I>(std::index_sequence<I...>){
				return std::tie(get<Ts>(comp) ...);
			}(std::index_sequence_for<Ts ...>{});
		}

		template <typename ...Ts>
		static auto get_unwrap_of(const value_type& comp) noexcept{
			return [&] <std::size_t... I>(std::index_sequence<I...>){
				return std::tie(get<Ts>(comp) ...);
			}(std::index_sequence_for<Ts ...>{});
		}

		constexpr static entity_id id_of_chunk(const value_type& comp) noexcept{
			return static_cast<const chunk_meta&>(get<chunk_meta>(comp)).id();
		}

		static void on_init(value_type& comp) = delete;
		static void on_terminate(value_type& comp) = delete;
		static void on_relocate(value_type& comp) = delete;

	};

	export
	template <typename TypeDesc>
	struct archetype_custom_behavior : archetype_custom_behavior_base<TypeDesc>{};

	export
	template <typename TypeDesc, archetype_serialize_identity id>
	struct archetype_serialize_info_base{
		using behavior = archetype_custom_behavior<TypeDesc>;
		using value_type = behavior::value_type;
		using dump_chunk = behavior::dump_chunk;

		static constexpr archetype_serialize_identity identity = id;
		static constexpr bool is_transient = (dump_chunk::chunk_element_count > 0 && id.index != archetype_serialize_identity::unknown);

		static srl::chunk_serialize_handle write(std::ostream& stream, const dump_chunk& chunk) noexcept {
			co_yield 0;
			co_return;
		}

		static void read(std::istream& stream, component_chunk_offset off, dump_chunk& chunk) noexcept {
			return;
		}
	};

	export
	template <typename TypeDesc>
	struct archetype_serialize_info : archetype_serialize_info_base<TypeDesc, archetype_serialize_identity{}>{
	};

	template <typename T>
	struct archetype_trait{
		using behavior = archetype_custom_behavior<T>;
		using value_type = behavior::value_type;
		static_assert(std::same_as<tuple_to_comp_t<T>, value_type>);

		using dump_chunk = behavior::dump_chunk;
		using serialize = archetype_serialize_info<T>;

		static constexpr unsigned expire_counter = []{
			if constexpr(requires{
				behavior::expire_counter;
			}){
				return behavior::expire_counter;
			} else{
				return 0;
			}
		}();

		static constexpr bool is_transient = serialize::is_transient;

		static void on_init(value_type& chunk) {
			if constexpr (requires{
				behavior::on_init(chunk);
			}){
				behavior::on_init(chunk);
			}
		}

		static void on_terminate(value_type& chunk) {
			if constexpr (requires{
				behavior::on_terminate(chunk);
			}){
				behavior::on_terminate(chunk);
			}
		}

		static void on_relocate(value_type& chunk) {
			if constexpr (requires{
				behavior::on_relocate(chunk);
			}){
				behavior::on_relocate(chunk);
			}
		}
	};

	template <typename L, typename R>
	struct type_pair{
		using first_type = L;
		using second_type = R;
	};

	struct type_comp_convert{
		using converter = std::add_pointer_t<void*(void*) noexcept>;
		converter comp_converter;

		[[nodiscard]] constexpr type_comp_convert() = default;

		template <typename Ty, typename MostDerivedTy, typename ChunkSeqTy>
		[[nodiscard]] constexpr type_comp_convert(std::in_place_type_t<Ty>, std::in_place_type_t<MostDerivedTy>, std::in_place_type_t<ChunkSeqTy>)
			noexcept :  comp_converter(+[](void* c) noexcept -> void*{
				auto& chunk = *static_cast<ChunkSeqTy*>(c);
				auto& most_derived_comp = chunk.template get<MostDerivedTy>();
				return static_cast<void*>(static_cast<Ty*>(std::addressof(most_derived_comp)));
			})  {}

		constexpr explicit operator bool() const noexcept{
			return comp_converter != nullptr;
		}


		template <typename T>
		[[nodiscard]] void* get(T& chunk) const noexcept{
			return comp_converter(std::addressof(chunk));
		}

		template <typename T>
		[[nodiscard]] void* get(T* chunk) const noexcept{
			return comp_converter(chunk);
		}
	};

	struct type_index_to_convertor{
		type_identity_index type{};
		type_comp_convert convertor{};
	};

	template <typename T>
	using derive_map_of_trait = decltype([]{
			using bases = component_trait<T>::base_types;
			return []<std::size_t ...Idx>(std::index_sequence<Idx...>){
				return std::type_identity<std::tuple<
					type_pair<std::tuple_element_t<Idx, bases>, T> ...
				>>{};
			}(std::make_index_sequence<std::tuple_size_v<bases>>{});
	}())::type;
}
