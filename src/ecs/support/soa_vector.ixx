module;

#include "mo_yanxi/adapted_attributes.hpp"

export module mo_yanxi.soa_vector;

import std;

namespace mo_yanxi{
	namespace detail{
		template <typename T, typename... Ts>
		inline constexpr std::size_t type_count_v = (0uz + ... + (static_cast<std::size_t>(std::same_as<T, Ts>)));

		template <typename T, std::size_t I, typename... Ts>
		struct type_index_impl;

		template <typename T, std::size_t I, typename Head, typename... Tail>
		struct type_index_impl<T, I, Head, Tail...>
			: std::conditional_t<std::same_as<T, Head>,
			                     std::integral_constant<std::size_t, I>,
			                     type_index_impl<T, I + 1, Tail...>>{};

		template <typename T, std::size_t I>
		struct type_index_impl<T, I>{
			static_assert(!std::same_as<T, T>, "type does not exist in soa_vector");
		};

		template <typename T, typename... Ts>
		inline constexpr std::size_t type_index_v = type_index_impl<T, 0, Ts...>::value;

		template <typename T>
		struct zero_storage_object{
			inline static T value{};
		};
	}

	export
	template <typename T>
	inline constexpr bool soa_zero_storage_column_v =
		std::is_empty_v<T>
		&& std::is_trivially_default_constructible_v<T>
		&& std::is_trivially_copyable_v<T>
		&& std::is_trivially_destructible_v<T>;

	export
	template <typename T>
		requires soa_zero_storage_column_v<T>
	[[nodiscard]] T& soa_zero_storage_object() noexcept{
		return detail::zero_storage_object<T>::value;
	}

	export
	template <typename Alloc, typename... Ts>
	struct soa_vector{
		static_assert(sizeof...(Ts) != 0, "soa_vector requires at least one column");
		static_assert(std::same_as<typename std::allocator_traits<Alloc>::value_type, std::byte>,
		              "soa_vector allocator value_type must be std::byte");
		static_assert((std::is_object_v<Ts> && ...), "soa_vector columns must be object types");
		static_assert((!std::is_const_v<Ts> && ...), "soa_vector columns cannot be const-qualified");
		static_assert((!std::is_volatile_v<Ts> && ...), "soa_vector columns cannot be volatile-qualified");
		static_assert(((!std::is_empty_v<Ts> || soa_zero_storage_column_v<Ts>) && ...),
		              "empty soa_vector columns must be trivial zero-storage tags");

		using allocator_type = Alloc;
		using size_type = std::uint32_t;
		using difference_type = std::int32_t;
		using reference = std::tuple<Ts&...>;
		using const_reference = std::tuple<const Ts&...>;

	private:
		using alloc_traits = std::allocator_traits<allocator_type>;
		using pointer_tuple = std::tuple<Ts*...>;

		template <typename T>
		using rebind_allocator = alloc_traits::template rebind_alloc<T>;

		template <typename T>
		using rebind_traits = std::allocator_traits<rebind_allocator<T>>;

		pointer_tuple data_{};
		size_type size_{};
		size_type capacity_{};
		ADAPTED_NO_UNIQUE_ADDRESS allocator_type alloc_{};

		template <std::size_t I>
		using element_at = std::tuple_element_t<I, std::tuple<Ts...>>;

		template <typename T>
		[[nodiscard]] FORCE_INLINE constexpr rebind_allocator<T> column_allocator() const
			noexcept(std::is_nothrow_constructible_v<rebind_allocator<T>, const allocator_type&>){
			return rebind_allocator<T>{alloc_};
		}

		template <std::size_t I>
		[[nodiscard]] FORCE_INLINE constexpr element_at<I>*& ptr() noexcept{
			return std::get<I>(data_);
		}

		template <std::size_t I>
		[[nodiscard]] FORCE_INLINE constexpr element_at<I>* ptr() const noexcept{
			return std::get<I>(data_);
		}

		template <typename T>
		[[nodiscard]] constexpr T* allocate_column(size_type capacity){
			if constexpr (soa_zero_storage_column_v<T>){
				return nullptr;
			}else{
				if(capacity == 0){
					return nullptr;
				}

				auto alloc = column_allocator<T>();
				return rebind_traits<T>::allocate(alloc, capacity);
			}
		}

