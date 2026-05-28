module;

#include <cassert>

export module mo_yanxi.strided_span;

import mo_yanxi.meta_programming;
import std;

namespace mo_yanxi{

	namespace detail{
		template <typename Ptr>
		[[nodiscard]] constexpr Ptr ptr_offset(Ptr ptr, std::ptrdiff_t bytes) noexcept{
			static_assert(std::is_pointer_v<Ptr>);
			static_assert(sizeof(Ptr) == sizeof(std::uintptr_t));

			auto address = std::bit_cast<std::uintptr_t>(ptr);
			if(bytes >= 0){
				address += static_cast<std::uintptr_t>(bytes);
			}else{
				address -= static_cast<std::uintptr_t>(-(bytes + 1)) + 1;
			}

			return std::bit_cast<Ptr>(address);
		}

		template <typename LeftPtr, typename RightPtr>
		[[nodiscard]] constexpr std::ptrdiff_t ptr_distance(LeftPtr left, RightPtr right) noexcept{
			static_assert(std::is_pointer_v<LeftPtr>);
			static_assert(std::is_pointer_v<RightPtr>);
			static_assert(sizeof(LeftPtr) == sizeof(std::uintptr_t));
			static_assert(sizeof(RightPtr) == sizeof(std::uintptr_t));

			const auto left_addr = std::bit_cast<std::uintptr_t>(left);
			const auto right_addr = std::bit_cast<std::uintptr_t>(right);
			if(left_addr >= right_addr){
				return static_cast<std::ptrdiff_t>(left_addr - right_addr);
			}

			return -static_cast<std::ptrdiff_t>(right_addr - left_addr);
		}

		template <typename LeftPtr, typename RightPtr>
		[[nodiscard]] constexpr bool ptr_address_less(LeftPtr left, RightPtr right) noexcept{
			static_assert(std::is_pointer_v<LeftPtr>);
			static_assert(std::is_pointer_v<RightPtr>);
			static_assert(sizeof(LeftPtr) == sizeof(std::uintptr_t));
			static_assert(sizeof(RightPtr) == sizeof(std::uintptr_t));
			return std::bit_cast<std::uintptr_t>(left) < std::bit_cast<std::uintptr_t>(right);
		}

		template <typename Ptr>
		[[nodiscard]] constexpr Ptr min_address(Ptr ptr) noexcept{
			return ptr;
		}

		template <typename Ptr, typename ...Ptrs>
		[[nodiscard]] constexpr Ptr min_address(Ptr first, Ptrs ...rest) noexcept{
			((first = ptr_address_less(rest, first) ? rest : first), ...);
			return first;
		}
	}

	template <class It, class T>
	concept Span_compatible_iterator =
		std::contiguous_iterator<It> && std::is_convertible_v<std::remove_reference_t<std::iter_reference_t<It>> (*)[], T (*)[]>;

	template <class Se, class It>
	concept Span_compatible_sentinel = std::sized_sentinel_for<Se, It> && !std::is_convertible_v<Se, size_t>;

	export
	enum span_stride : std::ptrdiff_t{
		dynamic_in_byte = 0//std::numeric_limits<std::ptrdiff_t>::max(),
	};

	template <typename T, T size>
	struct optional_dynamic_size{
		static constexpr T value = size;

		[[nodiscard]] optional_dynamic_size() = default;
		[[nodiscard]] optional_dynamic_size(T t){}

		constexpr explicit(false) operator T() const noexcept{
			return value;
		}
	};

	template <>
	struct optional_dynamic_size<std::size_t, std::dynamic_extent>{
		std::size_t value{};

		constexpr explicit(false) operator std::size_t() const noexcept{
			return value;
		}
	};

	template <>
	struct optional_dynamic_size<std::ptrdiff_t, dynamic_in_byte>{
		std::ptrdiff_t value{};

		constexpr explicit(false) operator std::ptrdiff_t() const noexcept{
			return value;
		}
	};

	export
	template <class Ty, std::ptrdiff_t Stride = dynamic_in_byte>
	struct strided_span_iterator {
		using iterator_concept  = std::random_access_iterator_tag;
		using iterator_category = std::random_access_iterator_tag;
		using value_type        = std::remove_cv_t<Ty>;
		using difference_type   = std::ptrdiff_t;
		using pointer           = Ty*;
		using reference         = Ty&;

