module;

#include <cassert>

export module mo_yanxi.game.meta.content;

export import mo_yanxi.game.meta.content_ref;
import mo_yanxi.flat_seq_map;
import mo_yanxi.heterogeneous;
import mo_yanxi.heterogeneous.open_addr_hash;
import mo_yanxi.meta_programming;
import std;

namespace mo_yanxi::game::meta{
	export struct content_add_error : std::runtime_error{
		using std::runtime_error::runtime_error;
	};

	template <typename T>
	void try_set_name(std::string_view name, T& content) noexcept {
		if constexpr (requires(T& c){
			{ c.name = name } noexcept -> std::same_as<std::string_view&>;
		}){
			content.name = name;
		}
	}


	export
	struct content_manager;

	struct content_registry_base{
	private:
		bool frozen{};

		std::type_index dst_type_index;

		friend content_manager;

	protected:
		[[nodiscard]] explicit content_registry_base(const std::type_index& dst_type_index) :
			dst_type_index(dst_type_index){
		}

		virtual void update_name() noexcept = 0;


	public:
		content_registry_base(const content_registry_base& other) = delete;
		content_registry_base(content_registry_base&& other) noexcept = delete;
		content_registry_base& operator=(const content_registry_base& other) = delete;
		content_registry_base& operator=(content_registry_base&& other) noexcept = delete;

		virtual ~content_registry_base() = default;

		virtual content_ref<void> find(std::string_view name) = 0;

		[[nodiscard]] std::type_index content_type_index() const noexcept{
			return dst_type_index;
		}

		[[nodiscard]] bool is_frozen() const{
			return frozen;
		}

		void set_frozen(bool frozen) noexcept{
			this->frozen = frozen;
		}
	};

	export
	template <typename T>
	struct content_registry : content_registry_base{

		using content_type = T;
		using cont_type = flat_seq_map<std::string, content_type, mo_yanxi::transparent::string_comparator_of<std::less>>;

	private:
		friend content_manager;
		cont_type contents{};

		void update_name() noexcept override{
			contents.each([](const std::string& name, content_type& value){
				meta::try_set_name(name, value);
			});
		}

	public:
		[[nodiscard]] explicit content_registry()
			: content_registry_base(typeid(content_type)){
		}

		[[nodiscard]] auto get_values() noexcept{
			return contents.get_values();
		}

		[[nodiscard]] auto get_values() const noexcept{
			return contents.get_values();
		}

		content_type& add(std::string_view name, content_type&& content){
			if(is_frozen()){
				throw content_add_error{"add content after frozen may cause dangling"};
			}

			auto [itr, suc] = contents.insert_unique(name, std::move(content));
			if(!suc){
				throw content_add_error{std::format("duplicated content name: {}", name)};
			}
			return itr->value;
		}

		template <typename ...Args>
		content_type& emplace(std::string_view name, Args&& ...args){
			if(is_frozen()){
				throw content_add_error{"add content after frozen may cause dangling"};
			}

			auto [itr, suc] = contents.insert_unique(name, std::forward<Args>(args) ...);
			if(!suc){
				throw content_add_error{std::format("duplicated content name: {}", name)};
			}
			return itr->value;
		}

		content_ref<void> find(std::string_view name) override{
			if(auto rst = find_raw(name)){
				return {rst, name};
			}else{
				return {};
			}
		}

		content_type* find_raw(std::string_view name){
			if(!is_frozen()){
				throw content_add_error{"use content before frozen may cause dangling"};
			}

			return contents.find_unique(name);
		}

	};



	struct content_manager{
	private:
		using map = type_fixed_hash_map<std::unique_ptr<content_registry_base>>;
		map contents{};
		string_open_addr_hash_map<void*> name_map{};

	public:

		void freeze() const noexcept{
			for (const auto& ty : contents | std::views::values){
				ty->set_frozen(true);
				ty->update_name();
			}
		}

		template <typename CommonBase, std::derived_from<CommonBase> ContentTy, typename ...Args>
			requires requires{
				requires std::constructible_from<ContentTy, Args&&...>;
			}
		ContentTy& emplace(std::string_view name, Args&& ...args){
			if(name_map.contains(name)){
				throw content_add_error{std::format("duplicated content name: {}", name)};
			}

			ContentTy* ty;
			if(const auto it = contents.find(typeid(ContentTy)); it != contents.end()){
				ty = std::addressof(static_cast<content_registry<ContentTy>*>(it->second.get())->emplace(name, std::forward<Args>(args) ...));
			}

			content_registry<ContentTy>& chunk = register_type<std::remove_cvref_t<ContentTy>>();
			ty = std::addressof(chunk.emplace(name, std::forward<Args>(args) ...));
			name_map.try_emplace(name, static_cast<CommonBase*>(ty));
			return *ty;
		}


		template <typename ContentTy>
		std::remove_cvref_t<ContentTy>& add(std::string_view name, ContentTy&& content){
			return this->emplace<ContentTy>(name, std::forward<ContentTy>(content));
		}


		template <typename T>
		content_registry<T>& register_type(){
			auto [itr, suc] = contents.try_emplace(typeid(T), std::make_unique<content_registry<T>>());
			if(!suc){

				throw content_add_error{std::format("Duplicated content on type", typeid(T).name())};
			}else{
				return static_cast<content_registry<T>&>(*itr->second);
			}
		}

		template <typename T>
		content_registry<T>& at_type() const {
			return static_cast<content_registry<T>&>(*contents.at(typeid(T)));
		}

		[[nodiscard]] const void* find(std::string_view name) const noexcept{
			auto rst = name_map.try_find(name);
			return rst ? *rst : nullptr;
		}

		[[nodiscard]] void* find(std::string_view name) noexcept{
			auto rst = name_map.try_find(name);
			return rst ? *rst : nullptr;
		}

		template <typename T>
		T* find(std::string_view name) noexcept{
			if(auto itr = contents.find(typeid(T)); itr != contents.end()){
				auto* rg = itr->second.get();
				return static_cast<content_registry<std::remove_cvref_t<T>>>(rg).find_raw(name);
			}

			return nullptr;
		}


		template <typename TargetGroup, typename Fn>
			requires (is_tuple_v<TargetGroup>)
		void each_content(Fn fn) const{
			using groups = TargetGroup;

			[&] <std::size_t... I>(std::index_sequence<I...>){
				([&]<std::size_t Idx>(){
					using Ty = std::tuple_element_t<Idx, groups>;
					if(const auto itr = contents.find(typeid(Ty)); itr != contents.end()){
						meta::content_registry<Ty>& g = static_cast<meta::content_registry<Ty>&>(*itr->second);
						for(Ty& v : g.get_values()){
							std::invoke(fn, v);
						}
					}

				}.template operator()<I>(), ...);
			}(std::make_index_sequence<std::tuple_size_v<groups>>{});
		}
	};
}