// ReSharper disable CppDFAUnreachableCode
module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.game.ecs.object_storage;

export import mo_yanxi.soa_vector;
export import mo_yanxi.type_register;

import std;

namespace mo_yanxi::game::ecs{
export struct object_handle{
	std::uint32_t channel{std::numeric_limits<std::uint32_t>::max()};
	std::uint32_t slot{std::numeric_limits<std::uint32_t>::max()};
	std::uint64_t generation{};

	[[nodiscard]] constexpr explicit operator bool() const noexcept{
		return channel != std::numeric_limits<std::uint32_t>::max()
			&& slot != std::numeric_limits<std::uint32_t>::max()
			&& generation != 0;
	}

	friend constexpr bool operator==(object_handle, object_handle) noexcept = default;
};

export struct object_common{
	object_handle handle{};
};

export template <typename Common>
concept object_common_like = requires(Common& common, object_handle handle){
	{ common.handle } -> std::same_as<object_handle&>;
	common.handle = handle;
};

export enum class object_event_post_result{
	queued,
	invalid_target
};

export enum class object_event_delivery_result{
	delivered,
	expired_target,
	unsupported_event
};

export struct object_event_delivery_counts{
	static constexpr std::size_t result_count{3};

	std::array<std::size_t, result_count> results{};

	constexpr void record(const object_event_delivery_result result) noexcept{
		++results[std::to_underlying(result)];
	}

	[[nodiscard]] constexpr std::size_t count(const object_event_delivery_result result) const noexcept{
		return results[std::to_underlying(result)];
	}

	[[nodiscard]] constexpr std::size_t total() const noexcept{
		std::size_t value{};
		for(const std::size_t count : results){
			value += count;
		}
		return value;
	}
};

export struct object_event_delivery_record{
	object_handle target{};
	type_identity_index event_type{};
	object_event_delivery_result result{};
};

export struct object_event_delivery_report{
	std::vector<object_event_delivery_record> records{};
	object_event_delivery_counts totals{};
	std::flat_map<type_identity_index, object_event_delivery_counts> counts_by_type{};

	void record(
		const object_handle target,
		const type_identity_index event_type,
		const object_event_delivery_result result){
		records.push_back(object_event_delivery_record{
				.target = target,
				.event_type = event_type,
				.result = result
			});
		totals.record(result);
		auto [it, inserted] = counts_by_type.try_emplace(event_type);
		(void)inserted;
		it->second.record(result);
	}

	[[nodiscard]] std::size_t size() const noexcept{
		return records.size();
	}

	[[nodiscard]] std::size_t count(const object_event_delivery_result result) const noexcept{
		return totals.count(result);
	}

	[[nodiscard]] std::size_t count(
		const type_identity_index event_type,
		const object_event_delivery_result result) const{
		const auto it = counts_by_type.find(event_type);
		if(it == counts_by_type.end()){
			return 0;
		}
		return it->second.count(result);
	}

	template <typename Event>
	[[nodiscard]] std::size_t count(const object_event_delivery_result result) const{
		return this->count(mo_yanxi::unstable_type_identity_of<Event>(), result);
	}
};

export template <typename... Events>
struct object_event_set{
};

export template <typename System, typename Context, typename Object, typename Common>
concept object_update_system = requires(
	System& system,
	Context& context,
	Common& common,
	Object& object){
		{ system.update(context, common, object) } -> std::same_as<void>;
	};

export template <typename System, typename Context, typename Object, typename Common, typename Event>
concept object_event_system = requires(
	System& system,
	Context& context,
	Common& common,
	Object& object,
	const Event& event){
		{ system.operator()(context, common, object, event) } -> std::same_as<object_event_delivery_result>;
	};

namespace detail{
template <typename>
struct is_object_event_set : std::false_type{
};

template <typename... Events>
struct is_object_event_set<object_event_set<Events...>> : std::true_type{
};

template <typename T>
inline constexpr bool is_object_event_set_v = is_object_event_set<T>::value;

template <typename System>
concept has_object_events = requires{
	typename System::object_events;
};

[[nodiscard]] constexpr std::size_t align_forward(
	const std::size_t value,
	const std::size_t alignment) noexcept{
	return (value + alignment - 1) & ~(alignment - 1);
}

[[nodiscard]] constexpr std::size_t max_alignment(
	const std::size_t lhs,
	const std::size_t rhs) noexcept{
	return lhs < rhs ? rhs : lhs;
}

struct pending_event_head{
	mo_yanxi::type_identity_index index{};
	object_handle target{};
	std::uint32_t payload_offset{};
	std::uint32_t total_size{};
	void (*destroy_payload)(void*) noexcept{};
};

struct pending_event_block{
	std::byte* data{};
	std::size_t used{};
	std::size_t capacity{};
	std::size_t alignment{};

