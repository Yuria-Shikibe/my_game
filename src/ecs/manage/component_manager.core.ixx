module;

#include <cassert>
#include "plf_hive.h"
#include "gch/small_vector.hpp"

#include "mo_yanxi/adapted_attributes.hpp"

#if DEBUG_CHECK
#define COMP_AT_CHECK
#endif

#if DEBUG_CHECK
#define CHECKED_STATIC_CAST(type) dynamic_cast<type>
#else
#define CHECKED_STATIC_CAST(type) static_cast<type>
#endif

// NOLINTBEGIN(*-misplaced-const)

export module mo_yanxi.game.ecs.component.manage:entity;

import :serializer;
export import mo_yanxi.seq_chunk;
import mo_yanxi.strided_span;
import mo_yanxi.soa_vector;

import mo_yanxi.game.srl;

import mo_yanxi.algo.hash;
import mo_yanxi.utility;
import mo_yanxi.concepts;
import mo_yanxi.meta_programming;
import mo_yanxi.heterogeneous.open_addr_hash;

import mo_yanxi.type_register;

import std;

import :misc;

namespace mo_yanxi::game::ecs{
	export struct bad_chunk_access : std::exception{
		[[nodiscard]] bad_chunk_access() = default;

		[[nodiscard]] explicit bad_chunk_access(char const* Message)
			: exception(Message){
		}
	};


	export using entity_data_chunk_index = std::vector<int>::size_type;
	export auto invalid_chunk_idx = std::numeric_limits<entity_data_chunk_index>::max();

	void attach_entity_to_archetype(entity_id entity, archetype_base* archetype, entity_data_chunk_index chunk_index) noexcept;
	void mark_entity_valid(entity_id entity, unsigned destroy_delay) noexcept;
	void update_entity_chunk_index(entity_id entity, entity_data_chunk_index chunk_index) noexcept;
	void detach_entity_from_archetype(entity_id entity) noexcept;

	export
	template <typename TupleT>
	struct archetype;

	export
	struct entity_ref;


	export
	template <entity_component_seq EntityChunkDesc, typename ChunkPartial>
		requires requires{
		requires contained_in<std::remove_cvref_t<ChunkPartial>, EntityChunkDesc>;
		}
	[[nodiscard]] constexpr auto chunk_of(ChunkPartial& value) noexcept -> decltype(*mo_yanxi::seq_chunk_cast<tuple_to_seq_chunk_t<EntityChunkDesc>>(&value)) {
		using Tup = tuple_to_seq_chunk_t<EntityChunkDesc>;
		decltype(auto) rst = mo_yanxi::seq_chunk_cast<Tup>(&value);
#if DEBUG_CHECK
		auto get_meta = [&]<typename C>() -> const chunk_meta& {
			return mo_yanxi::neighbour_of<chunk_meta, C>(value);
		};

		const entity_id eid = get_meta.template operator()<Tup>().id();
		assert(eid.is_inserted());
#endif
		return *rst;
	}

	export
	template <typename Tgt, entity_component_seq EntityChunkDesc, typename ChunkPartial>
		requires requires{
		requires contained_in<Tgt, EntityChunkDesc>;
		requires contained_in<std::remove_cvref_t<ChunkPartial>, EntityChunkDesc>;
		}
	[[nodiscard]] constexpr decltype(auto) chunk_neighbour_of(ChunkPartial& value) noexcept{
		return get<Tgt>(ecs::chunk_of<EntityChunkDesc>(value));
	}


	template <typename TupleT>
	struct archetype : archetype_base{
		using raw_tuple = TupleT;
		// using raw_tuple = std::tuple<chunk_meta>;

		using trait = archetype_trait<raw_tuple>;
		using appended_tuple = raw_tuple;
		using components = tuple_to_seq_chunk_t<appended_tuple>;
		static constexpr std::size_t chunk_comp_count = std::tuple_size_v<appended_tuple>;

		using storage_type = decltype([]<std::size_t... I>(std::index_sequence<I...>){
			return std::type_identity<soa_vector<
				std::allocator<std::byte>,
				std::tuple_element_t<I, appended_tuple>...>>{};
		}(std::make_index_sequence<chunk_comp_count>{}))::type;
		using storage_size_type = typename storage_type::size_type;

		using base_to_derive_map = all_apply_to<tuple_cat_t, unary_apply_to_tuple_t<derive_map_of_trait, appended_tuple>>;
		static constexpr std::size_t type_hash_map_size = std::tuple_size_v<base_to_derive_map>;

		struct type_access{
			type_identity_index type{};
			void* (*get)(archetype*, storage_size_type) noexcept{};
			strided_span<std::byte> (*slice)(archetype*) noexcept{};

			constexpr explicit operator bool() const noexcept{
				return get != nullptr && slice != nullptr;
			}
		};

		template <typename Requested, typename Stored>
		static void* get_component_ptr(archetype* self, storage_size_type idx) noexcept{
			auto& stored = self->chunks.template get<Stored>(idx);
			return static_cast<void*>(static_cast<Requested*>(std::addressof(stored)));
		}

		template <typename Requested, typename Stored>
		static strided_span<std::byte> get_component_slice(archetype* self) noexcept{
			if(self->chunks.empty()){
				return {};
			}

			auto* first = static_cast<Requested*>(
				std::addressof(self->chunks.template get<Stored>(typename storage_type::size_type{})));
			constexpr std::ptrdiff_t stride = std::same_as<Requested, Stored>
				? std::ptrdiff_t{}
				: static_cast<std::ptrdiff_t>(sizeof(Stored));
			return {
				reinterpret_cast<std::byte*>(first),
				self->chunks.size(),
				stride
			};
		}

		struct serializer : archetype_serializer{
			std::vector<typename trait::dump_chunk> buffer{};

			[[nodiscard]] archetype_serialize_identity get_identity() const noexcept final{
				return trait::serialize::identity;
			}


			void clear() noexcept final{
				buffer.clear();
			}

			void dump(const archetype& ty){
				buffer.reserve(buffer.size() + ty.size());

				for(std::size_t i = 0; i != ty.size(); ++i){
					auto seq_chunk = ty.make_component_row(i);
					buffer.push_back(trait::behavior::dump(seq_chunk));
				}
			}

			void write(std::ostream& stream) const final{
				std::uint32_t total_count = buffer.size();
				swapbyte_if_needed(total_count);

				try{
					stream.write(reinterpret_cast<char*>(&total_count), sizeof(total_count));
					for (const auto & seq_chunk : buffer){
						srl::chunk_serialize_handle hdl = trait::serialize::write(stream, seq_chunk);
						srl::srl_size off = hdl.get_offset();
						swapbyte_if_needed(off);
						stream.write(reinterpret_cast<char*>(&off), sizeof(off));

						hdl.resume();

						// if(hdl.get_state() != srl_state::succeed){
						// 	throw bad_archetype_serialize{"assertion failed: unfinished chunk serialize"};
						// }
					}
				}catch(const srl::srl_logical_error&){
					throw;
				}catch(...){
					throw srl::srl_logical_error{"serialize failed"};
				}


				//todo insert a check hash?
			}

			void read(std::istream& stream) final{
				std::uint32_t total_count;
				stream.read(reinterpret_cast<char*>(&total_count), sizeof(total_count));

				swapbyte_if_needed(total_count);

				buffer.resize(total_count);
				auto current = buffer.begin();
				std::ranges::range_size_t<decltype(buffer)> total_try_count{};

				while(total_try_count != total_count){
					auto last = stream.tellg();

					component_chunk_offset off;
					if(!stream.read(reinterpret_cast<char*>(&off), sizeof(off))){
						throw srl::srl_logical_error{"assertion failed: unfinished chunk serialize"};
					}
					swapbyte_if_needed(off);

					try{
						trait::serialize::read(stream, off, *current);
						++current;
					}catch(...){
						//TODO logit
						stream.seekg(last + static_cast<std::streamoff>(off));
					}

					++total_try_count;
				}

				buffer.erase(current, buffer.end());
			}

			void load(component_manager& target) const final;
		};

		using type_access_array_type = std::array<
				type_access,
				std::bit_ceil(type_hash_map_size * 2)>;

		//believe that this is faster than static
		const type_access_array_type type_access_hash_map = [] {
			type_access_array_type hash_array{};

			std::array<type_access, type_hash_map_size> temp{};
			[&] <std::size_t ...Idx>(std::index_sequence<Idx...>){
				((temp[Idx] = type_access{
					unstable_type_identity_of<typename std::tuple_element_t<Idx, base_to_derive_map>::first_type>(),
					&get_component_ptr<
						typename std::tuple_element_t<Idx, base_to_derive_map>::first_type,
						typename std::tuple_element_t<Idx, base_to_derive_map>::second_type>,
					&get_component_slice<
						typename std::tuple_element_t<Idx, base_to_derive_map>::first_type,
						typename std::tuple_element_t<Idx, base_to_derive_map>::second_type>
				}), ...);
			}(std::make_index_sequence<type_hash_map_size>{});

			algo::make_hash(hash_array, temp, &type_access::type);

			return hash_array;
		}();