		[[nodiscard]] constexpr reference operator*() const noexcept {
			assert(ptr != nullptr);
			return *ptr;
		}

		[[nodiscard]] constexpr pointer operator->() const noexcept {
			return ptr;
		}

		constexpr strided_span_iterator& operator++() noexcept {
			if(stride.value){
				ptr = detail::ptr_offset(ptr, stride.value);
			}else{
				++ptr;
			}

			return *this;
		}

		constexpr strided_span_iterator operator++(int) noexcept {
			strided_span_iterator tmp{*this};
			++*this;
			return tmp;
		}

		constexpr strided_span_iterator& operator--() noexcept {
			if(stride.value){
				ptr = detail::ptr_offset(ptr, -stride.value);
			}else{
				--ptr;
			}
			return *this;
		}

		constexpr strided_span_iterator operator--(int) noexcept {
			strided_span_iterator tmp{*this};
			--*this;
			return tmp;
		}

		constexpr strided_span_iterator& operator+=(const difference_type off) noexcept {
			if(stride.value){
				ptr = detail::ptr_offset(ptr, off * stride.value);
			}else{
				ptr += off;
			}

			return *this;
		}

		[[nodiscard]] constexpr strided_span_iterator operator+(const difference_type Off) const noexcept {
			strided_span_iterator Tmp{*this};
			Tmp += Off;
			return Tmp;
		}

		[[nodiscard]] friend constexpr strided_span_iterator operator+(const difference_type Off, strided_span_iterator Next) noexcept {
			Next += Off;
			return Next;
		}

		constexpr strided_span_iterator& operator-=(const difference_type off) noexcept {
			if(stride.value){
				ptr = detail::ptr_offset(ptr, -off * stride.value);
			}else{
				ptr -= off;
			}

			return *this;
		}

		[[nodiscard]] constexpr strided_span_iterator operator-(const difference_type off) const noexcept {
			strided_span_iterator tmp{*this};
			tmp -= off;
			return tmp;
		}

		[[nodiscard]] constexpr difference_type operator-(const strided_span_iterator& right) const noexcept {
			if(stride.value){
				return detail::ptr_distance(ptr, right.ptr) / stride.value;
			}else{
				return ptr - right.ptr;
			}
		}

		[[nodiscard]] constexpr reference operator[](const difference_type off) const noexcept {
			return *(*this + off);
		}

		[[nodiscard]] constexpr bool operator==(const strided_span_iterator& right) const noexcept {
			return ptr == right.ptr;
		}

		[[nodiscard]] constexpr auto operator<=>(const strided_span_iterator& right) const noexcept {
			return ptr <=> right.ptr;
		}

		[[nodiscard]] strided_span_iterator() = default;

		[[nodiscard]] explicit(false) strided_span_iterator(pointer ptr, std::ptrdiff_t stride = 0)
			: ptr(ptr),
			  stride(stride){
		}

	private:
		pointer ptr = nullptr;
		optional_dynamic_size<difference_type, Stride> stride{};

	};

	export
	template <class Ty, std::size_t Extent = std::dynamic_extent, std::ptrdiff_t Stride = dynamic_in_byte>
	class strided_span{
	private:
		Ty* ptr_;
		optional_dynamic_size<std::size_t, Extent> extent_;
		optional_dynamic_size<std::ptrdiff_t, Stride> stride_;

	public:
		using element_type = Ty;
		using value_type = std::remove_cv_t<Ty>;
		using size_type = std::size_t;
		using difference_type = std::ptrdiff_t;
		using pointer = Ty*;
		using const_pointer = const Ty*;
		using reference = Ty&;
		using const_reference = const Ty&;
		using iterator = strided_span_iterator<Ty>;
		using reverse_iterator = std::reverse_iterator<iterator>;
		using const_iterator         = std::const_iterator<iterator>;
		using const_reverse_iterator = std::const_iterator<reverse_iterator>;


		template <class Ty_, std::size_t Extent_, std::ptrdiff_t Stride_>
		friend class strided_span;
		// static constexpr size_type extent = Extent;

		// [span.cons] Constructors, copy, and assignment
		constexpr strided_span() noexcept requires (Extent == 0 || Extent == std::dynamic_extent)
		= default;