		template <typename T>
		constexpr void deallocate_column(T* ptr, size_type capacity) noexcept{
			if constexpr (soa_zero_storage_column_v<T>){
				return;
			}else{
				if(ptr == nullptr){
					return;
				}

				auto alloc = column_allocator<T>();
				rebind_traits<T>::deallocate(alloc, ptr, capacity);
			}
		}

		template <std::size_t... I>
		[[nodiscard]] constexpr pointer_tuple allocate_columns(size_type capacity, std::index_sequence<I...>){
			pointer_tuple result{};

			try{
				((std::get<I>(result) = allocate_column<element_at<I>>(capacity)), ...);
			}catch(...){
				this->deallocate_columns(result, capacity);
				throw;
			}

			return result;
		}

		[[nodiscard]] constexpr pointer_tuple allocate_columns(size_type capacity){
			return this->allocate_columns(capacity, std::index_sequence_for<Ts...>{});
		}

		template <std::size_t... I>
		constexpr void deallocate_columns(pointer_tuple& ptrs, size_type capacity, std::index_sequence<I...>) noexcept{
			(this->deallocate_column(std::get<I>(ptrs), capacity), ...);
			ptrs = {};
		}

		constexpr void deallocate_columns(pointer_tuple& ptrs, size_type capacity) noexcept{
			this->deallocate_columns(ptrs, capacity, std::index_sequence_for<Ts...>{});
		}

		template <std::size_t I>
		FORCE_INLINE constexpr void destroy_one(pointer_tuple& ptrs, size_type idx) noexcept{
			using T = element_at<I>;
			if constexpr (!std::is_trivially_destructible_v<T>){
				auto alloc = column_allocator<T>();
				rebind_traits<T>::destroy(alloc, std::get<I>(ptrs) + idx);
			}
		}

		template <std::size_t... I>
		FORCE_INLINE constexpr void destroy_row(pointer_tuple& ptrs, size_type idx, std::index_sequence<I...>) noexcept{
			(this->destroy_one<I>(ptrs, idx), ...);
		}

		FORCE_INLINE constexpr void destroy_row(pointer_tuple& ptrs, size_type idx) noexcept{
			this->destroy_row(ptrs, idx, std::index_sequence_for<Ts...>{});
		}

		constexpr void destroy_rows(pointer_tuple& ptrs, size_type count) noexcept{
			if constexpr ((std::is_trivially_destructible_v<Ts> && ...)){
				return;
			}

			while(count != 0){
				--count;
				this->destroy_row(ptrs, count);
			}
		}

		template <std::size_t I, typename Arg>
		constexpr void construct_one(pointer_tuple& ptrs, size_type idx, Arg&& arg){
			using T = element_at<I>;
			if constexpr (soa_zero_storage_column_v<T>){
				return;
			}else{
				auto alloc = column_allocator<T>();
				rebind_traits<T>::construct(alloc, std::get<I>(ptrs) + idx, std::forward<Arg>(arg));
			}
		}

		template <std::size_t I>
		constexpr void default_construct_one(pointer_tuple& ptrs, size_type idx){
			using T = element_at<I>;
			if constexpr (soa_zero_storage_column_v<T>){
				return;
			}else{
				auto alloc = column_allocator<T>();
				rebind_traits<T>::construct(alloc, std::get<I>(ptrs) + idx);
			}
		}

		template <typename Tuple, std::size_t... I>
		constexpr void construct_row_from_tuple(pointer_tuple& ptrs, size_type idx, Tuple&& args, std::index_sequence<I...>){
			std::size_t constructed{};

			try{
				((this->template construct_one<I>(ptrs, idx, std::get<I>(std::forward<Tuple>(args))), ++constructed), ...);
			}catch(...){
				this->destroy_first_n(ptrs, idx, constructed);
				throw;
			}
		}

		template <std::size_t... I>
		constexpr void default_construct_row(pointer_tuple& ptrs, size_type idx, std::index_sequence<I...>){
			std::size_t constructed{};

			try{
				((this->default_construct_one<I>(ptrs, idx), ++constructed), ...);
			}catch(...){
				this->destroy_first_n(ptrs, idx, constructed);
				throw;
			}
		}

