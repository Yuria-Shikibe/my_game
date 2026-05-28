//
// Created by Matrix on 2025/6/2.
//

export module mo_yanxi.game.meta.chamber;

export import mo_yanxi.game.ecs.component.chamber;
export import mo_yanxi.game.ecs.component.chamber.general;

import mo_yanxi.math.rect_ortho;
import mo_yanxi.game.ecs.component.hit_point;
import mo_yanxi.game.ecs.component.drawer;
import mo_yanxi.ui.elem.table;

import mo_yanxi.graphic.renderer.predecl;

import mo_yanxi.type_map;
import mo_yanxi.owner;
import mo_yanxi.game.srl;
import std;


namespace mo_yanxi::game::meta::chamber{
	struct chamber_with_energy_status : basic_chamber, ecs::chamber::energy_status{
		[[nodiscard]] std::optional<energy_status> get_energy_consumption() const noexcept override{
			return deduced_get_energy_consumption();
		}
	};

	export
	struct armor : basic_chamber{

	};

	export
	struct energy_generator : chamber_with_energy_status{
		ecs::chamber::build_ptr create_instance_chamber(ecs::chamber::chamber_manifold& grid, math::point2 where) const override;
	};

	template <std::derived_from<basic_chamber> T>
	using chamber_meta_instance_data_t = decltype(std::declval<const T&>().create_instance_data())::element_type;

	//
	// export
	// struct superstructure


	export
	struct turret_base : chamber_with_energy_status{
		friend basic_chamber;

		struct turret_instance_data : chamber_instance_data{
			float rotation;
		};

		math::pos_trans2z transform{};
		float rotate_torque{};
		float shooting_field_angle{};

		ecs::chamber::build_ptr create_instance_chamber(ecs::chamber::chamber_manifold& grid, math::point2 where) const override;

		[[nodiscard]] std::optional<math::vec2> get_part_offset() const noexcept override{
			return transform.vec;
		}

	protected:
		[[nodiscard]] owner<turret_instance_data*> create_instance_data_impl() const override{
			return new turret_instance_data{};
		}
	};


	export
	struct radar : chamber_with_energy_status{
		friend basic_chamber;

		math::pos_trans2z transform{};

		math::range targeting_range_radius{};
		math::range targeting_range_angular{-math::pi, math::pi};
		float reload_duration{};

		[[nodiscard]] std::optional<math::vec2> get_part_offset() const noexcept override{
			return transform.vec;
		}

		void draw(math::frect region, graphic::renderer_ui_ref renderer_ui, const graphic::camera2& camera) const override;

		ecs::chamber::build_ptr create_instance_chamber(ecs::chamber::chamber_manifold& grid, math::point2 where) const override;

		void install(ecs::chamber::build_ref build_ref) const override;

		struct radar_instance_data : chamber_instance_data{
			float rotation;

			void install(ecs::chamber::build_ref build_ref) override;

			void draw(const basic_chamber& meta, math::frect region, graphic::renderer_ui_ref renderer_ui, float camera_scale) const override;

			ui_build_handle build_editor_ui(ui::table& table, ui_edit_context& context) override;

			srl::chunk_serialize_handle write(std::ostream& stream) const override;

			void read(std::ispanstream& bounded_stream) override;
		};

	protected:
		[[nodiscard]] owner<radar_instance_data*> create_instance_data_impl() const override{
			return new radar_instance_data{};
		}
	};


	export
	struct thurster : chamber_with_energy_status{
		ecs::chamber::maneuver_component maneuver{};

		[[nodiscard]] const ecs::chamber::maneuver_component* get_maneuver_comp() const noexcept override{
			return &maneuver;
		}

		ecs::chamber::build_ptr create_instance_chamber(ecs::chamber::chamber_manifold& grid, math::point2 where) const override;

		void install(ecs::chamber::build_ref build_ref) const override;
	};

	export
	struct structural{

	};

	export
	struct structural_joint : basic_chamber, structural{
		ecs::chamber::build_ptr create_instance_chamber(ecs::chamber::chamber_manifold& grid, math::point2 where) const override;

		[[nodiscard]] unsigned structural_adjacent_distance() const noexcept override{
			return 1;
		}

		[[nodiscard]] graphic::color get_edit_outline_color() const noexcept override{
			return graphic::colors::ORANGE;
		}
	};


	export using chamber_types = std::tuple<
		turret_base
	, radar
	, armor
	, energy_generator
	, thurster
	, structural_joint
	>;


