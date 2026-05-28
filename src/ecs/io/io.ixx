module;

#include <cassert>
#define EMPTY_BUFFER_INIT /*[[indeterminate]]*/

export module mo_yanxi.game.srl;

import std;

namespace mo_yanxi::game::srl{
	export using srl_size = std::uint64_t;


	export struct srl_logical_error final : std::runtime_error{
		using std::runtime_error::runtime_error;

		[[nodiscard]] srl_logical_error()
			: runtime_error("srl logical error"){
		}
	};

	export struct srl_hard_error final : std::runtime_error{
		using std::runtime_error::runtime_error;

		[[nodiscard]] srl_hard_error()
			: runtime_error("srl hard error"){
		}
	};

	export void read_to_buffer(std::istream& stream, const std::size_t size, std::string& str){
		str.resize_and_overwrite(size, [&stream](char* buf, const std::size_t buf_size){
			stream.read(buf, static_cast<std::streamsize>(buf_size));
			return buf_size;
		});

		if(!stream){
			throw srl_hard_error{};
		}
	}

	export [[nodiscard]] std::string read_to_buffer(std::istream& stream, std::size_t size){
		std::string str;
		read_to_buffer(stream, size, str);
		return str;
	}

	export
	template <typename T>
		requires (std::is_trivially_copyable_v<T>)
	void read_to(std::istream& stream, T& value){
		if(!stream.read(reinterpret_cast<char*>(std::addressof(value)), static_cast<std::streamsize>(sizeof(T)))){
			throw srl_hard_error{};
		}
	}


	constexpr inline auto endian_need_swap = std::endian::big;

	export constexpr void swapbyte_if_needed(std::integral auto& val) noexcept {
		if constexpr (std::endian::native == endian_need_swap){
			val = std::byteswap(val);
		}
	}


	export
	template <std::derived_from<std::basic_istream<char>> S>
	srl_size read_chunk_head(S& stream){
		srl_size sz EMPTY_BUFFER_INIT;

		srl::read_to(stream, sz);

		swapbyte_if_needed(sz);
		return sz;
	}


	export
	template <std::derived_from<std::basic_istream<char>> S>
	void skip(S& stream, srl_size size){
		if(!size)return;

		if(stream.good()){
			stream.seekg(static_cast<std::streamoff>(size), std::ios_base::cur);
		}else{
			throw srl_hard_error{};
		}
	}

	export
	template <std::derived_from<std::basic_ostream<char>> S>
	void write_chunk_head(S& stream, srl_size sz){
		swapbyte_if_needed(sz);

		stream.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
	}

	export
	template <std::derived_from<std::basic_istream<char>> S>
	struct stream_skipper{
	private:
		// using S = std::basic_istream<char>;
		S& stream;
		using pos_type = S::pos_type;
		srl_size chunk_size;
		std::streampos initial_pos;

	public:
		[[nodiscard]] stream_skipper(S& stream, srl_size chunk_size)
			:
			stream(stream),
			chunk_size(chunk_size),
			initial_pos([](const stream_skipper& self) static{
				if(self.stream.good()){
					return self.stream.tellg();
				}
				throw srl_hard_error{};
			}(*this)){
		}


		[[nodiscard]] explicit(false) stream_skipper(S& stream) : stream_skipper{stream, srl::read_chunk_head(stream)} {
		}

		[[nodiscard]] constexpr std::streampos get_target_pos() const noexcept{
			return initial_pos + static_cast<std::streampos>(chunk_size);
		}

		[[nodiscard]] constexpr srl_size get_chunk_size() const noexcept{
			return chunk_size;
		}

		~stream_skipper(){
			if(chunk_size != 0 && stream){
				auto cur = stream.tellg();
				if(auto tgt = get_target_pos(); cur != tgt){
					assert(tgt > cur);
					stream.seekg(tgt);
				}
			}
		}
	};

	export
	struct chunk_serialize_handle{
		struct promise_type;
		using handle = std::coroutine_handle<promise_type>;

		[[nodiscard]] chunk_serialize_handle() noexcept = default;

		[[nodiscard]] explicit chunk_serialize_handle(handle&& hdl)
			: hdl{std::move(hdl)}{}

		struct promise_type{
			srl_size chunk_offset{};

			[[nodiscard]] promise_type() = default;

			chunk_serialize_handle get_return_object() noexcept{
				return chunk_serialize_handle{handle::from_promise(*this)};
			}

			[[nodiscard]] static auto initial_suspend() noexcept{ return std::suspend_never{}; }

			[[nodiscard]] static auto final_suspend() noexcept{ return std::suspend_always{}; }

			std::suspend_always yield_value(srl_size off) noexcept{
				chunk_offset = off;
				return {};
			}

			void return_void() noexcept{
			}

			[[noreturn]] void unhandled_exception() {
				throw srl_logical_error{};
			}


		private:
			friend chunk_serialize_handle;

		};

		void resume() const{
			hdl.resume();
		}

		[[nodiscard]] bool done() const noexcept{
			return hdl.done();
		}

		chunk_serialize_handle(const chunk_serialize_handle& other) = delete;

		chunk_serialize_handle(chunk_serialize_handle&& other) noexcept
			: hdl{std::exchange(other.hdl, {})}{
		}

		chunk_serialize_handle& operator=(const chunk_serialize_handle& other) = delete;

		chunk_serialize_handle& operator=(chunk_serialize_handle&& other) noexcept{
			if(this == &other) return *this;
			dstry();
			hdl = std::exchange(other.hdl, {});
			return *this;
		}

		~chunk_serialize_handle(){
			dstry();
		}

		[[nodiscard]] srl_size get_offset() const{
			return hdl.promise().chunk_offset;
		}

	private:
		void dstry() noexcept{
			if(hdl){
				hdl.destroy();
			}
		}

		handle hdl{};
	};
}
