module;

#include "plf_hive.h"
#include "gch/small_vector.hpp"

export module mo_yanxi.game.quad_tree;

export import mo_yanxi.game.quad_tree_interface;

export import mo_yanxi.math.vector2;
export import mo_yanxi.math.rect_ortho;
import mo_yanxi.utility;
import mo_yanxi.concepts;
import mo_yanxi.handle_wrapper;
import mo_yanxi.math;

import std;


namespace mo_yanxi::game{
	// using gch::erase_if;

	// using math::vector2;
	// using math::rect_ortho;

	//TODO flattened quad tree with pre allocated sub trees
	constexpr std::size_t MaximumItemCount = 8;

	constexpr std::size_t top_lft_index = 0;
	constexpr std::size_t top_rit_index = 1;
	constexpr std::size_t bot_lft_index = 2;
	constexpr std::size_t bot_rit_index = 3;


	// using ItemTy = int;
	// using Adaptor = quad_tree_trait_adaptor<ItemTy>;
	// using T = float;

	template <typename ItemTy, arithmetic T>
	struct quad_tree_trait{
		using adaptor_type = quad_tree_trait_adaptor<ItemTy, T>;
		using vec_t = adaptor_type::vector_type;
		using rect_type = adaptor_type::rect_type;

		[[nodiscard]] static bool equals(adaptor_type::const_reference lhs, adaptor_type::const_reference rhs) noexcept{
			static_assert(requires{
				{ adaptor_type::equals(lhs, rhs) } -> std::convertible_to<bool>;
			});

			return adaptor_type::equals(lhs, rhs);
		}

		static constexpr bool has_custom_intersect = requires(typename adaptor_type::const_reference value){
			{ adaptor_type::intersect_with(value, value) } -> std::convertible_to<bool>;
		};

		static constexpr bool has_point_intersect = requires(typename adaptor_type::const_reference value, typename vec_t::const_pass_t p){
			{ adaptor_type::contains(value, p) } -> std::convertible_to<bool>;
		};

		static rect_type bound_of(adaptor_type::const_reference value) noexcept{
			static_assert(requires{
				{ adaptor_type::get_bound(value) } -> std::convertible_to<rect_type>;
			}, "QuadTree Requires ValueType impl at `Rect getBound()` member function");

			return adaptor_type::get_bound(value);
		}

		static bool intersects(
			adaptor_type::const_reference subject,
			adaptor_type::const_reference object
		) noexcept{
			if(quad_tree_trait::equals(subject, object))return false;

			bool intersected = quad_tree_trait::bound_of(subject).overlap_exclusive(quad_tree_trait::bound_of(object));

			if constexpr(has_custom_intersect){
				if(intersected) intersected = adaptor_type::intersect_with(subject, object);
			}

			return intersected;
		}

		static bool contains(adaptor_type::const_reference object, vec_t::const_pass_t point) noexcept requires (has_point_intersect){
			return quad_tree_trait::bound_of(object).contains_loose(point) && adaptor_type::contains(object, point);
		}
	};

	export
	struct intersect_always_true{
		template <typename T>
		constexpr static bool operator()(const T&, const T&) noexcept{
			return true;
		}
	};


	export
	template <typename ItemTy, arithmetic T = float>
	struct quad_tree;

	template <typename ItemTy, arithmetic T = float>
	struct quad_tree_node{
		using arth_type = T;
		using value_type = ItemTy;
		using trait = quad_tree_trait<value_type, arth_type>;
		using rect_type = trait::rect_type;
		using vec_t = trait::vec_t;

		friend quad_tree<value_type, arth_type>;


		struct sub_nodes;

		using pool_type = plf::hive<sub_nodes>;

		struct sub_nodes{
			quad_tree_node nodes[4];
			//TODO remove ub
			// quad_tree_node bot_lft;
			// quad_tree_node bot_rit;
			// quad_tree_node top_lft;
			// quad_tree_node top_rit;

			[[nodiscard]] explicit sub_nodes(pool_type* node_pool, const std::array<rect_type, 4>& rects)
				:
			nodes{
				quad_tree_node{node_pool, rects[bot_lft_index]},
				quad_tree_node{node_pool, rects[bot_rit_index]},
				quad_tree_node{node_pool, rects[top_lft_index]},
				quad_tree_node{node_pool, rects[top_rit_index]}}
			{
			}