		template <std::size_t I>
		constexpr void destroy_if_before(pointer_tuple& ptrs, size_type idx, std::size_t count) noexcept{
			if(I < count){
				this->destroy_one<I>(ptrs, idx);
			}
		}

		template <std::size_t... I>
		constexpr void destroy_first_n(pointer_tuple& ptrs, size_type idx, std::size_t count, std::index_sequence<I...>) noexcept{
			(this->destroy_if_before<I>(ptrs, idx, count), ...);
		}

		constexpr void destroy_first_n(pointer_tuple& ptrs, size_type idx, std::size_t count) noexcept{
			this->destroy_first_n(ptrs, idx, count, std::index_sequence_for<Ts...>{});
		}

		template <std::size_t I>
		constexpr void move_construct_column(pointer_tuple& src, pointer_tuple& dst, size_type count, std::array<size_type, sizeof...(Ts)>& constructed){
			using T = element_at<I>;
			if constexpr (soa_zero_storage_column_v<T>){
				constructed[I] = count;
				return;
			}else{
				T* RESTRICT dst_ptr = std::get<I>(dst);
				T* RESTRICT src_ptr = std::get<I>(src);

				if constexpr (std::is_trivially_copyable_v<T>){
					if!consteval{
						// Reallocation moves into freshly allocated columns, so ranges are disjoint.
						if(count != 0){
							std::memcpy(dst_ptr, src_ptr, sizeof(T) * count);
						}
						constructed[I] = count;
						return;
					}
				}

				auto alloc = column_allocator<T>();

				for(; constructed[I] != count; ++constructed[I]){
					rebind_traits<T>::construct(
						alloc,
						dst_ptr + constructed[I],
						std::move_if_noexcept(src_ptr[constructed[I]]));
				}
			}
		}

		template <std::size_t I>
		constexpr void copy_construct_column(const pointer_tuple& src, pointer_tuple& dst, size_type count, std::array<size_type, sizeof...(Ts)>& constructed){
			using T = element_at<I>;
			if constexpr (soa_zero_storage_column_v<T>){
				constructed[I] = count;
				return;
			}else{
				T* RESTRICT dst_ptr = std::get<I>(dst);
				const T* RESTRICT src_ptr = std::get<I>(src);

				if constexpr (std::is_trivially_copyable_v<T>){
					if!consteval{
						if(count != 0){
							std::memcpy(dst_ptr, src_ptr, sizeof(T) * count);
						}
						constructed[I] = count;
						return;
					}
				}

				auto alloc = column_allocator<T>();

				for(; constructed[I] != count; ++constructed[I]){
					rebind_traits<T>::construct(alloc, dst_ptr + constructed[I], src_ptr[constructed[I]]);
				}
			}
		}

		template <std::size_t I>
		constexpr void destroy_constructed_column(pointer_tuple& ptrs, size_type count) noexcept{
			while(count != 0){
				--count;
				this->destroy_one<I>(ptrs, count);
			}
		}

		template <std::size_t... I>
		constexpr void destroy_constructed(pointer_tuple& ptrs, const std::array<size_type, sizeof...(Ts)>& counts, std::index_sequence<I...>) noexcept{
			(this->destroy_constructed_column<I>(ptrs, counts[I]), ...);
		}

		constexpr void destroy_constructed(pointer_tuple& ptrs, const std::array<size_type, sizeof...(Ts)>& counts) noexcept{
			this->destroy_constructed(ptrs, counts, std::index_sequence_for<Ts...>{});
		}

		template <std::size_t... I>
		constexpr void move_construct_columns(pointer_tuple& src, pointer_tuple& dst, size_type count, std::index_sequence<I...>){
			std::array<size_type, sizeof...(Ts)> constructed{};

			try{
				(move_construct_column<I>(src, dst, count, constructed), ...);
			}catch(...){
				this->destroy_constructed(dst, constructed);
				throw;
			}
		}

		constexpr void move_construct_columns(pointer_tuple& src, pointer_tuple& dst, size_type count){
			move_construct_columns(src, dst, count, std::index_sequence_for<Ts...>{});
		}

