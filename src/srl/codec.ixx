module;

#ifdef __RESHARPER__
#include <stdexcept>
#endif

export module mo_yanxi.srl.codec;

import std;
import mo_yanxi.srl.srl_byte_buffer;
import mo_yanxi.srl.byte_record;

export namespace mo_yanxi::srl{
constexpr inline std::uint32_t root_item_id = 0u;
constexpr inline std::uint32_t sequence_count_id = 1u;
constexpr inline std::uint32_t sequence_packed_data_id = 2u;
constexpr inline std::uint32_t sequence_element_first_id = 1024u;

template <typename T>
struct codec;

template <typename T>
struct trivial_atom : std::false_type{};

template <typename T>
constexpr bool inline trivial_atom_v = trivial_atom<std::remove_cv_t<T>>::value;

template <typename T>
using codec_value_type = std::remove_cvref_t<T>;

template <typename T>
concept codec_readable = requires(
	record_builder& builder,
	const byte_record_view& record,
	const std::uint32_t id,
	const codec_value_type<T>& value,
	codec_value_type<T>& out){
	{ codec<codec_value_type<T>>::tag } -> std::convertible_to<item_type_tag>;
	{ codec<codec_value_type<T>>::write(builder, id, value) } -> std::same_as<std::expected<void, error>>;
	{ codec<codec_value_type<T>>::read(record, id, out) } -> std::same_as<std::expected<void, error>>;
};

template <typename T>
concept codec_writable = requires(
	record_builder& builder,
	const std::uint32_t id,
	const codec_value_type<T>& value){
	{ codec<codec_value_type<T>>::tag } -> std::convertible_to<item_type_tag>;
	{ codec<codec_value_type<T>>::write(builder, id, value) } -> std::same_as<std::expected<void, error>>;
};

template <typename T>
concept serializable = codec_readable<T>;

template <typename T>
concept builtin_scalar_atom =
	(std::integral<std::remove_cv_t<T>> && !std::same_as<std::remove_cv_t<T>, bool>)
	|| std::floating_point<std::remove_cv_t<T>>
	|| std::is_enum_v<std::remove_cv_t<T>>;

template <typename T>
concept explicit_trivial_atom =
	trivial_atom_v<T>
	&& trivially_copyable_record_object<std::remove_cv_t<T>>
	&& !builtin_scalar_atom<T>
	&& !std::same_as<std::remove_cv_t<T>, std::string>
	&& !std::same_as<std::remove_cv_t<T>, std::string_view>;

template <typename T>
concept packed_sequence_atom = explicit_trivial_atom<T> || builtin_scalar_atom<T>;

template <typename T>
concept sequence_container =
	requires(T& value, const T& const_value, typename T::value_type element){
		typename T::value_type;
		value.clear();
		value.push_back(element);
		const_value.size();
		const_value.begin();
		const_value.end();
	};

template <typename T>
concept reservable_sequence = requires(T& value, const std::size_t size){
	value.reserve(size);
};

template <typename T>
concept byte_contiguous_sequence = requires(T& value, const T& const_value){
	const_value.data();
	value.data();
};

[[nodiscard]] constexpr std::unexpected<error> codec_fail(
	const error_code code,
	const std::uint32_t id = 0u,
	const std::uint32_t offset = 0u) noexcept{
	return std::unexpected{error{.code = code, .id = id, .offset = offset}};
}

[[nodiscard]] constexpr error_code codec_error_code(const srl_byte_buffer_error code) noexcept{
	switch(code){
	case srl_byte_buffer_error::none : return error_code::none;
	case srl_byte_buffer_error::allocation_failed : return error_code::allocation_failed;
	case srl_byte_buffer_error::size_overflow :
	case srl_byte_buffer_error::overwrite_size_out_of_bounds : return error_code::record_too_large;
	}
	return error_code::record_too_large;
}

[[nodiscard]] constexpr std::unexpected<error> codec_fail(
	const srl_byte_buffer_error code,
	const std::uint32_t id = 0u,
	const std::uint32_t offset = 0u) noexcept{
	return codec_fail(codec_error_code(code), id, offset);
}

template <typename T>
requires codec_writable<T>
[[nodiscard]] std::expected<void, error> write_to(
	record_builder& builder,
	const std::uint32_t field_id,
	const T& value){
	return codec<codec_value_type<T>>::write(builder, field_id, value);
}

template <typename T>
requires codec_readable<T>
[[nodiscard]] std::expected<void, error> read_from(
	const byte_record_view& record,
	const std::uint32_t field_id,
	T& out){
	return codec<codec_value_type<T>>::read(record, field_id, out);
}

//TODO add more constraint check for Member Pointer

template <typename Object, auto Member>
using field_member_type = std::remove_cvref_t<decltype(std::declval<Object&>().*Member)>;

template <std::uint32_t Id, auto Member>
struct field{
	static constexpr std::uint32_t id = Id;