	pending_event_block() = default;

	pending_event_block(
		std::byte* data_,
		const std::size_t capacity_,
		const std::size_t alignment_) noexcept
		: data(data_),
		  capacity(capacity_),
		  alignment(alignment_){
	}

	pending_event_block(const pending_event_block&) = delete;
	pending_event_block& operator=(const pending_event_block&) = delete;

	pending_event_block(pending_event_block&& other) noexcept
		: data(std::exchange(other.data, nullptr)),
		  used(std::exchange(other.used, 0)),
		  capacity(std::exchange(other.capacity, 0)),
		  alignment(std::exchange(other.alignment, 0)){
	}

	pending_event_block& operator=(pending_event_block&& other) noexcept{
		if(this == std::addressof(other)){
			return *this;
		}
		this->deallocate();
		data = std::exchange(other.data, nullptr);
		used = std::exchange(other.used, 0);
		capacity = std::exchange(other.capacity, 0);
		alignment = std::exchange(other.alignment, 0);
		return *this;
	}

	~pending_event_block(){
		this->deallocate();
	}

private:
	void deallocate() noexcept{
		if(data == nullptr){
			return;
		}
		::operator delete(data, std::align_val_t{alignment});
		data = nullptr;
		used = 0;
		capacity = 0;
		alignment = 0;
	}
};

class pending_event_buffer{
	static constexpr std::size_t default_block_capacity{4096};

	std::vector<pending_event_block> blocks_{};
	std::size_t event_count_{};

	[[nodiscard]] static pending_event_block allocate_block(
		const std::size_t minimum_capacity,
		const std::size_t required_alignment){
		const std::size_t alignment = max_alignment(required_alignment, alignof(pending_event_head));
		const std::size_t capacity = std::max(default_block_capacity, align_forward(minimum_capacity, alignment));
		auto* data = static_cast<std::byte*>(::operator new(capacity, std::align_val_t{alignment}));
		return pending_event_block{data, capacity, alignment};
	}

	[[nodiscard]] static bool can_append(
		const pending_event_block& block,
		const std::size_t total_size,
		const std::size_t required_alignment) noexcept{
		if(block.data == nullptr || block.alignment < required_alignment){
			return false;
		}
		const std::size_t event_offset = align_forward(block.used, alignof(pending_event_head));
		return event_offset + total_size <= block.capacity;
	}

	[[nodiscard]] pending_event_block& writable_block(
		const std::size_t total_size,
		const std::size_t required_alignment){
		if(blocks_.empty() || !can_append(blocks_.back(), total_size, required_alignment)){
			blocks_.push_back(allocate_block(total_size, required_alignment));
		}
		return blocks_.back();
	}

	template <typename Event>
	[[nodiscard]] static consteval auto payload_destroyer() noexcept{
		if constexpr(std::is_trivially_destructible_v<Event>){
			return static_cast<void (*)(void*) noexcept>(nullptr);
		} else{
			return +[](void* payload) noexcept{
				std::destroy_at(std::launder(static_cast<Event*>(payload)));
			};
		}
	}

public:
	pending_event_buffer() = default;

	pending_event_buffer(const pending_event_buffer&) = delete;
	pending_event_buffer& operator=(const pending_event_buffer&) = delete;

	pending_event_buffer(pending_event_buffer&& other) noexcept
		: blocks_(std::move(other.blocks_)),
		  event_count_(std::exchange(other.event_count_, 0)){
	}

