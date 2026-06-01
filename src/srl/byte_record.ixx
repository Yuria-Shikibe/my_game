module;

#include <cstddef>

export module mo_yanxi.srl.byte_record;

import std;
import mo_yanxi.srl.srl_byte_buffer;

export namespace mo_yanxi::srl{
static_assert(std::endian::native == std::endian::little, "byte_record requires a little-endian target");

constexpr inline std::uint32_t metadata_id = std::numeric_limits<std::uint32_t>::max();
constexpr inline std::uint32_t record_magic = 0x314c5253u; // "SRL1".
constexpr inline std::uint16_t format_major = 1;
constexpr inline std::uint16_t format_minor = 0;
constexpr inline std::uint16_t item_head_size = 16;
constexpr inline std::uint16_t record_alignment = 16;
constexpr inline std::uint64_t fnv1a64_offset = 14695981039346656037ull;
constexpr inline std::uint64_t fnv1a64_prime = 1099511628211ull;

enum class item_type_tag : std::uint16_t{
	invalid = 0,
	record = 1,
	bytes = 2,
	text_utf8 = 3,
	uint = 4,
	sint = 5,
	floating = 6,
	bool_u8 = 7
};

enum class error_code : std::uint8_t{
	none,
	allocation_failed,
	record_too_large,
	truncated_head,
	metadata_not_first,
	metadata_payload_invalid,
	invalid_magic,
	unsupported_major,
	unsupported_head_size,
	unsupported_alignment,
	payload_out_of_bounds,
	unaligned_payload,
	nonzero_padding,
	hash_mismatch,
	item_count_mismatch,
	reserved_item_id,
	invalid_type_tag,
	invalid_aux,
	invalid_payload_shape,
	missing_item,
	duplicate_item,
	type_mismatch
};

struct error{
	error_code code{error_code::none};
	std::uint32_t id{};
	std::uint32_t offset{};
};

[[nodiscard]] constexpr std::string_view message(const error_code code) noexcept{
	using namespace std::string_view_literals;
	static constexpr std::array arr{
			"none"sv,
			"allocation failed"sv,
			"record is larger than uint32 range"sv,
			"truncated item head"sv,
			"record metadata item is not first"sv,
			"invalid record metadata payload"sv,
			"invalid record magic"sv,
			"unsupported record major version"sv,
			"unsupported item head size"sv,
			"unsupported record alignment"sv,
			"item payload is out of bounds"sv,
			"item payload is not aligned"sv,
			"item padding is not zeroed"sv,
			"record content hash mismatch"sv,
			"record item count mismatch"sv,
			"reserved item id used outside metadata"sv,
			"invalid item type tag"sv,
			"invalid item auxiliary data"sv,
			"payload shape does not match item type tag"sv,
			"item is missing"sv,
			"item is duplicated"sv,
			"item type tag mismatch"sv,
		};

	return std::to_underlying(code) >= arr.size() ? "unknown serialization error"sv : arr[std::to_underlying(code)];
}

struct item_head_disk{
	std::uint32_t id{};
	std::uint32_t payload_size{};
	std::uint16_t type_tag{};
	std::uint16_t head_size{};
	std::uint32_t aux{};
};

static_assert(sizeof(item_head_disk) == item_head_size);

struct record_meta_disk{
	std::uint32_t magic{};
	std::uint16_t major{};
	std::uint16_t minor{};
	std::uint16_t item_head_size{};
	std::uint16_t alignment{};
	std::uint32_t item_count{};
	std::uint64_t schema_id{};
	std::uint64_t content_hash{};
};

static_assert(sizeof(record_meta_disk) == 32);
static_assert(offsetof(record_meta_disk, content_hash) == 24);

struct record_options{
	std::uint16_t major{format_major};
	std::uint16_t minor{format_minor};
	std::uint16_t alignment{record_alignment};
	std::uint64_t schema_id{};
};

struct record_meta{
	std::uint16_t major{};
	std::uint16_t minor{};
	std::uint16_t alignment{};
	std::uint32_t item_count{};
	std::uint64_t schema_id{};
	std::uint64_t content_hash{};
};

struct subrange{
	std::uint32_t src{};
	std::uint32_t dst{};

