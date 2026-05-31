import std;
import mo_yanxi.soa_vector;

namespace{
	using byte_allocator = std::allocator<std::byte>;

	struct point{
		int x{};
		int y{};
	};

	struct tracked{
		int* alive{};
		int value{};

		constexpr tracked(int& alive_count, int value_) noexcept
			: alive(std::addressof(alive_count)),
			  value(value_){
			++*alive;
		}

		constexpr tracked(const tracked& other) noexcept
			: alive(other.alive),
			  value(other.value){
			if(alive){
				++*alive;
			}
		}

		constexpr tracked(tracked&& other) noexcept
			: alive(other.alive),
			  value(other.value){
			if(alive){
				++*alive;
			}
		}

		constexpr tracked& operator=(const tracked& other) noexcept{
			value = other.value;
			return *this;
		}

		constexpr tracked& operator=(tracked&& other) noexcept{
			value = other.value;
			return *this;
		}

		constexpr ~tracked(){
			if(alive){
				--*alive;
			}
		}
	};

	consteval bool basic_storage_test(){
		mo_yanxi::soa_vector<byte_allocator, int, point> vec{};

		if(!vec.empty() || vec.size() != 0 || vec.capacity() != 0){
			return false;
		}

		vec.reserve(2);
		auto [first_int, first_point] = vec.emplace_back(1, point{2, 3});
		first_int += 4;
		first_point.y += 1;

		vec.emplace_back(5, point{6, 7});

		if(vec.size() != 2 || vec.capacity() != 2){
			return false;
		}

		if(vec.get<int>(0) != 5 || vec.get<1>(0).y != 4){
			return false;
		}

		int relocated_sum{};
		vec.reserve(4, [&](auto& self, std::uint32_t idx){
			relocated_sum += self.template get<0>(idx);
		});

		if(vec.capacity() != 4 || relocated_sum != 10){
			return false;
		}

		vec.erase_unstable(0, [&](auto& self, std::uint32_t idx){
			self.template get<int>(idx) += 10;
		});

		if(vec.size() != 1 || vec.get<int>(0) != 15 || vec.get<point>(0).x != 6){
			return false;
		}

		vec.clear();
		if(!vec.empty() || vec.capacity() != 4){
			return false;
		}

		vec.resize(2);
		if(vec.size() != 2 || vec.get<int>(1) != 0 || vec.get<point>(1).y != 0){
			return false;
		}

		mo_yanxi::soa_vector<byte_allocator, int, point> other{};
		other.emplace_back(42, point{8, 9});
		swap(vec, other);

		return vec.size() == 1 && other.size() == 2 && vec.get<int>(0) == 42 && other.get<int>(0) == 0;
	}

	consteval bool lifetime_test(){
		int alive{};

		{
			mo_yanxi::soa_vector<byte_allocator, tracked> vec{};
			vec.emplace_back(tracked{alive, 1});
			vec.emplace_back(tracked{alive, 2});

			if(alive != 2){
				return false;
			}

			int relocated_sum{};
			vec.reserve(8, [&](auto& self, std::uint32_t idx){
				relocated_sum += self.template get<tracked>(idx).value;
			});

			if(alive != 2 || relocated_sum != 3){
				return false;
			}

			vec.pop_back();
			if(alive != 1){
				return false;
			}
		}

		return alive == 0;
	}

	static_assert(basic_storage_test());
	static_assert(lifetime_test());
}

int main(){
	return 0;
}
