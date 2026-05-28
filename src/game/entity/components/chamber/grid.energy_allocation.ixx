module;

#include <cassert>
import mo_yanxi.algo.timsort;

export module mo_yanxi.game.ecs.component.chamber:energy_allocation;

import :chamber;


namespace mo_yanxi::game::ecs::chamber{
	enum struct energy_allocate_strategy{

	};

	struct energy_acquisition_entry{
		building_data* owner;
		energy_acquisition last_acquisition;

		constexpr float priority() const noexcept{
			return last_acquisition.priority;
		}
	};

	struct update_check{
		bool any_changed;
		bool priority_changed;
		unsigned total_requirements;
		unsigned total_minimum_requirements;
	};

	struct energy_allocator{
	private:
		unsigned last_valid_{};
		std::vector<energy_acquisition_entry> acquisitions_{};

		update_check update_acquisition() noexcept{
			update_check result{};
			for (auto& acquisition : acquisitions_){
				auto acq = acquisition.owner->get_real_energy_acquisition();

				result.any_changed = result.any_changed || acq != acquisition.last_acquisition;
				if(result.any_changed){
					result.priority_changed = result.priority_changed || acq.priority != acquisition.last_acquisition.priority;
					acquisition.last_acquisition = acq;
				}
				assert(acq.minimum_count <= acq.maximum_count);
				result.total_minimum_requirements += acq.minimum_count;
				result.total_requirements += acq.maximum_count;
			}
			return result;
		}

	public:
		template <std::ranges::input_range Rng>
			requires std::convertible_to<std::ranges::range_value_t<Rng>, building_data*>
		void insert_acquisition(Rng&& rng){
			acquisitions_.append_range(std::forward<Rng>(rng) | std::views::transform([](building_data* data){
				return energy_acquisition_entry{data, {.priority = data->ideal_energy_acquisition.priority}};
			}));

			mo_yanxi::algo::timsort(acquisitions_, std::ranges::greater{}, &energy_acquisition_entry::priority);
		}

		void insert_acquisition(building_data& data){
			auto priority = data.ideal_energy_acquisition.priority;

			auto where = std::ranges::lower_bound(acquisitions_, priority, std::ranges::greater{}, &energy_acquisition_entry::priority);
			acquisitions_.insert(where, energy_acquisition_entry{std::addressof(data), {.priority = data.ideal_energy_acquisition.priority}});
		}

		[[nodiscard]] unsigned last_valid() const noexcept{
			return last_valid_;
		}

		void update_allocation(unsigned valid_energy) noexcept{
			auto check = update_acquisition();
			if(!check.any_changed && last_valid_ == valid_energy){
				//state not changed, return
				return;
			}

			last_valid_ = valid_energy;
			if(check.priority_changed){
				mo_yanxi::algo::timsort(acquisitions_, std::ranges::greater{}, &energy_acquisition_entry::priority);
			}

			if(check.total_requirements <= valid_energy){
				//fully valid
				for (const auto & acquisition : acquisitions_){
					acquisition.owner->valid_energy = acquisition.last_acquisition.maximum_count;
				}
				return;
			}


			if(check.total_minimum_requirements <= valid_energy){
				valid_energy -= check.total_minimum_requirements;

				for (const auto & acquisition : acquisitions_){
					acquisition.owner->valid_energy = acquisition.last_acquisition.minimum_count;
					if(valid_energy > 0){
						unsigned valid;
						if(acquisition.last_acquisition.get_append_count() >= valid_energy){
							valid = valid_energy;
							valid_energy = 0;
						}else{
							valid = acquisition.last_acquisition.get_append_count();
							valid_energy -= valid;
						}

						acquisition.owner->valid_energy += valid;
					}
				}

				return;
			}else{
				for (const auto & acquisition : acquisitions_){
					unsigned valid;
					if(acquisition.last_acquisition.minimum_count >= valid_energy){
						valid = valid_energy;
						valid_energy = 0;
					}else{
						valid = acquisition.last_acquisition.minimum_count;
						valid_energy -= valid;
					}

					acquisition.owner->valid_energy = valid;
				}
			}
		}
	};


}
