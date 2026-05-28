//
// Created by Matrix on 2025/5/21.
//

export module mo_yanxi.game.content;

export import mo_yanxi.game.meta.content;

export import mo_yanxi.game.meta.chamber;
import mo_yanxi.meta_programming;
import std;

namespace mo_yanxi::game::content{
	export inline meta::content_manager chambers{};

	export void load();
}
