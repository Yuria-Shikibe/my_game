//
// Created by Matrix on 2025/4/9.
//

export module mo_yanxi.game.quad_tree_interface;

export import mo_yanxi.math.vector2;
export import mo_yanxi.math.rect_ortho;
import mo_yanxi.concepts;
import std;

namespace mo_yanxi::game{
	export
	template <typename Target_Ty, arithmetic N>
	struct quad_tree_adaptor_base{
		using target_type = Target_Ty;
		using const_reference = const Target_Ty&;

		using coord_type = N;
		using rect_type = math::rect_ortho<N>;
		using vector_type = math::vector2<N>;

		[[nodiscard]] static rect_type get_bound(const_reference self) noexcept = delete;

		[[nodiscard]] static bool intersect_with(const_reference self, const_reference other) = delete;

		[[nodiscard]] static bool contains(const_reference self, typename vector_type::const_pass_t point) = delete;

		[[nodiscard]] static bool equals(const_reference self, const_reference other) noexcept{
			if constexpr (std::is_pointer_v<target_type>){
				return self == other;
			}else{
				return std::addressof(self) == std::addressof(other);
			}
		}

	};

	export
	template <typename Target_Ty, arithmetic N = float>
	struct quad_tree_trait_adaptor : quad_tree_adaptor_base<Target_Ty, N>{

	};
}

