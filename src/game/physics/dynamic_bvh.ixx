export module mo_yanxi.game.physics.dynamic_bvh;

export import mo_yanxi.math.rect_ortho;

import std;

namespace mo_yanxi::game::physics{
	export
	struct bvh_proxy_id{
		static constexpr unsigned invalid_value = std::numeric_limits<unsigned>::max();

		unsigned value{invalid_value};

		[[nodiscard]] constexpr bool valid() const noexcept{
			return value != invalid_value;
		}

		friend constexpr bool operator==(bvh_proxy_id, bvh_proxy_id) noexcept = default;
		friend constexpr auto operator<=>(bvh_proxy_id, bvh_proxy_id) noexcept = default;
	};

	export
	template <typename T>
	struct bvh_trait{};

	export
	template <typename T>
	concept bvh_trait_has_id = requires(const T& value){
		typename bvh_trait<std::remove_cvref_t<T>>::id_type;
		{ bvh_trait<std::remove_cvref_t<T>>::id(value) } -> std::convertible_to<typename bvh_trait<std::remove_cvref_t<T>>::id_type>;
	};

	export
	template <typename T>
	concept bvh_trait_has_aabb = requires(const T& value){
		{ bvh_trait<std::remove_cvref_t<T>>::aabb(value) } -> std::convertible_to<math::frect>;
	};

	export
	template <typename T>
	concept bvh_trait_has_fat_aabb = requires(const T& value, const float margin){
		{ bvh_trait<std::remove_cvref_t<T>>::fat_aabb(value, margin) } -> std::convertible_to<math::frect>;
	};

	export
	template <typename T>
	class dynamic_bvh;

	export
	struct bvh_query_workspace{
	private:
		static constexpr std::size_t inline_capacity = 128;

		std::array<unsigned, inline_capacity> stack{};
		std::vector<unsigned> overflow{};
		std::size_t size{};

		void clear() noexcept{
			size = 0;
			overflow.clear();
		}

		[[nodiscard]] bool empty() const noexcept{
			return size == 0;
		}

		void push(const unsigned index){
			if(size < inline_capacity){
				stack[size] = index;
			}else{
				overflow.push_back(index);
			}
			++size;
		}

		[[nodiscard]] unsigned pop() noexcept{
			--size;
			if(size < inline_capacity){
				return stack[size];
			}

			const unsigned index = overflow.back();
			overflow.pop_back();
			return index;
		}

		template <typename>
		friend class dynamic_bvh;
	};

	template <typename T>
	class dynamic_bvh{
		using node_index = unsigned;
		static constexpr node_index null_node = std::numeric_limits<node_index>::max();
		static constexpr unsigned unallocated_height = std::numeric_limits<unsigned>::max();

		struct node{
			math::frect aabb{};
			math::frect fat_aabb{};
			node_index parent{null_node};
			node_index child_a{null_node};
			node_index child_b{null_node};
			node_index next{null_node};
			unsigned height{unallocated_height};
			T value{};

			[[nodiscard]] constexpr bool leaf() const noexcept{
				return child_a == null_node;
			}

			[[nodiscard]] constexpr bool allocated() const noexcept{
				return height != unallocated_height;
			}
		};

		std::vector<node> nodes_{};
		node_index root_{null_node};
		node_index free_list_{null_node};
		std::size_t leaf_count_{};
		float fat_margin_{0.1f};

		[[nodiscard]] static constexpr float perimeter(const math::frect rect) noexcept{
			return 2.f * (rect.width() + rect.height());
		}

		[[nodiscard]] static constexpr math::frect combine(const math::frect a, const math::frect b) noexcept{
			auto result = a;
			result.expand_by(b);
			return result;
		}

		[[nodiscard]] math::frect fatten(math::frect aabb) const noexcept{
			aabb.expand(fat_margin_);
			return aabb;
		}

		[[nodiscard]] math::frect fatten(math::frect aabb, const math::vec2 displacement) const noexcept{
			if(!displacement.is_zero()){
				auto swept = aabb;
				swept.move(displacement);
				aabb.expand_by(swept);
			}
			aabb.expand(fat_margin_);
			return aabb;
		}

		[[nodiscard]] math::frect trait_aabb(const T& value) const
			requires bvh_trait_has_aabb<T>{
			return bvh_trait<T>::aabb(value);
		}

		[[nodiscard]] math::frect trait_fat_aabb(const T& value) const
			requires bvh_trait_has_aabb<T>{
			if constexpr(bvh_trait_has_fat_aabb<T>){
				return bvh_trait<T>::fat_aabb(value, fat_margin_);
			}else{
				return this->fatten(this->trait_aabb(value));
			}
		}

