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
struct entity_pin{
private:
	entity_id id_{};

	[[nodiscard]] static bool can_pin(const entity_id id) noexcept{
		return id && id->get_state() == entity_state::valid;
	}

	void acquire_valid(const entity_id id) noexcept{
		if(can_pin(id)){
			id_ = id;
			id_->referenced_count.fetch_add(1, std::memory_order_relaxed);
		} else{
			id_ = nullptr;
		}
	}

	void acquire_pinned(const entity_id id) noexcept{
		if(id){
			id_ = id;
			id_->referenced_count.fetch_add(1, std::memory_order_relaxed);
		} else{
			id_ = nullptr;
		}
	}

	void release() noexcept{
		if(id_){
			const auto rst = id_->referenced_count.fetch_sub(1, std::memory_order_relaxed);
			assert(rst != 0);
			id_ = nullptr;
		}
	}

public:
	[[nodiscard]] constexpr entity_pin() noexcept = default;

	[[nodiscard]] explicit(false) entity_pin(const entity_id entity_id) noexcept{
		this->acquire_valid(entity_id);
	}

	[[nodiscard]] explicit(false) entity_pin(entity& entity) noexcept{
		this->acquire_valid(entity.id());
	}

	entity_pin(const entity_pin& other) noexcept{
		this->acquire_pinned(other.id_);
	}

	entity_pin(entity_pin&& other) noexcept
		: id_{std::exchange(other.id_, {})}{
	}

	entity_pin& operator=(const entity_pin& other) noexcept{
		if(this == &other) return *this;
		this->release();
		this->acquire_pinned(other.id_);
		return *this;
	}

	entity_pin& operator=(entity_pin&& other) noexcept{
		if(this == &other) return *this;
		this->release();
		id_ = std::exchange(other.id_, {});
		return *this;
	}

	entity_pin& operator=(const entity_id other) noexcept{
		if(id_ == other){
			if(!can_pin(id_)){
				this->release();
			}
			return *this;
		}

		this->release();
		this->acquire_valid(other);
		return *this;
	}

	entity_pin& operator=(std::nullptr_t) noexcept{
		this->release();
		return *this;
	}

	~entity_pin(){
		this->release();
	}

	[[nodiscard]] explicit operator bool() const noexcept{
		return id_ != nullptr;
	}

	[[nodiscard]] entity_id raw_id() const noexcept{
		return id_;
	}

	[[nodiscard]] bool is_inserted() const noexcept{
		return id_ && id_->is_inserted();
	}

	[[nodiscard]] bool is_expired() const noexcept{
		return id_ && id_->is_expired();
	}

	[[nodiscard]] entity_id valid_id() const noexcept{
		return this->is_inserted() ? id_ : nullptr;
	}

	friend bool operator==(const entity_pin& lhs, const entity_pin& rhs) noexcept = default;

	friend bool operator==(const entity_pin& lhs, const entity_id rhs) noexcept{
		return lhs.raw_id() == rhs;
	}

	friend bool operator==(const entity_id lhs, const entity_pin& rhs) noexcept{
		return lhs == rhs.raw_id();
	}
};

export
struct entity_ref{
private:
	entity_id id_{};

	[[nodiscard]] static bool can_acquire_ref(const entity_id id) noexcept{
		return id && id->get_state() == entity_state::valid;
	}

	void acquire(const entity_id id) noexcept{
		if(can_acquire_ref(id)){
			id_ = id;
			id_->referenced_count.fetch_add(1, std::memory_order_relaxed);
		} else{
			id_ = nullptr;
		}
	}

	void release() noexcept{
		if(id_){
			auto rst = id_->referenced_count.fetch_sub(1, std::memory_order_relaxed);
			assert(rst != 0);
			id_ = nullptr;
		}
	}

public:
	[[nodiscard]] constexpr entity_ref() noexcept = default;

	[[nodiscard]] explicit(false) entity_ref(entity_id entity_id) noexcept{
		acquire(entity_id);
	}

	[[nodiscard]] explicit(false) entity_ref(entity& entity) noexcept{
		acquire(entity.id());
	}

	entity_ref(const entity_ref& other) noexcept{
		acquire(other.id_);
	}