	private:
		storage_type chunks{};

	protected:
		using first_component = std::tuple_element_t<0, raw_tuple>;

		template <typename ...Ts>
		static auto get_unwrap_of(components& comp) noexcept{
			return archetype_custom_behavior<raw_tuple>::template get_unwrap_of<Ts ...>(comp);
		}

		template <typename ...Ts>
		static auto get_unwrap_of(const components& comp) noexcept{
			return archetype_custom_behavior<raw_tuple>::template get_unwrap_of<Ts ...>(comp);
		}

		constexpr static entity_id id_of_chunk(const components& comp) noexcept{
			return archetype_custom_behavior<raw_tuple>::id_of_chunk(comp);
		}

		[[nodiscard]] chunk_meta& meta_at(const entity_data_chunk_index idx) noexcept{
			return static_cast<chunk_meta&>(
				chunks.template get<first_component>(static_cast<typename storage_type::size_type>(idx)));
		}

		[[nodiscard]] const chunk_meta& meta_at(const entity_data_chunk_index idx) const noexcept{
			return static_cast<const chunk_meta&>(
				chunks.template get<first_component>(static_cast<typename storage_type::size_type>(idx)));
		}

		[[nodiscard]] entity_id id_at(const entity_data_chunk_index idx) const noexcept{
			return meta_at(idx).id();
		}

		template <std::size_t... I>
		[[nodiscard]] components make_component_row(std::size_t idx, std::index_sequence<I...>) const{
			const auto converted_idx = static_cast<typename storage_type::size_type>(idx);
			return components{chunks.template get<std::tuple_element_t<I, raw_tuple>>(converted_idx)...};
		}

		[[nodiscard]] components make_component_row(std::size_t idx) const{
			return make_component_row(idx, std::make_index_sequence<std::tuple_size_v<raw_tuple>>{});
		}

		template <std::size_t... I>
		void assign_component_row(std::size_t idx, components&& row, std::index_sequence<I...>){
			const auto converted_idx = static_cast<typename storage_type::size_type>(idx);
			((chunks.template get<std::tuple_element_t<I, raw_tuple>>(converted_idx) =
				std::move(get<std::tuple_element_t<I, raw_tuple>>(row))), ...);
		}

		void assign_component_row(std::size_t idx, components&& row){
			assign_component_row(idx, std::move(row), std::make_index_sequence<std::tuple_size_v<raw_tuple>>{});
		}

		template <std::size_t... I>
		void init_components_at(std::size_t idx, std::index_sequence<I...>){
			const auto converted_idx = static_cast<typename storage_type::size_type>(idx);
			chunk_meta& meta = meta_at(static_cast<entity_data_chunk_index>(idx));
			(component_trait<std::tuple_element_t<I, raw_tuple>>::on_init(
				meta,
				chunks.template get<std::tuple_element_t<I, raw_tuple>>(converted_idx)), ...);
		}

		void init_components_at(std::size_t idx){
			init_components_at(idx, std::make_index_sequence<std::tuple_size_v<raw_tuple>>{});
		}

		template <std::size_t... I>
		void terminate_components_at(std::size_t idx, std::index_sequence<I...>){
			const auto converted_idx = static_cast<typename storage_type::size_type>(idx);
			chunk_meta& meta = meta_at(static_cast<entity_data_chunk_index>(idx));
			(component_trait<std::tuple_element_t<I, raw_tuple>>::on_terminate(
				meta,
				chunks.template get<std::tuple_element_t<I, raw_tuple>>(converted_idx)), ...);
		}

		void terminate_components_at(std::size_t idx){
			terminate_components_at(idx, std::make_index_sequence<std::tuple_size_v<raw_tuple>>{});
		}

		template <std::size_t... I>
		void relocate_components_at(std::size_t idx, std::index_sequence<I...>){
			const auto converted_idx = static_cast<typename storage_type::size_type>(idx);
			chunk_meta& meta = meta_at(static_cast<entity_data_chunk_index>(idx));
			(component_trait<std::tuple_element_t<I, raw_tuple>>::on_relocate(
				meta,
				chunks.template get<std::tuple_element_t<I, raw_tuple>>(converted_idx)), ...);
		}

		void relocate_components_at(std::size_t idx){
			relocate_components_at(idx, std::make_index_sequence<std::tuple_size_v<raw_tuple>>{});
		}

		void relocate_row_at(std::size_t idx){
			relocate_components_at(idx);

			if constexpr (requires(components& row){ trait::on_relocate(row); }){
				auto row = make_component_row(idx);
				trait::on_relocate(row);
				assign_component_row(idx, std::move(row));
			}
		}

		struct relocate_hook{
			archetype* self{};

			void operator()(storage_type&, typename storage_type::size_type idx) const{
				self->relocate_row_at(idx);
			}
		};

	public:

		[[nodiscard]] std::span<const components> get_chunk_view() const noexcept = delete;

		[[nodiscard]] std::span<first_component> first_column() noexcept{
			return chunks.template column<first_component>();
		}


		[[nodiscard]] std::size_t size() const noexcept final{
			return chunks.size();
		}

		[[nodiscard]] entity_id entity_at(const std::size_t idx) const noexcept final{
			assert(idx < chunks.size());
			return this->id_at(static_cast<entity_data_chunk_index>(idx));
		}

		void reserve(std::size_t sz) override{
			if(sz > storage_type::max_size()){
				throw std::length_error{"archetype capacity exceeds storage size_type"};
			}
			chunks.reserve(static_cast<typename storage_type::size_type>(sz), relocate_hook{this});
		}

	private:
		template <std::size_t... I>
		void emplace_component_row(components&& comp, std::index_sequence<I...>){
			chunks.emplace_back_with_relocate(
				relocate_hook{this},
				std::move(get<std::tuple_element_t<I, raw_tuple>>(comp))...);
		}

	public:
		std::size_t insert_components(components&& comp){
			auto idx = chunks.size();
			entity_id eid = this->id_of_chunk(comp);

			if(!eid || !eid.is_staging()){
				throw std::runtime_error{"Duplicated Insert"};
			}

			ecs::attach_entity_to_archetype(eid, this, idx);
			this->emplace_component_row(std::move(comp), std::make_index_sequence<std::tuple_size_v<raw_tuple>>{});

			this->init_components_at(idx);

			if constexpr (requires(components& row){ trait::on_init(row); }){
				auto row = this->make_component_row(idx);
				trait::on_init(row);
					this->assign_component_row(idx, std::move(row));
			}
			{
				auto row = this->make_component_row(idx);
				this->init(row);
				this->assign_component_row(idx, std::move(row));
			}
			ecs::mark_entity_valid(eid, trait::expire_counter);

			return idx;
		}

		std::size_t insert(const entity_id eid) final {
			if(!eid)return 0;

			if constexpr (requires{
				components{eid};
			}){
				return this->insert_components(components{eid});
			}else if constexpr (std::is_default_constructible_v<components>){
				components comps{};
				chunk_meta& meta = get<chunk_meta>(comps);
				meta.eid_ = eid;
				return this->insert_components(std::move(comps));
			}else{
				static_assert(false, "failed to construct components");
			}

		}

	protected:
		void erase_at(const entity_id e, const std::size_t old_idx) final {
			auto chunk_size = chunks.size();
			auto idx = static_cast<entity_data_chunk_index>(old_idx);

			if(idx >= chunk_size){
				throw std::runtime_error{"Invalid Erase"};
			}

			assert(this->id_at(idx) == e);

			{
				auto row = make_component_row(idx);
				this->terminate(row);
				assign_component_row(idx, std::move(row));
			}

			if constexpr (requires(components& row){ trait::on_terminate(row); }){
				auto row = make_component_row(idx);
				trait::on_terminate(row);
				assign_component_row(idx, std::move(row));
			}

			terminate_components_at(idx);

			chunks.erase_unstable(
				static_cast<typename storage_type::size_type>(idx),
				[this](storage_type&, typename storage_type::size_type moved_idx){
					if(entity_id moved = this->id_at(moved_idx)){
						ecs::update_entity_chunk_index(moved, moved_idx);
					}

					this->relocate_row_at(moved_idx);
				});

			ecs::detach_entity_from_archetype(e);
		}

	public:
		[[nodiscard]] bool has_type(type_identity_index type) const final{
			return algo::access_hash(type_access_hash_map, type, &type_access::type, {}, [](const type_access& conv){
				return !conv;
			}) != type_access_hash_map.end();
		}