	[[nodiscard]] constexpr std::uint32_t size() const noexcept{
		return dst - src;
	}
};

struct chunk_ref{
	subrange payload{};
	std::uint16_t type_tag{};
	std::uint32_t aux{};

	[[nodiscard]] constexpr item_type_tag tag() const noexcept{
		return static_cast<item_type_tag>(type_tag);
	}
};

struct payload_view{
	std::span<const std::byte> bytes{};
	std::uint16_t type_tag{};
	std::uint32_t aux{};

	[[nodiscard]] constexpr item_type_tag tag() const noexcept{
		return static_cast<item_type_tag>(type_tag);
	}
};

using byte_record_index = std::flat_map<std::uint32_t, chunk_ref>;

template <typename T>
concept trivially_copyable_record_object =
	std::is_object_v<std::remove_cv_t<T>>
	&& !std::is_array_v<std::remove_cv_t<T>>
	&& std::is_trivially_copyable_v<std::remove_cv_t<T>>;

template <trivially_copyable_record_object T>
using record_object_type = std::remove_cv_t<T>;

static_assert(trivially_copyable_record_object<item_head_disk>);
static_assert(trivially_copyable_record_object<record_meta_disk>);
}

namespace mo_yanxi::srl{
[[nodiscard]] constexpr std::unexpected<error> fail(
	const error_code code,
	const std::uint32_t id = 0,
	const std::uint32_t offset = 0) noexcept{
	return std::unexpected{error{.code = code, .id = id, .offset = offset}};
}

[[nodiscard]] constexpr error_code to_error_code(const srl_byte_buffer_error code) noexcept{
	switch(code){
	case srl_byte_buffer_error::none : return error_code::none;
	case srl_byte_buffer_error::allocation_failed : return error_code::allocation_failed;
	case srl_byte_buffer_error::size_overflow :
	case srl_byte_buffer_error::overwrite_size_out_of_bounds : return error_code::record_too_large;
	}
	return error_code::record_too_large;
}

[[nodiscard]] constexpr std::unexpected<error> fail(
	const srl_byte_buffer_error code,
	const std::uint32_t id = 0,
	const std::uint32_t offset = 0) noexcept{
	return srl::fail(srl::to_error_code(code), id, offset);
}

[[nodiscard]] constexpr std::uint32_t align_up(
	const std::uint32_t value,
	const std::uint16_t alignment) noexcept{
	const auto a = static_cast<std::uint32_t>(alignment);
	return (value + a - 1u) / a * a;
}

[[nodiscard]] constexpr bool add_overflow(
	const std::uint32_t lhs,
	const std::uint32_t rhs,
	std::uint32_t& out) noexcept{
	if(lhs > std::numeric_limits<std::uint32_t>::max() - rhs){
		return true;
	}
	out = lhs + rhs;
	return false;
}

template <trivially_copyable_record_object T>
[[nodiscard]] constexpr std::span<const std::byte> trivial_object_bytes(const T& value) noexcept{
	return std::as_bytes(std::span{std::addressof(value), 1u});
}

template <trivially_copyable_record_object T>
[[nodiscard]] record_object_type<T> read_trivial_at(
	const std::span<const std::byte> data,
	const std::uint32_t offset) noexcept{
	using object_type = record_object_type<T>;
	alignas(object_type) std::byte storage[sizeof(object_type)];
	std::memcpy(storage, data.data() + offset, sizeof(object_type));
	const auto* const object = std::start_lifetime_as<object_type>(storage);
	return *object;
}

template <trivially_copyable_record_object T>
void write_trivial_at(
	std::span<std::byte> data,
	const std::uint32_t offset,
	const T& value) noexcept{
	const std::span<const std::byte> bytes = srl::trivial_object_bytes(value);
	std::memcpy(data.data() + offset, bytes.data(), bytes.size());
}

template <trivially_copyable_record_object T>
[[nodiscard]] std::expected<void, error> append_trivial(srl_byte_buffer& out, const T& value) noexcept{
	if(const auto appended = out.append(srl::trivial_object_bytes(value)); !appended){
		return srl::fail(appended.error());
	}
	return {};
}

[[nodiscard]] item_head_disk read_head(
	const std::span<const std::byte> data,
	const std::uint32_t offset) noexcept{
	return srl::read_trivial_at<item_head_disk>(data, offset);
}

[[nodiscard]] record_meta_disk read_meta(const std::span<const std::byte> data, const std::uint32_t offset) noexcept{
	return srl::read_trivial_at<record_meta_disk>(data, offset);
}

[[nodiscard]] constexpr bool scalar_size_valid(const std::uint32_t size) noexcept{
	return size == 1u || size == 2u || size == 4u || size == 8u;
}

[[nodiscard]] constexpr bool builtin_tag(const std::uint16_t tag) noexcept{
	return tag >= std::to_underlying(item_type_tag::record)
		&& tag <= std::to_underlying(item_type_tag::bool_u8);
}

[[nodiscard]] constexpr bool validate_item_payload_shape(
	const std::uint16_t tag,
	const std::uint32_t payload_size,
	const std::uint32_t aux) noexcept{
	if(tag == std::to_underlying(item_type_tag::invalid)){
		return false;
	}
	if(!builtin_tag(tag)){
		return true;
	}
	if(aux != 0u){
		return false;
	}
	switch(static_cast<item_type_tag>(tag)){
	case item_type_tag::record :
	case item_type_tag::bytes :
	case item_type_tag::text_utf8 : return true;
	case item_type_tag::uint :
	case item_type_tag::sint : return scalar_size_valid(payload_size);
	case item_type_tag::floating : return payload_size == 4u || payload_size == 8u;
	case item_type_tag::bool_u8 : return payload_size == 1u;
	case item_type_tag::invalid : return false;
	}
	return false;
}

[[nodiscard]] std::uint64_t hash_record_bytes(const std::span<const std::byte> data) noexcept{
	constexpr std::uint32_t hash_offset =
		item_head_size + static_cast<std::uint32_t>(offsetof(record_meta_disk, content_hash));
	std::uint64_t hash = fnv1a64_offset;
	for(std::uint32_t idx = 0; idx != data.size(); ++idx){
		std::byte byte = data[idx];
		if(idx >= hash_offset && idx < hash_offset + sizeof(std::uint64_t)){
			byte = std::byte{};
		}
		hash ^= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(byte));
		hash *= fnv1a64_prime;
	}
	return hash;
}