		template <std::size_t... I>
		constexpr void copy_construct_columns(const pointer_tuple& src, pointer_tuple& dst, size_type count, std::index_sequence<I...>){
			std::array<size_type, sizeof...(Ts)> constructed{};

			try{
				(this->copy_construct_column<I>(src, dst, count, constructed), ...);
			}catch(...){
				this->destroy_constructed(dst, constructed);
				throw;
			}
		}

		constexpr void copy_construct_columns(const pointer_tuple& src, pointer_tuple& dst, size_type count){
			this->copy_construct_columns(src, dst, count, std::index_sequence_for<Ts...>{});
		}

		template <typename RelocateFn>
		static constexpr bool relocate_invocable =
			std::invocable<RelocateFn&, soa_vector&, size_type> ||
			std::invocable<RelocateFn&, size_type>;

		template <typename RelocateFn>
		FORCE_INLINE constexpr void invoke_relocate(RelocateFn& fn, size_type idx)
			noexcept(std::is_nothrow_invocable_v<RelocateFn&, soa_vector&, size_type> ||
			         std::is_nothrow_invocable_v<RelocateFn&, size_type>)
			requires relocate_invocable<RelocateFn>{
			if constexpr (std::invocable<RelocateFn&, soa_vector&, size_type>){
				std::invoke(fn, *this, idx);
			}else{
				std::invoke(fn, idx);
			}
		}

		template <typename RelocateFn>
		constexpr void invoke_relocate_all(RelocateFn& fn)
			requires relocate_invocable<RelocateFn>{
			for(size_type i = 0; i != size_; ++i){
				invoke_relocate(fn, i);
			}
		}

		struct no_relocate_hook{
			constexpr void operator()(size_type) const noexcept{
			}
		};

		template <typename RelocateFn>
		constexpr void reallocate(size_type new_capacity, RelocateFn&& relocate_fn)
			requires relocate_invocable<RelocateFn>{
			pointer_tuple new_data = allocate_columns(new_capacity);

			try{
				this->move_construct_columns(data_, new_data, size_);
			}catch(...){
				this->deallocate_columns(new_data, new_capacity);
				throw;
			}

			pointer_tuple old_data = data_;
			const size_type old_capacity = capacity_;
			data_ = new_data;
			capacity_ = new_capacity;

			try{
				invoke_relocate_all(relocate_fn);
			}catch(...){
				destroy_rows(old_data, size_);
				this->deallocate_columns(old_data, old_capacity);
				throw;
			}

			destroy_rows(old_data, size_);
			this->deallocate_columns(old_data, old_capacity);
		}

		[[nodiscard]] static constexpr size_type checked_capacity(std::size_t value){
			if(value > static_cast<std::size_t>(std::numeric_limits<size_type>::max())){
				throw std::length_error{"soa_vector capacity exceeds size_type"};
			}
			return static_cast<size_type>(value);
		}

		[[nodiscard]] constexpr size_type grow_capacity(size_type required) const{
			size_type next = capacity_ == 0 ? size_type{1} : static_cast<size_type>(capacity_ * 2u);
			if(next < required){
				next = required;
			}
			return next;
		}

		template <std::size_t... I>
		[[nodiscard]] FORCE_INLINE constexpr reference row_at(size_type idx, std::index_sequence<I...>) noexcept{
			return reference{this->template get<I>(idx)...};
		}

		template <std::size_t... I>
		[[nodiscard]] FORCE_INLINE constexpr const_reference row_at(size_type idx, std::index_sequence<I...>) const noexcept{
			return const_reference{this->template get<I>(idx)...};
		}

	public:
		constexpr soa_vector() noexcept(std::is_nothrow_default_constructible_v<allocator_type>) = default;

		[[nodiscard]] constexpr explicit soa_vector(const allocator_type& alloc) noexcept(std::is_nothrow_copy_constructible_v<allocator_type>)
			: alloc_(alloc){
		}

		constexpr soa_vector(const soa_vector& other)
			: alloc_(alloc_traits::select_on_container_copy_construction(other.alloc_)){
			if(other.size_ == 0){
				return;
			}

			pointer_tuple new_data = this->allocate_columns(other.size_);
			try{
				this->copy_construct_columns(other.data_, new_data, other.size_);
			}catch(...){
				this->deallocate_columns(new_data, other.size_);
				throw;
			}

			data_ = new_data;
			size_ = other.size_;
			capacity_ = other.size_;
		}