		void get_components_span_impl(std::span<void*> components, std::size_t idx) noexcept final {
			const auto converted_idx = static_cast<storage_size_type>(idx);
			const auto end = this->type_access_hash_map.end();
			constexpr auto empty_access = [](const type_access& conv) noexcept{
				return !conv;
			};

			for(void*& component : components){
				const auto type = static_cast<type_identity_index>(component);
				if(auto itr = algo::access_hash(this->type_access_hash_map, type, &type_access::type, {}, empty_access); itr != end){
					component = itr->get(this, converted_idx);
				}else{
					component = nullptr;
				}
			}
		}

		strided_span<std::byte> get_staging_chunk_partial_slice(type_identity_index) noexcept final{
			return {};
		}

		[[nodiscard]] auto at(entity_data_chunk_index idx) const noexcept{
			return chunks[static_cast<typename storage_type::size_type>(idx)];
		}

		[[nodiscard]] auto at(entity_data_chunk_index idx) noexcept{
			return chunks[static_cast<typename storage_type::size_type>(idx)];
		}

		[[nodiscard]] auto operator[](entity_data_chunk_index idx) const noexcept{
			return chunks[static_cast<typename storage_type::size_type>(idx)];
		}

		[[nodiscard]] auto operator[](entity_data_chunk_index idx) noexcept{
			return chunks[static_cast<typename storage_type::size_type>(idx)];
		}

		[[nodiscard]] std::unique_ptr<archetype_serializer> dump() const final{
			if constexpr (trait::is_transient){
				return {};
			}else{
				std::unique_ptr<serializer> ret{std::make_unique<serializer>()};
				ret->dump(*this);
				return ret;
			}
		}

	protected:
		virtual void terminate(components& comps){

		}

		virtual void init(components& comps){

		}

	public:
		template <typename T>
			requires (contained_in<T, appended_tuple>)
		[[nodiscard]] std::span<T> exact_slice() noexcept{
			return chunks.template column<T>();
		}

		template <typename T>
			requires (contained_in<T, appended_tuple>)
		[[nodiscard]] std::span<const T> exact_slice() const noexcept{
			return chunks.template column<T>();
		}

		template <typename T>
		[[nodiscard]] strided_span<T> slice() noexcept{
			constexpr auto idx = tuple_match_first_v<find_if_first_equal, base_to_derive_map, T>;
			if constexpr (idx != std::tuple_size_v<base_to_derive_map>){
				using stored_type = typename std::tuple_element_t<idx, base_to_derive_map>::second_type;
				constexpr std::ptrdiff_t stride = std::same_as<T, stored_type>
					? std::ptrdiff_t{}
					: static_cast<std::ptrdiff_t>(sizeof(stored_type));
				if(chunks.empty()){
					return {nullptr, 0, stride};
				}else{
					return strided_span<T>{
						static_cast<T*>(std::addressof(chunks.template get<stored_type>(typename storage_type::size_type{}))),
						chunks.size(), stride
					};
				}
			}else{
				static_assert(false, "Invalid Type! Try Register Base Type?");
			}
		}

		template <typename T>
		[[nodiscard]] strided_span<const T> slice() const noexcept{
			constexpr auto idx = tuple_match_first_v<find_if_first_equal, base_to_derive_map, T>;
			if constexpr (idx != std::tuple_size_v<base_to_derive_map>){
				using stored_type = typename std::tuple_element_t<idx, base_to_derive_map>::second_type;
				constexpr std::ptrdiff_t stride = std::same_as<T, stored_type>
					? std::ptrdiff_t{}
					: static_cast<std::ptrdiff_t>(sizeof(stored_type));
				if(chunks.empty()){
					return {nullptr, 0, stride};
				}else{
					return strided_span<const T>{
						static_cast<const T*>(std::addressof(chunks.template get<stored_type>(typename storage_type::size_type{}))),
						chunks.size(), stride
					};
				}
			}else{
				static_assert(false, "Invalid Type! Try Register Base Type?");
			}
		}
	};

	template <typename T>
	T* archetype_base::try_get_comp(const entity_id id) noexcept{
		return try_get_comp<T>(id->chunk_index());
	}

	export
	struct archetype_slice{
		using acquirer_type = std::add_pointer_t<strided_span<std::byte>(archetype_base*) noexcept>;
	private:
		archetype_base* idt{};
		acquirer_type getter{};

	public:
		[[nodiscard]] constexpr archetype_slice(archetype_base* identity, acquirer_type getter)
			: idt(identity),
			  getter(getter){
		}

		[[nodiscard]] constexpr archetype_base* identity() const noexcept{
			return idt;
		}

		[[nodiscard]] constexpr acquirer_type get_slice_generator() const noexcept{
			return getter;
		}

		// using T = int;
		template <typename T>
		[[nodiscard]] constexpr strided_span<T> slice() const noexcept {
			auto span = getter(idt);
			return strided_span<T>{reinterpret_cast<T*>(span.data()), span.size(), span.stride()};
		}

		constexpr auto operator<=>(const archetype_slice& o) const noexcept{
			return std::compare_three_way{}(idt, o.idt);
		}

		constexpr bool operator==(const archetype_slice& o) const noexcept{
			return idt == o.idt;
		}
	};

	export
	struct entity_command_buffer{
	private:
		struct spawn_operation{
			type_identity_index type{};
			entity_id entity{};
			std::move_only_function<void(component_manager&, std::size_t)> reserve{};
			std::move_only_function<void(component_manager&)> commit{};
		};

		struct destroy_operation{
			entity_id entity{};

			[[nodiscard]] friend constexpr bool operator==(const destroy_operation&, const destroy_operation&) noexcept = default;

			[[nodiscard]] friend constexpr auto operator<=>(const destroy_operation& lhs, const destroy_operation& rhs) noexcept{
				return lhs.entity <=> rhs.entity;
			}
		};

		std::vector<spawn_operation> spawns_{};
		std::vector<destroy_operation> destroys_{};

		friend component_manager;

	public:
		[[nodiscard]] bool empty() const noexcept{
			return spawns_.empty() && destroys_.empty();
		}

		[[nodiscard]] std::size_t spawn_count() const noexcept{
			return spawns_.size();
		}

		[[nodiscard]] std::size_t destroy_count() const noexcept{
			return destroys_.size();
		}

		template <entity_component_seq Tuple, typename... Args>
			requires (is_tuple_v<Tuple> && (contained_in<std::decay_t<Args>, Tuple> && ...))
		entity_id spawn(component_manager& manager, Args&& ...args);

		template <typename Tuple>
		entity_id spawn(component_manager& manager, tuple_to_comp_t<Tuple>&& comps);

		void destroy(entity_id entity){
			if(entity){
				destroys_.push_back({entity});
			}
		}

		void clear() noexcept{
			spawns_.clear();
			destroys_.clear();
		}
	};

	export
	struct component_manager{
		template <typename T>
		using small_vector_of = gch::small_vector<T>;
		// using archetype_map_type = fixed_open_hash_map<type_identity_index, std::unique_ptr<archetype_base>>;
		using archetype_map_type = std::unordered_map<type_identity_index, std::unique_ptr<archetype_base>>;

	private:
		struct entity_record{
			std::atomic<entity_state> state{entity_state::expired};
			std::uint32_t generation{};
			type_identity_index type{};
			archetype_base* archetype{};
			entity_data_chunk_index chunk_index{invalid_chunk_idx};
			unsigned destroy_delay{};
		};

		struct destroy_ticket{
			entity_id entity{};
			archetype_base* archetype{};
			entity_data_chunk_index chunk_index{invalid_chunk_idx};
		};

		struct archetype_query_cache_key{
			std::vector<type_identity_index> includes{};
			std::vector<type_identity_index> excludes{};

			[[nodiscard]] friend bool operator==(
				const archetype_query_cache_key& lhs,
				const archetype_query_cache_key& rhs) = default;
		};

		struct archetype_query_cache_key_hash{
			[[nodiscard]] std::size_t operator()(const archetype_query_cache_key& key) const noexcept{
				std::size_t value{};
				auto combine = [&value](const type_identity_index type) noexcept{
					value ^= std::hash<type_identity_index>{}(type)
						+ 0x9e3779b97f4a7c15ull
						+ (value << 6u)
						+ (value >> 2u);
				};

				for(const type_identity_index type : key.includes){
					combine(type);
				}
				value ^= 0x517cc1b727220a95ull + (value << 6u) + (value >> 2u);
				for(const type_identity_index type : key.excludes){
					combine(type);
				}
				return value;
			}
		};

		struct archetype_query_match{
			archetype_base* archetype{};
		};