struct parse_items_result{
	byte_record_index payloads{};
	std::uint32_t item_count{};
};

struct parse_result{
	record_meta meta{};
	byte_record_index payloads{};
};

[[nodiscard]] std::expected<parse_items_result, error> parse_items(
	const std::span<const std::byte> data,
	std::uint32_t offset,
	const std::uint16_t alignment) noexcept{
	try{
		if(data.size() > std::numeric_limits<std::uint32_t>::max()){
			return srl::fail(error_code::record_too_large);
		}

		parse_items_result result{};

		while(offset != data.size()){
			if(offset > data.size() || data.size() - offset < item_head_size){
				return srl::fail(error_code::truncated_head, 0, offset);
			}

			const item_head_disk head = read_head(data, offset);
			if(head.head_size != item_head_size){
				return srl::fail(error_code::unsupported_head_size, head.id, offset);
			}
			if(head.id == metadata_id){
				return srl::fail(error_code::reserved_item_id, head.id, offset);
			}
			if(head.type_tag == std::to_underlying(item_type_tag::invalid)){
				return srl::fail(error_code::invalid_type_tag, head.id, offset);
			}
			if(builtin_tag(head.type_tag) && head.aux != 0u){
				return srl::fail(error_code::invalid_aux, head.id, offset);
			}
			if(!validate_item_payload_shape(head.type_tag, head.payload_size, head.aux)){
				return srl::fail(error_code::invalid_payload_shape, head.id, offset);
			}

			std::uint32_t payload_src{};
			if(add_overflow(offset, item_head_size, payload_src)){
				return srl::fail(error_code::payload_out_of_bounds, head.id, offset);
			}
			if(payload_src % alignment != 0u){
				return srl::fail(error_code::unaligned_payload, head.id, offset);
			}

			std::uint32_t payload_dst{};
			if(add_overflow(payload_src, head.payload_size, payload_dst) || payload_dst > data.size()){
				return srl::fail(error_code::payload_out_of_bounds, head.id, offset);
			}

			const std::uint32_t padded_end = align_up(payload_dst, alignment);
			if(padded_end > data.size() || padded_end < payload_dst){
				return srl::fail(error_code::payload_out_of_bounds, head.id, offset);
			}
			for(std::uint32_t idx = payload_dst; idx != padded_end; ++idx){
				if(data[idx] != std::byte{}){
					return srl::fail(error_code::nonzero_padding, head.id, idx);
				}
			}

			const auto insertion = result.payloads.try_emplace(head.id, chunk_ref{
				                                                   .payload = subrange{
					                                                   .src = payload_src, .dst = payload_dst
				                                                   },
				                                                   .type_tag = head.type_tag,
				                                                   .aux = head.aux
			                                                   });
			if(!insertion.second){
				return srl::fail(error_code::duplicate_item, head.id, offset);
			}

			offset = padded_end;
			++result.item_count;
		}

		return result;
	} catch(const std::bad_alloc&){
		return srl::fail(error_code::allocation_failed);
	} catch(const std::length_error&){
		return srl::fail(error_code::allocation_failed);
	}
}