			void reserved_clear() noexcept{
				nodes[0].reserved_clear();
				nodes[1].reserved_clear();
				nodes[2].reserved_clear();
				nodes[3].reserved_clear();
			}

			constexpr auto begin() noexcept{
				return nodes;
			}

			constexpr auto end() noexcept{
				return nodes + 4;
			}

			constexpr auto begin() const noexcept{
				return nodes;
			}

			constexpr auto end() const noexcept{
				return nodes + 4;
			}

			constexpr quad_tree_node& at(const unsigned i) noexcept{
				return nodes[i];
			}

			constexpr const quad_tree_node& at(const unsigned i) const noexcept{
				return nodes[i];
			}

			sub_nodes(const sub_nodes& other) = delete;
			sub_nodes(sub_nodes&& other) noexcept = delete;
			sub_nodes& operator=(const sub_nodes& other) = delete;
			sub_nodes& operator=(sub_nodes&& other) noexcept = delete;
		};

		struct sub_node_ptr{
			friend quad_tree_node;

		private:
			exclusive_handle_member<pool_type*> pool_{};
			exclusive_handle_member<sub_nodes*> children{};

		public:
			[[nodiscard]] sub_node_ptr() = default;

			[[nodiscard]] explicit(false) sub_node_ptr(pool_type* node_pool)
				: pool_(node_pool){
			}

			[[nodiscard]] explicit(false) sub_node_ptr(
				pool_type* node_pool,
				const std::array<rect_type, 4>& rects)
				: pool_(node_pool), children(std::to_address(node_pool->emplace(node_pool, rects))){
			}

			void reset(const std::array<rect_type, 4>& rects){
				assert(pool_);
				if(children){
					auto itr = pool_->get_iterator(children);
					assert(itr != pool_->end());
					pool_->erase(itr);
				}

				children = std::to_address(pool_->emplace(pool_, rects));
			}

			void reset(std::nullptr_t){
				assert(pool_);
				if(children){
					auto itr = pool_->get_iterator(children);
					assert(itr != pool_->end());
					pool_->erase(itr);
					children = nullptr;
				}
			}

		private:
			sub_node_ptr(const sub_node_ptr& other) = delete;
			sub_node_ptr(sub_node_ptr&& other) noexcept = delete;
			sub_node_ptr& operator=(const sub_node_ptr& other) = delete;
			sub_node_ptr& operator=(sub_node_ptr&& other) noexcept = delete;

		public:

			~sub_node_ptr() = default;

			[[nodiscard]] pool_type& node_pool() noexcept{
				return *pool_;
			}

			sub_nodes* operator->() const noexcept{
				return children;
			}

			explicit operator bool() const noexcept{
				return children;
			}

			constexpr auto begin() noexcept{
				return children->begin();
			}

			constexpr auto end() noexcept{
				return children->end();
			}

			constexpr auto begin() const noexcept{
				return std::as_const(*children).begin();
			}

			constexpr auto end() const noexcept{
				return std::as_const(*children).end();
			}
		};
	protected:
		rect_type boundary{};

		sub_node_ptr children{};
		gch::small_vector<ItemTy, MaximumItemCount> items{};
		unsigned branch_size = 0;
		bool leaf = true;

		void update_pool(pool_type& pool) noexcept{
			if(children.pool_){
				children.pool_ = std::addressof(pool);
			}

			if(children)for(auto& val : children){
				val.update_pool(pool);
			}
		}

		[[nodiscard]] quad_tree_node() = default;

		[[nodiscard]] explicit quad_tree_node(pool_type* pool, const rect_type& boundary)
			: boundary(boundary), children(pool){
		}
	public:
		[[nodiscard]] rect_type get_boundary() const noexcept{
			return boundary;
		}

		[[nodiscard]] unsigned size() const noexcept{
			return branch_size;
		}

		[[nodiscard]] bool empty() const noexcept{
			return branch_size == 0;
		}

		[[nodiscard]] bool is_leaf() const noexcept{
			return leaf;
		}

		/**
		 * @brief indicate if there are any item at its children
		 */
		[[nodiscard]] bool has_valid_children() const noexcept{
			return !leaf && branch_size != items.size();
		}

		[[nodiscard]] std::span<const value_type> get_items() const noexcept{
			return {items.data(), items.size()};
		}

		[[nodiscard]] bool overlaps(const rect_type object) const noexcept{
			return boundary.overlap_exclusive(object);
		}