		mutable std::mutex entity_slot_mutex_{};
		mutable std::mutex pending_spawn_mutex_{};
		mutable std::mutex pending_destroy_mutex_{};
		mutable std::mutex archetype_query_cache_mutex_{};
		std::deque<entity_record> entities{};
		std::vector<std::uint32_t> free_entity_slots_{};
		std::vector<entity_command_buffer::spawn_operation> pending_spawns_{};
		std::vector<entity_command_buffer::destroy_operation> pending_destroys_{};
		archetype_map_type archetypes{};


		//TODO make all archetype slices on the same vector, while hash map provides type to span
		std::vector<archetype_slice> archetype_slices_{};
		fixed_open_hash_map<type_identity_index, std::span<const archetype_slice>> type_to_archetype{};
		// std::unordered_map<type_identity_index, std::span<const archetype_slice>> type_to_archetype{};
		mutable std::unordered_map<
			archetype_query_cache_key,
			std::shared_ptr<const std::vector<archetype_query_match>>,
			archetype_query_cache_key_hash> archetype_query_cache_{};

		float update_delta{};
		unsigned clock{};

		friend struct entity_command_buffer;
		friend void ecs::attach_entity_to_archetype(entity_id entity, archetype_base* archetype, entity_data_chunk_index chunk_index) noexcept;
		friend void ecs::mark_entity_valid(entity_id entity, unsigned destroy_delay) noexcept;
		friend void ecs::update_entity_chunk_index(entity_id entity, entity_data_chunk_index chunk_index) noexcept;
		friend void ecs::detach_entity_from_archetype(entity_id entity) noexcept;

		[[nodiscard]] entity_record* try_get_record(const entity_id entity) noexcept{
			if(entity.owner_ != this || static_cast<std::size_t>(entity.slot_) >= entities.size()){
				return nullptr;
			}

			entity_record& record = entities[entity.slot_];
			if(record.generation != entity.generation_){
				return nullptr;
			}

			return std::addressof(record);
		}

		[[nodiscard]] const entity_record* try_get_record(const entity_id entity) const noexcept{
			if(entity.owner_ != this || static_cast<std::size_t>(entity.slot_) >= entities.size()){
				return nullptr;
			}

			const entity_record& record = entities[entity.slot_];
			if(record.generation != entity.generation_){
				return nullptr;
			}

			return std::addressof(record);
		}

		[[nodiscard]] bool is_current_staging(const entity_id entity) const noexcept{
			const entity_record* record = this->try_get_record(entity);
			return record != nullptr && record->state.load(std::memory_order_acquire) == entity_state::staging;
		}

		void enqueue_pending_destroy(entity_id entity){
			std::lock_guard lock{pending_destroy_mutex_};
			pending_destroys_.push_back({entity});
		}

		void release_entity_slot(entity_id entity) noexcept{
			std::lock_guard lock{entity_slot_mutex_};
			entity_record* record = this->try_get_record(entity);
			if(record == nullptr || record->state.load(std::memory_order_acquire) != entity_state::expired){
				return;
			}

			record->type = {};
			record->archetype = nullptr;
			record->chunk_index = invalid_chunk_idx;
			record->destroy_delay = 0;
			free_entity_slots_.push_back(entity.slot_);
		}

		void attach_entity(entity_id entity, archetype_base* archetype, entity_data_chunk_index chunk_index) noexcept{
			entity_record* record = this->try_get_record(entity);
			assert(record != nullptr);
			record->archetype = archetype;
			record->chunk_index = chunk_index;
		}

		void mark_valid(entity_id entity, const unsigned destroy_delay) noexcept{
			entity_record* record = this->try_get_record(entity);
			assert(record != nullptr);
			record->destroy_delay = destroy_delay;
			record->state.store(entity_state::valid, std::memory_order_release);
		}

		void update_chunk_index(entity_id entity, const entity_data_chunk_index chunk_index) noexcept{
			entity_record* record = this->try_get_record(entity);
			if(record == nullptr){
				return;
			}

			const entity_state state = record->state.load(std::memory_order_acquire);
			if(state == entity_state::valid || state == entity_state::expired){
				record->chunk_index = chunk_index;
			}
		}

		void detach_from_archetype(entity_id entity) noexcept{
			entity_record* record = this->try_get_record(entity);
			if(record == nullptr){
				return;
			}

			record->archetype = nullptr;
			record->chunk_index = invalid_chunk_idx;
		}

		template <typename Tuple>
		[[nodiscard]] entity_command_buffer::spawn_operation make_spawn_operation(
			entity_id entity,
			tuple_to_comp_t<Tuple>&& comps){
			get<chunk_meta>(comps).eid_ = entity;

			return entity_command_buffer::spawn_operation{
				.type = unstable_type_identity_of<Tuple>(),
				.entity = entity,
				.reserve = [](component_manager& manager, const std::size_t count){
					auto& archetype = manager.template add_archetype<Tuple>();
					archetype.reserve(archetype.size() + count);
				},
				.commit = [entity, comps = std::move(comps)](component_manager& manager) mutable {
					if(!manager.is_current_staging(entity)){
						return;
					}

					get<chunk_meta>(comps).eid_ = entity;
					auto& archetype = manager.template add_archetype<Tuple>();
					(void)archetype.insert_components(std::move(comps));
				}
			};
		}

		template <tuple_spec Tuple, std::size_t... Idx>
		static void append_query_types(
			std::vector<type_identity_index>& target,
			std::index_sequence<Idx...>){
			(target.push_back(unstable_type_identity_of<std::tuple_element_t<Idx, Tuple>>()), ...);
		}

		template <tuple_spec Includes, tuple_spec Excludes>
		[[nodiscard]] static archetype_query_cache_key make_archetype_query_cache_key(){
			archetype_query_cache_key key{};
			key.includes.reserve(std::tuple_size_v<Includes>);
			key.excludes.reserve(std::tuple_size_v<Excludes>);
			component_manager::append_query_types<Includes>(
				key.includes,
				std::make_index_sequence<std::tuple_size_v<Includes>>{});
			component_manager::append_query_types<Excludes>(
				key.excludes,
				std::make_index_sequence<std::tuple_size_v<Excludes>>{});
			return key;
		}

		[[nodiscard]] const archetype_slice* find_archetype_slice(
			const type_identity_index type,
			archetype_base* identity) const noexcept{
			if(auto itr = type_to_archetype.find(type); itr != type_to_archetype.end()){
				const archetype_slice target{identity, {}};
				const auto found = std::ranges::lower_bound(itr->second, target);
				if(found != itr->second.end() && found->identity() == identity){
					return std::to_address(found);
				}
			}
			return nullptr;
		}

		[[nodiscard]] bool archetype_has_component(
			const type_identity_index type,
			archetype_base* identity) const noexcept{
			return this->find_archetype_slice(type, identity) != nullptr;
		}

		template <tuple_spec Tuple, std::size_t... Idx>
		[[nodiscard]] bool archetype_has_all_components(
			archetype_base* identity,
			std::index_sequence<Idx...>) const noexcept{
			return (this->archetype_has_component(
				unstable_type_identity_of<std::tuple_element_t<Idx, Tuple>>(),
				identity) && ...);
		}

		template <tuple_spec Tuple, std::size_t... Idx>
		[[nodiscard]] bool archetype_has_any_components(
			archetype_base* identity,
			std::index_sequence<Idx...>) const noexcept{
			return (this->archetype_has_component(
				unstable_type_identity_of<std::tuple_element_t<Idx, Tuple>>(),
				identity) || ...);
		}

		template <tuple_spec Tuple, tuple_spec Exclusives>
		[[nodiscard]] std::vector<archetype_query_match> build_archetype_query_cache() const{
			using first_type = std::tuple_element_t<0, Tuple>;
			std::vector<archetype_query_match> result{};

			const auto first_itr = type_to_archetype.find(unstable_type_identity_of<first_type>());
			if(first_itr == type_to_archetype.end()){
				return result;
			}

			result.reserve(first_itr->second.size());
			for(const archetype_slice& slice : first_itr->second){
				archetype_base* identity = slice.identity();
				if(!this->archetype_has_all_components<Tuple>(
					identity,
					std::make_index_sequence<std::tuple_size_v<Tuple>>{})){
					continue;
				}
				if constexpr (std::tuple_size_v<Exclusives> > 0){
					if(this->archetype_has_any_components<Exclusives>(
						identity,
						std::make_index_sequence<std::tuple_size_v<Exclusives>>{})){
						continue;
					}
				}
				result.push_back({identity});
			}

			return result;
		}