		[[nodiscard]] node_index allocate_node(){
			if(free_list_ != null_node){
				const node_index index = free_list_;
				free_list_ = nodes_[index].next;
				nodes_[index] = node{};
				nodes_[index].height = 0;
				return index;
			}

			if(nodes_.size() >= static_cast<std::size_t>(null_node)){
				throw std::length_error{"dynamic_bvh node index exhausted"};
			}

			nodes_.push_back(node{});
			nodes_.back().height = 0;
			return static_cast<node_index>(nodes_.size() - 1);
		}

		void free_node(const node_index index) noexcept{
			nodes_[index] = node{};
			nodes_[index].next = free_list_;
			nodes_[index].height = unallocated_height;
			free_list_ = index;
		}

		[[nodiscard]] node_index select_best_sibling(const math::frect leaf_aabb) const noexcept{
			node_index index = root_;
			while(!nodes_[index].leaf()){
				const auto& current = nodes_[index];
				const auto& child_a = nodes_[current.child_a];
				const auto& child_b = nodes_[current.child_b];

				const auto combined = combine(current.fat_aabb, leaf_aabb);
				const auto inheritance_cost = 2.f * (perimeter(combined) - perimeter(current.fat_aabb));

				const auto cost_a = [&]{
					const auto next = combine(child_a.fat_aabb, leaf_aabb);
					if(child_a.leaf()){
						return perimeter(next) + inheritance_cost;
					}
					return perimeter(next) - perimeter(child_a.fat_aabb) + inheritance_cost;
				}();

				const auto cost_b = [&]{
					const auto next = combine(child_b.fat_aabb, leaf_aabb);
					if(child_b.leaf()){
						return perimeter(next) + inheritance_cost;
					}
					return perimeter(next) - perimeter(child_b.fat_aabb) + inheritance_cost;
				}();

				index = cost_a < cost_b ? current.child_a : current.child_b;
			}

			return index;
		}

		void refit_node(const node_index index) noexcept{
			auto& current = nodes_[index];
			if(current.leaf()){
				return;
			}

			const auto& child_a = nodes_[current.child_a];
			const auto& child_b = nodes_[current.child_b];
			current.height = 1u + std::max(child_a.height, child_b.height);
			current.fat_aabb = combine(child_a.fat_aabb, child_b.fat_aabb);
			current.aabb = combine(child_a.aabb, child_b.aabb);
		}

		[[nodiscard]] node_index balance(const node_index index) noexcept{
			auto& current = nodes_[index];
			if(current.leaf() || current.height < 2u){
				return index;
			}

			const node_index child_a_index = current.child_a;
			const node_index child_b_index = current.child_b;
			auto& child_a = nodes_[child_a_index];
			auto& child_b = nodes_[child_b_index];
			const int balance_factor = static_cast<int>(child_b.height) - static_cast<int>(child_a.height);

			if(balance_factor > 1){
				const node_index grand_a_index = child_b.child_a;
				const node_index grand_b_index = child_b.child_b;
				auto& grand_a = nodes_[grand_a_index];
				auto& grand_b = nodes_[grand_b_index];

				child_b.child_a = index;
				child_b.parent = current.parent;
				current.parent = child_b_index;

				if(child_b.parent != null_node){
					if(nodes_[child_b.parent].child_a == index){
						nodes_[child_b.parent].child_a = child_b_index;
					}else{
						nodes_[child_b.parent].child_b = child_b_index;
					}
				}else{
					root_ = child_b_index;
				}

				if(grand_a.height > grand_b.height){
					child_b.child_b = grand_a_index;
					current.child_b = grand_b_index;
					grand_b.parent = index;
				}else{
					child_b.child_b = grand_b_index;
					current.child_b = grand_a_index;
					grand_a.parent = index;
				}

				this->refit_node(index);
				this->refit_node(child_b_index);
				return child_b_index;
			}

			if(balance_factor < -1){
				const node_index grand_a_index = child_a.child_a;
				const node_index grand_b_index = child_a.child_b;
				auto& grand_a = nodes_[grand_a_index];
				auto& grand_b = nodes_[grand_b_index];

				child_a.child_a = index;
				child_a.parent = current.parent;
				current.parent = child_a_index;

				if(child_a.parent != null_node){
					if(nodes_[child_a.parent].child_a == index){
						nodes_[child_a.parent].child_a = child_a_index;
					}else{
						nodes_[child_a.parent].child_b = child_a_index;
					}
				}else{
					root_ = child_a_index;
				}

				if(grand_a.height > grand_b.height){
					child_a.child_b = grand_a_index;
					current.child_a = grand_b_index;
					grand_b.parent = index;
				}else{
					child_a.child_b = grand_b_index;
					current.child_a = grand_a_index;
					grand_a.parent = index;
				}

				this->refit_node(index);
				this->refit_node(child_a_index);
				return child_a_index;
			}

			return index;
		}

