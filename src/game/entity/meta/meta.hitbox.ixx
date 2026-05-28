module;

export module mo_yanxi.game.meta.hitbox;

export import mo_yanxi.math.quad;
import std;

export namespace mo_yanxi::game::meta{

	struct hitbox{
		struct comp{
			math::rect_box_identity<float> box;
			math::trans2 trans;

			[[nodiscard]] math::frect_box crop() const noexcept{
				math::rect_box<float> box;
				box.update(trans, this->box);
				return box;
			}
		};

		std::vector<comp> components{};

		[[nodiscard]] constexpr math::rect_ortho<float> get_bound() const noexcept{
			if(components.empty())return {};
			float minX = std::numeric_limits<float>::max();
			float minY = std::numeric_limits<float>::max();
			float maxX = std::numeric_limits<float>::lowest();
			float maxY = std::numeric_limits<float>::lowest();

			auto binder = [&](math::vector2<float> vec){
				minX = math::min(minX, vec.x);
				minY = math::min(minY, vec.y);
				maxX = math::max(maxX, vec.x);
				maxY = math::max(maxY, vec.y);
			};

			for(auto& component : components){
				auto bound = component.crop().get_bound();

				binder(bound.vert_00());
				binder(bound.vert_11());
			}

			return {tags::from_vertex, {minX, minY}, {maxX, maxY}};
		}

		constexpr decltype(auto) operator[](this auto&& self, const std::size_t index) noexcept{
			return self.components[index];
		}

		template <typename S>
		constexpr auto begin(this S&& self) noexcept{
			return std::ranges::begin(std::forward_like<S>(self.components));
		}

		template <typename S>
		constexpr auto end(this S&& self) noexcept{
			return std::ranges::end(std::forward_like<S>(self.components));
		}

		[[nodiscard]] constexpr std::size_t size() const noexcept{
			return components.size();
		}
	};

	struct hitbox_transed : hitbox{
		math::trans2 trans{};

		constexpr void apply() noexcept{
			for (auto & component : components){
				component.trans = trans.apply_inv_to(component.trans);
			}
			trans = {};
		}


	};
}