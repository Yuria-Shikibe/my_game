export module mo_yanxi.game.physics.shape_codec;

import std;
import mo_yanxi.srl.codec;
import mo_yanxi.game.physics.shape;
import mo_yanxi.game.physics.collision_shape_editor_metadata;

namespace mo_yanxi::game::physics{
struct collision_shape_record_packed_data{
	std::vector<collision_shape_record_part> parts{};
	std::vector<math::vec2> polygon_vertices{};
};
}

export namespace mo_yanxi::srl{
template <>
struct trivial_atom<math::vec2> : std::true_type{};

template <>
struct codec<math::trans2>
	: srl::record_codec<math::trans2, codec<math::trans2>>{
	using field_spec = std::tuple<
		srl::field<1u, &math::trans2::vec>,
		srl::field<2u, &math::trans2::rot>>;
};

template <>
struct codec<game::physics::circle_shape>
	: srl::record_codec<game::physics::circle_shape, codec<game::physics::circle_shape>>{
	using field_spec = std::tuple<
		srl::field<1u, &game::physics::circle_shape::radius>>;
};

template <>
struct codec<game::physics::capsule_shape>
	: srl::record_codec<game::physics::capsule_shape, codec<game::physics::capsule_shape>>{
	using field_spec = std::tuple<
		srl::field<1u, &game::physics::capsule_shape::begin>,
		srl::field<2u, &game::physics::capsule_shape::end>,
		srl::field<3u, &game::physics::capsule_shape::radius>>;
};

template <>
struct codec<game::physics::box_shape>
	: srl::record_codec<game::physics::box_shape, codec<game::physics::box_shape>>{
	using field_spec = std::tuple<
		srl::field<1u, &game::physics::box_shape::half_extent>>;
};

template <>
struct codec<game::physics::convex_polygon_shape>
	: srl::record_codec<game::physics::convex_polygon_shape, codec<game::physics::convex_polygon_shape>>{
	using field_spec = std::tuple<
		srl::field<1u, &game::physics::convex_polygon_shape::vertices>>;
};

template <>
struct codec<game::physics::convex_polygon_storage>
	: srl::record_codec<game::physics::convex_polygon_storage, codec<game::physics::convex_polygon_storage>>{
	using field_spec = std::tuple<
		srl::field<1u, &game::physics::convex_polygon_storage::vertex_offset>,
		srl::field<2u, &game::physics::convex_polygon_storage::vertex_count>>;
};

template <typename Shape>
requires srl::codec_readable<Shape>
struct codec<game::physics::shape_of<Shape>>
	: srl::record_codec<game::physics::shape_of<Shape>, codec<game::physics::shape_of<Shape>>>{
	using field_spec = std::tuple<
		srl::field<1u, &game::physics::shape_of<Shape>::local_transform>,
		srl::field<2u, &game::physics::shape_of<Shape>::shape>>;
};

template <>
struct codec<game::physics::collision_shape>
	: srl::record_codec<game::physics::collision_shape, codec<game::physics::collision_shape>>{
	static constexpr record_options options{.schema_id = 0x67616d655f636f6cull};

	using field_spec = std::tuple<
		srl::field<1u, &game::physics::collision_shape::circles>,
		srl::field<2u, &game::physics::collision_shape::capsules>,
		srl::field<3u, &game::physics::collision_shape::boxes>,
		srl::field<4u, &game::physics::collision_shape::convex_polygons>>;
};

template <>
struct codec<game::physics::collision_shape_record_part>{
	static constexpr item_type_tag tag = item_type_tag::record;