	template <typename Object>
	[[nodiscard]] static std::expected<void, error> write(
		record_builder& builder,
		const Object& value){
		return srl::write_to(builder, id, value.*Member);
	}

	template <typename Object>
	[[nodiscard]] static std::expected<void, error> read(
		const byte_record_view& record,
		Object& out){
		return srl::read_from(record, id, out.*Member);
	}
};

template <std::uint32_t Id, auto Member, auto DefaultValue>
struct defaulted_field{
	static constexpr std::uint32_t id = Id;

	template <typename Object>
	[[nodiscard]] static std::expected<void, error> write(
		record_builder& builder,
		const Object& value){
		if(value.*Member == DefaultValue){
			return {};
		}
		return srl::write_to(builder, id, value.*Member);
	}

	template <typename Object>
	[[nodiscard]] static std::expected<void, error> read(
		const byte_record_view& record,
		Object& out){
		using member_type = field_member_type<Object, Member>;
		const auto ref = record.find_ref(id, codec<member_type>::tag);
		if(ref){
			return srl::read_from(record, id, out.*Member);
		}
		if(ref.error().code != error_code::missing_item){
			return std::unexpected{ref.error()};
		}
		out.*Member = DefaultValue;
		return {};
	}
};

template <typename T, typename... Fields>
[[nodiscard]] std::expected<void, error> write_fields(
	record_builder& builder,
	const T& value,
	std::tuple<Fields...>){
	using field_spec = std::tuple<Fields...>;
	return [&]<std::size_t... Index>(std::index_sequence<Index...>) -> std::expected<void, error>{
		std::expected<void, error> result{};
		(void)([&]() -> bool{
			using field_type = std::tuple_element_t<Index, field_spec>;
			if(const auto written = field_type::write(builder, value); !written){
				result = std::unexpected{written.error()};
				return false;
			}
			return true;
		}() && ...);
		return result;
	}(std::make_index_sequence<std::tuple_size_v<field_spec>>{});
}

template <typename T, typename... Fields>
[[nodiscard]] std::expected<void, error> read_fields(
	const byte_record_view& record,
	T& out,
	std::tuple<Fields...>){
	using field_spec = std::tuple<Fields...>;
	return [&]<std::size_t... Index>(std::index_sequence<Index...>) -> std::expected<void, error>{
		std::expected<void, error> result{};
		(void)([&]() -> bool{
			using field_type = std::tuple_element_t<Index, field_spec>;
			if(const auto read = field_type::read(record, out); !read){
				result = std::unexpected{read.error()};
				return false;
			}
			return true;
		}() && ...);
		return result;
	}(std::make_index_sequence<std::tuple_size_v<field_spec>>{});
}

template <typename T, typename Derived>
struct record_codec{
	static constexpr item_type_tag tag = item_type_tag::record;

	[[nodiscard]] static std::expected<void, error> write(
		record_builder& builder,
		const std::uint32_t id,
		const T& value){
		auto nested = record_builder::create_body();
		if(!nested){
			return std::unexpected{nested.error()};
		}
		if(const auto written = srl::write_fields(*nested, value, typename Derived::field_spec{}); !written){
			return std::unexpected{written.error()};
		}

		auto bytes = std::move(*nested).finalize();
		if(!bytes){
			return std::unexpected{bytes.error()};
		}
		return builder.add_record(id, *bytes);
	}

