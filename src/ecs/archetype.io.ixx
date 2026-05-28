export module mo_yanxi.game.ecs.component.io;

export import mo_yanxi.game.ecs.component.manage;

import mo_yanxi.game.srl;
import std;

namespace mo_yanxi::game::ecs{
	export
	std::unique_ptr<archetype_serializer> get_archetype_serializer(const archetype_serialize_identity& identity){
		throw srl::srl_logical_error{
			std::format("protobuf archetype serializer is not migrated: {}", identity.index)
		};
	}
}
