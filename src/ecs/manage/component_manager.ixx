module;

#include "plf_hive.h"
#include "gch/small_vector.hpp"
#include <cassert>
#include "mo_yanxi/adapted_attributes.hpp"

export module mo_yanxi.game.ecs.component.manage;

export import :entity;
export import :misc;
export import :serializer;
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

public:
	[[nodiscard]] constexpr entity_pin() noexcept = default;

	[[nodiscard]] explicit(false) constexpr entity_pin(const entity_id entity_id) noexcept
		: id_(entity_id){
	}

	entity_pin& operator=(const entity_id other) noexcept{
		id_ = other;
		return *this;
	}

	entity_pin& operator=(std::nullptr_t) noexcept{
		id_ = {};
		return *this;
	}

	[[nodiscard]] explicit operator bool() const noexcept{
		return static_cast<bool>(id_);
	}

	[[nodiscard]] entity_id raw_id() const noexcept{
		return id_;
	}

	[[nodiscard]] bool is_inserted() const noexcept{
		return id_.is_inserted();
	}

	[[nodiscard]] bool is_expired() const noexcept{
		return id_ && id_.is_expired();
	}

	[[nodiscard]] entity_id valid_id() const noexcept{
		return this->is_inserted() ? id_ : entity_id{};
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
		return id && id.is_inserted();
	}

	void acquire(const entity_id id) noexcept{
		id_ = can_acquire_ref(id) ? id : entity_id{};
	}

	void release() noexcept{
		id_ = {};
	}

public:
	[[nodiscard]] constexpr entity_ref() noexcept = default;

	[[nodiscard]] explicit(false) entity_ref(entity_id entity_id) noexcept{
		acquire(entity_id);
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

	entity_ref& operator=(std::nullptr_t) noexcept{
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

	const entity_id* operator->() const noexcept{
		assert(this->operator bool());
		return std::addressof(id_);
	}

	const entity_id& operator*() const noexcept{
		assert(this->operator bool());
		return id_;
	}

	void reset(const entity_id entity_id = {}) noexcept{
		this->operator=(entity_id);
	}

	explicit operator bool() const noexcept{
		return can_acquire_ref(id_);
	}

	[[nodiscard]] entity_id id() const noexcept{
		return can_acquire_ref(id_) ? id_ : entity_id{};
	}

	template <typename C, typename T>
	FORCE_INLINE T& operator->*(T C::* mptr) const noexcept{
		assert(this->operator bool());
		return id_.operator->*<C, T>(mptr);
	}


	template <typename T>
		requires (std::is_member_function_pointer_v<T>)
	FORCE_INLINE decltype(auto) operator->*(T mfptr) const noexcept{
		assert(this->operator bool());
		return id_.operator->*(mfptr);
	}

	explicit(false) operator entity_id() const noexcept{
		return id();
	}

	/**
	 * @brief drop the referenced entity if it is already erased
	 */
	bool drop_if_expired() noexcept{
		if(id_ && id_.is_expired()){
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
			if(id_.is_expired()){
				release();
				return false;
			}

			if constexpr(check_inserted){
				if(!id_.is_inserted()){
					return false;
				}
			}

			return true;
		}
	}

	[[nodiscard]] bool is_expired() const noexcept{
		return id_ && id_.is_expired();
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
	(void)entity;
	return 0;
}

void archetype_base::erase(const entity_id entity){
	if(!entity)return;
	this->erase_at(entity, entity->chunk_index());
}

void archetype_base::detach_entity(const entity_id entity) noexcept{
	ecs::detach_entity_from_archetype(entity);
}

void archetype_base::erase_at(const entity_id entity, std::size_t){
	archetype_base::detach_entity(entity);
}
}


template <>
struct std::hash<mo_yanxi::game::ecs::entity_id>{
	std::size_t operator()(const mo_yanxi::game::ecs::entity_id& id) const noexcept{
		std::size_t value = std::hash<const void*>{}(id.owner());
		value ^= std::hash<std::uint32_t>{}(id.slot()) + 0x9e3779b9u + (value << 6u) + (value >> 2u);
		value ^= std::hash<std::uint32_t>{}(id.generation()) + 0x9e3779b9u + (value << 6u) + (value >> 2u);
		return value;
	}
};

template <>
struct std::hash<mo_yanxi::game::ecs::entity_ref>{
	std::size_t operator()(const mo_yanxi::game::ecs::entity_ref& ref) const noexcept{
		return std::hash<mo_yanxi::game::ecs::entity_id>{}(ref.id());
	}
};