		constexpr soa_vector(soa_vector&& other) noexcept(std::is_nothrow_move_constructible_v<allocator_type>)
			: data_(std::exchange(other.data_, {})),
			  size_(std::exchange(other.size_, {})),
			  capacity_(std::exchange(other.capacity_, {})),
			  alloc_(std::move(other.alloc_)){
		}

		constexpr soa_vector& operator=(const soa_vector& other){
			if(this == std::addressof(other)){
				return *this;
			}

			if constexpr (alloc_traits::propagate_on_container_copy_assignment::value){
				if(alloc_ != other.alloc_){
					clear();
					this->deallocate_columns(data_, capacity_);
					capacity_ = 0;
				}
				alloc_ = other.alloc_;
			}

			this->assign_copy(other);
			return *this;
		}

		constexpr soa_vector& operator=(soa_vector&& other)
			noexcept(alloc_traits::propagate_on_container_move_assignment::value ||
			         alloc_traits::is_always_equal::value){
			if(this == std::addressof(other)){
				return *this;
			}

			if constexpr (alloc_traits::propagate_on_container_move_assignment::value || alloc_traits::is_always_equal::value){
				clear();
				this->deallocate_columns(data_, capacity_);
				data_ = std::exchange(other.data_, {});
				size_ = std::exchange(other.size_, {});
				capacity_ = std::exchange(other.capacity_, {});
				if constexpr (alloc_traits::propagate_on_container_move_assignment::value){
					alloc_ = std::move(other.alloc_);
				}
			}else{
				if(alloc_ == other.alloc_){
					clear();
					this->deallocate_columns(data_, capacity_);
					data_ = std::exchange(other.data_, {});
					size_ = std::exchange(other.size_, {});
					capacity_ = std::exchange(other.capacity_, {});
				}else{
					this->assign_move(other);
					other.clear();
				}
			}

			return *this;
		}

		constexpr ~soa_vector(){
			clear();
			this->deallocate_columns(data_, capacity_);
		}

		[[nodiscard]] constexpr allocator_type get_allocator() const noexcept(std::is_nothrow_copy_constructible_v<allocator_type>){
			return alloc_;
		}

		[[nodiscard]] FORCE_INLINE constexpr bool empty() const noexcept{
			return size_ == 0;
		}

		[[nodiscard]] FORCE_INLINE constexpr size_type size() const noexcept{
			return size_;
		}

		[[nodiscard]] FORCE_INLINE constexpr size_type capacity() const noexcept{
			return capacity_;
		}

		[[nodiscard]] static constexpr size_type max_size() noexcept{
			constexpr auto max = std::numeric_limits<size_type>::max();
			return max;
		}

		template <std::size_t I>
		[[nodiscard]] FORCE_INLINE constexpr element_at<I>* data() noexcept{
			static_assert(!soa_zero_storage_column_v<element_at<I>>, "zero-storage soa_vector columns have no data pointer");
			return ptr<I>();
		}

		template <std::size_t I>
		[[nodiscard]] FORCE_INLINE constexpr const element_at<I>* data() const noexcept{
			static_assert(!soa_zero_storage_column_v<element_at<I>>, "zero-storage soa_vector columns have no data pointer");
			return ptr<I>();
		}

		template <typename T>
			requires (detail::type_count_v<T, Ts...> == 1)
		[[nodiscard]] FORCE_INLINE constexpr T* data() noexcept{
			return data<detail::type_index_v<T, Ts...>>();
		}

		template <typename T>
			requires (detail::type_count_v<T, Ts...> == 1)
		[[nodiscard]] FORCE_INLINE constexpr const T* data() const noexcept{
			return data<detail::type_index_v<T, Ts...>>();
		}

		template <std::size_t I>
		[[nodiscard]] constexpr std::span<element_at<I>> column() noexcept{
			static_assert(!soa_zero_storage_column_v<element_at<I>>, "zero-storage soa_vector columns have no span");
			return {data<I>(), size_};
		}

		template <std::size_t I>
		[[nodiscard]] constexpr std::span<const element_at<I>> column() const noexcept{
			static_assert(!soa_zero_storage_column_v<element_at<I>>, "zero-storage soa_vector columns have no span");
			return {data<I>(), size_};
		}

