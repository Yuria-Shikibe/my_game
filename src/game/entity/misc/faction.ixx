module;

#include <cassert>

export module mo_yanxi.game.faction;

import mo_yanxi.graphic.color;
import mo_yanxi.graphic.image_atlas.util;

import std;

namespace mo_yanxi::game{
	export
	using faction_id = unsigned;

	export
	struct faction{
		faction_id id{};
		std::string name{};

		graphic::color color{};
		graphic::borrowed_image_region icon{};

		[[nodiscard]] bool is_hostile_to(faction_id o) const noexcept{
			return o != id;
		}
	};

	export const inline faction faction_0{0};
	export const inline faction faction_1{1};

	export
	struct faction_ref{
	private:
		const faction* meta_{};
		faction_id id_{};

	public:
		[[nodiscard]] constexpr faction_ref() = default;

		[[nodiscard]] constexpr explicit(false) faction_ref(const faction* meta)
			noexcept : meta_(meta), id_(meta ? meta->id : faction_id{}){
		}

		[[nodiscard]] constexpr explicit(false) faction_ref(const faction& meta)
			noexcept : meta_(&meta), id_(meta.id){
		}

		constexpr bool operator==(const faction_ref& o) const noexcept{
			return id_ == o.id_;
		}

		[[nodiscard]] constexpr const faction* get_meta() const noexcept{
			return meta_;
		}

		constexpr const  faction* operator->() const noexcept{
			return meta_;
		}

		constexpr const  faction& operator*() const noexcept{
			assert(meta_);
			return *meta_;
		}

		explicit operator bool() const noexcept{
			return meta_ != nullptr;
		}

		[[nodiscard]] constexpr faction_id get_id() const noexcept{
			return id_;
		}
	};
}