	pending_event_buffer& operator=(pending_event_buffer&& other) noexcept{
		if(this == std::addressof(other)){
			return *this;
		}
		this->clear();
		blocks_ = std::move(other.blocks_);
		event_count_ = std::exchange(other.event_count_, 0);
		return *this;
	}

	~pending_event_buffer(){
		this->clear();
	}

	[[nodiscard]] bool empty() const noexcept{
		return event_count_ == 0;
	}

	[[nodiscard]] std::size_t size() const noexcept{
		return event_count_;
	}

	template <typename Event, typename... Args>
		requires std::constructible_from<Event, Args&&...>
	void emplace(object_handle target, Args&&... args){
		static constexpr std::size_t payload_offset = align_forward(sizeof(pending_event_head), alignof(Event));
		static constexpr std::size_t total_size = align_forward(
			payload_offset + sizeof(Event),
			alignof(pending_event_head));
		static constexpr std::size_t required_alignment = max_alignment(
			alignof(pending_event_head),
			alignof(Event));

		if(!std::in_range<std::uint32_t>(payload_offset) || !std::in_range<std::uint32_t>(total_size)){
			throw std::bad_alloc{};
		}

		pending_event_block& block = this->writable_block(total_size, required_alignment);
		const std::size_t event_offset = align_forward(block.used, alignof(pending_event_head));
		std::byte* event_base = block.data + event_offset;
		auto* head = new(event_base) pending_event_head{
				.index = mo_yanxi::unstable_type_identity_of<Event>(),
				.target = target,
				.payload_offset = static_cast<std::uint32_t>(payload_offset),
				.total_size = static_cast<std::uint32_t>(total_size),
				.destroy_payload = payload_destroyer<Event>()
			};

		try{
			new(event_base + payload_offset) Event(std::forward<Args>(args)...);
		} catch(...){
			head->~pending_event_head();
			throw;
		}

		block.used = event_offset + total_size;
		++event_count_;
	}

	template <typename Fn>
	void for_each(Fn&& fn) const{
		for(const pending_event_block& block : blocks_){
			std::size_t event_offset{};
			while(event_offset < block.used){
				const auto* head = std::launder(reinterpret_cast<const pending_event_head*>(
					block.data + event_offset));
				const void* payload = static_cast<const void*>(block.data + event_offset + head->payload_offset);
				std::invoke(fn, *head, payload);
				event_offset += head->total_size;
			}
		}
	}

	void clear() noexcept{
		for(pending_event_block& block : blocks_){
			std::size_t event_offset{};
			while(event_offset < block.used){
				auto* head = std::launder(reinterpret_cast<pending_event_head*>(block.data + event_offset));
				if(head->destroy_payload != nullptr){
					void* payload = static_cast<void*>(block.data + event_offset + head->payload_offset);
					head->destroy_payload(payload);
				}
				event_offset += head->total_size;
			}
			block.used = 0;
		}
		blocks_.clear();
		event_count_ = 0;
	}
};
}

export template <typename Context, typename Common = object_common>
	requires object_common_like<Common>
struct object_channel_base{
	virtual ~object_channel_base() = default;

	[[nodiscard]] virtual mo_yanxi::type_identity_index object_type() const noexcept = 0;
	[[nodiscard]] virtual std::uint32_t channel_index() const noexcept = 0;
	[[nodiscard]] virtual std::size_t size() const noexcept = 0;
	[[nodiscard]] virtual bool contains(object_handle handle) const noexcept = 0;

	[[nodiscard]] Common* try_common(const object_handle handle) noexcept{
		return this->try_common_impl(handle);
	}

	[[nodiscard]] const Common* try_common(const object_handle handle) const noexcept{
		return const_cast<object_channel_base*>(this)->try_common_impl(handle);
	}

	[[nodiscard]] std::span<Common> common_span() noexcept{
		return this->common_span_impl();
	}

	[[nodiscard]] std::span<const Common> common_span() const noexcept{
		return const_cast<object_channel_base*>(this)->common_span_impl();
	}