		void refit_upwards(node_index index) noexcept{
			while(index != null_node){
				index = this->balance(index);
				this->refit_node(index);
				index = nodes_[index].parent;
			}
		}

		void insert_leaf(const node_index leaf){
			if(root_ == null_node){
				root_ = leaf;
				nodes_[root_].parent = null_node;
				return;
			}

			const node_index sibling = this->select_best_sibling(nodes_[leaf].fat_aabb);
			const node_index old_parent = nodes_[sibling].parent;
			const node_index new_parent = this->allocate_node();

			nodes_[new_parent].parent = old_parent;
			nodes_[new_parent].fat_aabb = combine(nodes_[leaf].fat_aabb, nodes_[sibling].fat_aabb);
			nodes_[new_parent].aabb = combine(nodes_[leaf].aabb, nodes_[sibling].aabb);
			nodes_[new_parent].height = nodes_[sibling].height + 1u;
			nodes_[new_parent].child_a = sibling;
			nodes_[new_parent].child_b = leaf;

			nodes_[sibling].parent = new_parent;
			nodes_[leaf].parent = new_parent;

			if(old_parent == null_node){
				root_ = new_parent;
			}else if(nodes_[old_parent].child_a == sibling){
				nodes_[old_parent].child_a = new_parent;
			}else{
				nodes_[old_parent].child_b = new_parent;
			}

			this->refit_upwards(new_parent);
		}

		void remove_leaf(const node_index leaf) noexcept{
			if(leaf == root_){
				root_ = null_node;
				return;
			}

			const node_index parent = nodes_[leaf].parent;
			const node_index grand_parent = nodes_[parent].parent;
			const node_index sibling = nodes_[parent].child_a == leaf ? nodes_[parent].child_b : nodes_[parent].child_a;

			if(grand_parent != null_node){
				if(nodes_[grand_parent].child_a == parent){
					nodes_[grand_parent].child_a = sibling;
				}else{
					nodes_[grand_parent].child_b = sibling;
				}
				nodes_[sibling].parent = grand_parent;
				this->free_node(parent);
				this->refit_upwards(grand_parent);
			}else{
				root_ = sibling;
				nodes_[sibling].parent = null_node;
				this->free_node(parent);
			}

			nodes_[leaf].parent = null_node;
		}

	public:
		[[nodiscard]] explicit dynamic_bvh(const float fat_margin = 0.1f) noexcept
			: fat_margin_(fat_margin){
		}

		[[nodiscard]] std::size_t size() const noexcept{
			return leaf_count_;
		}

		[[nodiscard]] bool empty() const noexcept{
			return leaf_count_ == 0;
		}

		[[nodiscard]] unsigned height() const noexcept{
			return root_ == null_node ? 0u : nodes_[root_].height;
		}

		[[nodiscard]] float fat_margin() const noexcept{
			return fat_margin_;
		}

		void clear() noexcept{
			nodes_.clear();
			root_ = null_node;
			free_list_ = null_node;
			leaf_count_ = 0;
		}

		[[nodiscard]] bvh_proxy_id create_proxy(const math::frect aabb, T value, const math::vec2 displacement = {}){
			const node_index index = this->allocate_node();
			nodes_[index].aabb = aabb;
			nodes_[index].fat_aabb = this->fatten(aabb, displacement);
			nodes_[index].value = std::move(value);
			nodes_[index].height = 0;
			this->insert_leaf(index);
			++leaf_count_;
			return {index};
		}

		[[nodiscard]] bvh_proxy_id create_proxy(T value)
			requires bvh_trait_has_aabb<T>{
			const auto aabb = this->trait_aabb(value);
			const auto fat_aabb = this->trait_fat_aabb(value);
			const node_index index = this->allocate_node();
			nodes_[index].aabb = aabb;
			nodes_[index].fat_aabb = fat_aabb;
			nodes_[index].value = std::move(value);
			nodes_[index].height = 0;
			this->insert_leaf(index);
			++leaf_count_;
			return {index};
		}

		void destroy_proxy(const bvh_proxy_id proxy) noexcept{
			if(!proxy.valid()){
				return;
			}

			const node_index index = proxy.value;
			if(static_cast<std::size_t>(index) >= nodes_.size() || !nodes_[index].allocated()){
				return;
			}

			this->remove_leaf(index);
			this->free_node(index);
			--leaf_count_;
		}