		template <typename T>
			requires (detail::type_count_v<T, Ts...> == 1)
		[[nodiscard]] constexpr std::span<T> column() noexcept{
			return column<detail::type_index_v<T, Ts...>>();
		}

		template <typename T>
			requires (detail::type_count_v<T, Ts...> == 1)
		[[nodiscard]] constexpr std::span<const T> column() const noexcept{
			return column<detail::type_index_v<T, Ts...>>();
		}

		template <std::size_t I>
		[[nodiscard]] FORCE_INLINE constexpr element_at<I>& get(size_type idx) noexcept{
			if constexpr (soa_zero_storage_column_v<element_at<I>>){
				return soa_zero_storage_object<element_at<I>>();
			}else{
				return data<I>()[idx];
			}
		}

		template <std::size_t I>
		[[nodiscard]] FORCE_INLINE constexpr const element_at<I>& get(size_type idx) const noexcept{
			if constexpr (soa_zero_storage_column_v<element_at<I>>){
				return soa_zero_storage_object<element_at<I>>();
			}else{
				return data<I>()[idx];
			}
		}

		template <typename T>
			requires (detail::type_count_v<T, Ts...> == 1)
		[[nodiscard]] FORCE_INLINE constexpr T& get(size_type idx) noexcept{
			return get<detail::type_index_v<T, Ts...>>(idx);
		}

		template <typename T>
			requires (detail::type_count_v<T, Ts...> == 1)
		[[nodiscard]] FORCE_INLINE constexpr const T& get(size_type idx) const noexcept{
			return get<detail::type_index_v<T, Ts...>>(idx);
		}

		template <std::size_t I>
		[[nodiscard]] constexpr element_at<I>& at(size_type idx){
			if(idx >= size_){
				throw std::out_of_range{"soa_vector index out of range"};
			}
			return get<I>(idx);
		}

		template <std::size_t I>
		[[nodiscard]] constexpr const element_at<I>& at(size_type idx) const{
			if(idx >= size_){
				throw std::out_of_range{"soa_vector index out of range"};
			}
			return get<I>(idx);
		}

		[[nodiscard]] FORCE_INLINE constexpr reference operator[](size_type idx) noexcept{
			return this->row_at(idx, std::index_sequence_for<Ts...>{});
		}

		[[nodiscard]] FORCE_INLINE constexpr const_reference operator[](size_type idx) const noexcept{
			return this->row_at(idx, std::index_sequence_for<Ts...>{});
		}

		[[nodiscard]] FORCE_INLINE constexpr reference front() noexcept{
			return (*this)[0];
		}

		[[nodiscard]] FORCE_INLINE constexpr const_reference front() const noexcept{
			return (*this)[0];
		}

		[[nodiscard]] FORCE_INLINE constexpr reference back() noexcept{
			return (*this)[size_ - 1];
		}

		[[nodiscard]] FORCE_INLINE constexpr const_reference back() const noexcept{
			return (*this)[size_ - 1];
		}

		constexpr void reserve(size_type new_capacity){
			no_relocate_hook hook{};
			reserve(new_capacity, hook);
		}

		template <typename RelocateFn>
		constexpr void reserve(size_type new_capacity, RelocateFn&& relocate_fn)
			requires relocate_invocable<RelocateFn>{
			if(new_capacity <= capacity_){
				return;
			}

			this->reallocate(new_capacity, std::forward<RelocateFn>(relocate_fn));
		}

		constexpr void shrink_to_fit(){
			no_relocate_hook hook{};
			this->shrink_to_fit(hook);
		}

		template <typename RelocateFn>
		constexpr void shrink_to_fit(RelocateFn&& relocate_fn)
			requires relocate_invocable<RelocateFn>{
			if(size_ == capacity_){
				return;
			}

			this->reallocate(size_, std::forward<RelocateFn>(relocate_fn));
		}

		constexpr void resize(size_type count)
			requires ((std::default_initializable<Ts> && ...))
		{
			no_relocate_hook hook{};
			this->resize(count, hook);
		}