	virtual bool erase(object_handle handle) = 0;
	virtual void update_all(Context& context) = 0;
	virtual object_event_delivery_result operator()(
		Context& context,
		object_handle target,
		mo_yanxi::type_identity_index event_type,
		const void* event_payload) = 0;

protected:
	[[nodiscard]] virtual Common* try_common_impl(object_handle handle) noexcept = 0;
	[[nodiscard]] virtual std::span<Common> common_span_impl() noexcept = 0;
};

export template <typename Object, typename Context, typename Common = object_common>
	requires object_common_like<Common>
struct object_channel_typed_base : object_channel_base<Context, Common>{
	[[nodiscard]] virtual object_handle emplace_value(Common common, Object&& object) = 0;

	[[nodiscard]] Object* try_get(const object_handle handle) noexcept{
		return this->try_get_impl(handle);
	}

	[[nodiscard]] const Object* try_get(const object_handle handle) const noexcept{
		return const_cast<object_channel_typed_base*>(this)->try_get_impl(handle);
	}

	[[nodiscard]] std::span<Object> object_span() noexcept{
		return this->object_span_impl();
	}

	[[nodiscard]] std::span<const Object> object_span() const noexcept{
		return const_cast<object_channel_typed_base*>(this)->object_span_impl();
	}

protected:
	[[nodiscard]] virtual Object* try_get_impl(object_handle handle) noexcept = 0;
	[[nodiscard]] virtual std::span<Object> object_span_impl() noexcept = 0;
};

struct slot_record{
	std::uint32_t row{};
	std::uint64_t generation{1};
	bool alive{};
};

export template <typename Object, typename Context, typename System, typename Common = object_common>
	requires object_common_like<Common> && object_update_system<System, Context, Object, Common>
struct object_channel final : object_channel_typed_base<Object, Context, Common>{
	using storage_type = soa_vector<std::allocator<std::byte>, Common, Object>;
	using size_type = storage_type::size_type;

private:
	using event_handler_fn = object_event_delivery_result (*)(
		System&,
		Context&,
		Common&,
		Object&,
		const void*);

	static constexpr size_type invalid_row = std::numeric_limits<size_type>::max();

	std::uint32_t channel_index_{};
	storage_type storage_{};
	std::vector<slot_record> slots_{};
	std::vector<std::uint32_t> row_to_slot_{};
	std::vector<std::uint32_t> free_slots_{};
	std::flat_map<type_identity_index, event_handler_fn> event_handlers_{};
	ADAPTED_NO_UNIQUE_ADDRESS System system_{};

	template <typename Event>
	void register_event_handler(){
		static_assert(
				object_event_system<System, Context, Object, Common, Event>,
				"Each event in System::object_events must have operator()(Context&, Common&, Object&, const Event&).")
			;

		event_handlers_.emplace(
			mo_yanxi::unstable_type_identity_of<Event>(),
			+[](System& system, Context& context, Common& common, Object& object, const void* payload){
				return system.operator()(
					context,
					common,
					object,
					*std::launder(static_cast<const Event*>(payload)));
			});
	}
	template <typename Event, typename Fn>
		requires (std::is_empty_v<Fn> && std::is_invocable_r_v<object_event_delivery_result, Fn, System&, Context&, Common&, Object&, const Event&>)
	void register_event_handler(Fn fn){
		event_handlers_.emplace(
			mo_yanxi::unstable_type_identity_of<Event>(),
			+[](System& system, Context& context, Common& common, Object& object, const void* payload){
				return Fn{}(
					system,
					context,
					common,
					object,
					*std::launder(static_cast<const Event*>(payload)));
			});
	}

	template <typename... Events>
	void register_event_handlers(object_event_set<Events...>){
		(this->register_event_handler<Events>(), ...);
	}

	void register_default_event_handlers(){
		if constexpr(detail::has_object_events<System>){
			static_assert(
				detail::is_object_event_set_v<typename System::object_events>,
				"System::object_events must be object_event_set<...>.");
			this->register_event_handlers(typename System::object_events{});
		}
	}

