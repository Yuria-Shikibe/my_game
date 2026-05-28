export module mo_yanxi.game.physics.dynamic_bvh;

export import mo_yanxi.math.rect_ortho;

import std;

namespace mo_yanxi::game::physics{
	export
	struct bvh_proxy_id{
		std::uint32_t value{std::numeric_limits<std::uint32_t>::max()};

		[[nodiscard]] constexpr bool valid() const noexcept{
			return value != std::numeric_limits<std::uint32_t>::max();
		}

		friend constexpr bool operator==(bvh_proxy_id, bvh_proxy_id) noexcept = default;
		friend constexpr auto operator<=>(bvh_proxy_id, bvh_proxy_id) noexcept = default;
	};

	export
	template <typename T>
	class dynamic_bvh{
		static constexpr int null_node = -1;

		struct node{
			math::frect aabb{};
			math::frect fat_aabb{};
			int parent{null_node};
			int child_a{null_node};
			int child_b{null_node};
			int next{null_node};
			int height{-1};
			T value{};

			[[nodiscard]] constexpr bool leaf() const noexcept{
				return child_a == null_node;
			}

			[[nodiscard]] constexpr bool allocated() const noexcept{
				return height >= 0;
			}
		};

		std::vector<node> nodes_{};
		int root_{null_node};
		int free_list_{null_node};
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

		[[nodiscard]] int allocate_node(){
			if(free_list_ != null_node){
				const int index = free_list_;
				free_list_ = nodes_[index].next;
				nodes_[index] = node{};
				nodes_[index].height = 0;
				return index;
			}

			nodes_.push_back(node{});
			nodes_.back().height = 0;
			return static_cast<int>(nodes_.size() - 1);
		}

		void free_node(const int index) noexcept{
			nodes_[index] = node{};
			nodes_[index].next = free_list_;
			nodes_[index].height = -1;
			free_list_ = index;
		}

		[[nodiscard]] int select_best_sibling(const math::frect leaf_aabb) const noexcept{
			int index = root_;
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

		void refit_upwards(int index) noexcept{
			while(index != null_node){
				auto& current = nodes_[index];
				if(!current.leaf()){
					const auto& child_a = nodes_[current.child_a];
					const auto& child_b = nodes_[current.child_b];
					current.height = 1 + std::max(child_a.height, child_b.height);
					current.fat_aabb = combine(child_a.fat_aabb, child_b.fat_aabb);
					current.aabb = combine(child_a.aabb, child_b.aabb);
				}
				index = current.parent;
			}
		}

		void insert_leaf(const int leaf){
			if(root_ == null_node){
				root_ = leaf;
				nodes_[root_].parent = null_node;
				return;
			}

			const int sibling = select_best_sibling(nodes_[leaf].fat_aabb);
			const int old_parent = nodes_[sibling].parent;
			const int new_parent = allocate_node();

			nodes_[new_parent].parent = old_parent;
			nodes_[new_parent].fat_aabb = combine(nodes_[leaf].fat_aabb, nodes_[sibling].fat_aabb);
			nodes_[new_parent].aabb = combine(nodes_[leaf].aabb, nodes_[sibling].aabb);
			nodes_[new_parent].height = nodes_[sibling].height + 1;
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

			refit_upwards(nodes_[new_parent].parent);
		}

		void remove_leaf(const int leaf) noexcept{
			if(leaf == root_){
				root_ = null_node;
				return;
			}

			const int parent = nodes_[leaf].parent;
			const int grand_parent = nodes_[parent].parent;
			const int sibling = nodes_[parent].child_a == leaf ? nodes_[parent].child_b : nodes_[parent].child_a;

			if(grand_parent != null_node){
				if(nodes_[grand_parent].child_a == parent){
					nodes_[grand_parent].child_a = sibling;
				}else{
					nodes_[grand_parent].child_b = sibling;
				}
				nodes_[sibling].parent = grand_parent;
				free_node(parent);
				refit_upwards(grand_parent);
			}else{
				root_ = sibling;
				nodes_[sibling].parent = null_node;
				free_node(parent);
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

		[[nodiscard]] float fat_margin() const noexcept{
			return fat_margin_;
		}

		void clear() noexcept{
			nodes_.clear();
			root_ = null_node;
			free_list_ = null_node;
			leaf_count_ = 0;
		}

		[[nodiscard]] bvh_proxy_id create_proxy(const math::frect aabb, T value){
			const int index = allocate_node();
			nodes_[index].aabb = aabb;
			nodes_[index].fat_aabb = fatten(aabb);
			nodes_[index].value = std::move(value);
			nodes_[index].height = 0;
			insert_leaf(index);
			++leaf_count_;
			return {static_cast<std::uint32_t>(index)};
		}

		void destroy_proxy(const bvh_proxy_id proxy) noexcept{
			if(!proxy.valid()){
				return;
			}

			const int index = static_cast<int>(proxy.value);
			if(index < 0 || static_cast<std::size_t>(index) >= nodes_.size() || !nodes_[index].allocated()){
				return;
			}

			remove_leaf(index);
			free_node(index);
			--leaf_count_;
		}

		[[nodiscard]] bool move_proxy(const bvh_proxy_id proxy, const math::frect aabb){
			const int index = static_cast<int>(proxy.value);
			auto& proxy_node = nodes_.at(static_cast<std::size_t>(index));
			if(proxy_node.fat_aabb.contains_loose(aabb)){
				proxy_node.aabb = aabb;
				return false;
			}

			remove_leaf(index);
			proxy_node.aabb = aabb;
			proxy_node.fat_aabb = fatten(aabb);
			insert_leaf(index);
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

		template <typename Fn>
		void query(const math::frect region, Fn&& fn) const{
			if(root_ == null_node){
				return;
			}

			std::vector<int> stack{root_};
			while(!stack.empty()){
				const int index = stack.back();
				stack.pop_back();

				const auto& current = nodes_[index];
				if(!current.allocated() || !current.fat_aabb.overlap_exclusive(region)){
					continue;
				}

				if(current.leaf()){
					if constexpr(std::is_invocable_r_v<bool, Fn, bvh_proxy_id, const T&>){
						if(std::invoke(fn, bvh_proxy_id{static_cast<std::uint32_t>(index)}, current.value)){
							return;
						}
					}else{
						std::invoke(fn, bvh_proxy_id{static_cast<std::uint32_t>(index)}, current.value);
					}
				}else{
					stack.push_back(current.child_a);
					stack.push_back(current.child_b);
				}
			}
		}

		template <typename Fn>
		void collect_pairs(Fn&& fn) const{
			for(std::uint32_t index = 0; index != nodes_.size(); ++index){
				const auto& current = nodes_[index];
				if(!current.allocated() || !current.leaf()){
					continue;
				}

				query(current.fat_aabb, [&](const bvh_proxy_id other_id, const T& other){
					if(index >= other_id.value){
						return false;
					}
					if constexpr(std::is_invocable_r_v<bool, Fn, bvh_proxy_id, const T&, bvh_proxy_id, const T&>){
						return std::invoke(fn, bvh_proxy_id{index}, current.value, other_id, other);
					}else{
						std::invoke(fn, bvh_proxy_id{index}, current.value, other_id, other);
						return false;
					}
				});
			}
		}
	};
}