		template <Span_compatible_iterator<element_type> It>
		constexpr explicit(Extent != std::dynamic_extent) strided_span(It first, size_type count, difference_type stride = 0) noexcept // strengthened
			: ptr_(std::to_address(first)), extent_(count), stride_(stride){
		}

		constexpr explicit(Extent != std::dynamic_extent) strided_span(pointer first, size_type count, difference_type stride = 0) noexcept // strengthened
			: ptr_(first), extent_(count), stride_(stride){
		}


		template <Span_compatible_iterator<element_type> It, Span_compatible_sentinel<It> Se>
		constexpr explicit(Extent != std::dynamic_extent) strided_span(It first, Se last, difference_type stride = 0)
			noexcept(noexcept(last - first)) // strengthened
			: ptr_(std::to_address(first)), extent_(static_cast<size_type>(last - first)), stride_(stride){
		}

		template <typename T>
		constexpr explicit strided_span(strided_span<T, Extent, Stride> other)
			: ptr_(reinterpret_cast<Ty*>(other.data())), extent_(other.extent_.value * sizeof(T) / sizeof(value_type)), stride_(other.stride_.value){
		}


		strided_span(const strided_span& other) = default;
		strided_span(strided_span&& other) noexcept = default;
		strided_span& operator=(const strided_span& other) = default;
		strided_span& operator=(strided_span&& other) noexcept = default;

		constexpr explicit(false) operator strided_span<std::byte, Extent, Stride>() noexcept{
			return {reinterpret_cast<std::byte*>(data()), extent_  * sizeof(value_type), stride_};
		}

		constexpr explicit(false) operator strided_span<const std::byte, Extent, Stride>() const noexcept {
			return {reinterpret_cast<const std::byte*>(data()), extent_  * sizeof(value_type), stride_};
		}
		// [span.obs] Observers
		[[nodiscard]] constexpr size_type size() const noexcept{
			return extent_.value;
		}

		[[nodiscard]] constexpr difference_type stride() const noexcept{
			return stride_.value;
		}

		[[nodiscard]] constexpr size_type size_bytes() const noexcept{
			return extent_.value * sizeof(element_type);
		}

		[[nodiscard]] constexpr bool empty() const noexcept{
			return extent_.value == 0;
		}

		// [span.elem] Element access
		[[nodiscard]] constexpr reference operator[](const size_type off) const noexcept /* strengthened */{
			assert(ptr_);
			assert(off < extent_.value);

			if(!stride_.value){
				return ptr_[off];
			}else{
				const auto dst = detail::ptr_offset(ptr_, static_cast<difference_type>(off) * stride_.value);
				return *dst;
			}
		}

		[[nodiscard]] constexpr reference front() const noexcept /* strengthened */{
			assert(ptr_);
			assert(extent_.value > 0);
			return *ptr_;
		}

		[[nodiscard]] constexpr reference back() const noexcept /* strengthened */{
			assert(ptr_);
			assert(extent_.value > 0);
			if(stride_.value){
				const auto end = detail::ptr_offset(ptr_, static_cast<difference_type>(extent_.value - 1) * stride_.value);
				return *end;
			}else{
				const auto end = ptr_ + extent_.value - 1;
				return *end;
			}
		}

		[[nodiscard]] constexpr pointer data() const noexcept{
			return ptr_;
		}

		// [span.iterators] Iterator support
		[[nodiscard]] constexpr iterator begin() const noexcept{
			return iterator{ptr_, stride_.value};
		}

		[[nodiscard]] constexpr iterator end() const noexcept{
			if(stride_.value){
				const auto end = detail::ptr_offset(ptr_, static_cast<difference_type>(extent_.value) * stride_.value);
				return iterator{end, stride_.value};
			}else{
				const auto end = ptr_ + extent_.value;
				return iterator{end};
			}
		}

		[[nodiscard]] constexpr const_iterator cbegin() const noexcept {
			return begin();
		}

		[[nodiscard]] constexpr const_iterator cend() const noexcept {
			return end();
		}

		[[nodiscard]] constexpr reverse_iterator rbegin() const noexcept{
			return reverse_iterator{end()};
		}

		[[nodiscard]] constexpr reverse_iterator rend() const noexcept{
			return reverse_iterator{begin()};
		}

