module mo_yanxi.game.ecs.component.manage;

import mo_yanxi.game.ecs.component.io;

namespace mo_yanxi::game::ecs{
	component_pack::component_pack(const component_manager& manager){
		auto view = manager.get_archetypes();
		chunks.reserve(view.size());
		for (const auto & archetype_base : view){
			chunks.push_back(archetype_base->dump());
		}
	}

	void component_pack::write(std::ostream& stream) const{
		std::uint32_t chunk_count = chunks.size();
		swapbyte_if_needed(chunk_count);
		stream.write(reinterpret_cast<const char*>(&chunk_count), sizeof(chunk_count));

		for(const auto& chunk : chunks){
			auto idt = chunk->get_identity();
			swapbyte_if_needed(idt.index);
			stream.write(reinterpret_cast<const char*>(&idt), sizeof(idt));
			chunk->write(stream);
		}
	}

	void component_pack::read(std::istream& stream){
		std::uint32_t chunk_count;
		stream.read(reinterpret_cast<char*>(&chunk_count), sizeof(chunk_count));
		swapbyte_if_needed(chunk_count);

		chunks.resize(chunk_count);
		for(auto& chunk : chunks){
			archetype_serialize_identity idt;
			stream.read(reinterpret_cast<char*>(&idt), sizeof(idt));
			swapbyte_if_needed(idt.index);

			chunk = get_archetype_serializer(idt);
			chunk->read(stream);
		}
	}

	void component_pack::load_to(component_manager& manager) const{
		for (const auto & archetype_serializer : chunks){
			archetype_serializer->load(manager);
		}
	}
}
