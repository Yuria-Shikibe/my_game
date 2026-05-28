//
// Created by Matrix on 2025/6/29.
//

export module mo_yanxi.game.ecs.component.command;

export import mo_yanxi.game.ecs.component.manage;
export import mo_yanxi.game.path_sequence;
import mo_yanxi.math;
import mo_yanxi.math.trans2;
import mo_yanxi.math.vector2;
import std;

namespace mo_yanxi::game::ecs{
	using arth_type = float;

	using variant_t = std::variant<
		std::monostate,
		math::trans2,
		entity_ref
	>;

	export
	struct reference_target{

		[[nodiscard]] std::optional<mo_yanxi::math::trans2> get_target_trans() const noexcept;

		[[nodiscard]] bool is_absolute() const noexcept{
			return target.index() == 0;
		}

		void reset() noexcept{
			target = std::monostate{};
		}

		template <typename T>
			requires std::assignable_from<variant_t&, const T&>
		reference_target& operator=(const T& rhs) noexcept{
			target = rhs;
			return *this;
		}

		constexpr bool operator==(const reference_target&) const noexcept = default;

	private:
		variant_t target{};
	};

	export
	struct formation{
		math::trans2 trans{};
	};


	export
	struct orbit{
		arth_type semi_major{};
		arth_type semi_minor{};
		math::trans2 offset{}; //using uniformed angle?
	};

	export
	struct route{
		using variant_t = std::variant<
			std::monostate,
			path_seq,
			// orbit,
			formation
		>;

		[[nodiscard]] std::optional<math::trans2> get_next_local() const noexcept;

		march_state update(math::trans2 current_local_trs, arth_type dst_margin, arth_type ang_margin) noexcept;

		template <typename S, typename T>
			requires requires(T t, variant_t& v){
				std::visit(t, std::forward_like<S>(v));
			}
		decltype(auto) visit(this S&& self, T fn){
			return std::visit(fn, std::forward_like<S>(self.route_));
		}

		template <typename T>
		[[nodiscard]] constexpr bool holds() const noexcept{
			return std::holds_alternative<T>(route_);
		}

		template <typename T>
		[[nodiscard]] constexpr const T* get_if() const noexcept{
			return std::get_if<T>(&route_);
		}

		template <typename T>
		[[nodiscard]] constexpr T* get_if()  noexcept{
			return std::get_if<T>(&route_);
		}


		template <typename T>
			requires std::assignable_from<variant_t&, T&&>
		route& operator=(T&& rhs) noexcept{
			route_ = std::forward<T>(rhs);
			return *this;
		}


	private:
		variant_t route_{};
	};

	export
	struct move_command{
		bool disable_rotate{};
		bool disable_move{};

		bool translational{};

		reference_target reference{};
		reference_target override_face_target{};

		route route{};

		march_state update(
			math::trans2 current,
			arth_type dst_margin, arth_type ang_margin
		) noexcept;

		[[nodiscard]] std::optional<math::trans2> get_next(math::trans2 current, arth_type dst_margin) const noexcept;
	};
}