[[nodiscard]] std::expected<parse_result, error> parse_index(
	const std::span<const std::byte> data) noexcept{
	try{
		if(data.size() > std::numeric_limits<std::uint32_t>::max()){
			return srl::fail(error_code::record_too_large);
		}
		if(data.size() < item_head_size + sizeof(record_meta_disk)){
			return srl::fail(error_code::truncated_head);
		}

		const item_head_disk meta_head = read_head(data, 0);
		if(meta_head.id != metadata_id){
			return srl::fail(error_code::metadata_not_first);
		}
		if(meta_head.head_size != item_head_size
			|| meta_head.type_tag != std::to_underlying(item_type_tag::bytes)
			|| meta_head.aux != 0u
			|| meta_head.payload_size != sizeof(record_meta_disk)){
			return srl::fail(error_code::metadata_payload_invalid, metadata_id);
		}

		const record_meta_disk meta = read_meta(data, item_head_size);
		if(meta.magic != record_magic){
			return srl::fail(error_code::invalid_magic, metadata_id);
		}
		if(meta.major != format_major){
			return srl::fail(error_code::unsupported_major, metadata_id);
		}
		if(meta.item_head_size != item_head_size){
			return srl::fail(error_code::unsupported_head_size, metadata_id);
		}
		if(meta.alignment != record_alignment){
			return srl::fail(error_code::unsupported_alignment, metadata_id);
		}
		if(meta.content_hash != hash_record_bytes(data)){
			return srl::fail(error_code::hash_mismatch, metadata_id);
		}

		auto parsed_items = parse_items(
			data,
			align_up(static_cast<std::uint32_t>(item_head_size + meta_head.payload_size), meta.alignment),
			meta.alignment);
		if(!parsed_items){
			return srl::fail(parsed_items.error().code, parsed_items.error().id, parsed_items.error().offset);
		}

		if(parsed_items->item_count != meta.item_count){
			return srl::fail(error_code::item_count_mismatch, metadata_id);
		}

		return parse_result{
				.meta = record_meta{
					.major = meta.major,
					.minor = meta.minor,
					.alignment = meta.alignment,
					.item_count = meta.item_count,
					.schema_id = meta.schema_id,
					.content_hash = meta.content_hash
				},
				.payloads = std::move(parsed_items->payloads)
			};
	} catch(const std::bad_alloc&){
		return srl::fail(error_code::allocation_failed);
	} catch(const std::length_error&){
		return srl::fail(error_code::allocation_failed);
	}
}

template <typename Record>
[[nodiscard]] std::span<const std::byte> payload_span_impl(
	const Record& record,
	const chunk_ref& ref) noexcept{
	return {record.data.data() + ref.payload.src, ref.payload.size()};
}