		template <tuple_spec Tuple, tuple_spec Exclusives>
		[[nodiscard]] std::shared_ptr<const std::vector<archetype_query_match>> cached_archetype_query() const{
			auto key = component_manager::make_archetype_query_cache_key<Tuple, Exclusives>();
			std::lock_guard lock{archetype_query_cache_mutex_};
			if(const auto itr = archetype_query_cache_.find(key); itr != archetype_query_cache_.end()){
				return itr->second;
			}

			auto result = this->build_archetype_query_cache<Tuple, Exclusives>();
			auto cached = std::make_shared<const std::vector<archetype_query_match>>(std::move(result));
			const auto [itr, inserted] = archetype_query_cache_.try_emplace(std::move(key), std::move(cached));
			(void)inserted;
			return itr->second;
		}

		void invalidate_archetype_query_cache(){
			std::lock_guard lock{archetype_query_cache_mutex_};
			archetype_query_cache_.clear();
		}

		[[nodiscard]] const archetype_slice& require_archetype_slice(
			const type_identity_index type,
			archetype_base* identity) const noexcept{
			const archetype_slice* slice = this->find_archetype_slice(type, identity);
			assert(slice != nullptr);
			return *slice;
		}

		template <tuple_spec Tuple, std::size_t... Idx>
		[[nodiscard]] auto make_archetype_slice_array(
			archetype_base* identity,
			std::index_sequence<Idx...>) const noexcept{
			return std::array<archetype_slice, sizeof...(Idx)>{
				this->require_archetype_slice(
					unstable_type_identity_of<std::tuple_element_t<Idx, Tuple>>(),
					identity)...
			};
		}

	public:
		void update_update_delta(float dlt) noexcept{
			update_delta = dlt;
			++clock;
		}

		auto get_archetypes() const noexcept{
			return archetypes | std::views::values;
		}

		[[nodiscard]] FORCE_INLINE constexpr float get_update_delta() const noexcept{
			return update_delta;
		}


		[[nodiscard]] FORCE_INLINE constexpr unsigned get_clock() const noexcept{
			return clock;
		}