		[[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept {
			return rbegin();
		}

		[[nodiscard]] constexpr const_reverse_iterator crend() const noexcept {
			return rend();
		}
	};

	template <typename Ty, typename Cond>
	using cond_add_const_to = std::conditional_t<std::is_const_v<Cond>, std::add_const_t<Ty>, Ty>;

	export
	template <class ValueTuple>
	struct strided_multi_span_iterator {
		using base_types = tuple_const_to_inner_t<ValueTuple>;
		using ptr_internal = std::byte*;

	public:
		using iterator_concept  = std::random_access_iterator_tag;
		using iterator_category = std::random_access_iterator_tag;
		using value_type        = unary_apply_to_tuple_t<std::add_lvalue_reference_t, tuple_const_to_inner_t<ValueTuple>>;
		static constexpr auto tuple_sz = std::tuple_size_v<value_type>;
		using difference_type   = std::ptrdiff_t;
		using pointer           = value_type*;
		using reference         = value_type&;

		[[nodiscard]] constexpr value_type operator*() const noexcept {
			return [this] <std::size_t ...Idx>(std::index_sequence<Idx...>) {
				return value_type{reinterpret_cast<std::tuple_element_t<Idx, value_type>>(
					*ptrs_[Idx]
				) ...};
			}(std::make_index_sequence<std::tuple_size_v<value_type>>{});
		}

		constexpr strided_multi_span_iterator& operator++() noexcept {
			[&]<std::size_t... Idx>(std::index_sequence<Idx...>){
				((ptrs_[Idx] = detail::ptr_offset(ptrs_[Idx], strides_[Idx])), ...);
			}(std::make_index_sequence<tuple_sz>{});

			return *this;
		}

		constexpr strided_multi_span_iterator operator++(int) noexcept {
			strided_multi_span_iterator tmp{*this};
			++*this;
			return tmp;
		}

		constexpr strided_multi_span_iterator& operator--() noexcept {
			[&]<std::size_t... Idx>(std::index_sequence<Idx...>){
				((ptrs_[Idx] = detail::ptr_offset(ptrs_[Idx], -strides_[Idx])), ...);
			}(std::make_index_sequence<tuple_sz>{});

			return *this;
		}

		constexpr strided_multi_span_iterator operator--(int) noexcept {
			strided_multi_span_iterator tmp{*this};
			--*this;
			return tmp;
		}

		constexpr strided_multi_span_iterator& operator+=(const difference_type off) noexcept {
			[&]<std::size_t... Idx>(std::index_sequence<Idx...>){
				((ptrs_[Idx] = detail::ptr_offset(ptrs_[Idx], off * strides_[Idx])), ...);
			}(std::make_index_sequence<tuple_sz>{});

			return *this;
		}

		[[nodiscard]] constexpr strided_multi_span_iterator operator+(const difference_type Off) const noexcept {
			strided_multi_span_iterator Tmp{*this};
			Tmp += Off;
			return Tmp;
		}

		[[nodiscard]] friend constexpr strided_multi_span_iterator operator+(const difference_type Off, strided_multi_span_iterator Next) noexcept {
			Next += Off;
			return Next;
		}

		constexpr strided_multi_span_iterator& operator-=(const difference_type off) noexcept {
			[&]<std::size_t... Idx>(std::index_sequence<Idx...>){
				((ptrs_[Idx] = detail::ptr_offset(ptrs_[Idx], -off * strides_[Idx])), ...);
			}(std::make_index_sequence<tuple_sz>{});

			return *this;
		}

		[[nodiscard]] constexpr strided_multi_span_iterator operator-(const difference_type off) const noexcept {
			strided_multi_span_iterator tmp{*this};
			tmp -= off;
			return tmp;
		}

		[[nodiscard]] constexpr difference_type operator-(const strided_multi_span_iterator& right) const noexcept {
			return detail::ptr_distance(ptrs_[0], right.ptrs_[0]) / strides_[0];
		}

		[[nodiscard]] constexpr reference operator[](const difference_type off) const noexcept {
			return *(*this + off);
		}

		[[nodiscard]] constexpr bool operator==(const strided_multi_span_iterator& right) const noexcept {
			return ptrs_[0] == right.ptrs_[0];
		}

		[[nodiscard]] constexpr auto operator<=>(const strided_multi_span_iterator& right) const noexcept {
			return ptrs_[0] <=> right.ptrs_[0];
		}

		[[nodiscard]] strided_multi_span_iterator() = default;

		[[nodiscard]] explicit(false) strided_multi_span_iterator(
			std::array<ptr_internal, tuple_sz> ptrs,
			std::array<std::ptrdiff_t, tuple_sz> strides)
			: ptrs_(ptrs),
			  strides_(strides){
		}

	private:
		std::array<ptr_internal, tuple_sz> ptrs_{};
		std::array<std::ptrdiff_t, tuple_sz> strides_{};
	};

	export
	template <typename ValueTuple>
	struct strided_multi_span{
	private:
		static constexpr auto tuple_sz = std::tuple_size_v<ValueTuple>;
		static_assert(tuple_sz != 0);
		std::array<std::byte*, tuple_sz> data_{};
		std::array<std::ptrdiff_t, tuple_sz> strides_{};
		std::size_t size_{};

	public:
		using value_type        = unary_apply_to_tuple_t<std::add_lvalue_reference_t, tuple_const_to_inner_t<ValueTuple>>;
		using size_type = std::size_t;
		using difference_type = std::ptrdiff_t;
		using pointer = value_type*;
		using const_pointer = const value_type*;
		using reference = value_type&;
		using const_reference = const value_type&;
		using iterator = strided_multi_span_iterator<value_type>;
		using reverse_iterator = std::reverse_iterator<iterator>;
		using const_iterator         = strided_multi_span_iterator<const value_type>;
		using const_reverse_iterator = std::const_iterator<reverse_iterator>;

		[[nodiscard]] constexpr strided_multi_span() = default;

		template <typename ...Ty>
		[[nodiscard]] constexpr strided_multi_span(strided_span<Ty> ...spans) noexcept
			:
			data_{const_cast<std::byte*>(reinterpret_cast<const std::byte*>(spans.data())) ...},
			strides_{(spans.stride() ? spans.stride() : static_cast<std::ptrdiff_t>(sizeof(Ty))) ...},
			size_(std::min(std::initializer_list<std::size_t>{spans.size() ...}))
		{

		}

		[[nodiscard]] constexpr strided_multi_span(const unary_apply_to_tuple_t<strided_span, tuple_const_to_inner_t<ValueTuple>>& spans) noexcept{
			[&, this] <std::size_t ...Idx>(std::index_sequence<Idx...>){
				this->operator=(strided_multi_span{std::get<Idx>(spans) ...});
			}(std::make_index_sequence<tuple_sz>{});
		}

		value_type operator[](std::size_t idx) const noexcept{
			return [&, this] <std::size_t ...Idx>(std::index_sequence<Idx...>) {
				return value_type{reinterpret_cast<std::tuple_element_t<Idx, value_type>>(
					*detail::ptr_offset(data_[Idx], static_cast<difference_type>(idx) * strides_[Idx])
				) ...};
			}(std::make_index_sequence<std::tuple_size_v<value_type>>{});
		}

		[[nodiscard]] constexpr std::size_t size() const noexcept{
			return size_;
		}

		[[nodiscard]] iterator begin() const noexcept{
			return iterator{data_, strides_};
		}

		[[nodiscard]] iterator end() const noexcept{
			auto ends = data_;
			[&]<std::size_t... Idx>(std::index_sequence<Idx...>){
				((ends[Idx] = detail::ptr_offset(ends[Idx], static_cast<difference_type>(size_) * strides_[Idx])), ...);
			}(std::make_index_sequence<tuple_sz>{});
			return iterator{ends, strides_};
		}

		[[nodiscard]] constexpr const_iterator cbegin() const noexcept {
			return begin();
		}

		[[nodiscard]] constexpr const_iterator cend() const noexcept {
			return end();
		}

		[[nodiscard]] constexpr reverse_iterator rbegin() const noexcept{
			return reverse_iterator{end()};
		}

		[[nodiscard]] constexpr reverse_iterator rend() const noexcept{
			return reverse_iterator{begin()};
		}

		[[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept {
			return rbegin();
		}

		[[nodiscard]] constexpr const_reverse_iterator crend() const noexcept {
			return rend();
		}
	};
}
