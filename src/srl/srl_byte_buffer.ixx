module;

#include <cstddef>
#include <new>

export module mo_yanxi.srl.srl_byte_buffer;

import std;

export namespace mo_yanxi::srl{
enum class srl_byte_buffer_error : std::uint8_t{
	none,
	allocation_failed,
	size_overflow,
	overwrite_size_out_of_bounds
};

class srl_byte_buffer{
public:
	using value_type = std::byte;
	using size_type = std::size_t;
	using iterator = std::byte*;
	using const_iterator = const std::byte*;

	static constexpr size_type alignment = 16u;

	[[nodiscard]] constexpr srl_byte_buffer() noexcept = default;

	srl_byte_buffer(const srl_byte_buffer&) = delete;
	srl_byte_buffer& operator=(const srl_byte_buffer&) = delete;

	[[nodiscard]] constexpr srl_byte_buffer(srl_byte_buffer&& other) noexcept
		: data_{std::exchange(other.data_, nullptr)},
		  size_{std::exchange(other.size_, 0u)},
		  capacity_{std::exchange(other.capacity_, 0u)}{
	}

	srl_byte_buffer& operator=(srl_byte_buffer&& other) noexcept{
		if(this == std::addressof(other)){
			return *this;
		}
		this->release();
		data_ = std::exchange(other.data_, nullptr);
		size_ = std::exchange(other.size_, 0u);
		capacity_ = std::exchange(other.capacity_, 0u);
		return *this;
	}

	~srl_byte_buffer() noexcept{
		this->release();
	}

	[[nodiscard]] constexpr std::byte* data() noexcept{
		return data_;
	}

	[[nodiscard]] constexpr const std::byte* data() const noexcept{
		return data_;
	}

	[[nodiscard]] constexpr size_type size() const noexcept{
		return size_;
	}

	[[nodiscard]] constexpr size_type capacity() const noexcept{
		return capacity_;
	}

	[[nodiscard]] constexpr bool empty() const noexcept{
		return size_ == 0u;
	}

	[[nodiscard]] constexpr iterator begin() noexcept{
		return data_;
	}

	[[nodiscard]] constexpr const_iterator begin() const noexcept{
		return data_;
	}

	[[nodiscard]] constexpr const_iterator cbegin() const noexcept{
		return data_;
	}

	[[nodiscard]] constexpr iterator end() noexcept{
		return data_ + size_;
	}

	[[nodiscard]] constexpr const_iterator end() const noexcept{
		return data_ + size_;
	}

	[[nodiscard]] constexpr const_iterator cend() const noexcept{
		return data_ + size_;
	}

	[[nodiscard]] constexpr std::byte& back() noexcept{
		return data_[size_ - 1u];
	}

	[[nodiscard]] constexpr const std::byte& back() const noexcept{
		return data_[size_ - 1u];
	}

	[[nodiscard]] constexpr std::span<std::byte> span() noexcept{
		return {data_, size_};
	}

	[[nodiscard]] constexpr std::span<const std::byte> span() const noexcept{
		return {data_, size_};
	}

	[[nodiscard]] constexpr operator std::span<std::byte>() noexcept{
		return this->span();
	}

	[[nodiscard]] constexpr operator std::span<const std::byte>() const noexcept{
		return this->span();
	}

	[[nodiscard]] std::expected<void, srl_byte_buffer_error> reserve(const size_type target) noexcept{
		if(target <= capacity_){
			return {};
		}
		return this->reallocate(growth_capacity(target));
	}

	[[nodiscard]] std::expected<void, srl_byte_buffer_error> resize_uninitialized(
		const size_type target) noexcept{
		if(const auto reserved = this->reserve(target); !reserved){
			return reserved;
		}
		size_ = target;
		return {};
	}

	[[nodiscard]] std::expected<void, srl_byte_buffer_error> resize(
		const size_type target,
		const std::byte value = {}) noexcept{
		const size_type old_size = size_;
		if(const auto resized = this->resize_uninitialized(target); !resized){
			return resized;
		}
		if(target > old_size){
			std::fill(data_ + old_size, data_ + target, value);
		}
		return {};
	}

	[[nodiscard]] std::expected<void, srl_byte_buffer_error> append(
		const std::span<const std::byte> bytes) noexcept{
		size_type target{};
		if(add_overflow(size_, bytes.size(), target)){
			return std::unexpected{srl_byte_buffer_error::size_overflow};
		}

		if(const auto resized = this->resize_uninitialized(target); !resized){
			return resized;
		}
		if(!bytes.empty()){
			std::memcpy(data_ + target - bytes.size(), bytes.data(), bytes.size());
		}
		return {};
	}

	[[nodiscard]] std::expected<void, srl_byte_buffer_error> assign(
		const std::span<const std::byte> bytes) noexcept{
		if(const auto resized = this->resize_uninitialized(bytes.size()); !resized){
			return resized;
		}
		if(!bytes.empty()){
			std::memcpy(data_, bytes.data(), bytes.size());
		}
		return {};
	}

	template <typename Overwriter>
	requires std::is_nothrow_invocable_r_v<size_type, Overwriter, std::byte*, size_type>
	[[nodiscard]] std::expected<void, srl_byte_buffer_error> resize_and_overwrite(
		const size_type target,
		Overwriter&& overwrite) noexcept{
		if(const auto reserved = this->reserve(target); !reserved){
			return reserved;
		}

		const size_type written = std::forward<Overwriter>(overwrite)(data_, target);
		if(written > target){
			return std::unexpected{srl_byte_buffer_error::overwrite_size_out_of_bounds};
		}
		size_ = written;
		return {};
	}

	void clear() noexcept{
		size_ = 0u;
	}

private:
	static constexpr bool add_overflow(const size_type lhs, const size_type rhs, size_type& out) noexcept{
		if(lhs > std::numeric_limits<size_type>::max() - rhs){
			return true;
		}
		out = lhs + rhs;
		return false;
	}

	[[nodiscard]] static constexpr size_type growth_capacity(const size_type target) noexcept{
		const size_type doubled = target > std::numeric_limits<size_type>::max() / 2u
			? target
			: target * 2u;
		return std::max(target, std::max(doubled, alignment));
	}

	[[nodiscard]] static std::byte* allocate(const size_type count) noexcept{
		if(count == 0u){
			return nullptr;
		}
		return static_cast<std::byte*>(::operator new(
			count,
			std::align_val_t{alignment},
			std::nothrow));
	}

	static void deallocate(std::byte* ptr, const size_type count) noexcept{
		if(ptr != nullptr){
			::operator delete(ptr, count, std::align_val_t{alignment});
		}
	}

	[[nodiscard]] std::expected<void, srl_byte_buffer_error> reallocate(
		const size_type target_capacity) noexcept{
		std::byte* const next = allocate(target_capacity);
		if(next == nullptr){
			return std::unexpected{srl_byte_buffer_error::allocation_failed};
		}

		if(size_ != 0u){
			std::memcpy(next, data_, size_);
		}
		deallocate(data_, capacity_);
		data_ = next;
		capacity_ = target_capacity;
		return {};
	}

	void release() noexcept{
		deallocate(data_, capacity_);
		data_ = nullptr;
		size_ = 0u;
		capacity_ = 0u;
	}

	std::byte* data_{};
	size_type size_{};
	size_type capacity_{};
};
}