		template <entity_component_seq Tuple, typename... Args>
			requires (is_tuple_v<Tuple> && (contained_in<std::decay_t<Args>, Tuple> && ...))
		entity_id spawn(Args&& ...args){
			using raw_tuple_type = std::tuple<Args&& ...>;
			raw_tuple_type ref_tuple{std::forward<Args>(args)...};

			using in_tuple = std::tuple<std::decay_t<Args>...>;
			constexpr std::size_t in_tuple_size = sizeof...(Args);

			return [&] <std::size_t ...Idx>(std::index_sequence<Idx...>) -> entity_id {
				return this->spawn<Tuple>(tuple_to_comp_t<Tuple>{[&] <std::size_t I> (){
					using cur_type = std::tuple_element_t<I, Tuple>;
					constexpr std::size_t mapped_cur_idx = tuple_index_v<cur_type, in_tuple>;

					if constexpr (mapped_cur_idx == in_tuple_size){
						return cur_type{};
					}else{
						using param_type = std::tuple_element_t<mapped_cur_idx, raw_tuple_type>;
						return cur_type{std::forward<param_type>(std::get<mapped_cur_idx>(ref_tuple))};
					}
				}.template operator()<Idx>() ...});
			}(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
		}

		template <typename Tuple>
		entity_id spawn(tuple_to_comp_t<Tuple>&& comps){
			entity_id entity = this->acquire_entity<Tuple>();
			auto operation = this->make_spawn_operation<Tuple>(entity, std::move(comps));
			{
				std::lock_guard lock{pending_spawn_mutex_};
				pending_spawns_.push_back(std::move(operation));
			}
			return entity;
		}

		template <entity_component_seq Tuple, typename... Args>
			requires (is_tuple_v<Tuple> && (contained_in<std::decay_t<Args>, Tuple> && ...))
		entity_id create_entity_deferred(Args&& ...args){
			return this->spawn<Tuple>(std::forward<Args>(args)...);
		}

		template <typename Tuple>
		entity_id create_entity_deferred(tuple_to_comp_t<Tuple>&& comps){
			return this->spawn<Tuple>(std::move(comps));
		}

		template <typename Tuple, std::derived_from<archetype<Tuple>> Archetype = archetype<Tuple>, typename ... Args>
			requires (is_tuple_v<Tuple>)
		auto& add_archetype(Args&& ...args){
			static constexpr type_identity_index idx = unstable_type_identity_of<Tuple>();
			if(const auto itr = archetypes.find(idx); itr != archetypes.end()){
				return static_cast<Archetype&>(*itr->second);
			}

			return static_cast<Archetype&>(*archetypes.try_emplace(idx, this->create_new_archetype_map<Tuple, Archetype>(std::forward<Args>(args) ...)).first->second);
		}

		template <typename Archetype>
			requires requires{
				typename Archetype::raw_tuple;
			}
		auto& add_archetype(){
			return add_archetype<typename Archetype::raw_tuple, Archetype>();
		}

		template <typename ...Tuple>
			requires (sizeof...(Tuple) > 1 && (is_tuple_v<Tuple> && ...))
		void add_archetype(){
			(add_archetype<Tuple>(), ...);
		}

		template <typename Tuple>
			requires (is_tuple_v<Tuple>)
		entity_id acquire_entity(){
			static constexpr type_identity_index idx = unstable_type_identity_of<Tuple>();

			std::lock_guard lock{entity_slot_mutex_};
			std::uint32_t slot{};
			if(free_entity_slots_.empty()){
				if(entities.size() >= static_cast<std::size_t>(invalid_entity_slot)){
					throw std::length_error{"entity slot capacity exceeded"};
				}
				slot = static_cast<std::uint32_t>(entities.size());
				entities.emplace_back();
			}else{
				slot = free_entity_slots_.back();
				free_entity_slots_.pop_back();
			}

			entity_record& record = entities[slot];
			record.generation = record.generation == std::numeric_limits<std::uint32_t>::max()
				? 1u
				: record.generation + 1u;
			if(record.generation == 0){
				record.generation = 1u;
			}
			record.type = idx;
			record.archetype = nullptr;
			record.chunk_index = invalid_chunk_idx;
			record.destroy_delay = 0;
			record.state.store(entity_state::staging, std::memory_order_release);
			return entity_id{this, slot, record.generation};
		}

	private:
		template <typename ...Ts>
			requires (!is_tuple_v<Ts> && ...)
		entity_id create_entity(){
			return this->acquire_entity<std::tuple<Ts ...>>();
		}
	public:
		void destroy_all(){
			for(std::size_t slot = 0; slot != entities.size(); ++slot){
				entity_record& record = entities[slot];
				const entity_state state = record.state.load(std::memory_order_acquire);
				if(state == entity_state::staging || state == entity_state::valid){
					(void)this->destroy(entity_id{this, static_cast<std::uint32_t>(slot), record.generation});
				}
			}
		}

		void mark_expired_all(){
			this->destroy_all();
		}

		bool destroy(entity_id entity){
			entity_record* record = this->try_get_record(entity);
			if(record == nullptr){
				return false;
			}

			entity_state expected = entity_state::staging;
			if(!record->state.compare_exchange_strong(
				expected,
				entity_state::expired,
				std::memory_order_acq_rel,
				std::memory_order_acquire)){
				expected = entity_state::valid;
				if(!record->state.compare_exchange_strong(
					expected,
					entity_state::expired,
					std::memory_order_acq_rel,
					std::memory_order_acquire)){
					return false;
				}
			}

			this->enqueue_pending_destroy(entity);
			return true;
		}

		bool mark_expired(entity_id entity){
			return this->destroy(entity);
		}

		void submit(entity_command_buffer&& buffer);

		void commit(){
			this->commit_destroy();
			this->commit_spawn();
		}

		void do_deferred(){
			this->commit();
		}

		void commit_spawn(){
			std::vector<entity_command_buffer::spawn_operation> spawns{};
			{
				std::lock_guard lock{pending_spawn_mutex_};
				spawns.swap(pending_spawns_);
			}

			if(spawns.empty()){
				return;
			}

			std::unordered_map<type_identity_index, std::pair<std::size_t, std::size_t>> grouped{};
			grouped.reserve(spawns.size());
			for(std::size_t i = 0; i != spawns.size(); ++i){
				const auto& spawn = spawns[i];
				if(!this->is_current_staging(spawn.entity)){
					continue;
				}
				auto& group = grouped[spawn.type];
				if(group.first == 0){
					group.second = i;
				}
				++group.first;
			}

			for(const auto& [type, group] : grouped){
				(void)type;
				std::invoke(spawns[group.second].reserve, *this, group.first);
			}

			for(auto& spawn : spawns){
				if(this->is_current_staging(spawn.entity)){
					std::invoke(spawn.commit, *this);
				}
			}
		}

		void do_deferred_add(){
			this->commit_spawn();
		}

		void commit_destroy(){
			std::vector<entity_command_buffer::destroy_operation> destroys{};
			{
				std::lock_guard lock{pending_destroy_mutex_};
				destroys.swap(pending_destroys_);
			}

			if(destroys.empty()){
				return;
			}

			std::ranges::sort(destroys);
			const auto unique_end = std::ranges::unique(destroys);
			destroys.erase(unique_end.begin(), unique_end.end());

			std::vector<entity_command_buffer::destroy_operation> delayed{};
			std::vector<destroy_ticket> tickets{};
			delayed.reserve(destroys.size());
			tickets.reserve(destroys.size());

			for(const entity_command_buffer::destroy_operation destroy : destroys){
				const entity_id entity = destroy.entity;
				entity_record* record = this->try_get_record(entity);
				if(record == nullptr || record->state.load(std::memory_order_acquire) != entity_state::expired){
					continue;
				}

				if(record->destroy_delay != 0){
					--record->destroy_delay;
					delayed.push_back({entity});
					continue;
				}

				if(record->archetype != nullptr && record->chunk_index != invalid_chunk_idx){
					tickets.push_back({
						.entity = entity,
						.archetype = record->archetype,
						.chunk_index = record->chunk_index
					});
				}else{
					this->release_entity_slot(entity);
				}
			}

			std::ranges::sort(tickets, [](const destroy_ticket& lhs, const destroy_ticket& rhs){
				if(lhs.archetype != rhs.archetype){
					return std::less<archetype_base*>{}(lhs.archetype, rhs.archetype);
				}
				return lhs.chunk_index > rhs.chunk_index;
			});

			for(const destroy_ticket& ticket : tickets){
				entity_record* record = this->try_get_record(ticket.entity);
				if(record == nullptr || record->state.load(std::memory_order_acquire) != entity_state::expired){
					continue;
				}
				if(record->archetype != ticket.archetype || record->chunk_index != ticket.chunk_index){
					continue;
				}

				ticket.archetype->erase_at(ticket.entity, ticket.chunk_index);
				this->release_entity_slot(ticket.entity);
			}

			if(!delayed.empty()){
				std::lock_guard lock{pending_destroy_mutex_};
				pending_destroys_.insert(pending_destroys_.end(), delayed.begin(), delayed.end());
			}
		}

		void do_deferred_destroy(){
			this->commit_destroy();
		}

		void deferred_destroy_no_reference_entities(){
			this->destroy_all();
		}

		template <typename Tuple>
			requires (is_tuple_v<Tuple>)
		auto get_slice_of() const{
			static_assert(std::tuple_size_v<Tuple> > 0);

			using chunk_spans = unary_apply_to_tuple_t<strided_span, Tuple>;
			using searched_spans = small_vector_of<chunk_spans>;

			searched_spans result{};

			this->slice_and_then<Tuple>([&](chunk_spans spans){
				                            result.push_back(spans);
			                            }, [&](std::size_t sz){
				                            result.reserve(sz);
			                            });

			return result;
		}

		[[nodiscard]] static bool is_inserted_row(const chunk_meta& meta) noexcept{
			const entity_id id = meta.id();
			return id && id.is_inserted();
		}

		template <typename Exclusives = std::tuple<>, typename Fn, typename S>
		FORCE_INLINE void each_unfiltered(this S& self, Fn fn){
			using raw_params = remove_mfptr_this_args<Fn>;

			if constexpr (std::same_as<std::remove_cvref_t<std::tuple_element_t<0, raw_params>>, component_manager>){
				using params = unary_apply_to_tuple_t<std::decay_t, tuple_drop_first_elem_t<raw_params>>;
				using span_tuple = unary_apply_to_tuple_t<strided_span, params>;

				self.template slice_and_then<params, Exclusives>([&self, f = std::move(fn)] FORCE_INLINE (const span_tuple& p) {
					auto count = std::ranges::size(std::get<0>(p));

					unary_apply_to_tuple_t<strided_span_iterator, params> iterators{};

					[&] <std::size_t ...Idx> FORCE_INLINE (std::index_sequence<Idx...>) {
						((std::get<Idx>(iterators) = std::ranges::begin(std::get<Idx>(p))), ...);
					}(std::make_index_sequence<std::tuple_size_v<params>>{});

					for(std::remove_cvref_t<decltype(count)> i = 0; i != count; ++i){
						[&] <std::size_t ...Idx> FORCE_INLINE (std::index_sequence<Idx...>) {
							std::invoke(f, self, *(std::get<Idx>(iterators)++) ...);
						}(std::make_index_sequence<std::tuple_size_v<params>>{});
					}
				});
			}else{
				using params = unary_apply_to_tuple_t<std::decay_t, raw_params>;
				using span_tuple = unary_apply_to_tuple_t<strided_span, params>;

				self.template slice_and_then<params, Exclusives>([f = std::move(fn)] FORCE_INLINE (const span_tuple& p) {
					auto count = std::ranges::size(std::get<0>(p));

					unary_apply_to_tuple_t<strided_span_iterator, params> iterators{};

					[&] <std::size_t ...Idx> FORCE_INLINE (std::index_sequence<Idx...>) {
						((std::get<Idx>(iterators) = std::ranges::begin(std::get<Idx>(p))), ...);
					}(std::make_index_sequence<std::tuple_size_v<params>>{});

					for(std::remove_cvref_t<decltype(count)> i = 0; i != count; ++i){
						[&] <std::size_t ...Idx> FORCE_INLINE (std::index_sequence<Idx...>) {
							std::invoke(f,
								(assert(std::get<Idx>(iterators) != std::ranges::end(std::get<Idx>(p))),
									*(std::get<Idx>(iterators)++)) ...);
						}(std::make_index_sequence<std::tuple_size_v<params>>{});
					}
				});
			}

		}

		template <typename Exclusives = std::tuple<>, typename Fn, typename S>
		FORCE_INLINE void each(this S& self, Fn fn){
			using raw_params = remove_mfptr_this_args<Fn>;

			if constexpr (std::same_as<std::remove_cvref_t<std::tuple_element_t<0, raw_params>>, component_manager>){
				using params = unary_apply_to_tuple_t<std::decay_t, tuple_drop_first_elem_t<raw_params>>;
				if constexpr (contained_in<chunk_meta, params>){
					using span_tuple = unary_apply_to_tuple_t<strided_span, params>;
					static constexpr std::size_t meta_idx = tuple_index_v<chunk_meta, params>;

					self.template slice_and_then<params, Exclusives>([&self, f = std::move(fn)] FORCE_INLINE (const span_tuple& p) {
						auto count = std::ranges::size(std::get<0>(p));

						unary_apply_to_tuple_t<strided_span_iterator, params> iterators{};

						[&] <std::size_t ...Idx> FORCE_INLINE (std::index_sequence<Idx...>) {
							((std::get<Idx>(iterators) = std::ranges::begin(std::get<Idx>(p))), ...);
						}(std::make_index_sequence<std::tuple_size_v<params>>{});

						for(std::remove_cvref_t<decltype(count)> i = 0; i != count; ++i){
							[&] <std::size_t ...Idx> FORCE_INLINE (std::index_sequence<Idx...>) {
								(assert(std::get<Idx>(iterators) != std::ranges::end(std::get<Idx>(p))), ...);
								if(component_manager::is_inserted_row(*std::get<meta_idx>(iterators))){
									std::invoke(f, self, *std::get<Idx>(iterators) ...);
								}
								(++std::get<Idx>(iterators), ...);
							}(std::make_index_sequence<std::tuple_size_v<params>>{});
						}
					});
				}else{
					using params_with_meta = tuple_cat_t<std::tuple<chunk_meta>, params>;
					using span_tuple = unary_apply_to_tuple_t<strided_span, params_with_meta>;

					self.template slice_and_then<params_with_meta, Exclusives>([&self, f = std::move(fn)] FORCE_INLINE (const span_tuple& p) {
						auto count = std::ranges::size(std::get<0>(p));

						unary_apply_to_tuple_t<strided_span_iterator, params_with_meta> iterators{};

						[&] <std::size_t ...Idx> FORCE_INLINE (std::index_sequence<Idx...>) {
							((std::get<Idx>(iterators) = std::ranges::begin(std::get<Idx>(p))), ...);
						}(std::make_index_sequence<std::tuple_size_v<params_with_meta>>{});

						for(std::remove_cvref_t<decltype(count)> i = 0; i != count; ++i){
							[&] <std::size_t ...Idx> FORCE_INLINE (std::index_sequence<Idx...>) {
								[&] <std::size_t ...InvokeIdx> FORCE_INLINE (std::index_sequence<InvokeIdx...>) {
									(assert(std::get<Idx>(iterators) != std::ranges::end(std::get<Idx>(p))), ...);
									if(component_manager::is_inserted_row(*std::get<0>(iterators))){
										std::invoke(f, self, *std::get<InvokeIdx + 1>(iterators) ...);
									}
								}(std::make_index_sequence<std::tuple_size_v<params>>{});
								(++std::get<Idx>(iterators), ...);
							}(std::make_index_sequence<std::tuple_size_v<params_with_meta>>{});
						}
					});
				}
			}else{
				using params = unary_apply_to_tuple_t<std::decay_t, raw_params>;
				if constexpr (contained_in<chunk_meta, params>){
					using span_tuple = unary_apply_to_tuple_t<strided_span, params>;
					static constexpr std::size_t meta_idx = tuple_index_v<chunk_meta, params>;

					self.template slice_and_then<params, Exclusives>([f = std::move(fn)] FORCE_INLINE (const span_tuple& p) {
						auto count = std::ranges::size(std::get<0>(p));

						unary_apply_to_tuple_t<strided_span_iterator, params> iterators{};

						[&] <std::size_t ...Idx> FORCE_INLINE (std::index_sequence<Idx...>) {
							((std::get<Idx>(iterators) = std::ranges::begin(std::get<Idx>(p))), ...);
						}(std::make_index_sequence<std::tuple_size_v<params>>{});

						for(std::remove_cvref_t<decltype(count)> i = 0; i != count; ++i){
							[&] <std::size_t ...Idx> FORCE_INLINE (std::index_sequence<Idx...>) {
								(assert(std::get<Idx>(iterators) != std::ranges::end(std::get<Idx>(p))), ...);
								if(component_manager::is_inserted_row(*std::get<meta_idx>(iterators))){
									std::invoke(f, *std::get<Idx>(iterators) ...);
								}
								(++std::get<Idx>(iterators), ...);
							}(std::make_index_sequence<std::tuple_size_v<params>>{});
						}
					});
				}else{
					using params_with_meta = tuple_cat_t<std::tuple<chunk_meta>, params>;
					using span_tuple = unary_apply_to_tuple_t<strided_span, params_with_meta>;

					self.template slice_and_then<params_with_meta, Exclusives>([f = std::move(fn)] FORCE_INLINE (const span_tuple& p) {
						auto count = std::ranges::size(std::get<0>(p));

						unary_apply_to_tuple_t<strided_span_iterator, params_with_meta> iterators{};

						[&] <std::size_t ...Idx> FORCE_INLINE (std::index_sequence<Idx...>) {
							((std::get<Idx>(iterators) = std::ranges::begin(std::get<Idx>(p))), ...);
						}(std::make_index_sequence<std::tuple_size_v<params_with_meta>>{});

						for(std::remove_cvref_t<decltype(count)> i = 0; i != count; ++i){
							[&] <std::size_t ...Idx> FORCE_INLINE (std::index_sequence<Idx...>) {
								[&] <std::size_t ...InvokeIdx> FORCE_INLINE (std::index_sequence<InvokeIdx...>) {
									(assert(std::get<Idx>(iterators) != std::ranges::end(std::get<Idx>(p))), ...);
									if(component_manager::is_inserted_row(*std::get<0>(iterators))){
										std::invoke(f, *std::get<InvokeIdx + 1>(iterators) ...);
									}
								}(std::make_index_sequence<std::tuple_size_v<params>>{});
								(++std::get<Idx>(iterators), ...);
							}(std::make_index_sequence<std::tuple_size_v<params_with_meta>>{});
						}
					});
				}
			}

		}

		template <typename Exclusives = std::tuple<>, typename Fn, typename S>
		FORCE_INLINE void sliced_each(this S& self, Fn fn){
			self.template each<Exclusives>(std::move(fn));
		}

		template <typename ...T>
			requires (!is_tuple_v<T> && ...)
		auto get_slice_of() const{
			return get_slice_of<std::tuple<T...>>();
		}

		[[nodiscard]] entity_state state_of(const entity_id entity) const noexcept{
			const entity_record* record = this->try_get_record(entity);
			if(record == nullptr){
				return entity_state::expired;
			}
			return record->state.load(std::memory_order_acquire);
		}

		[[nodiscard]] std::size_t chunk_index_of(const entity_id entity) const noexcept{
			const entity_record* record = this->try_get_record(entity);
			if(record == nullptr){
				return invalid_chunk_idx;
			}
			return record->chunk_index;
		}

		template <typename T>
		[[nodiscard]] bool has(const entity_id entity) const noexcept{
			const entity_record* record = this->try_get_record(entity);
			return record != nullptr
				&& record->state.load(std::memory_order_acquire) == entity_state::valid
				&& record->archetype != nullptr
				&& record->archetype->template has_type<T>();
		}

		template <typename T>
		[[nodiscard]] T* try_get(const entity_id entity) const noexcept{
			return this->template get_entity_partial_chunk<T>(entity);
		}

		template <typename Tuple>
		tuple_to_seq_chunk_t<Tuple>* get_entity_full_chunk(const entity_id entity) const noexcept{
			if(!entity.is_inserted())return nullptr;
			static_assert(!std::same_as<Tuple, Tuple>,
				"Full entity chunk pointers are not available for SoA archetype storage; use get_entity_partial_chunk instead.");
			return nullptr;
		}

		template <typename T>
		T* get_entity_partial_chunk(const entity_id entity) const noexcept{
			const entity_record* record = this->try_get_record(entity);
			if(record == nullptr || record->state.load(std::memory_order_acquire) != entity_state::valid){
				return nullptr;
			}
			if(record->archetype == nullptr || record->chunk_index == invalid_chunk_idx){
				return nullptr;
			}
			return record->archetype->try_get_comp<T>(record->chunk_index);
		}

	private:

		struct savement{
			std::ptrdiff_t off;
			std::size_t size;
		};


		struct subrange{
			using value_type = const archetype_slice;
			using rng = std::span<value_type>;
			using itr = rng::pointer;

		// private:
			itr begin_itr;
			itr end_itr;
		public:

			[[nodiscard]] FORCE_INLINE constexpr subrange() noexcept = default;

			[[nodiscard]] FORCE_INLINE constexpr explicit(false) subrange(rng range) noexcept :
				begin_itr(std::to_address(range.begin())),
				end_itr(std::to_address(range.end())){
			}

			[[nodiscard]] FORCE_INLINE constexpr bool empty() const noexcept{
				return begin_itr == end_itr;
			}

			[[nodiscard]] FORCE_INLINE value_type& front() const noexcept{
				return *begin_itr;
			}

			[[nodiscard]] FORCE_INLINE itr begin() const noexcept{
				return begin_itr;
			}

			[[nodiscard]] FORCE_INLINE itr end() const noexcept{
				return end_itr;
			}
		};

	public:
		template <
			tuple_spec Tuple = std::tuple<>,
			tuple_spec Exclusives = std::tuple<>,
			typename Fn,
			std::invocable<std::size_t>	ReserveFn = std::identity>
			requires (!tuple_has_duplicate_types_v<tuple_cat_t<Tuple, Exclusives>>)
		void slice_and_then(Fn fn, ReserveFn reserve_fn = {}) const{
			static_assert(std::tuple_size_v<Tuple> > 0);
			using chunk_spans = unary_apply_to_tuple_t<strided_span, Tuple>;
			const auto matched_archetypes = this->template cached_archetype_query<Tuple, Exclusives>();

			if constexpr (!std::same_as<ReserveFn, std::identity>){
				(void)std::invoke(reserve_fn, matched_archetypes->size());
			}

			static constexpr auto checker = [] <typename RTup>(const RTup& rng){
#if DEBUG_CHECK
				auto stride = std::get<0>(rng).stride();

				[&] <std::size_t ... Idx>(std::index_sequence<Idx...>) FORCE_INLINE{
					(assert(std::get<Idx + 1>(rng).stride() == stride && "Incompatible stride"), ...);
				}(std::make_index_sequence<std::tuple_size_v<RTup> - 1>{});
#endif
			};

			for(const archetype_query_match match : *matched_archetypes){
				auto slices = this->template make_archetype_slice_array<Tuple>(
					match.archetype,
					std::make_index_sequence<std::tuple_size_v<Tuple>>{});

				[&] <std::size_t ...Idx> (std::index_sequence<Idx...>) FORCE_INLINE {
					if constexpr (std::invocable<Fn, std::array<archetype_slice, std::tuple_size_v<Tuple>>>){
						std::invoke(fn, slices);
					}else if constexpr (std::invocable<Fn, chunk_spans>){
						auto rngs = std::make_tuple(slices[Idx].template slice<std::tuple_element_t<Idx, Tuple>>() ...);
						checker(rngs);
						std::invoke(fn, std::move(rngs));
					}else{
						std::invoke(fn, slices[Idx].template slice<std::tuple_element_t<Idx, Tuple>>() ...);
					}
				}
				(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
			}
		}

	private:
		template <typename Tuple = std::tuple<>>
		void add_new_archetype_map(archetype<Tuple>& type){
			using archetype_t = archetype<Tuple>;

			fixed_open_hash_map<type_identity_index, savement> type_to_index_map{0};

			auto insert = [&, this]<typename T>(){
				static constexpr type_identity_index idx{unstable_type_identity_of<T>()};

				auto itr = type_to_archetype.find(idx);
				const bool requires_resize = (archetype_slices_.size() == archetype_slices_.capacity());


				auto make_mark = [&]{
					if(!requires_resize)return;
					type_to_index_map.seemly_clear();
					type_to_index_map.reserve_exact(type_to_archetype.bucket_count());
					for (const auto & [key, value] : type_to_archetype){
						type_to_index_map.try_emplace(key, value.data() - archetype_slices_.data(), value.size());
					}
				};



				archetype_slice elem{
					&type,
					+[](archetype_base* arg) noexcept -> strided_span<std::byte>{
						auto slice = static_cast<archetype_t*>(arg)->template slice<T>();
						return strided_span<std::byte>{
							const_cast<std::byte*>(reinterpret_cast<const std::byte*>(std::ranges::data(slice))),
							slice.size(),
							slice.stride()
						};
					}
				};

				if(itr != type_to_archetype.end()){
					const auto span = itr->second;
					const savement savement{span.data() - archetype_slices_.data(), span.size()};
					//already has a span:
					if(const auto it = std::ranges::lower_bound(span, elem); it == span.end() || it->identity() != elem.identity()){
						make_mark();
						archetype_slices_.insert(archetype_slices_.begin() + (std::to_address(it) - archetype_slices_.data()), std::move(elem));

						if(requires_resize){
							for (auto&& [key, value] : type_to_archetype){
								auto original_idx = type_to_index_map.at(key);
								if(savement.off >= original_idx.off + original_idx.size){
									value = {archetype_slices_.data() + original_idx.off, original_idx.size};
								}else if(savement.off + savement.size <= original_idx.off){
									value = {archetype_slices_.data() + original_idx.off + 1, original_idx.size};
								}else{
									value = {archetype_slices_.data() + original_idx.off, original_idx.size + 1};
								}
							}
						}else{
							for (auto&& [key, value] : type_to_archetype){
								if(savement.off >= value.data() + value.size() - archetype_slices_.data()){
								}else if(savement.off + savement.size <= value.data() - archetype_slices_.data()){
									value = {value.data() + 1, value.size()};
								}else{
									value = {value.data(), value.size()  + 1};
								}
							}
						}
					}
				}else{
					make_mark();
					auto& inserted = archetype_slices_.emplace_back(std::move(elem));
					if(requires_resize){
						for (auto&& [key, value] : type_to_archetype){
							auto original_idx = type_to_index_map.at(key);
							value = {archetype_slices_.data() + original_idx.off, original_idx.size};
						}
					}

					type_to_archetype.try_emplace(idx, std::span{std::addressof(inserted), 1});
				}
			};

			static constexpr auto base_map_idx = unstable_type_identity_of<typename archetype_t::base_to_derive_map>();

			[&] <std::size_t ...Idx> (std::index_sequence<Idx...>){
				(insert.template operator()<typename std::tuple_element_t<Idx, typename archetype_t::base_to_derive_map>::first_type>(), ...);
			}(std::make_index_sequence<std::tuple_size_v<typename archetype_t::base_to_derive_map>>{});

			this->invalidate_archetype_query_cache();
		}


		template <typename Tuple, std::derived_from<archetype<Tuple>> Archetype = archetype<Tuple>, typename ...Args>
		std::unique_ptr<archetype_base> create_new_archetype_map(Args&& ...args){
			std::unique_ptr<archetype_base> ptr = std::make_unique<Archetype>(std::forward<Args>(args)...);
			this->add_new_archetype_map<Tuple>(static_cast<Archetype&>(*ptr));
			return ptr;
		}

		//TODO delete empty archetype map?
	};

	inline void attach_entity_to_archetype(
		const entity_id entity,
		archetype_base* archetype,
		const entity_data_chunk_index chunk_index) noexcept{
		if(component_manager* manager = entity.owner()){
			manager->attach_entity(entity, archetype, chunk_index);
		}
	}

	inline void mark_entity_valid(const entity_id entity, const unsigned destroy_delay) noexcept{
		if(component_manager* manager = entity.owner()){
			manager->mark_valid(entity, destroy_delay);
		}
	}

	inline void update_entity_chunk_index(const entity_id entity, const entity_data_chunk_index chunk_index) noexcept{
		if(component_manager* manager = entity.owner()){
			manager->update_chunk_index(entity, chunk_index);
		}
	}

	inline void detach_entity_from_archetype(const entity_id entity) noexcept{
		if(component_manager* manager = entity.owner()){
			manager->detach_from_archetype(entity);
		}
	}

	inline entity_state entity_id::get_state() const noexcept{
		if(owner_ == nullptr){
			return entity_state::expired;
		}
		return owner_->state_of(*this);
	}

	inline bool entity_id::is_staging() const noexcept{
		return this->get_state() == entity_state::staging;
	}

	inline bool entity_id::is_inserted() const noexcept{
		return this->get_state() == entity_state::valid;
	}

	inline bool entity_id::is_expired() const noexcept{
		return this->get_state() == entity_state::expired;
	}

	inline std::size_t entity_id::chunk_index() const noexcept{
		if(owner_ == nullptr){
			return invalid_chunk_idx;
		}
		return owner_->chunk_index_of(*this);
	}

	template <typename T>
	T* entity_id::try_get() const noexcept{
		if(owner_ == nullptr){
			return nullptr;
		}
		return owner_->template try_get<T>(*this);
	}

	template <typename T>
	bool entity_id::has() const noexcept{
		return owner_ != nullptr && owner_->template has<T>(*this);
	}

	template <typename T>
	T& entity_id::at() const noexcept{
		T* ptr = this->template try_get<T>();
#ifdef COMP_AT_CHECK
		if(ptr == nullptr){
			std::println(std::cerr, "[FATAL ERROR] Illegal Access To Chunk<{}> on entity slot<{}:{}>", name_of<T>(), slot_, generation_);
			std::terminate();
		}
#endif
		assert(ptr != nullptr);
		return *ptr;
	}

	template <typename Tuple, typename T>
	T* entity_id::get() const noexcept{
		return this->template try_get<T>();
	}

	template <entity_component_seq Tuple, typename... Args>
		requires (is_tuple_v<Tuple> && (contained_in<std::decay_t<Args>, Tuple> && ...))
	entity_id entity_command_buffer::spawn(component_manager& manager, Args&& ...args){
		using raw_tuple_type = std::tuple<Args&& ...>;
		raw_tuple_type ref_tuple{std::forward<Args>(args)...};

		using in_tuple = std::tuple<std::decay_t<Args>...>;
		constexpr std::size_t in_tuple_size = sizeof...(Args);

		return [&] <std::size_t ...Idx>(std::index_sequence<Idx...>) -> entity_id {
			return this->spawn<Tuple>(manager, tuple_to_comp_t<Tuple>{[&] <std::size_t I> (){
				using cur_type = std::tuple_element_t<I, Tuple>;
				constexpr std::size_t mapped_cur_idx = tuple_index_v<cur_type, in_tuple>;

				if constexpr (mapped_cur_idx == in_tuple_size){
					return cur_type{};
				}else{
					using param_type = std::tuple_element_t<mapped_cur_idx, raw_tuple_type>;
					return cur_type{std::forward<param_type>(std::get<mapped_cur_idx>(ref_tuple))};
				}
			}.template operator()<Idx>() ...});
		}(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
	}

	template <typename Tuple>
	entity_id entity_command_buffer::spawn(component_manager& manager, tuple_to_comp_t<Tuple>&& comps){
		entity_id entity = manager.template acquire_entity<Tuple>();
		spawns_.push_back(manager.template make_spawn_operation<Tuple>(entity, std::move(comps)));
		return entity;
	}

	inline void component_manager::submit(entity_command_buffer&& buffer){
		for(const entity_command_buffer::destroy_operation destroy : buffer.destroys_){
			(void)this->destroy(destroy.entity);
		}

		if(!buffer.spawns_.empty()){
			std::lock_guard lock{pending_spawn_mutex_};
			pending_spawns_.insert(
				pending_spawns_.end(),
				std::make_move_iterator(buffer.spawns_.begin()),
				std::make_move_iterator(buffer.spawns_.end()));
		}

		buffer.clear();
	}

	template <typename TupleT>
	void archetype<TupleT>::serializer::load(component_manager& target) const{
		for (const auto& seq_chunk : buffer){
			(void)target.template spawn<raw_tuple>(trait::behavior::load(seq_chunk));
		}
	}
}

// NOLINTEND(*-misplaced-const)

