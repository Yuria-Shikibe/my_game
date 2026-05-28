//
// Created by Matrix on 2025/7/1.
//

export module mo_yanxi.game.path_sequence;

import mo_yanxi.math.trans2;
import std;

namespace mo_yanxi::game{
	using arth_type = float;

	export
	enum struct march_state{
		underway,
		reached,
		finished,
		no_dest
	};

	export
	struct path_seq{
		using value_type = math::trans2;

		using cont_type = std::vector<value_type>;
		using size_type = cont_type::size_type;

		[[nodiscard]] constexpr std::optional<value_type> next() const noexcept{
			return current_index_ ? std::optional{path_[current_index_ - 1]} : std::optional<value_type>{};
		}

		[[nodiscard]] constexpr march_state check_arrive(const value_type current, const arth_type dst_margin, const arth_type ang_margin) const noexcept{
			if(auto n = next()){
				if(n->within(current, dst_margin, ang_margin)){
					return current_index_ == size() ? march_state::finished : march_state::reached;
				}

				return march_state::underway;
			}

			return march_state::no_dest;
		}


		[[nodiscard]] constexpr march_state check_arrive_and_update(const value_type current, const arth_type dst_margin, const arth_type ang_margin) noexcept{
			const auto rst = check_arrive(current, dst_margin, ang_margin);
			if(rst == march_state::reached){
				to_next();
			}
			return rst;
		}

		[[nodiscard]] constexpr bool empty() const noexcept{
			return path_.empty();
		}

		constexpr void append(const value_type pos){
			path_.push_back(pos);
		}

		constexpr void insert(const size_type idx, const value_type pos){
			if(idx < current_index_){
				++current_index_;
			}
			path_.insert(path_.begin() + idx, pos);
		}

		constexpr void insert_after_current(const value_type pos){
			path_.insert(path_.begin() + current_index_, pos);
		}

		constexpr void set_current_index(const size_type idx) noexcept{
			current_index_ = std::min(idx, path_.size());
		}

		[[nodiscard]] constexpr size_type size() const noexcept{
			return path_.size();
		}

		[[nodiscard]] constexpr size_type current_index() const noexcept{
			return current_index_;
		}

		[[nodiscard]] constexpr value_type* current() noexcept{
			return current_index_ ? path_.data() + (current_index_ - 1) : nullptr;
		}

		[[nodiscard]] constexpr const value_type* get_current() const noexcept{
			return current_index_ ? path_.data() + (current_index_ - 1) : nullptr;
		}

		constexpr bool erase_current() noexcept{
			if(current_index_ == 0)return false;
			path_.erase(path_.begin() + (current_index_ - 1));
			if(current_index_ > size()){
				current_index_--;
			}
			return true;
		}

		constexpr bool erase_current_and_to_prev() noexcept{
			if(current_index_ == 0)return false;
			path_.erase(path_.begin() + (--current_index_));
			return true;
		}

		constexpr bool to_next() noexcept{
			if(current_index_ == path_.size())return false;
			++current_index_;
			return true;
		}


		constexpr bool to_prev() noexcept{
			if(current_index_ == 0)return false;
			--current_index_;
			return true;
		}

		constexpr std::make_signed_t<size_type> advance(std::make_signed_t<size_type> offset) noexcept{
			auto last = static_cast<std::make_signed_t<size_type>>(current_index_);
			auto next = last + offset;
			auto rst = std::clamp<decltype(next)>(next, 0, size());
			current_index_ = static_cast<size_type>(rst);
			return rst - last;
		}



		template <std::ranges::input_range Rng>
			requires std::convertible_to<std::ranges::range_reference_t<Rng>, value_type>
		constexpr void insert_range(const size_type idx, Rng&& rng){
			if(idx < current_index_){
				current_index_ += std::ranges::size(rng);
			}
			path_.insert_range(path_.begin() + idx, std::forward<Rng>(rng));
		}

		[[nodiscard]] constexpr arth_type get_progress() const noexcept{
			return static_cast<float>(current_index_) / static_cast<float>(path_.size());
		}

		[[nodiscard]] constexpr value_type lerp(const arth_type progress) const noexcept{
			return math::lerp_span(path_, progress);
		}

		constexpr const value_type& operator[](const size_type idx) const noexcept{
			return path_[idx];
		}

		constexpr value_type& operator[](const size_type idx) noexcept{
			return path_[idx];
		}

		// constexpr value_type operator[](const arth_type idx) const noexcept{
		// 	return lerp(idx);
		// }

		[[nodiscard]] constexpr std::span<const value_type> to_span() const noexcept{
			return path_;
		}

		constexpr path_seq(const path_seq& other) = default;

		constexpr path_seq(path_seq&& other) noexcept
			: path_{std::move(other.path_)},
			  current_index_{std::exchange(other.current_index_, {})}{
		}

		constexpr path_seq& operator=(const path_seq& other) = default;

		constexpr path_seq& operator=(path_seq&& other) noexcept{
			if(this == &other) return *this;
			path_ = std::move(other.path_);
			current_index_ = std::exchange(other.current_index_, {});
			return *this;
		}

		[[nodiscard]] constexpr path_seq() noexcept = default;

	private:
		cont_type path_{};
		size_type current_index_{};
	};
}