		[[nodiscard]] bool contains(const value_type& object) const noexcept{
			return boundary.contains_loose(trait::bound_of(object));
		}

		[[nodiscard]] bool overlaps(const value_type& object) const noexcept{
			return boundary.overlap_exclusive(trait::bound_of(object));
		}

		[[nodiscard]] bool contains(const vec_t object) const noexcept{
			return boundary.contains_loose(object);
		}

	private:
		[[nodiscard]] bool withinBound(const value_type& object, const arth_type dst) const noexcept{
			return trait::bound_of(object).get_center().within(this->boundary.get_center(), dst);
		}

		[[nodiscard]] bool is_children_cached() const noexcept{
			return static_cast<bool>(children);
		}

		void internalInsert(value_type&& item){
			this->items.push_back(std::move(item));
			++this->branch_size;
		}

		void split(){
			if(!std::exchange(this->leaf, false)) return;

			if(!is_children_cached()){
				children.reset(this->boundary.split());
			}

			gch::erase_if(this->items, [this](value_type& item){
				if(quad_tree_node* sub = this->get_wrappable_child(trait::bound_of(item))){
					sub->internalInsert(std::move(item));
					return true;
				}

				return false;
			});
		}

		void unsplit() noexcept{
			if(std::exchange(this->leaf, true)) return;

			assert(is_children_cached());

			this->items.reserve(this->branch_size);
			for (auto& node : children){
				auto view = node.items | std::views::as_rvalue;
				this->items.append(std::ranges::begin(view), std::ranges::end(view));
				node.items.clear();
				node.reserved_clear();
			}
		}

		[[nodiscard]] quad_tree_node* get_wrappable_child(const rect_type target_bound) const noexcept{
			assert(!this->is_leaf());
			assert(is_children_cached());

			auto [midX, midY] = this->boundary.get_center();

			// Object can completely fit within the top quadrants
			const bool topQuadrant = target_bound.get_src_y() > midY;
			// Object can completely fit within the bottom quadrants
			const bool bottomQuadrant = target_bound.get_end_y() < midY;

			// Object can completely fit within the left quadrants
			if(target_bound.get_end_x() < midX){
				if(topQuadrant){
					return &children->at(top_lft_index);
				}

				if(bottomQuadrant){
					return &children->at(bot_lft_index);
				}
			} else if(target_bound.get_src_x() > midX){
				// Object can completely fit within the right quadrants
				if(topQuadrant){
					return &children->at(top_rit_index);
				}

				if(bottomQuadrant){
					return &children->at(bot_rit_index);
				}
			}

			return nullptr;
		}

	public:
		void reserved_clear() noexcept{
			if(std::exchange(this->branch_size, 0) == 0) return;

			if(!this->leaf){
				children->reserved_clear();
				this->leaf = true;
			}

			this->items.clear();
		}

		void clear() noexcept{
			children.reset(nullptr);

			this->leaf = true;
			this->branch_size = 0;

			this->items.clear();
		}

	private:
		[[nodiscard]] constexpr bool isBranchEmpty() const noexcept{
			return this->branch_size == 0;
		}

		void insertImpl(value_type&& box){
			//If this box is inbound, it must be added.
			++this->branch_size;

			// Otherwise, subdivide and then add the object to whichever node will accept it
			if(this->leaf){
				if(this->items.size() < MaximumItemCount){
					this->items.push_back(std::move(box));
					return;
				}

				split();
			}

			const rect_type rect = trait::bound_of(box);

			if(quad_tree_node* child = this->get_wrappable_child(rect)){
				child->insertImpl(std::move(box));
				return;
			}

			if(this->items.size() < MaximumItemCount * 2){
				this->items.push_back(std::move(box));
			}else{
				const auto [cx, cy] = rect.get_center();
				const auto [bx, by] = this->boundary.get_center();

				const bool left = cx < bx;
				const bool bottom = cy < by;

				if(bottom){
					if(left){
						children->at(bot_lft_index).insertImpl(std::move(box));
					}else{
						children->at(bot_rit_index).insertImpl(std::move(box));
					}
				}else{
					if(left){
						children->at(top_lft_index).insertImpl(std::move(box));
					}else{
						children->at(top_rit_index).insertImpl(std::move(box));
					}
				}

			}
		}