template <typename Record>
[[nodiscard]] std::expected<chunk_ref, error> find_ref_impl(
	const Record& record,
	const std::uint32_t id,
	const item_type_tag expected_tag) noexcept{
	const auto it = record.payloads.find(id);
	if(it == record.payloads.end()){
		return srl::fail(error_code::missing_item, id);
	}
	const chunk_ref ref = it->second;
	if(expected_tag != item_type_tag::invalid && ref.type_tag != std::to_underlying(expected_tag)){
		return srl::fail(error_code::type_mismatch, id);
	}
	return ref;
}

template <typename Record>
[[nodiscard]] std::expected<payload_view, error> find_impl(
	const Record& record,
	const std::uint32_t id,
	const item_type_tag expected_tag) noexcept{
	const auto ref = find_ref_impl(record, id, expected_tag);
	if(!ref){
		return srl::fail(ref.error().code, ref.error().id, ref.error().offset);
	}
	return payload_view{
			.bytes = payload_span_impl(record, *ref),
			.type_tag = ref->type_tag,
			.aux = ref->aux
		};
}

template <trivially_copyable_record_object T>
[[nodiscard]] std::expected<record_object_type<T>, error> read_trivial_payload_impl(
	const payload_view& payload,
	const std::uint32_t id = 0) noexcept{
	using object_type = record_object_type<T>;
	if(payload.bytes.size() != sizeof(object_type)){
		return srl::fail(error_code::invalid_payload_shape, id);
	}
	return srl::read_trivial_at<object_type>(payload.bytes, 0u);
}

template <typename Record, trivially_copyable_record_object T>
[[nodiscard]] std::expected<record_object_type<T>, error> read_trivial_impl(
	const Record& record,
	const std::uint32_t id,
	const item_type_tag expected_tag) noexcept{
	const auto payload = find_impl(record, id, expected_tag);
	if(!payload){
		return srl::fail(payload.error().code, payload.error().id, payload.error().offset);
	}
	return srl::read_trivial_payload_impl<T>(*payload, id);
}
}

export namespace mo_yanxi::srl{
template <trivially_copyable_record_object T>
[[nodiscard]] std::expected<record_object_type<T>, error> read_trivial(
	const payload_view& payload) noexcept{
	return srl::read_trivial_payload_impl<T>(payload);
}

struct byte_record_view{
	std::span<const std::byte> data{};
	byte_record_index payloads{};

	[[nodiscard]] std::expected<chunk_ref, error> find_ref(
		const std::uint32_t id,
		const item_type_tag expected_tag = item_type_tag::invalid) const noexcept{
		return find_ref_impl(*this, id, expected_tag);
	}

	[[nodiscard]] std::expected<payload_view, error> find(
		const std::uint32_t id,
		const item_type_tag expected_tag = item_type_tag::invalid) const noexcept{
		return find_impl(*this, id, expected_tag);
	}

	[[nodiscard]] std::span<const std::byte> payload_span(const chunk_ref& ref) const noexcept{
		return payload_span_impl(*this, ref);
	}

	template <trivially_copyable_record_object T>
	[[nodiscard]] std::expected<record_object_type<T>, error> read_trivial(
		const std::uint32_t id,
		const item_type_tag expected_tag = item_type_tag::bytes) const noexcept{
		return srl::read_trivial_impl<byte_record_view, T>(*this, id, expected_tag);
	}
};

struct byte_record{
	srl_byte_buffer data{};
	byte_record_index payloads{};
	record_meta meta{};

	[[nodiscard]] std::expected<chunk_ref, error> find_ref(
		const std::uint32_t id,
		const item_type_tag expected_tag = item_type_tag::invalid) const noexcept{
		return find_ref_impl(*this, id, expected_tag);
	}

	[[nodiscard]] std::expected<payload_view, error> find(
		const std::uint32_t id,
		const item_type_tag expected_tag = item_type_tag::invalid) const noexcept{
		return find_impl(*this, id, expected_tag);
	}

	[[nodiscard]] std::span<const std::byte> payload_span(const chunk_ref& ref) const noexcept{
		return payload_span_impl(*this, ref);
	}

