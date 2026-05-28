//
// Created by Matrix on 2025/6/18.
//

export module mo_yanxi.game.meta.grid.srl;

export import mo_yanxi.game.meta.chamber;
export import mo_yanxi.game.srl;

import std;

namespace mo_yanxi::game::meta::srl{
	using namespace game::srl;

	const chamber::basic_chamber* find_chamber_meta(std::string_view content_name) noexcept;

	export [[nodiscard]] chunk_serialize_handle write_grid(std::ostream& stream, const chamber::grid& grid);
	export void read_grid(std::istream& stream, chamber::grid& grid);
}