	public:
		bool insert(value_type&& box){
			// Ignore objects that do not belong in this quad tree
			if(!this->overlaps(box)){
				return false;
			}

			this->insertImpl(std::move(box));
			return true;
		}

		bool insert(const value_type& box){
			// Ignore objects that do not belong in this quad tree
			if(!this->overlaps(box)){
				return false;
			}

			this->insertImpl(value_type{box});
			return true;
		}


		bool erase(const value_type& box) noexcept{
			if(isBranchEmpty()) return false;

			bool result;
			if(this->is_leaf()){
				result = static_cast<bool>(gch::erase_if(this->items, [&box](const value_type& o){
					return trait::equals(box, o);
				}));
			} else{
				if(quad_tree_node* child = this->get_wrappable_child(trait::bound_of(box))){
					result = child->erase(box);
				} else{
					result = static_cast<bool>(gch::erase_if(this->items, [&box](const value_type& o){
						return trait::equals(box, o);
					}));
				}
			}

			if(result){
				--this->branch_size;
				if(this->branch_size < MaximumItemCount) unsplit();
			}

			return result;
		}

		// ------------------------------------------------------------------------------
		// external usage
		// ------------------------------------------------------------------------------

		template <std::regular_invocable<value_type&> Func>
		void each(Func func){
			for(auto& item : this->items){
				std::invoke(func, item);
			}

			if(this->has_valid_children()){
				for (auto& node : children){
					node.each(func);
				}
			}
		}

		template <std::regular_invocable<quad_tree_node&> Func>
		void each(Func func){
			std::invoke(func, *this);

			if(this->has_valid_children()){
				for (auto& node : children){
					node.each(mo_yanxi::pass_fn(func));
				}
			}
		}



		[[nodiscard]] value_type* intersect_any(const value_type& to_test) noexcept{
			if(isBranchEmpty() || !this->overlaps(to_test)){
				return nullptr;
			}

			for(auto&& item : this->items){
				if(trait::intersects(item, to_test)){
					return std::addressof(item);
				}
			}

			// If this node has children, check if the rectangle overlaps with any rectangle in the children
			if(this->has_valid_children()){
				for (auto& node : children){
					if(auto rst = node.intersect_any(to_test)){
						return rst;
					}
				}
			}

			// Otherwise, the rectangle does not overlap with any rectangle in the quad tree
			return nullptr;
		}

		[[nodiscard]] const value_type* intersect_any(const value_type& to_test) const noexcept{
			return const_cast<quad_tree_node*>(this)->intersect_any(to_test);
		}

		[[nodiscard]] value_type* intersect_any(const vec_t to_test) noexcept requires (trait::has_point_intersect){
			if(isBranchEmpty() || !this->contains(to_test)){
				return nullptr;
			}

			for(auto&& item : this->items){
				if(trait::contains(item, to_test)){
					return std::addressof(item);
				}
			}

			if(this->has_valid_children()){
				for (auto& node : children){
					if(auto rst = node.intersect_any(to_test)) return rst;
				}
			}

			return nullptr;
		}

		[[nodiscard]] const value_type* intersect_any(const vec_t to_test) const noexcept requires (trait::has_point_intersect){
			return const_cast<quad_tree_node*>(this)->intersect_any(to_test);
		}

		[[nodiscard]] value_type* intersect_any(const rect_type to_test) noexcept{
			if(isBranchEmpty() || !this->overlaps(to_test)){
				return nullptr;
			}

			for(auto&& item : this->items){
				if(trait::bound_of(item).overlap_exclusive(to_test)){
					return std::addressof(item);
				}
			}

			if(this->has_valid_children()){
				for (auto& node : children){
					if(auto rst = node.intersect_any(to_test)) return rst;
				}
			}

			return nullptr;
		}

		[[nodiscard]] const value_type* intersect_any(const rect_type to_test) const noexcept{
			return const_cast<quad_tree_node*>(this)->intersect_any(to_test);
		}

