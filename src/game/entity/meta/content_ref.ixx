module;

#include <cassert>

export module mo_yanxi.game.meta.content_ref;

import std;

namespace mo_yanxi::game::meta{

	template <typename T>
	concept named_content = requires(T& c, std::string_view sv){
		{ c.name = sv } noexcept -> std::same_as<std::string_view&>;
	};

	export
	template <typename T = void>
	struct content_ref{
		T* content;
		std::string_view name;

		constexpr auto& operator*() const noexcept requires (!std::is_void_v<T>){
			assert(content != nullptr);
			return *content;
		}

		constexpr T* operator->() const noexcept{
			return content;
		}

		explicit constexpr operator bool() const noexcept{
			return content != nullptr;
		}

		explicit(false) constexpr operator content_ref<const void>() const noexcept{
			return content_ref<const void>{ name, content};
		}

		explicit(false) constexpr operator content_ref<void>() const noexcept requires (!std::is_const_v<T>){
			return content_ref<void>{ name, content};
		}

		explicit(false) constexpr operator content_ref<const T>() const noexcept requires (!std::is_void_v<T>){
			return content_ref<const T>{ name, content};
		}

		template <typename Ty>
		[[nodiscard]] constexpr content_ref<Ty> as() const noexcept requires (std::same_as<T, void>){
			return content_ref<Ty>{ name, static_cast<Ty*>(content)};
		}

		template <typename Ty>
		[[nodiscard]] constexpr content_ref<const Ty> as() const noexcept requires (std::same_as<T, const void>){
			return content_ref<const Ty>{ name, static_cast<const Ty*>(content)};
		}
	};

}