	template <trivially_copyable_record_object T>
	[[nodiscard]] std::expected<record_object_type<T>, error> read_trivial(
		const std::uint32_t id,
		const item_type_tag expected_tag = item_type_tag::bytes) const noexcept{
		return srl::read_trivial_impl<byte_record, T>(*this, id, expected_tag);
	}
};

[[nodiscard]] std::expected<byte_record_view, error> parse_record_view(
	const std::span<const std::byte> data) noexcept{
	auto parsed = parse_index(data);
	if(!parsed){
		return srl::fail(parsed.error().code, parsed.error().id, parsed.error().offset);
	}
	return byte_record_view{
			.data = data,
			.payloads = std::move(parsed->payloads)
		};
}

[[nodiscard]] std::expected<byte_record_view, error> parse_record_body_view(
	const std::span<const std::byte> data) noexcept{
	auto parsed = parse_items(data, 0u, record_alignment);
	if(!parsed){
		return srl::fail(parsed.error().code, parsed.error().id, parsed.error().offset);
	}
	return byte_record_view{
			.data = data,
			.payloads = std::move(parsed->payloads)
		};
}

[[nodiscard]] std::expected<byte_record, error> parse_record(
	const std::span<const std::byte> data) noexcept{
	try{
		byte_record result{};
		if(const auto assigned = result.data.assign(data); !assigned){
			return srl::fail(assigned.error());
		}

		auto parsed = parse_index(result.data.span());
		if(!parsed){
			return srl::fail(parsed.error().code, parsed.error().id, parsed.error().offset);
		}

		result.payloads = std::move(parsed->payloads);
		result.meta = parsed->meta;
		return result;
	} catch(const std::bad_alloc&){
		return srl::fail(error_code::allocation_failed);
	} catch(const std::length_error&){
		return srl::fail(error_code::allocation_failed);
	}
}

template <typename Record>
requires std::same_as<std::remove_cvref_t<Record>, byte_record>
	|| std::same_as<std::remove_cvref_t<Record>, byte_record_view>
[[nodiscard]] std::expected<byte_record_view, error> parse_nested_record(
	const Record& parent,
	const chunk_ref& ref) noexcept{
	if(ref.type_tag != std::to_underlying(item_type_tag::record)){
		return srl::fail(error_code::type_mismatch);
	}
	return parse_record_body_view(parent.payload_span(ref));
}

struct record_builder{
private:
	srl_byte_buffer data_{};
	std::flat_set<std::uint32_t> ids_{};
	record_options options_{};
	std::uint32_t item_count_{};
	bool has_metadata_{};

	record_builder() = default;

public:
	[[nodiscard]] static std::expected<record_builder, error> create(
		const record_options options = {}) noexcept{
		try{
			if(options.major != format_major){
				return srl::fail(error_code::unsupported_major, metadata_id);
			}
			if(options.alignment != record_alignment){
				return srl::fail(error_code::unsupported_alignment, metadata_id);
			}

			record_builder builder{};
			builder.options_ = options;
			builder.has_metadata_ = true;
			if(const auto reserved = builder.data_.reserve(item_head_size + sizeof(record_meta_disk)); !reserved){
				return srl::fail(reserved.error());
			}
			if(const auto appended = append_trivial(builder.data_, item_head_disk{
					.id = metadata_id,
					.payload_size = sizeof(record_meta_disk),
					.type_tag = std::to_underlying(item_type_tag::bytes),
					.head_size = item_head_size,
					.aux = 0
			            }); !appended){
				return srl::fail(appended.error().code, appended.error().id, appended.error().offset);
			}
			if(const auto appended = append_trivial(builder.data_, record_meta_disk{
					.magic = record_magic,
					.major = options.major,
					.minor = options.minor,
					.item_head_size = item_head_size,
					.alignment = options.alignment,
					.item_count = 0,
					.schema_id = options.schema_id,
					.content_hash = 0
			            }); !appended){
				return srl::fail(appended.error().code, appended.error().id, appended.error().offset);
			}
			return builder;
		} catch(const std::bad_alloc&){
			return srl::fail(error_code::allocation_failed);
		} catch(const std::length_error&){
			return srl::fail(error_code::allocation_failed);
		}
	}