	[[nodiscard]] std::uint32_t acquire_slot(const size_type row){
		if(!free_slots_.empty()){
			const std::uint32_t slot = free_slots_.back();
			free_slots_.pop_back();
			slot_record& record = slots_[slot];
			record.row = row;
			record.alive = true;
			return slot;
		}

		if(slots_.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())){
			throw std::length_error{"object_channel slot capacity exceeded"};
		}

		const auto slot = static_cast<std::uint32_t>(slots_.size());
		slots_.push_back(slot_record{
				.row = row,
				.generation = 1,
				.alive = true
			});
		return slot;
	}

	void release_failed_slot(const std::uint32_t slot) noexcept{
		slot_record& record = slots_[slot];
		record.row = invalid_row;
		record.alive = false;
		try{
			free_slots_.push_back(slot);
		} catch(...){
			std::terminate();
		}
	}

	[[nodiscard]] object_handle make_handle(const std::uint32_t slot) const noexcept{
		return {
				.channel = channel_index_,
				.slot = slot,
				.generation = slots_[slot].generation
			};
	}

	[[nodiscard]] size_type row_index_of(const object_handle handle) const noexcept{
		if(handle.channel != channel_index_ || handle.slot >= slots_.size()){
			return invalid_row;
		}

		const slot_record& record = slots_[handle.slot];
		if(!record.alive || record.generation != handle.generation || record.row >= storage_.size()){
			return invalid_row;
		}
		return record.row;
	}

	void update_moved_row(const size_type row) noexcept{
		const std::uint32_t slot = row_to_slot_[row];
		slots_[slot].row = row;
		storage_.template get<Common>(row).handle = this->make_handle(slot);
	}

protected:
	[[nodiscard]] Common* try_common_impl(const object_handle handle) noexcept override{
		const size_type row = this->row_index_of(handle);
		if(row == invalid_row){
			return nullptr;
		}
		return std::addressof(storage_.template get<Common>(row));
	}

	[[nodiscard]] Object* try_get_impl(const object_handle handle) noexcept override{
		const size_type row = this->row_index_of(handle);
		if(row == invalid_row){
			return nullptr;
		}
		return std::addressof(storage_.template get<Object>(row));
	}

	[[nodiscard]] std::span<Common> common_span_impl() noexcept override{
		return storage_.template column<Common>();
	}

	[[nodiscard]] std::span<Object> object_span_impl() noexcept override{
		return storage_.template column<Object>();
	}

public:
	[[nodiscard]] explicit object_channel(const std::uint32_t channel_index, System system = {})
		: channel_index_(channel_index),
		  system_(std::move(system)){
		this->register_default_event_handlers();
	}

	[[nodiscard]] type_identity_index object_type() const noexcept override{
		return mo_yanxi::unstable_type_identity_of<Object>();
	}

	[[nodiscard]] std::uint32_t channel_index() const noexcept override{
		return channel_index_;
	}

	[[nodiscard]] std::size_t size() const noexcept override{
		return storage_.size();
	}

	[[nodiscard]] bool contains(const object_handle handle) const noexcept override{
		return this->row_index_of(handle) != invalid_row;
	}

	[[nodiscard]] object_handle emplace_value(Common common, Object&& object) override{
		const size_type row = storage_.size();
		const std::uint32_t slot = this->acquire_slot(row);
		const object_handle handle = this->make_handle(slot);
		common.handle = handle;

		bool row_recorded{};
		try{
			row_to_slot_.push_back(slot);
			row_recorded = true;
			storage_.emplace_back(std::move(common), std::move(object));
		} catch(...){
			if(row_recorded){
				row_to_slot_.pop_back();
			}
			this->release_failed_slot(slot);
			throw;
		}

		return handle;
	}

	bool erase(const object_handle handle) override{
		const size_type row = this->row_index_of(handle);
		if(row == invalid_row){
			return false;
		}

		const std::uint32_t erased_slot = handle.slot;
		storage_.erase_unstable(row, [this](storage_type&, const size_type moved_row) noexcept{
			row_to_slot_[moved_row] = row_to_slot_.back();
			this->update_moved_row(moved_row);
		});
		row_to_slot_.pop_back();

		slot_record& record = slots_[erased_slot];
		record.row = invalid_row;
		record.alive = false;
		++record.generation;
		if(record.generation == 0){
			std::terminate();
		}
		free_slots_.push_back(erased_slot);
		return true;
	}

