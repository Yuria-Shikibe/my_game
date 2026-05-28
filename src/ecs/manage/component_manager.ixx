module;

#include "plf_hive.h"
#include "gch/small_vector.hpp"
#include <cassert>
#include "mo_yanxi/adapted_attributes.hpp"

export module mo_yanxi.game.ecs.component.manage;

export import :entity;
export import :misc;
export import :serializer;
export import mo_yanxi.heterogeneous.open_addr_hash;
export import mo_yanxi.strided_span;

import mo_yanxi.concepts;
import mo_yanxi.meta_programming;
import mo_yanxi.utility;

import std;



namespace mo_yanxi::game::ecs{
	export
	struct entity_ref{
	private:
		entity_id id_{};

		void try_incr_ref() const noexcept{
			if(id_ && !id_->is_expired()){
				id_->referenced_count.fetch_add(1, std::memory_order_relaxed);
			}
		}

		void try_drop_ref() const noexcept{
			if(id_){
				auto rst = id_->referenced_count.fetch_sub(1, std::memory_order_relaxed);
				assert(rst != 0);
			}
		}

	public:
		[[nodiscard]] constexpr entity_ref() noexcept = default;

		[[nodiscard]] explicit(false) entity_ref(entity_id entity_id) noexcept :
			id_(entity_id){
			try_incr_ref();
		}

		[[nodiscard]] explicit(false) entity_ref(entity& entity) noexcept :
			id_(entity.id()){
			if(entity){
				id_->referenced_count.fetch_add(1, std::memory_order_relaxed);
			}
		}

		entity_ref(const entity_ref& other) noexcept
			: id_{other.id_}{
			try_incr_ref();
		}

		entity_ref(entity_ref&& other) noexcept
			: id_{std::exchange(other.id_, {})}{
		}

		entity_ref& operator=(const entity_ref& other) noexcept{
			if(this == &other) return *this;
			try_drop_ref();
			id_ = other.id_;
			try_incr_ref();
			return *this;
		}

		entity_ref& operator=(entity_id other) noexcept{
			if(id_ == other) return *this;
			try_drop_ref();
			id_ = other;
			try_incr_ref();
			return *this;
		}

		entity_ref& operator=(std::nullptr_t other) noexcept{
			try_drop_ref();
			id_ = nullptr;
			return *this;
		}

		entity_ref& operator=(entity_ref&& other) noexcept{
			if(this == &other) return *this;
			try_drop_ref();
			id_ = std::exchange(other.id_, {});
			return *this;
		}

		~entity_ref(){
			try_drop_ref();
		}

		constexpr entity* operator->() const noexcept{
			return id_;
		}

		constexpr entity& operator*() const noexcept{
			assert(id_);
			return *id_;
		}

		void reset(const entity_id entity_id = nullptr) noexcept{
			this->operator=(entity_id);
		}

		void reset(entity& entity) noexcept{
			this->operator=(entity);
		}

		explicit constexpr operator bool() const noexcept{
			return id_ && *id_;
		}

		[[nodiscard]] entity_id id() const noexcept{
			return id_;
		}

		template <typename C, typename T>
		FORCE_INLINE constexpr T& operator->*(T C::* mptr) const noexcept{
			assert(id_);
			return id_->operator->*<C, T>(mptr);
		}


		template <typename T>
			requires (std::is_member_function_pointer_v<T>)
		FORCE_INLINE constexpr decltype(auto) operator->*(T mfptr) const noexcept{
			assert(id_);
			return id_->operator->*(mfptr);
		}

		constexpr explicit(false) operator entity&() const noexcept{
			assert(id_);
			return *id_;
		}

		constexpr explicit(false) operator entity_id() const noexcept{
			return id_;
		}

		/**
		 * @brief drop the referenced entity if it is already erased
		 */
		constexpr bool drop_if_expired() noexcept{
			if(id_ && id_->is_expired()){
				this->operator=(nullptr);
				return true;
			}
			return false;
		}
		/**
		 * @brief drop the referenced entity if it is already erased
		 */
		template <bool check_inserted = false>
		constexpr bool check_or_drop() noexcept{
			if(!id_)return false;
			else {
				if(id_->is_expired()){
					this->operator=(nullptr);
					return false;
				}

				if constexpr (check_inserted){
					if(!id_->is_inserted()){
						return false;
					}
				}

				return true;
			}
		}

		[[nodiscard]] constexpr bool is_expired() const noexcept{
			return id_ && id_->is_expired();
		}

		[[nodiscard]] constexpr bool is_valid() const noexcept{
			return id_ && !id_->is_expired();
		}

		constexpr friend bool operator==(const entity_ref& lhs, const entity_ref& rhs) noexcept = default;
		constexpr friend bool operator==(const entity_ref& lhs, const entity_id eid) noexcept{
			return lhs.id_ == eid;
		}
		constexpr friend bool operator==(const entity_id eid, const entity_ref& rhs) noexcept{
			return eid == rhs.id_;
		}
	};

	export
	template <typename T>
	struct readonly_decay : std::type_identity<std::add_const_t<std::decay_t<T>>>{};

	template <typename T>
	struct readonly_decay<const T&> : readonly_decay<T>{};

	template <typename T>
	struct readonly_decay<const volatile T&> : readonly_decay<T>{};

	template <typename T>
	struct readonly_decay<T&> : std::type_identity<std::decay_t<T>>{};

	template <typename T>
	struct readonly_decay<volatile T&> : std::type_identity<std::decay_t<T>>{};

	export
	template <typename T>
	using readonly_const_decay_t = readonly_decay<T>::type;


	std::size_t archetype_base::insert(const entity_id entity){
		entity->id()->archetype_ = this;
		return 0;
	}

	void archetype_base::erase(const entity_id entity){
		entity->chunk_index_ = invalid_chunk_idx;
		entity->archetype_ = nullptr;
	}

}


template <>
struct std::hash<mo_yanxi::game::ecs::entity_ref>{
	static std::size_t operator()(const mo_yanxi::game::ecs::entity_ref& ref) noexcept{
		static constexpr std::hash<const void*> hasher{};
		return hasher(ref.id());
	}
};