	[[nodiscard]] static std::expected<void, error> read(
		const byte_record_view& record,
		const std::uint32_t id,
		T& out){
		const auto ref = record.find_ref(id, tag);
		if(!ref){
			return std::unexpected{ref.error()};
		}
		auto nested = parse_nested_record(record, *ref);
		if(!nested){
			return std::unexpected{nested.error()};
		}
		return srl::read_fields(*nested, out, typename Derived::field_spec{});
	}
};

template <typename T>
[[nodiscard]] constexpr record_options default_pack_options() noexcept{
	if constexpr(requires{ { codec<codec_value_type<T>>::options } -> std::convertible_to<record_options>; }){
		return codec<codec_value_type<T>>::options;
	} else{
		return {};
	}
}

template <codec_writable T>
[[nodiscard]] std::expected<srl_byte_buffer, error> pack(
	const T& value,
	const record_options options = srl::default_pack_options<T>()){
	auto builder = record_builder::create(options);
	if(!builder){
		return std::unexpected{builder.error()};
	}

	if(const auto written = srl::write_to(*builder, root_item_id, value); !written){
		return std::unexpected{written.error()};
	}

	return std::move(*builder).finalize();
}

template <codec_readable T>
[[nodiscard]] std::expected<void, error> unpack(
	const std::span<const std::byte> bytes,
	T& out){
	auto record = parse_record_view(bytes);
	if(!record){
		return std::unexpected{record.error()};
	}
	return srl::read_from(*record, root_item_id, out);
}

template <codec_readable T>
requires std::default_initializable<codec_value_type<T>>
[[nodiscard]] std::expected<codec_value_type<T>, error> extract(
	const std::span<const std::byte> bytes){
	codec_value_type<T> result{};
	if(const auto unpacked = srl::unpack(bytes, result); !unpacked){
		return std::unexpected{unpacked.error()};
	}
	return result;
}

#pragma region CodecSpec
template <typename T>
requires explicit_trivial_atom<T>
struct codec<T>{
	static constexpr item_type_tag tag = item_type_tag::bytes;

	[[nodiscard]] static std::expected<void, error> write(
		record_builder& builder,
		const std::uint32_t id,
		const T& value) noexcept{
		return builder.add_trivial(id, value, tag);
	}

	[[nodiscard]] static std::expected<void, error> read(
		const byte_record_view& record,
		const std::uint32_t id,
		T& out) noexcept{
		auto value = record.read_trivial<T>(id, tag);
		if(!value){
			return std::unexpected{value.error()};
		}
		out = *value;
		return {};
	}
};

template <typename T>
requires (std::integral<T> && !std::same_as<T, bool>)
struct codec<T>{
	static constexpr item_type_tag tag = std::is_unsigned_v<T> ? item_type_tag::uint : item_type_tag::sint;

	[[nodiscard]] static std::expected<void, error> write(
		record_builder& builder,
		const std::uint32_t id,
		const T value) noexcept{
		return builder.add_trivial(id, value, tag);
	}

	[[nodiscard]] static std::expected<void, error> read(
		const byte_record_view& record,
		const std::uint32_t id,
		T& out) noexcept{
		auto value = record.read_trivial<T>(id, tag);
		if(!value){
			return std::unexpected{value.error()};
		}
		out = *value;
		return {};
	}
};


template <typename T>
requires (std::floating_point<T> && (sizeof(T) == 4u || sizeof(T) == 8u))
struct codec<T>{
	static constexpr item_type_tag tag = item_type_tag::floating;

	[[nodiscard]] static std::expected<void, error> write(
		record_builder& builder,
		const std::uint32_t id,
		const T value) noexcept{
		return builder.add_trivial(id, value, tag);
	}

	[[nodiscard]] static std::expected<void, error> read(
		const byte_record_view& record,
		const std::uint32_t id,
		T& out) noexcept{
		auto value = record.read_trivial<T>(id, tag);
		if(!value){
			return std::unexpected{value.error()};
		}
		out = *value;
		return {};
	}
};

template <>
struct codec<bool>{
	static constexpr item_type_tag tag = item_type_tag::bool_u8;

	[[nodiscard]] static std::expected<void, error> write(
		record_builder& builder,
		const std::uint32_t id,
		const bool value) noexcept{
		const std::uint8_t byte = value ? 1u : 0u;
		return builder.add_trivial(id, byte, tag);
	}

	[[nodiscard]] static std::expected<void, error> read(
		const byte_record_view& record,
		const std::uint32_t id,
		bool& out) noexcept{
		auto value = record.read_trivial<std::uint8_t>(id, tag);
		if(!value){
			return std::unexpected{value.error()};
		}
		if(*value > 1u){
			return codec_fail(error_code::invalid_payload_shape, id);
		}
		out = *value != 0u;
		return {};
	}
};

template <typename T>
requires std::is_enum_v<T>
struct codec<T>{
	using underlying_type = std::underlying_type_t<T>;