	// export
	// struct chamber_interpreter{
	// 	[[nodiscard]] constexpr chamber_interpreter() = default;
	//
	// 	constexpr virtual ~chamber_interpreter() = default;
	//
	// 	[[nodiscard]] virtual const basic_chamber& get(const basic_chamber& info) const noexcept{
	// 		return info;
	// 	}
	//
	// 	[[nodiscard]] virtual std::optional<math::trans2> get_part_trans(const basic_chamber& info) const noexcept{
	// 		return std::nullopt;
	// 	}
	//
	// 	[[nodiscard]] virtual std::optional<math::range> get_radius_range(const basic_chamber& info) const noexcept{
	// 		return std::nullopt;
	// 	}
	//
	// 	[[nodiscard]] virtual std::optional<math::range> get_range_angular(const basic_chamber& info) const noexcept{
	// 		return std::nullopt;
	// 	}
	//
	// 	[[nodiscard]] virtual std::optional<ecs::chamber::energy_consumer_status> get_energy_consumption(const basic_chamber& info) const noexcept{
	// 		return std::nullopt;
	// 	}
	//
	// protected:
	// 	template <typename S>
	// 	std::optional<ecs::chamber::energy_consumer_status> duduced_get_energy_consumption(this const S& self, const basic_chamber& info){
	// 		auto& dinfo = self.get(info);
	// 		using Ty = std::remove_cvref_t<decltype(dinfo)>;
	// 		if constexpr (std::derived_from<Ty, ecs::chamber::energy_consumer_status>){
	// 			return static_cast<const ecs::chamber::energy_consumer_status&>(dinfo);
	// 		}else{
	// 			return std::nullopt;
	// 		}
	// 	}
	// };
	//
	// export
	// template <typename T>
	// struct chamber_interpreter_of : chamber_interpreter{};
	//
	//
	//
	//
	// export
	// template <>
	// struct chamber_interpreter_of<turret_base> final : chamber_interpreter{
	// 	[[nodiscard]] const turret_base& get(const basic_chamber& info) const noexcept override{
	// 		return static_cast<const turret_base&>(info);
	// 	}
	//
	// 	[[nodiscard]] std::optional<math::trans2> get_part_trans(const basic_chamber& info) const noexcept override{
	// 		return get(info).transform;
	// 	}
	//
	// 	[[nodiscard]] std::optional<math::range> get_range_angular(const basic_chamber& info) const noexcept override{
	// 		return math::range{-math::pi, math::pi};
	// 	}
	//
	// 	[[nodiscard]] std::optional<ecs::chamber::energy_consumer_status> get_energy_consumption(const basic_chamber& info) const noexcept override{
	// 		return duduced_get_energy_consumption(info);
	// 	}
	// };
	//
	//
	//
	// export
	// template <>
	// struct chamber_interpreter_of<radar> final : chamber_interpreter{
	// 	[[nodiscard]] const radar& get(const basic_chamber& info) const noexcept override{
	// 		return static_cast<const radar&>(info);
	// 	}
	//
	// 	[[nodiscard]] std::optional<math::trans2> get_part_trans(const basic_chamber& info) const noexcept override{
	// 		return get(info).transform;
	// 	}
	//
	// 	[[nodiscard]] std::optional<math::range> get_range_angular(const basic_chamber& info) const noexcept override{
	// 		return get(info).targeting_range_angular;
	// 	}
	//
	// 	std::optional<math::range> get_radius_range(const basic_chamber& info) const noexcept override{
	// 		return get(info).targeting_range_radius;
	// 	}
	//
	// 	[[nodiscard]] std::optional<ecs::chamber::energy_consumer_status> get_energy_consumption(const basic_chamber& info) const noexcept override{
	// 		return duduced_get_energy_consumption(info);
	// 	}
	// };
	//
	//
	//
	//
	//
	// using chamber_type_to_interpreter = const chamber_interpreter&(*)() noexcept;
	// static constexpr inline chamber_interpreter def;
	//
	// using chamber_interpreter_factory_type = type_map<chamber_type_to_interpreter, chamber_types>;
	//
	// template <typename T>
	// struct getter{
	// 	static constexpr chamber_interpreter_of<T> interpreter{};
	// 	static constexpr const chamber_interpreter& operator()() noexcept{
	// 		return interpreter;
	// 	}
	// };
	//
	// export constexpr inline chamber_interpreter_factory_type chamber_interpreter_factory = []{
	// 	chamber_interpreter_factory_type f{};
	// 	[&] <std::size_t ...Idx>(std::index_sequence<Idx...>){
	// 		([&]<std::size_t I>(){
	// 			using T = std::tuple_element_t<I, chamber_types>;
	// 			f.at<T>() = std::addressof(getter<T>::operator());
	// 		}.template operator()<Idx>(), ...);
	//
	// 	}(std::make_index_sequence<std::tuple_size_v<chamber_types>>{});
	// 	return f;
	// }();
}