		template <typename RelocateFn>
		constexpr void resize(size_type count, RelocateFn&& relocate_fn)
			requires (relocate_invocable<RelocateFn> && (std::default_initializable<Ts> && ...))
		{
			if(count < size_){
				while(size_ != count){
					pop_back();
				}
				return;
			}

			if(count > capacity_){
				this->reserve(count, std::forward<RelocateFn>(relocate_fn));
			}

			while(size_ != count){
				this->default_construct_row(data_, size_, std::index_sequence_for<Ts...>{});
				++size_;
			}
		}

		constexpr void swap(soa_vector& other)
			noexcept((alloc_traits::propagate_on_container_swap::value || alloc_traits::is_always_equal::value) &&
			         std::is_nothrow_swappable_v<allocator_type>){
			using std::swap;

			swap(data_, other.data_);
			swap(size_, other.size_);
			swap(capacity_, other.capacity_);

			if constexpr (alloc_traits::propagate_on_container_swap::value){
				swap(alloc_, other.alloc_);
			}
		}

		friend constexpr void swap(soa_vector& lhs, soa_vector& rhs) noexcept(noexcept(lhs.swap(rhs))){
			lhs.swap(rhs);
		}

		constexpr void clear() noexcept{
			this->destroy_rows(data_, size_);
			size_ = 0;
		}

		constexpr void pop_back() noexcept{
			--size_;
			this->destroy_row(data_, size_);
		}

		template <typename... Args>
			requires (sizeof...(Args) == sizeof...(Ts) && (std::constructible_from<Ts, Args&&> && ...))
		constexpr reference emplace_back(Args&&... args){
			no_relocate_hook hook{};
			return this->emplace_back_with_relocate(hook, std::forward<Args>(args)...);
		}

		constexpr reference emplace_back()
			requires ((std::default_initializable<Ts> && ...))
		{
			no_relocate_hook hook{};
			return this->emplace_back_with_relocate(hook);
		}

		template <typename RelocateFn, typename... Args>
			requires (sizeof...(Args) == sizeof...(Ts) && (std::constructible_from<Ts, Args&&> && ...))
		constexpr reference emplace_back_with_relocate(RelocateFn&& relocate_fn, Args&&... args)
			requires relocate_invocable<RelocateFn>{
			if(size_ == capacity_){
				this->reserve(grow_capacity(static_cast<size_type>(size_ + 1)), std::forward<RelocateFn>(relocate_fn));
			}

			auto tuple_args = std::forward_as_tuple(std::forward<Args>(args)...);
			this->construct_row_from_tuple(data_, size_, std::move(tuple_args), std::index_sequence_for<Ts...>{});
			++size_;
			return back();
		}

		template <typename RelocateFn>
		constexpr reference emplace_back_with_relocate(RelocateFn&& relocate_fn)
			requires (relocate_invocable<RelocateFn> && (std::default_initializable<Ts> && ...))
		{
			if(size_ == capacity_){
				this->reserve(grow_capacity(static_cast<size_type>(size_ + 1)), std::forward<RelocateFn>(relocate_fn));
			}

			this->default_construct_row(data_, size_, std::index_sequence_for<Ts...>{});
			++size_;
			return back();
		}

		template <typename... Args>
			requires (sizeof...(Args) == sizeof...(Ts) && (std::constructible_from<Ts, const Args&> && ...))
		constexpr reference push_back(const Args&... args){
			return this->emplace_back(args...);
		}

		template <typename RelocateFn>
		constexpr void erase_unstable(size_type idx, RelocateFn&& relocate_fn)
			requires relocate_invocable<RelocateFn>{
			const auto last = static_cast<size_type>(size_ - 1);
			if(idx != last){
				move_assign_row(idx, last);
				this->invoke_relocate(relocate_fn, idx);
			}

			pop_back();
		}

		constexpr void erase_unstable(size_type idx){
			no_relocate_hook hook{};
			this->erase_unstable(idx, hook);
		}

	private:
		template <std::size_t I>
		FORCE_INLINE constexpr void move_assign_one(size_type dst, size_type src)
			noexcept(std::is_nothrow_move_assignable_v<element_at<I>>){
			using T = element_at<I>;
			if constexpr (soa_zero_storage_column_v<T>){
				return;
			}else{
				if constexpr (std::is_trivially_copyable_v<T>){
					if!consteval{
						// erase_unstable only calls this for different row indices.
						std::memcpy(data<I>() + dst, data<I>() + src, sizeof(T));
						return;
					}
				}

				get<I>(dst) = std::move(get<I>(src));
			}
		}