	static constexpr item_type_tag tag = codec<underlying_type>::tag;

	[[nodiscard]] static std::expected<void, error> write(
		record_builder& builder,
		const std::uint32_t id,
		const T value) noexcept{
		return codec<underlying_type>::write(builder, id, static_cast<underlying_type>(value));
	}

	[[nodiscard]] static std::expected<void, error> read(
		const byte_record_view& record,
		const std::uint32_t id,
		T& out) noexcept{
		underlying_type value{};
		if(const auto read = codec<underlying_type>::read(record, id, value); !read){
			return std::unexpected{read.error()};
		}
		out = static_cast<T>(value);
		return {};
	}
};

template <>
struct codec<std::string>{
	static constexpr item_type_tag tag = item_type_tag::text_utf8;

	[[nodiscard]] static std::expected<void, error> write(
		record_builder& builder,
		const std::uint32_t id,
		const std::string& value) noexcept{
		return builder.add(
			id,
			tag,
			std::as_bytes(std::span{value.data(), value.size()}));
	}

	[[nodiscard]] static std::expected<void, error> read(
		const byte_record_view& record,
		const std::uint32_t id,
		std::string& out) noexcept{
		const auto payload = record.find(id, tag);
		if(!payload){
			return std::unexpected{payload.error()};
		}
		try{
			out.assign(
				reinterpret_cast<const char*>(payload->bytes.data()),
				payload->bytes.size());
		} catch(const std::bad_alloc&){
			return codec_fail(error_code::allocation_failed, id);
		} catch(const std::length_error&){
			return codec_fail(error_code::allocation_failed, id);
		}
		return {};
	}
};

template <>
struct codec<std::string_view>{
	static constexpr item_type_tag tag = item_type_tag::text_utf8;

	[[nodiscard]] static std::expected<void, error> write(
		record_builder& builder,
		const std::uint32_t id,
		const std::string_view value) noexcept{
		return builder.add(
			id,
			tag,
			std::as_bytes(std::span{value.data(), value.size()}));
	}
};

template <codec_readable T>
struct codec<std::optional<T>>{
	static constexpr item_type_tag tag = codec<T>::tag;

	[[nodiscard]] static std::expected<void, error> write(
		record_builder& builder,
		const std::uint32_t id,
		const std::optional<T>& value){
		if(!value.has_value()){
			return {};
		}
		return codec<T>::write(builder, id, *value);
	}

	[[nodiscard]] static std::expected<void, error> read(
		const byte_record_view& record,
		const std::uint32_t id,
		std::optional<T>& out){
		const auto ref = record.find_ref(id, tag);
		if(!ref){
			if(ref.error().code == error_code::missing_item){
				out.reset();
				return {};
			}
			return std::unexpected{ref.error()};
		}

		T value{};
		if(const auto read = codec<T>::read(record, id, value); !read){
			return std::unexpected{read.error()};
		}
		out = std::move(value);
		return {};
	}
};

template <typename T>
[[nodiscard]] std::expected<void, error> write_packed_trivial_sequence(
	record_builder& builder,
	const std::uint32_t id,
	const T& values) noexcept{
	try{
		using element_type = T::value_type;
		if constexpr(byte_contiguous_sequence<T>){
			return builder.add(
				id,
				item_type_tag::bytes,
				std::as_bytes(std::span{values.data(), values.size()}));
		} else{
			srl_byte_buffer bytes{};
			for(const element_type& value : values){
				if(const auto appended = bytes.append(std::as_bytes(std::span{std::addressof(value), 1u})); !appended){
					return codec_fail(appended.error(), id);
				}
			}
			return builder.add(id, item_type_tag::bytes, bytes.span());
		}
	} catch(const std::bad_alloc&){
		return codec_fail(error_code::allocation_failed, id);
	} catch(const std::length_error&){
		return codec_fail(error_code::allocation_failed, id);
	}
}

template <typename T>
[[nodiscard]] std::expected<void, error> read_packed_trivial_sequence(
	const byte_record_view& record,
	const std::uint32_t id,
	const std::uint32_t count,
	T& out){
	using element_type = T::value_type;

	const auto payload = record.find(id, item_type_tag::bytes);
	if(!payload){
		return std::unexpected{payload.error()};
	}
	if(count > std::numeric_limits<std::uint32_t>::max() / sizeof(element_type)
		|| payload->bytes.size() != static_cast<std::size_t>(count) * sizeof(element_type)){
		return codec_fail(error_code::invalid_payload_shape, id);
	}

	try{
		out.clear();
		if constexpr(reservable_sequence<T>){
			out.reserve(count);
		}

		for(std::uint32_t index = 0u; index != count; ++index){
			auto element = srl::read_trivial<element_type>(payload_view{
					.bytes = payload->bytes.subspan(
						static_cast<std::size_t>(index) * sizeof(element_type),
						sizeof(element_type)),
					.type_tag = std::to_underlying(item_type_tag::bytes),
					.aux = 0u
				});
			if(!element){
				return std::unexpected{element.error()};
			}
			out.push_back(*element);
		}
	} catch(const std::bad_alloc&){
		return codec_fail(error_code::allocation_failed, id);
	} catch(const std::length_error&){
		return codec_fail(error_code::allocation_failed, id);
	}
	return {};
}

template <sequence_container T>
requires codec_readable<typename T::value_type>
struct sequence_codec{
	using element_type = T::value_type;