		template <
			typename S,
			typename Ty = const value_type&,
			std::invocable<copy_const_t<S, value_type>&, copy_const_t<S, value_type>&> Func,
			std::predicate<copy_const_t<S, value_type>&, copy_const_t<S, value_type>&> Filter = decltype(trait::intersects)>
			requires (std::same_as<std::remove_cvref_t<Ty>, value_type>)
		bool intersect_all(this S& self, Ty& to_test, Func func, Filter filter = trait::intersects){
			using Vty = copy_const_t<S, value_type>&;

			if(self.isBranchEmpty() || !self.overlaps(to_test)) return false;

			for(auto&& element : self.items){
				if(std::invoke(filter, to_test, element)){
					if constexpr (std::is_invocable_r_v<bool, Func, Vty, Vty>){
						if(std::invoke(func, to_test, element)){
							return true;
						}
					}else{
						std::invoke(func, to_test, element);
					}
				}
			}

			// If this node has children, check if the rectangle overlaps with any rectangle in the children
			if(self.has_valid_children()){
				for (auto& node : self.children){
					if(node.intersect_all(to_test, mo_yanxi::pass_fn(func), mo_yanxi::pass_fn(filter))){
						return true;
					}
				}
			}

			return false;
		}

		template <
			typename S,
			std::invocable<copy_const_t<S, value_type>&> Func,
			std::predicate<copy_const_t<S, value_type>&> Filter>
		bool intersect_all(const S& self, copy_const_t<S, value_type>& to_test, Func func, Filter filter) {
			if(self.isBranchEmpty() || !self.overlaps(to_test)) return false;

			for(auto&& element : self.items){
				if(std::invoke(filter, element)){
					if constexpr (std::is_invocable_r_v<bool, Func, copy_const_t<S, value_type>&>){
						if(std::invoke(func, element)){
							return true;
						}
					}else{
						std::invoke(func, element);
					}
				}
			}

			// If this node has children, check if the rectangle overlaps with any rectangle in the children
			if(self.has_valid_children()){
				for (auto& node : self.children){
					if(node.intersect_all(to_test, mo_yanxi::pass_fn(func), mo_yanxi::pass_fn(filter))){
						return true;
					}
				}
			}

			return false;
		}

		template <
			typename S,
			typename Region,
			std::predicate<Region&, const rect_type&> Pred,
			std::invocable<Region&, copy_const_t<S, value_type>&> Func
		>
			// requires !std::same_as<Region, rect_type>
		bool intersect_then(
			this S& self,
			Region& region,
			Pred boundCheck,
			Func func){
			if(self.isBranchEmpty() || !std::invoke(boundCheck, region, self.boundary)) return false;

			for(auto& cont : self.items){
				if(std::invoke(boundCheck, region, trait::bound_of(cont))){
					if constexpr (std::is_invocable_r_v<bool, Func, Region&, copy_const_t<S, value_type>&>){
						if(std::invoke(func, region, cont)){
							return true;
						}
					}else{
						std::invoke(func, region, cont);
					}
				}
			}

			// If this node has children, check if the rectangle overlaps with any rectangle in the children
			if(self.has_valid_children()){
				for (auto& node : self.children){
					if(node.intersect_then(region, mo_yanxi::pass_fn(boundCheck), mo_yanxi::pass_fn(func))){
						return true;
					}
				}
			}

			return false;
		}


	/*
	private:
		template <std::predicate<const ItemTy&, const ItemTy&> Filter>
		void get_all_intersected_impl(const ItemTy& object, Filter filter, std::vector<ItemTy*>& out) const{
			if(isBranchEmpty() || !this->overlaps(object)) return;

			for(const auto element : this->items){
				if(trait::is_intersected_between(object, *element)){
					if(std::invoke(filter, object, *element)){
						out.push_back(element);
					}
				}
			}

			// If this node has children, check if the rectangle overlaps with any rectangle in the children
			if(this->has_valid_children()){
				for (const quad_tree_node & node : children){
					node.get_all_intersected_impl(object, filter, out);
				}
			}
		}

	public:
		template <std::predicate<const ItemTy&, const ItemTy&> Filter>
		[[nodiscard]] std::vector<ItemTy*> get_all_intersected(const ItemTy& object, Filter filter) const{
			std::vector<ItemTy*> rst{};
			rst.reserve(this->branch_size / 4);

			this->get_all_intersected_impl<Filter>(object, mo_yanxi::pass_fn(filter), rst);
			return rst;
		}

		template <std::invocable<ItemTy&> Func>
		void intersect_then(const ItemTy& object, Func func) const{
			if(isBranchEmpty() || !this->overlaps(object)) return;

			for(auto* cont : this->items){
				if(trait::is_intersected_between(object, *cont)){
					std::invoke(mo_yanxi::pass_fn(func), *cont);
				}
			}

			// If this node has children, check if the rectangle overlaps with any rectangle in the children
			if(this->has_valid_children()){
				for (const quad_tree_node & node : children){
					node.intersect_then(object, mo_yanxi::pass_fn(func));
				}
			}

		}*/