		[[nodiscard]] bool move_proxy(const bvh_proxy_id proxy, const math::frect aabb, const math::vec2 displacement = {}){
			const node_index index = proxy.value;
			auto& proxy_node = nodes_.at(static_cast<std::size_t>(index));
			const auto next_fat_aabb = this->fatten(aabb, displacement);
			const auto contained_aabb = displacement.is_zero() ? aabb : next_fat_aabb;
			if(proxy_node.fat_aabb.contains_loose(contained_aabb)){
				proxy_node.aabb = aabb;
				return false;
			}

			this->remove_leaf(index);
			proxy_node.aabb = aabb;
			proxy_node.fat_aabb = next_fat_aabb;
			this->insert_leaf(index);
			return true;
		}

		[[nodiscard]] bool move_proxy(const bvh_proxy_id proxy, T value)
			requires (bvh_trait_has_aabb<T> && !std::same_as<T, math::frect>){
			const node_index index = proxy.value;
			auto& proxy_node = nodes_.at(static_cast<std::size_t>(index));
			const auto next_aabb = this->trait_aabb(value);
			const auto next_fat_aabb = this->trait_fat_aabb(value);
			if(proxy_node.fat_aabb.contains_loose(next_fat_aabb)){
				proxy_node.aabb = next_aabb;
				proxy_node.value = std::move(value);
				return false;
			}

			this->remove_leaf(index);
			proxy_node.aabb = next_aabb;
			proxy_node.fat_aabb = next_fat_aabb;
			proxy_node.value = std::move(value);
			this->insert_leaf(index);
			return true;
		}

		[[nodiscard]] bool refresh_proxy(const bvh_proxy_id proxy)
			requires bvh_trait_has_aabb<T>{
			const node_index index = proxy.value;
			auto& proxy_node = nodes_.at(static_cast<std::size_t>(index));
			const auto next_aabb = this->trait_aabb(proxy_node.value);
			const auto next_fat_aabb = this->trait_fat_aabb(proxy_node.value);
			if(proxy_node.fat_aabb.contains_loose(next_fat_aabb)){
				proxy_node.aabb = next_aabb;
				return false;
			}

			this->remove_leaf(index);
			proxy_node.aabb = next_aabb;
			proxy_node.fat_aabb = next_fat_aabb;
			this->insert_leaf(index);
			return true;
		}

		[[nodiscard]] const T& value(const bvh_proxy_id proxy) const{
			return nodes_.at(proxy.value).value;
		}

		[[nodiscard]] T& value(const bvh_proxy_id proxy){
			return nodes_.at(proxy.value).value;
		}

		[[nodiscard]] math::frect aabb(const bvh_proxy_id proxy) const{
			return nodes_.at(proxy.value).aabb;
		}

		[[nodiscard]] math::frect fat_aabb(const bvh_proxy_id proxy) const{
			return nodes_.at(proxy.value).fat_aabb;
		}

	private:
		template <typename Fn>
		void query_impl(const math::frect region, Fn& fn, bvh_query_workspace& workspace) const{
			if(root_ == null_node){
				return;
			}

			workspace.clear();
			workspace.push(root_);
			while(!workspace.empty()){
				const node_index index = workspace.pop();

				const auto& current = nodes_[index];
				if(!current.allocated() || !current.fat_aabb.overlap_exclusive(region)){
					continue;
				}

				if(current.leaf()){
					if constexpr(std::is_invocable_r_v<bool, Fn, bvh_proxy_id, const T&>){
						if(std::invoke(fn, bvh_proxy_id{index}, current.value)){
							return;
						}
					}else{
						std::invoke(fn, bvh_proxy_id{index}, current.value);
					}
				}else{
					workspace.push(current.child_a);
					workspace.push(current.child_b);
				}
			}
		}

	public:
		template <typename Fn>
		void query(const math::frect region, Fn&& fn, bvh_query_workspace& workspace) const{
			this->query_impl(region, fn, workspace);
		}

		template <typename Fn>
		void query(const math::frect region, Fn&& fn) const{
			bvh_query_workspace workspace{};
			this->query(region, std::forward<Fn>(fn), workspace);
		}

		template <typename Fn>
		void collect_pairs(Fn&& fn) const{
			bvh_query_workspace workspace{};
			for(node_index index = 0; static_cast<std::size_t>(index) < nodes_.size(); ++index){
				const auto& current = nodes_[index];
				if(!current.allocated() || !current.leaf()){
					continue;
				}

				this->query(current.fat_aabb, [&](const bvh_proxy_id other_id, const T& other){
					if(index >= other_id.value){
						return false;
					}
					if constexpr(std::is_invocable_r_v<bool, Fn, bvh_proxy_id, const T&, bvh_proxy_id, const T&>){
						return std::invoke(fn, bvh_proxy_id{index}, current.value, other_id, other);
					}else{
						std::invoke(fn, bvh_proxy_id{index}, current.value, other_id, other);
						return false;
					}
				}, workspace);
			}
		}
	};
}