	static constexpr item_type_tag tag = item_type_tag::record;

	[[nodiscard]] static std::expected<void, error> write(
		record_builder& builder,
		const std::uint32_t id,
		const T& values){
		if(values.size() > std::numeric_limits<std::uint32_t>::max()){
			return codec_fail(error_code::record_too_large, id);
		}
		const auto count = static_cast<std::uint32_t>(values.size());

		auto nested = record_builder::create_body();
		if(!nested){
			return std::unexpected{nested.error()};
		}
		if(const auto written = srl::write_to(*nested, sequence_count_id, count); !written){
			return std::unexpected{written.error()};
		}

		if constexpr(packed_sequence_atom<element_type>){
			if(const auto written = srl::write_packed_trivial_sequence(*nested, sequence_packed_data_id, values); !written){
				return std::unexpected{written.error()};
			}
		} else{
			std::uint32_t index{};
			for(const element_type& value : values){
				if(index > std::numeric_limits<std::uint32_t>::max() - sequence_element_first_id){
					return codec_fail(error_code::record_too_large, id);
				}
				if(const auto written = srl::write_to(*nested, sequence_element_first_id + index, value); !written){
					return std::unexpected{written.error()};
				}
				++index;
			}
		}

		auto bytes = std::move(*nested).finalize();
		if(!bytes){
			return std::unexpected{bytes.error()};
		}
		return builder.add_record(id, *bytes);
	}

	[[nodiscard]] static std::expected<void, error> read(
		const byte_record_view& record,
		const std::uint32_t id,
		T& out){
		const auto ref = record.find_ref(id, tag);
		if(!ref){
			return std::unexpected{ref.error()};
		}
		auto nested = parse_nested_record(record, *ref);
		if(!nested){
			return std::unexpected{nested.error()};
		}

		std::uint32_t count{};
		if(const auto read = srl::read_from(*nested, sequence_count_id, count); !read){
			return std::unexpected{read.error()};
		}

		if constexpr(packed_sequence_atom<element_type>){
			return srl::read_packed_trivial_sequence(*nested, sequence_packed_data_id, count, out);
		} else{
			try{
				T values{};
				if constexpr(reservable_sequence<T>){
					values.reserve(count);
				}

				for(std::uint32_t index = 0u; index != count; ++index){
					element_type value{};
					if(const auto read = srl::read_from(*nested, sequence_element_first_id + index, value); !read){
						return std::unexpected{read.error()};
					}
					values.push_back(std::move(value));
				}
				out = std::move(values);
			} catch(const std::bad_alloc&){
				return codec_fail(error_code::allocation_failed, id);
			} catch(const std::length_error&){
				return codec_fail(error_code::allocation_failed, id);
			}
			return {};
		}
	}
};

template <typename T, typename Allocator>
requires codec_readable<T>
struct codec<std::vector<T, Allocator>> : sequence_codec<std::vector<T, Allocator>>{};

template <typename T, typename Allocator>
requires codec_readable<T>
struct codec<std::deque<T, Allocator>> : sequence_codec<std::deque<T, Allocator>>{};

#pragma endregion
}