	[[nodiscard]] static std::expected<record_builder, error> create_body() noexcept{
		return record_builder{};
	}

	[[nodiscard]] std::expected<void, error> add(
		const std::uint32_t id,
		const item_type_tag tag,
		const std::span<const std::byte> payload,
		const std::uint32_t aux = 0) noexcept{
		try{
			if(id == metadata_id){
				return srl::fail(error_code::reserved_item_id, id);
			}
			if(payload.size() > std::numeric_limits<std::uint32_t>::max()){
				return srl::fail(error_code::record_too_large, id);
			}

			const auto tag_value = std::to_underlying(tag);
			const auto payload_size = static_cast<std::uint32_t>(payload.size());
			if(tag == item_type_tag::invalid){
				return srl::fail(error_code::invalid_type_tag, id);
			}
			if(builtin_tag(tag_value) && aux != 0u){
				return srl::fail(error_code::invalid_aux, id);
			}
			if(!validate_item_payload_shape(tag_value, payload_size, aux)){
				return srl::fail(error_code::invalid_payload_shape, id);
			}
			if(item_count_ == std::numeric_limits<std::uint32_t>::max()){
				return srl::fail(error_code::record_too_large, id);
			}
			if(data_.size() > std::numeric_limits<std::uint32_t>::max()
				|| payload.size() > std::numeric_limits<std::uint32_t>::max()
				|| data_.size() + item_head_size + payload.size()
				> std::numeric_limits<std::uint32_t>::max() - (options_.alignment - 1u)){
				return srl::fail(error_code::record_too_large, id);
			}
			const std::uint32_t padded_size = align_up(
				static_cast<std::uint32_t>(data_.size() + item_head_size + payload.size()),
				options_.alignment);
			if(const auto reserved = data_.reserve(padded_size); !reserved){
				return srl::fail(reserved.error(), id);
			}
			const auto insertion = ids_.insert(id);
			if(!insertion.second){
				return srl::fail(error_code::duplicate_item, id);
			}

			if(const auto appended = append_trivial(data_, item_head_disk{
					.id = id,
					.payload_size = payload_size,
					.type_tag = tag_value,
					.head_size = item_head_size,
					.aux = aux
			            }); !appended){
				return appended;
			}

			if(const auto appended = data_.append(payload); !appended){
				return srl::fail(appended.error(), id);
			}
			if(const auto resized = data_.resize(padded_size, std::byte{}); !resized){
				return srl::fail(resized.error(), id);
			}
			++item_count_;
			return {};
		} catch(const std::bad_alloc&){
			return srl::fail(error_code::allocation_failed, id);
		} catch(const std::length_error&){
			return srl::fail(error_code::allocation_failed, id);
		}
	}

	[[nodiscard]] std::expected<void, error> add_record(
		const std::uint32_t id,
		const std::span<const std::byte> payload) noexcept{
		return this->add(id, item_type_tag::record, payload);
	}

	template <trivially_copyable_record_object T>
	[[nodiscard]] std::expected<void, error> add_trivial(
		const std::uint32_t id,
		const T& payload,
		const item_type_tag tag = item_type_tag::bytes,
		const std::uint32_t aux = 0) noexcept{
		return this->add(id, tag, srl::trivial_object_bytes(payload), aux);
	}

	[[nodiscard]] std::expected<srl_byte_buffer, error> finalize() && noexcept{
		try{
			if(!has_metadata_){
				return std::move(data_);
			}

			record_meta_disk meta{
				.magic = record_magic,
				.major = options_.major,
				.minor = options_.minor,
				.item_head_size = item_head_size,
				.alignment = options_.alignment,
				.item_count = item_count_,
				.schema_id = options_.schema_id,
				.content_hash = 0
			};
			srl::write_trivial_at(data_.span(), item_head_size, meta);
			const std::uint64_t hash = hash_record_bytes(data_);
			meta.content_hash = hash;
			srl::write_trivial_at(data_.span(), item_head_size, meta);
			return std::move(data_);
		} catch(const std::bad_alloc&){
			return srl::fail(error_code::allocation_failed);
		} catch(const std::length_error&){
			return srl::fail(error_code::allocation_failed);
		}
	}
};
}
