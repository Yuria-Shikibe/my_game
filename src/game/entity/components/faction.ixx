//
// Created by Matrix on 2025/5/11.
//

export module mo_yanxi.game.ecs.component.faction;

export import mo_yanxi.game.faction;

namespace mo_yanxi::game::ecs{
	export
	struct faction_data{
		faction_ref faction;


	private:
		struct faction_dump{
			faction_id id{};

			faction_dump& operator=(const faction_data& data){
				id = data.faction.get_id();
				return *this;
			}

			explicit(false) operator faction_data() const{
				faction_data data{};
				if(id == 0){
					data.faction = faction_0;
				}else{
					data.faction = faction_1;
				}
				return data;
			}
		};

	public:
		using dump_type = faction_dump;
	};
}