	entity_ref(entity_ref&& other) noexcept
		: id_{std::exchange(other.id_, {})}{
	}

	entity_ref& operator=(const entity_ref& other) noexcept{
		if(this == &other) return *this;
		release();
		acquire(other.id_);
		return *this;
	}

	entity_ref& operator=(entity_id other) noexcept{
		if(id_ == other){
			if(!can_acquire_ref(id_)){
				release();
			}
			return *this;
		}

		release();
		acquire(other);
		return *this;
	}

	entity_ref& operator=(std::nullptr_t other) noexcept{
		release();
		return *this;
	}

	entity_ref& operator=(entity_ref&& other) noexcept{
		if(this == &other) return *this;
		release();
		id_ = std::exchange(other.id_, {});
		return *this;
	}

	~entity_ref(){
		release();
	}

	entity* operator->() const noexcept{
		assert(this->operator bool());
		return id_;
	}

	entity& operator*() const noexcept{
		assert(this->operator bool());
		return *id_;
	}

	void reset(const entity_id entity_id = nullptr) noexcept{
		this->operator=(entity_id);
	}

	void reset(entity& entity) noexcept{
		this->operator=(entity);
	}

	explicit operator bool() const noexcept{
		return can_acquire_ref(id_);
	}

	[[nodiscard]] entity_id id() const noexcept{
		return can_acquire_ref(id_) ? id_ : nullptr;
	}

	template <typename C, typename T>
	FORCE_INLINE T& operator->*(T C::* mptr) const noexcept{
		assert(this->operator bool());
		return id_->operator->*<C, T>(mptr);
	}


	template <typename T>
		requires (std::is_member_function_pointer_v<T>)
	FORCE_INLINE decltype(auto) operator->*(T mfptr) const noexcept{
		assert(this->operator bool());
		return id_->operator->*(mfptr);
	}

	explicit(false) operator entity&() const noexcept{
		assert(this->operator bool());
		return *id_;
	}

	explicit(false) operator entity_id() const noexcept{
		return id();
	}

	/**
	 * @brief drop the referenced entity if it is already erased
	 */
	bool drop_if_expired() noexcept{
		if(id_ && id_->is_expired()){
			release();
			return true;
		}
		return false;
	}

	/**
	 * @brief drop the referenced entity if it is already erased
	 */
	template <bool check_inserted = false>
	bool check_or_drop() noexcept{
		if(!id_) return false;
		else{
			if(id_->is_expired()){
				release();
				return false;
			}

			if constexpr(check_inserted){
				if(!id_->is_inserted()){
					return false;
				}
			}

			return true;
		}
	}

	[[nodiscard]] bool is_expired() const noexcept{
		return id_ && id_->is_expired();
	}

	[[nodiscard]] bool is_valid() const noexcept{
		return can_acquire_ref(id_);
	}

	friend bool operator==(const entity_ref& lhs, const entity_ref& rhs) noexcept{
		return lhs.id() == rhs.id();
	}

	friend bool operator==(const entity_ref& lhs, const entity_id eid) noexcept{
		return lhs.id() == eid;
	}

	friend bool operator==(const entity_id eid, const entity_ref& rhs) noexcept{
		return eid == rhs.id();
	}
};

export
template <typename T>
struct readonly_decay : std::type_identity<std::add_const_t<std::decay_t<T>>>{
};

template <typename T>
struct readonly_decay<const T&> : readonly_decay<T>{
};

template <typename T>
struct readonly_decay<const volatile T&> : readonly_decay<T>{
};

template <typename T>
struct readonly_decay<T&> : std::type_identity<std::decay_t<T>>{
};

template <typename T>
struct readonly_decay<volatile T&> : std::type_identity<std::decay_t<T>>{
};

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

void archetype_base::erase_at(const entity_id entity, std::size_t){
	erase(entity);
}
}


template <>
struct std::hash<mo_yanxi::game::ecs::entity_ref>{
	static std::size_t operator()(const mo_yanxi::game::ecs::entity_ref& ref) noexcept{
		static constexpr std::hash<const void*> hasher{};
		return hasher(ref.id());
	}
};
