//
// Created by Matrix on 2026/5/1.
//

export module mo_yanxi.gui.game_examples.loop_exec;

export import mo_yanxi.game.instance;
import mo_yanxi.gui.cfg.builtin.main_loop;
import std;

namespace mo_yanxi::gui::cfg::builtin{
export
class game_instance_holder{
private:
	std::unique_ptr<game::game_instance> game_{std::make_unique<game::game_instance>()};

public:
	game_instance_holder() = default;

	game_instance_holder(const game_instance_holder&) = delete;
	game_instance_holder& operator=(const game_instance_holder&) = delete;

	game_instance_holder(game_instance_holder&& other) noexcept = default;
	game_instance_holder& operator=(game_instance_holder&& other) noexcept = default;
	~game_instance_holder() = default;

	[[nodiscard]] game::game_instance& get() noexcept{
		return *game_;
	}

	[[nodiscard]] const game::game_instance& get() const noexcept{
		return *game_;
	}

	[[nodiscard]] game::game_instance* operator->() noexcept{
		return game_.get();
	}

	[[nodiscard]] const game::game_instance* operator->() const noexcept{
		return game_.get();
	}
};

export
struct main_loop_payload{
	game_instance_holder game{};
};

export using main_loop_type = main_loop<main_loop_payload>;

export
void main_loop_fn(main_loop<main_loop_payload>& main_loop);
}
