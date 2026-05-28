module;

#include <gch/small_vector.hpp>

export module mo_yanxi.game.ecs.task_graph;

export import mo_yanxi.game.ecs.component.manage;

import mo_yanxi.open_addr_hash_map;
import std;

namespace mo_yanxi::concurrent{
	// using task_id = std::size_t;
	//
	// export struct no_fail_task_graph;
	// export struct task_group;
	//
	// template <typename T>
	// using small_vector_of = gch::small_vector<T>;
	//
	//
	//
	// //TODO auto wait
	// export
	// struct task_context;
	//
	// export
	// struct task{
	// 	using function_wrap_type = std::move_only_function<void(task_context&) const>;
	//
	// private:
	// 	bool autoCtrl_{};
	// 	task_id id_{};
	// 	friend no_fail_task_graph;
	// 	friend task_group;
	// 	function_wrap_type runnable{};
	//
	// 	std::unordered_set<task*> parents{};
	// 	std::unordered_set<task*> children{};
	// public:
	// 	[[nodiscard]] task() = default;
	//
	// 	[[nodiscard]] explicit(false) task(function_wrap_type&& runnable)
	// 		: runnable(std::move(runnable)){
	// 	}
	//
	// 	[[nodiscard]] task_id id() const noexcept{
	// 		return id_;
	// 	}
	//
	// 	[[nodiscard]] bool is_auto() const noexcept{
	// 		return autoCtrl_;
	// 	}
	//
	//
	// private:
	// 	void add_depend_by(task* task){
	// 		children.insert(task);
	// 	}
	//
	// public:
	// 	template <std::ranges::forward_range Rng>
	// 	void add_dependencies(Rng&& rng){
	// 		if constexpr (std::ranges::sized_range<Rng>){
	// 			parents.reserve(parents.size() + std::ranges::size(rng));
	// 		}
	//
	// 		for(task* task : rng){
	// 			task->add_depend_by(this);
	// 		}
	//
	// 		parents.insert_range(std::forward<Rng>(rng));
	// 	}
	//
	// 	void add_dependency(task& task){
	// 		task.add_depend_by(this);
	// 		parents.insert(&task);
	// 	}
	//
	// 	void erase_dependency(task& task){
	// 		task.children.erase(this);
	// 		parents.erase(&task);
	// 	}
	// };
	//
	// struct task_context{
	// 	using done_latch_container_type = small_vector_of<std::latch*>;
	//
	// 	friend task_group;
	// private:
	// 	const task* task_{};
	// 	bool marked_done{};
	// 	std::latch* wait_latch{};
	// 	done_latch_container_type done_latch{};
	// 	std::stop_token stop_token{};
	// 	std::stop_token thread_stop_token{};
	//
	// public:
	// 	[[nodiscard]] task_context() = default;
	//
	// 	[[nodiscard]] task_context(
	// 		const task* task,
	// 		std::latch* wait_latch,
	// 		done_latch_container_type&& done_latch,
	// 		const std::stop_source& stop_source,
	// 		std::stop_token thread_stop_token
	// 	) noexcept :
	// 		task_(task),
	// 		wait_latch(wait_latch),
	// 		done_latch(std::move(done_latch)),
	// 		stop_token(stop_source.get_token()), thread_stop_token(std::move(thread_stop_token)){
	// 	}
	//
	// 	[[nodiscard]] bool stop_requested() const noexcept{
	// 		return thread_stop_token.stop_requested() || stop_token.stop_requested();
	// 	}
	//
	// 	[[nodiscard]] bool stop_possible() const noexcept{
	// 		return thread_stop_token.stop_requested() || stop_token.stop_requested();
	// 	}
	//
	// 	[[nodiscard]] const task& task() const noexcept{
	// 		return *task_;
	// 	}
	//
	// 	void acquire() const noexcept{
	// 		if(wait_latch){
	// 			wait_latch->wait();
	// 		}
	// 	}
	//
	// 	void release() noexcept{
	// 		if(!marked_done){
	// 			marked_done = true;
	// 			for (auto latch : done_latch){
	// 				latch->count_down();
	// 			}
	// 		}
	// 	}
	//
	// 	~task_context(){
	// 		release();
	// 	}
	// };
	//
	//
	// struct task_group{
	// 	friend no_fail_task_graph;
	//
	// 	struct task_sequence{
	// 		/**
	// 		 * @brief a sequence of task
	// 		 */
	// 		std::vector<const task*> seq{};
	//
	// 		/**
	// 		 * @brief branch node -> [child, ...]
	// 		 */
	// 		pointer_hash_map<const task*, small_vector_of<const task*>> branches{};
	// 	};
	//
	//
	// private:
	// 	std::vector<task_sequence> sequences{};
	//
	// public:
	// 	void merge(task_group&& other){
	// 		sequences.append_range(other.sequences | std::views::as_rvalue);
	// 	}
	//
	// 	struct exec_instance{
	// 		struct task_pack{
	// 			using task_seq = small_vector_of<std::span<const task* const>>;
	// 			exec_instance* context{};
	// 			task_seq sequence{};
	//
	// 			[[nodiscard]] bool stopped() const noexcept{
	// 				return context->stop_source_.stop_requested();
	// 			}
	//
	// 			[[nodiscard]] std::generator<const task_pack&> split(std::size_t chunk_size) const noexcept{
	// 				for (const auto& tasks : sequence | std::views::chunk(chunk_size)){
	// 					task_pack pack{context, {tasks.begin(), tasks.end()}};
	// 					co_yield pack;
	// 				}
	// 			}
	//
	// 			void operator()() const{
	// 				this->operator()({});
	// 			}
	//
	// 			void operator()(std::stop_token stop_token) const{
	// 				for (const auto & tsk : sequence | std::views::join){
	// 					generic_run(tsk, stop_token);
	// 					const auto counter = context->task_done_counter_.fetch_sub(1, std::memory_order_relaxed);
	//
	// 					if(counter == 1){
	// 						context->done_flag_.test_and_set(std::memory_order_release);
	// 						context->done_flag_.notify_all();
	// 					}
	// 				}
	// 			}
	//
	// 		private:
	// 			void generic_run(
	// 				const task* tsk,
	// 				const std::stop_token& stop_token) const{
	// 				task_context::done_latch_container_type currentLatches{};
	//
	// 				if(stopped() || stop_token.stop_requested())return;
	// 				std::latch* to_wait{context->try_find_latch_at(tsk)};
	//
	// 				for (const auto & child : tsk->children){
	// 					if(auto latch = context->try_find_latch_at(child)){
	// 						currentLatches.push_back(latch);
	// 					}
	// 				}
	//
	// 				task_context ctx{
	// 					tsk,
	// 					to_wait,
	// 					std::move(currentLatches),
	// 					context->stop_source_,
	// 					stop_token
	// 				};
	//
	// 				if(tsk->is_auto()){
	// 					ctx.acquire();
	// 				}
	//
	// 				if(stopped() || stop_token.stop_requested())return;
	// 				if(tsk->runnable)tsk->runnable(ctx);
	// 			}
	//
	// 		};
	//
	// 	private:
	// 		std::size_t total_count_{};
	// 		std::atomic_size_t task_done_counter_{};
	// 		std::atomic_flag done_flag_{};
	//
	// 		std::unordered_map<const task*, std::latch> barriers_{};
	// 		std::vector<task_pack> packaged_tasks_{};
	// 		std::stop_source stop_source_{};
	//
	// 		std::latch* try_find_latch_at(const task* task) noexcept{
	// 			if(stop_source_.stop_requested())return nullptr;
	//
	// 			if(auto itr = barriers_.find(task); itr != barriers_.end()){
	// 				return &itr->second;
	// 			}
	//
	// 			return nullptr;
	// 		}
	// 	public:
	//
	// 		[[nodiscard]] exec_instance() = default;
	//
	// 		[[nodiscard]] explicit(false) exec_instance(const task_group& task_group, std::size_t threads_count){
	//
	// 			{
	// 				//set pivot latches
	// 				pointer_hash_map<const task*, std::size_t> pivots{};
	//
	// 				for (auto& seq : task_group.sequences){
	// 					total_count_ += seq.seq.size();
	//
	// 					for (auto& children : seq.branches | std::views::values){
	// 						for (auto && child : children){
	// 							++pivots[child];
	// 						}
	// 					}
	// 				}
	//
	// 				for (const auto& [node, count] : pivots){
	// 					//sequenced task also count down it, so add 1
	// 					barriers_.try_emplace(node, static_cast<std::ptrdiff_t>(count + 1));
	// 				}
	// 			}
	//
	// 			if(threads_count == 0){
	// 				threads_count = std::thread::hardware_concurrency();
	// 			}
	//
	//
	// 			auto rng = task_group.sequences | std::views::transform(&task_sequence::seq);
	// 			const auto sz = task_group.sequences.size();
	//
	// 			packaged_tasks_.reserve(std::min(threads_count, sz));
	// 			const auto avg_count = sz / threads_count;
	// 			const auto remaining_count = sz % threads_count;
	//
	// 			auto begin = rng.begin();
	// 			const auto end = rng.end();
	// 			while(begin != end){
	// 				auto next = begin + (avg_count + (packaged_tasks_.size() < remaining_count));
	//
	// 				packaged_tasks_.push_back({this, {begin, next}});
	// 				begin = next;
	// 			}
	// 		}
	//
	// 		[[nodiscard]] bool empty() const noexcept{
	// 			return total_count_ == 0;
	// 		}
	//
	// 		explicit operator bool() const noexcept{
	// 			return done();
	// 		}
	//
	// 		bool has_launched() const noexcept{
	// 			if(empty())return false;
	// 			return task_done_counter_.load(std::memory_order::relaxed) != 0 || done();
	// 		}
	//
	// 		void wait() const noexcept{
	// 			if(!has_launched())return;
	// 			done_flag_.wait(false, std::memory_order_acquire);
	// 		}
	//
	// 		[[nodiscard]] bool done() const noexcept{
	// 			return done_flag_.test(std::memory_order_acquire);
	// 		}
	//
	// 		[[nodiscard]] float get_progress() const noexcept{
	// 			if(done())return 1;
	// 			if(empty())return 0;
	// 			return static_cast<float>(total_count_ - task_done_counter_.load(std::memory_order_relaxed)) / static_cast<float>(total_count_);
	// 		}
	//
	// 		[[nodiscard]] std::span<const task_pack> launch(){
	// 			if(empty())return {};
	//
	// 			if(done() || task_done_counter_.exchange(total_count_, std::memory_order::relaxed) != 0){
	// 				throw std::runtime_error{"Task Instance Already Launched!"};
	// 			}
	//
	// 			return std::span{packaged_tasks_};
	// 		}
	//
	// 		bool request_stop() noexcept{
	// 			return stop_source_.request_stop();
	// 		}
	//
	// 		std::stop_token get_stop_token() const noexcept{
	// 			return stop_source_.get_token();
	// 		}
	//
	//
	// 		~exec_instance(){
	// 			if(has_launched() && !done()){
	// 				request_stop();
	// 				wait();
	// 			}
	// 		}
	// 	};
	//
	// 	[[nodiscard]] exec_instance create_instance(std::size_t threads_count = 0) const {
	// 		return {*this, threads_count};
	// 	}
	// };
	//
	//
	// /**
	//  * @brief All task are guaranteed to be done.
	//  */
	// struct no_fail_task_graph{
	// private:
	// 	std::unordered_map<task_id, task> tasks{};
	// public:
	//
	// 	task* try_find(task_id tid) noexcept{
	// 		if(auto it = tasks.find(tid); it != tasks.end()){
	// 			return &it->second;
	// 		}
	// 		return nullptr;
	// 	}
	//
	// 	task& at(task_id tid) noexcept{
	// 		return tasks.at(tid);
	// 	}
	//
	// 	template <std::ranges::forward_range Rng = std::initializer_list<task_id>>
	// 	void set_dependency(task_id id, Rng&& rng){
	// 		tasks[id].add_dependencies(std::forward<Rng>(rng));
	// 	}
	//
	// 	void clear(){
	// 		tasks.clear();
	// 	}
	//
	// 	template <std::ranges::forward_range Rng = std::initializer_list<task_id>, std::invocable<> Task>
	// 		requires (std::convertible_to<std::ranges::range_value_t<Rng>, task_id>)
	// 	bool add_task(task_id id, Rng&& dependencies, Task&& fn){
	// 		return this->add_task(id, std::forward<Rng>(dependencies), [f = std::forward<Task>(fn)](auto&){
	// 			std::invoke(f);
	// 		}, true);
	// 	}
	//
	// 	template <std::invocable<task_context&> Fn, std::ranges::forward_range Rng = std::initializer_list<task_id>>
	// 		requires (std::convertible_to<std::ranges::range_value_t<Rng>, task_id>)
	// 	bool add_task(task_id id, Rng&& dependencies, Fn&& fn, const bool autoCtrl = false){
	// 		auto [itr, suc] = tasks.try_emplace(id, task::function_wrap_type{std::forward<Fn>(fn)});
	// 		itr->second.id_ = id;
	// 		itr->second.autoCtrl_ = autoCtrl;
	//
	// 		if(!suc){
	// 			if(itr->second.runnable)return false;
	// 			itr->second.runnable = std::move(fn);
	// 		}
	//
	// 		itr->second.add_dependencies(dependencies | std::views::transform([&](task_id did) -> task* {
	// 			return &tasks[did];
	// 		}) | std::views::filter(std::identity{}));
	//
	// 		return true;
	// 	}
	//
	// 	[[nodiscard]] std::vector<task_group> to_sorted_individual_groups() const {
	// 		return gen_impl() | std::views::values | std::ranges::to<std::vector>();
	// 	}
	//
	// 	[[nodiscard]] task_group to_sorted_group() const {
	// 		task_group groups{};
	//
	// 		pointer_hash_map<const task*, std::size_t> in_degree{};
	// 		std::vector<const task*> global_zero_in_degree_queue{};
	//
	// 		for (auto&& [id, tsk] : tasks) {
	// 			const auto current = &tsk;
	// 			const auto in_deg = current->parents.size();
	//
	// 			if (in_deg == 0) {
	// 				global_zero_in_degree_queue.push_back(current);
	// 			}else{
	// 				in_degree.try_emplace(current, in_deg);
	// 			}
	// 		}
	//
	// 		std::deque<const task*> local_zero_degree;
	// 		for (auto head : global_zero_in_degree_queue){
	// 			task_group::task_sequence seq{};
	// 			local_zero_degree.clear();
	// 			local_zero_degree.push_back(head);
	//
	// 			while(!local_zero_degree.empty()){
	// 				auto cur = local_zero_degree.front();
	// 				local_zero_degree.pop_front();
	//
	// 				for (auto child : cur->children){
	// 					if (--in_degree[child] == 0) {
	// 						local_zero_degree.push_back(child);
	// 					}else{
	// 						seq.branches[cur].push_back(child);
	// 					}
	// 				}
	//
	// 				seq.seq.push_back(cur);
	// 			}
	//
	// 			groups.sequences.push_back(std::move(seq));
	// 		}
	//
	// 		for (const auto& [node, degree] : in_degree) {
	// 			if (degree != 0) {
	// 				throw std::runtime_error("Cycle detected in task graph");
	// 			}
	// 		}
	//
	// 		return groups;
	// 	}
	//
	// private:
	// 	pointer_hash_map<const task*, task_group> gen_impl() const{
	// 		pointer_hash_map<const task*, const task*> union_set_to_destination{};
	// 		pointer_hash_map<const task*, task_group> groups{};
	//
	// 		pointer_hash_map<const task*, std::size_t> in_degree{};
	// 		std::vector<const task*> global_zero_in_degree_queue{};
	//
	// 		auto union_set_find = [&](this auto&& self, const task* t) -> const  task*{
	// 			auto& r = union_set_to_destination[t];
	// 			return r == t ? t : r = self(r);
	// 		};
	//
	// 		auto join_to = [&](const task* lhs, const  task* rhs){
	// 			const auto fL = union_set_find(lhs);
	// 			const auto fR = union_set_find(rhs);
	// 			if(fL != fR){
	// 				union_set_to_destination[fL] = fR;
	// 			}
	// 		};
	//
	// 		for (auto& [id, tsk] : tasks){
	// 			union_set_to_destination.try_emplace(&tsk, &tsk);
	// 		}
	//
	//
	// 		for (auto&& [id, tsk] : tasks) {
	// 			const auto current = &tsk;
	// 			const auto in_deg = current->parents.size();
	//
	// 			if (in_deg == 0) {
	// 				global_zero_in_degree_queue.push_back(current);
	// 			}else{
	// 				in_degree.try_emplace(current, in_deg);
	// 			}
	//
	// 			for (const auto & child : tsk.children){
	// 				join_to(&tsk, child);
	// 			}
	// 		}
	//
	// 		std::deque<const task*> local_zero_degree;
	// 		for (auto head : global_zero_in_degree_queue){
	// 			task_group::task_sequence seq{};
	// 			local_zero_degree.clear();
	// 			local_zero_degree.push_back(head);
	//
	// 			while(!local_zero_degree.empty()){
	// 				auto cur = local_zero_degree.front();
	// 				local_zero_degree.pop_front();
	//
	// 				for (auto child : cur->children){
	// 					if (--in_degree[child] == 0) {
	// 						local_zero_degree.push_back(child);
	// 					}else{
	// 						seq.branches[cur].push_back(child);
	// 					}
	// 				}
	//
	// 				seq.seq.push_back(cur);
	// 			}
	//
	// 			auto& currentGroup = groups[union_set_find(head)];
	// 			currentGroup.sequences.push_back(std::move(seq));
	// 		}
	//
	// 		for (const auto& [node, degree] : in_degree) {
	// 			if (degree != 0) {
	// 				throw std::runtime_error("Cycle detected in task graph");
	// 			}
	// 		}
	//
	// 		return groups;
	// 	}
	// };
}