	void update_all(Context& context) override{
		for(size_type row = 0; row != storage_.size(); ++row){
			system_.update(
				context,
				storage_.template get<Common>(row),
				storage_.template get<Object>(row));
		}
	}

	object_event_delivery_result operator()(
		Context& context,
		const object_handle target,
		const type_identity_index event_type,
		const void* event_payload) override{
		const size_type row = this->row_index_of(target);
		if(row == invalid_row){
			return object_event_delivery_result::expired_target;
		}

		const auto it = event_handlers_.find(event_type);
		if(it == event_handlers_.end()){
			return object_event_delivery_result::unsupported_event;
		}

		return it->second(
			system_,
			context,
			storage_.template get<Common>(row),
			storage_.template get<Object>(row),
			event_payload);
	}
};

export template <typename Context, typename Common = object_common>
	requires object_common_like<Common>
struct object_collection{
private:
	std::vector<std::unique_ptr<object_channel_base<Context, Common>>> channels_{};
	std::flat_map<mo_yanxi::type_identity_index, object_channel_base<Context, Common>*> type_to_channel_{};
	detail::pending_event_buffer pending_events_{};

	[[nodiscard]] object_channel_base<Context, Common>* channel_base_at(const object_handle handle) noexcept{
		if(handle.channel >= channels_.size()){
			return nullptr;
		}
		return channels_[handle.channel].get();
	}

	[[nodiscard]] const object_channel_base<Context, Common>* channel_base_at(const object_handle handle) const noexcept{
		if(handle.channel >= channels_.size()){
			return nullptr;
		}
		return channels_[handle.channel].get();
	}

public:
	[[nodiscard]] std::size_t channel_count() const noexcept{
		return channels_.size();
	}

	[[nodiscard]] bool empty() const noexcept{
		return channels_.empty();
	}

	template <typename Object, typename System>
		requires object_update_system<System, Context, Object, Common>
	object_channel<Object, Context, System, Common>& register_channel(System system){
		const auto type = mo_yanxi::unstable_type_identity_of<Object>();
		if(type_to_channel_.contains(type)){
			throw std::logic_error{"object channel is already registered"};
		}

		if(channels_.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())){
			throw std::length_error{"object_collection channel capacity exceeded"};
		}