		/*template <std::invocable<ItemTy&, rect_type> Func>
		void intersect_then(const rect_type rect, Func func) const{
			if(isBranchEmpty() || !this->overlaps(rect)) return;

			//If this node has children, check if the rectangle overlaps with any rectangle in the children
			if(this->has_valid_children()){
				for (const quad_tree_node & node : children){
					node.intersect_then(rect, mo_yanxi::pass_fn(func));
				}
			}

			for(auto* cont : this->items){
				if(trait::bound_of(*cont).overlap_Exclusive(rect)){
					std::invoke(mo_yanxi::pass_fn(func), *cont, rect);
				}
			}
		}

		template <typename Region, std::predicate<const rect_type&, const Region&> Pred, std::invocable<ItemTy&, const Region&> Func>
			requires !std::same_as<Region, rect_type>
		void intersect_then(const Region& region, Pred boundCheck, Func func) const{
			if(isBranchEmpty() || !std::invoke(boundCheck, this->boundary, region)) return;

			// If this node has children, check if the rectangle overlaps with any rectangle in the children
			if(this->has_valid_children()){
				for (const quad_tree_node & node : children){
					node.template intersect_then<Region>(region, mo_yanxi::pass_fn(boundCheck), mo_yanxi::pass_fn(func));
				}
			}

			for(auto cont : this->items){
				if(std::invoke(mo_yanxi::pass_fn(boundCheck), trait::bound_of(*cont), region)){
					std::invoke(mo_yanxi::pass_fn(func), *cont, region);
				}
			}
		}*/

		template <std::regular_invocable<vec_t, value_type&> Func>
		void intersect_then(const vec_t point, Func func){
			if(isBranchEmpty() || !this->contains(point)) return;

			for(auto& cont : this->items){
				if(trait::contains(cont, point)){
					std::invoke(func, point, cont);
				}
			}

			// If this node has children, check if the rectangle overlaps with any rectangle in the children
			if(this->has_valid_children()){
				for (auto& node : children){
					node.intersect_then(point, mo_yanxi::pass_fn(func));
				}
			}
		}

		template <std::invocable<value_type&, value_type&> Func>
		void within(value_type& to_test, const arth_type radius, Func func){
			if(isBranchEmpty() || !this->withinBound(to_test, radius)) return;

			// If this node has children, check if the rectangle overlaps with any rectangle in the children
			if(this->has_valid_children()){
				for (const quad_tree_node & node : children){
					node.within(to_test, radius, mo_yanxi::pass_fn(func));
				}
			}

			for( auto& cont : this->items){
				std::invoke(func, to_test, cont);
			}
		}
	};

	// using ItemTy = int;
	// using T = float;
	export
	template <typename ItemTy, arithmetic T = float>
	struct quad_tree{
		using node_type = quad_tree_node<ItemTy, T>;

	private:
		node_type::pool_type pool{};
		node_type::rect_type bound_{};

		node_type* root{};
	public:
		[[nodiscard]] quad_tree() = default;

		[[nodiscard]] explicit quad_tree(typename node_type::rect_type boundary)
			: bound_{boundary}, root(&pool.emplace(&pool, std::array{boundary, boundary, boundary, boundary})->at(0))
		{}

		node_type::rect_type bound() const noexcept{
			return bound_;
		}

		constexpr node_type* operator->() noexcept{
			return root;
		}

		constexpr const node_type* operator->() const noexcept{
			return root;
		}
	private:

		void reset_pools_on_move() noexcept{
			if(root)root->update_pool(pool);
		}
	public:

		quad_tree(const quad_tree& other) = delete;
		quad_tree& operator=(const quad_tree& other) = delete;

		quad_tree(quad_tree&& other) noexcept
			: pool{std::move(other.pool)},
				bound_(other.bound_),
			  root{other.root}{
			reset_pools_on_move();
		}

		quad_tree& operator=(quad_tree&& other) noexcept{
			if(this == &other) return *this;
			pool = std::move(other.pool);
			bound_ = other.bound_;
			root = other.root;

			reset_pools_on_move();
			return *this;
		}
	};

}