	[[nodiscard]] static std::expected<void, error> write(
		record_builder& builder,
		const std::uint32_t id,
		const game::physics::collision_shape_record_part& value){
		auto nested = record_builder::create_body();
		if(!nested){
			return std::unexpected{nested.error()};
		}

		if(const auto written = srl::write_to(*nested, 1u, value.type); !written){
			return std::unexpected{written.error()};
		}
		if(const auto written = srl::write_to(*nested, 2u, value.local_transform); !written){
			return std::unexpected{written.error()};
		}

		switch(value.type){
		case game::physics::shape_type::circle:
			if(const auto written = srl::write_to(*nested, 3u, value.payload.circle); !written){
				return std::unexpected{written.error()};
			}
			break;
		case game::physics::shape_type::capsule:
			if(const auto written = srl::write_to(*nested, 3u, value.payload.capsule); !written){
				return std::unexpected{written.error()};
			}
			break;
		case game::physics::shape_type::box:
			if(const auto written = srl::write_to(*nested, 3u, value.payload.box); !written){
				return std::unexpected{written.error()};
			}
			break;
		case game::physics::shape_type::convex_polygon:
			if(const auto written = srl::write_to(*nested, 3u, value.payload.convex_polygon); !written){
				return std::unexpected{written.error()};
			}
			break;
		default:
			return srl::codec_fail(error_code::invalid_payload_shape, id);
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
		game::physics::collision_shape_record_part& out){
		const auto ref = record.find_ref(id, tag);
		if(!ref){
			return std::unexpected{ref.error()};
		}
		auto nested = parse_nested_record(record, *ref);
		if(!nested){
			return std::unexpected{nested.error()};
		}

		game::physics::shape_type type{};
		if(const auto read = srl::read_from(*nested, 1u, type); !read){
			return std::unexpected{read.error()};
		}

		math::trans2 local_transform{};
		if(const auto read = srl::read_from(*nested, 2u, local_transform); !read){
			return std::unexpected{read.error()};
		}

		game::physics::collision_shape_record_part result{};
		result.type = type;
		result.local_transform = local_transform;
		switch(type){
		case game::physics::shape_type::circle:{
			game::physics::circle_shape payload{};
			if(const auto read = srl::read_from(*nested, 3u, payload); !read){
				return std::unexpected{read.error()};
			}
			std::construct_at(std::addressof(result.payload.circle), payload);
			out = result;
			return {};
		}
		case game::physics::shape_type::capsule:{
			game::physics::capsule_shape payload{};
			if(const auto read = srl::read_from(*nested, 3u, payload); !read){
				return std::unexpected{read.error()};
			}
			std::construct_at(std::addressof(result.payload.capsule), payload);
			out = result;
			return {};
		}
		case game::physics::shape_type::box:{
			game::physics::box_shape payload{};
			if(const auto read = srl::read_from(*nested, 3u, payload); !read){
				return std::unexpected{read.error()};
			}
			std::construct_at(std::addressof(result.payload.box), payload);
			out = result;
			return {};
		}
		case game::physics::shape_type::convex_polygon:{
			game::physics::convex_polygon_storage payload{};
			if(const auto read = srl::read_from(*nested, 3u, payload); !read){
				return std::unexpected{read.error()};
			}
			std::construct_at(std::addressof(result.payload.convex_polygon), payload);
			out = result;
			return {};
		}
		default:
			return srl::codec_fail(error_code::invalid_payload_shape, id);
		}
	}
};

template <>
struct codec<game::physics::collision_shape_record_packed_data>
	: srl::record_codec<
		game::physics::collision_shape_record_packed_data,
		codec<game::physics::collision_shape_record_packed_data>>{
	using field_spec = std::tuple<
		srl::field<1u, &game::physics::collision_shape_record_packed_data::parts>,
		srl::field<2u, &game::physics::collision_shape_record_packed_data::polygon_vertices>>;
};

template <>
struct codec<game::physics::collision_shape_record>{
	static constexpr item_type_tag tag = item_type_tag::record;
	static constexpr record_options options{.schema_id = 0x67616d655f637072ull};

	[[nodiscard]] static std::expected<void, error> write(
		record_builder& builder,
		const std::uint32_t id,
		const game::physics::collision_shape_record& value){
		try{
			game::physics::collision_shape_record_packed_data packed{};
			packed.parts.assign(value.records().begin(), value.records().end());
			packed.polygon_vertices.assign(value.polygon_vertices().begin(), value.polygon_vertices().end());
			return srl::write_to(builder, id, packed);
		} catch(const std::bad_alloc&){
			return srl::codec_fail(error_code::allocation_failed, id);
		} catch(const std::length_error&){
			return srl::codec_fail(error_code::allocation_failed, id);
		}
	}

	[[nodiscard]] static std::expected<void, error> read(
		const byte_record_view& record,
		const std::uint32_t id,
		game::physics::collision_shape_record& out){
		game::physics::collision_shape_record_packed_data packed{};
		if(const auto read = srl::read_from(record, id, packed); !read){
			return std::unexpected{read.error()};
		}

		if(!game::physics::collision_shape_record::validate_packed_data(
			packed.parts,
			packed.polygon_vertices)){
			return srl::codec_fail(error_code::invalid_payload_shape, id);
		}

		try{
			out.assign_packed_data(packed.parts, packed.polygon_vertices);
		} catch(const std::bad_alloc&){
			return srl::codec_fail(error_code::allocation_failed, id);
		} catch(const std::length_error&){
			return srl::codec_fail(error_code::allocation_failed, id);
		} catch(const std::invalid_argument&){
			return srl::codec_fail(error_code::invalid_payload_shape, id);
		}
		return {};
	}
};

template <>
struct codec<game::physics::collision_shape_editor_mirror_modifier>
	: srl::record_codec<
		game::physics::collision_shape_editor_mirror_modifier,
		codec<game::physics::collision_shape_editor_mirror_modifier>>{
	using field_spec = std::tuple<
		srl::field<1u, &game::physics::collision_shape_editor_mirror_modifier::enabled>,
		srl::field<2u, &game::physics::collision_shape_editor_mirror_modifier::mirror_x>,
		srl::field<3u, &game::physics::collision_shape_editor_mirror_modifier::mirror_y>,
		srl::field<4u, &game::physics::collision_shape_editor_mirror_modifier::origin>>;
};

template <>
struct codec<game::physics::collision_shape_editor_part>
	: srl::record_codec<
		game::physics::collision_shape_editor_part,
		codec<game::physics::collision_shape_editor_part>>{
	using field_spec = std::tuple<
		srl::field<1u, &game::physics::collision_shape_editor_part::type>,
		srl::field<2u, &game::physics::collision_shape_editor_part::local_transform>,
		srl::field<3u, &game::physics::collision_shape_editor_part::circle>,
		srl::field<4u, &game::physics::collision_shape_editor_part::capsule>,
		srl::field<5u, &game::physics::collision_shape_editor_part::box>,
		srl::field<6u, &game::physics::collision_shape_editor_part::convex_polygon>>;
};

template <>
struct codec<game::physics::collision_shape_editor_document>
	: srl::record_codec<
		game::physics::collision_shape_editor_document,
		codec<game::physics::collision_shape_editor_document>>{
	static constexpr record_options options{.schema_id = 0x67616d655f636564ull};

	using field_spec = std::tuple<
		srl::field<1u, &game::physics::collision_shape_editor_document::parts>,
		srl::field<2u, &game::physics::collision_shape_editor_document::mirror>>;
};
}