		const auto channel_index = static_cast<std::uint32_t>(channels_.size());
		auto channel = std::make_unique<object_channel<Object, Context, System, Common>>(channel_index, std::move(system));
		auto* result = channel.get();
		type_to_channel_.emplace(type, result);
		try{
			channels_.push_back(std::move(channel));
		} catch(...){
			type_to_channel_.erase(type);
			throw;
		}
		return *result;
	}

	template <typename Object>
	[[nodiscard]] bool contains_channel() const noexcept{
		return type_to_channel_.contains(mo_yanxi::unstable_type_identity_of<Object>());
	}

	template <typename Object>
	[[nodiscard]] object_channel_typed_base<Object, Context, Common>* try_channel() noexcept{
		const auto it = type_to_channel_.find(mo_yanxi::unstable_type_identity_of<Object>());
		if(it == type_to_channel_.end()){
			return nullptr;
		}
		return static_cast<object_channel_typed_base<Object, Context, Common>*>(it->second);
	}

	template <typename Object>
	[[nodiscard]] const object_channel_typed_base<Object, Context, Common>* try_channel() const noexcept{
		const auto it = type_to_channel_.find(mo_yanxi::unstable_type_identity_of<Object>());
		if(it == type_to_channel_.end()){
			return nullptr;
		}
		return static_cast<const object_channel_typed_base<Object, Context, Common>*>(it->second);
	}

	template <typename Object>
	[[nodiscard]] object_channel_typed_base<Object, Context, Common>& channel(){
		if(auto* channel = this->try_channel<Object>()){
			return *channel;
		}
		throw std::logic_error{"object channel is not registered"};
	}

	template <typename Object>
	[[nodiscard]] const object_channel_typed_base<Object, Context, Common>& channel() const{
		if(const auto* channel = this->try_channel<Object>()){
			return *channel;
		}
		throw std::logic_error{"object channel is not registered"};
	}

	template <typename Object, typename... Args>
	[[nodiscard]] object_handle emplace(Common common, Args&&... args){
		return this->channel<Object>().emplace_value(
			std::move(common),
			Object{std::forward<Args>(args)...});
	}

	template <typename Object>
	[[nodiscard]] Object* try_get(const object_handle handle) noexcept{
		auto* channel = this->try_channel<Object>();
		if(channel == nullptr || channel->channel_index() != handle.channel){
			return nullptr;
		}
		return channel->try_get(handle);
	}

	template <typename Object>
	[[nodiscard]] const Object* try_get(const object_handle handle) const noexcept{
		const auto* channel = this->try_channel<Object>();
		if(channel == nullptr || channel->channel_index() != handle.channel){
			return nullptr;
		}
		return channel->try_get(handle);
	}

	[[nodiscard]] Common* try_common(const object_handle handle) noexcept{
		if(auto* channel = this->channel_base_at(handle)){
			return channel->try_common(handle);
		}
		return nullptr;
	}

	[[nodiscard]] const Common* try_common(const object_handle handle) const noexcept{
		if(const auto* channel = this->channel_base_at(handle)){
			return channel->try_common(handle);
		}
		return nullptr;
	}

	bool erase(const object_handle handle){
		if(auto* channel = this->channel_base_at(handle)){
			return channel->erase(handle);
		}
		return false;
	}

	template <typename Object, typename Fn>
	std::size_t for_each(Fn&& fn){
		auto& channel = this->channel<Object>();
		auto commons = channel.common_span();
		auto objects = channel.object_span();
		for(std::size_t i = 0; i != objects.size(); ++i){
			if constexpr(std::invocable<Fn&, Common&, Object&>){
				std::invoke(fn, commons[i], objects[i]);
			} else{
				std::invoke(fn, objects[i]);
			}
		}
		return objects.size();
	}

	template <typename Object, typename Fn>
	std::size_t for_each(Fn&& fn) const{
		const auto& channel = this->channel<Object>();
		auto commons = channel.common_span();
		auto objects = channel.object_span();
		for(std::size_t i = 0; i != objects.size(); ++i){
			if constexpr(std::invocable<Fn&, const Common&, const Object&>){
				std::invoke(fn, commons[i], objects[i]);
			} else{
				std::invoke(fn, objects[i]);
			}
		}
		return objects.size();
	}

	void update_all(Context& context){
		for(auto& channel : channels_){
			channel->update_all(context);
		}
	}

	template <typename Event>
	[[nodiscard]] object_event_post_result post(object_handle target, Event&& event){
		using event_type = std::remove_cvref_t<Event>;
		if(this->try_common(target) == nullptr){
			return object_event_post_result::invalid_target;
		}

		pending_events_.emplace<event_type>(target, std::forward<Event>(event));
		return object_event_post_result::queued;
	}

	template <typename Event, typename... Args>
		requires std::constructible_from<Event, Args&&...>
	[[nodiscard]] object_event_post_result emplace_event(object_handle target, Args&&... args){
		if(this->try_common(target) == nullptr){
			return object_event_post_result::invalid_target;
		}

		pending_events_.emplace<Event>(target, std::forward<Args>(args)...);
		return object_event_post_result::queued;
	}

	[[nodiscard]] object_event_delivery_report deliver_events(Context& context){
		object_event_delivery_report report{};
		report.records.reserve(pending_events_.size());
		detail::pending_event_buffer events = std::exchange(pending_events_, {});

		events.for_each([&](const detail::pending_event_head& event, const void* payload){
			auto* channel = this->channel_base_at(event.target);
			if(channel == nullptr){
				report.record(event.target, event.index, object_event_delivery_result::expired_target);
				return;
			}
			report.record(
				event.target,
				event.index,
				channel->operator()(context, event.target, event.index, payload));
		});
		return report;
	}
};
}