		template <std::size_t... I>
		constexpr void move_assign_row(size_type dst, size_type src, std::index_sequence<I...>)
			noexcept((std::is_nothrow_move_assignable_v<element_at<I>> && ...)){
			(move_assign_one<I>(dst, src), ...);
		}

		constexpr void move_assign_row(size_type dst, size_type src)
			noexcept((std::is_nothrow_move_assignable_v<Ts> && ...)){
			this->move_assign_row(dst, src, std::index_sequence_for<Ts...>{});
		}

		template <std::size_t I>
		constexpr void copy_assign_or_construct_one(const soa_vector& other, size_type idx){
			using T = element_at<I>;
			if constexpr (soa_zero_storage_column_v<T>){
				return;
			}else{
				if(idx < size_){
					get<I>(idx) = other.get<I>(idx);
				}else{
					this->template construct_one<I>(data_, idx, other.get<I>(idx));
				}
			}
		}

		template <std::size_t... I>
		constexpr void copy_assign_or_construct_row(const soa_vector& other, size_type idx, std::index_sequence<I...>){
			std::size_t constructed{};
			try{
				((this->copy_assign_or_construct_one<I>(other, idx), ++constructed), ...);
			}catch(...){
				if(idx >= size_){
					destroy_first_n(data_, idx, constructed);
				}
				throw;
			}
		}

		constexpr void assign_copy(const soa_vector& other){
			if(other.size_ > capacity_){
				pointer_tuple new_data = allocate_columns(other.size_);
				try{
					this->copy_construct_columns(other.data_, new_data, other.size_);
				}catch(...){
					this->deallocate_columns(new_data, other.size_);
					throw;
				}

				clear();
				this->deallocate_columns(data_, capacity_);
				data_ = new_data;
				size_ = other.size_;
				capacity_ = other.size_;
				return;
			}

			const size_type common = std::min(size_, other.size_);
			for(size_type i = 0; i != common; ++i){
				this->copy_assign_row(other, i);
			}
			for(size_type i = common; i != other.size_; ++i){
				this->copy_assign_or_construct_row(other, i, std::index_sequence_for<Ts...>{});
				++size_;
			}
			while(size_ > other.size_){
				pop_back();
			}
		}

		template <std::size_t I>
		constexpr void copy_assign_one(const soa_vector& other, size_type idx){
			using T = element_at<I>;
			if constexpr (soa_zero_storage_column_v<T>){
				return;
			}else{
				get<I>(idx) = other.get<I>(idx);
			}
		}

		template <std::size_t... I>
		constexpr void copy_assign_row(const soa_vector& other, size_type idx, std::index_sequence<I...>){
			(this->copy_assign_one<I>(other, idx), ...);
		}

		constexpr void copy_assign_row(const soa_vector& other, size_type idx){
			this->copy_assign_row(other, idx, std::index_sequence_for<Ts...>{});
		}

		constexpr void assign_move(soa_vector& other){
			clear();
			this->reserve(other.size_);
			for(size_type i = 0; i != other.size_; ++i){
				this->move_construct_row_from(other, i);
				++size_;
			}
		}

		template <std::size_t I>
		constexpr void move_construct_one_from(soa_vector& other, size_type idx){
			using T = element_at<I>;
			if constexpr (soa_zero_storage_column_v<T>){
				return;
			}else{
				this->template construct_one<I>(data_, idx, std::move(other.get<I>(idx)));
			}
		}

		template <std::size_t... I>
		constexpr void move_construct_row_from(soa_vector& other, size_type idx, std::index_sequence<I...>){
			std::size_t constructed{};
			try{
				((this->move_construct_one_from<I>(other, idx), ++constructed), ...);
			}catch(...){
				this->destroy_first_n(data_, idx, constructed);
				throw;
			}
		}

		constexpr void move_construct_row_from(soa_vector& other, size_type idx){
			this->move_construct_row_from(other, idx, std::index_sequence_for<Ts...>{});
		}
	};
}
