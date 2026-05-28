
export module mo_yanxi.game.ecs.component.manage:serializer;

import std;

import mo_yanxi.game.srl;

#if DEBUG_CHECK
#define CHECKED_STATIC_CAST(type) dynamic_cast<type>
#else
#define CHECKED_STATIC_CAST(type) static_cast<type>
#endif


namespace mo_yanxi::game::ecs{
	export struct archetype_base;
	export struct component_manager;

	export using component_chunk_offset = srl::srl_size;

	constexpr inline auto endian_need_swap = std::endian::big;

	constexpr inline void swapbyte_if_needed(std::integral auto& val){
		val = std::byteswap(val);
	}

	export struct archetype_serialize_identity{
		static constexpr std::uint64_t unknown{};
		std::uint64_t index{unknown};
	};


	export struct archetype_serializer{
		virtual ~archetype_serializer() = default;

		[[nodiscard]] virtual archetype_serialize_identity get_identity() const noexcept{
			return {};
		}
		// virtual void dump(archetype_base& base) = 0;
		virtual void write(std::ostream& stream) const = 0;
		virtual void read(std::istream& stream) = 0;
		virtual void load(component_manager& target) const = 0;
		virtual void clear() noexcept = 0;
	};


	struct await_offset_tag_t{};
	constexpr inline await_offset_tag_t await_offset_tag{};


	export
	struct component_pack{
	private:
		std::vector<std::unique_ptr<archetype_serializer>> chunks;

	public:
		[[nodiscard]] component_pack() = default;

		[[nodiscard]] explicit(false) component_pack(const component_manager& manager);

		void clear_items() const noexcept{
			for (auto& archetype_serializer : chunks){
				archetype_serializer->clear();
			}
		}
		void clear() noexcept{
			chunks.clear();
		}

		void write(std::ostream& stream) const;

		void read(std::istream& stream);

		void load_to(component_manager& manager) const;
	};

}
