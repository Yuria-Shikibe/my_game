# ECS Policy

## TODO

* 组件间不变式的自动初始化：如碰撞箱的变化跟随主变换

## 设计目标

* 实体公开句柄使用 `ecs::entity_id`。它是轻量值类型，包含 owner、slot、generation，可复制、比较、hash。
* ECS 采用多生产者、单提交模型：任意线程只记录创建/销毁命令，`component_manager::commit()` 是唯一结构变更点。
* 旧 handle 通过 generation 自动失效。slot 复用后，旧 `entity_id` 不能访问新实体。
* 新代码优先使用 `spawn`、`destroy`、`commit`、`each`、`try_get` 这条主路径，避免长期持有组件指针或依赖兼容 wrapper。

## 核心接口

| API | 用途 | 约定 |
|-----|------|------|
| `manager.spawn<Desc>(...) -> entity_id` | 创建实体并立即返回 staging handle | 组件在 `commit()` 后才可查询和访问 |
| `manager.destroy(entity_id) -> bool` | 请求销毁实体 | 通过 CAS 立即把 staging/valid 实体标为 expired；同一实体只会成功一次 |
| `manager.commit()` | 提交结构变更 | 先 destroy，后 spawn；业务代码默认使用它 |
| `manager.each(fn)` | 遍历匹配组件的 valid 实体 | 组件类型由 callback 参数推导；自动跳过 expired/stale 行；不得与 `commit()` 并发执行 |
| `manager.each_unfiltered(fn)` | raw 遍历匹配 archetype 行 | 组件类型由 callback 参数推导；允许看到 destroy 后、commit 前仍未物理删除的行 |
| `manager.try_get<T>(id)` | 通过 handle 获取组件 | staging、expired、stale handle 返回 `nullptr` |
| `id.try_get<T>()` / `id.at<T>()` | 句柄便捷访问 | `try_get` 可空；`at` 要求组件存在 |
| `entity_command_buffer` + `manager.submit(std::move(buffer))` | 多线程批量记录创建/销毁 | buffer 在线程本地写入，submit 后交给 manager 等待 `commit()` |

`commit_destroy()` 和 `commit_spawn()` 可用于测试或内部阶段控制；新业务代码应优先使用 `commit()`。

## 实体生命周期

实体状态以 `entity_id::get_state()`、`is_staging()`、`is_inserted()`、`is_expired()` 观察。无效、stale 或 owner 不匹配的 handle 不应被当作可访问实体。

| State | 进入时机 | 查询可见 | 组件可访问 | 说明 |
|-------|----------|----------|------------|------|
| `Staging` | `spawn` 返回后，提交前 | 否 | 否 | handle 可保存，但实体尚未插入 archetype |
| `Valid` | `commit()` 的 spawn 阶段完成后 | 是 | 是 | 也称 inserted，是普通系统可操作的状态 |
| `Expired` | `destroy` 成功后立即进入 | 否 | 否 | 即使物理 erase 尚未提交，也应视为不可访问 |
| `Destroyed/Recycled` | `commit()` 的 destroy 阶段回收 slot 后 | 否 | 否 | 仅作概念说明；旧 generation handle 永久 stale |

`commit()` 先处理 destroy，再处理 spawn：

1. 对 valid 实体，destroy 阶段从 archetype 中 unstable erase，并更新被搬移实体的 row index。
2. 对 staging 实体，如果提交前已被 destroy，spawn 阶段直接跳过并释放 slot。
3. 如果实体配置了 destroy delay，destroy 阶段会递减延迟并重新入队，直到允许物理删除。

## 查询与组件访问

系统遍历默认使用 `each`。它会在内部读取 `chunk_meta.id()` 并跳过非 inserted 实体，所以 `destroy()` 成功后，即使还没有执行下一次 `commit()`，业务遍历也不会再看到该实体。

```cpp
manager.each([](const ecs::chunk_meta& meta, position& pos, velocity& vel){
	const ecs::entity_id id = meta.id();
	// 这里只会看到 valid 实体。
});
```

单实体访问使用 `try_get`：

```cpp
ecs::entity_id id = manager.spawn<player_desc>(std::move(components));

manager.commit();

if(player_state* state = id.try_get<player_state>()){
	state->hp = 10.0f;
}

if(manager.destroy(id)){
	// 从这里开始，查询和 try_get 都应跳过该实体。
}

manager.commit();
```

不要跨 `commit()` 长期保存组件指针、chunk row 或 archetype 内部地址；需要跨帧保存实体关系时保存 `entity_id`，并在每次使用前重新 `try_get`。

`each_unfiltered` 是显式 opt-in 的 raw 遍历入口，会走 archetype slice 直遍历路径。它可能看到已经 `destroy()` 但尚未被 `commit_destroy()` 物理 erase 的行；使用它时必须自行检查 `chunk_meta.id().is_inserted()`，或只在确实需要 raw storage 视图的内部/测试代码中使用。

## 提交流程与多线程约束

* `spawn` 和 `destroy` 可由多个线程记录命令；它们不直接改动 archetype 数据布局。
* `entity_command_buffer` 用于线程本地批量记录，减少 manager 锁竞争。
* `submit(std::move(buffer))` 只把命令交给 manager；实际结构变更仍发生在下一次 `commit()`。
* 查询、组件写入、组件指针访问不得与 `commit()` 并发。当前 ECS 不提供完全并发读写语义。
* `destroy(entity_id)` 对同一 current handle 只允许一次成功；重复销毁、stale handle、owner 不匹配 handle 返回 `false`。

```cpp
std::vector<std::jthread> workers;

for(std::uint32_t i = 0; i != worker_count; ++i){
	workers.emplace_back([&manager, i]{
		ecs::entity_command_buffer buffer{};

		for(std::uint32_t n = 0; n != batch_size; ++n){
			(void)buffer.spawn<bullet_desc>(
				manager,
				make_bullet_components(i, n)
			);
		}

		manager.submit(std::move(buffer));
	});
}

workers.clear();
manager.commit();
```

## Archetype 策略

* archetype 创建、`type_to_archetype` 更新和查询索引失效都应发生在提交路径中。
* spawn 按 archetype 类型分组批量 reserve/insert，避免逐实体扩容和 per-archetype staging 锁。
* 查询索引缓存 include/exclude 到 archetype slice 的匹配结果；新增 archetype 时必须使缓存失效。
* `slice_and_then` / `get_slice_of` 是低层 raw slice API，不自动过滤 expired 行；业务遍历优先使用 `each`。
* `chunk_meta` 继续作为内部行元数据和兼容查询参数使用。新代码若只需要实体身份，优先取 `meta.id()` 并保存 `entity_id`。

## 引用与跨帧保存

* `entity_id` 可以跨帧保存，但它只是可验证 handle，不代表实体仍存在。
* 跨帧关系、物理 contact/cache、事件 payload 应保存 `entity_id` 和必要快照；使用时重新验证。
* `entity_ref` 和 `entity_pin` 只作为兼容或轻量 wrapper 存在，不提供保活语义。新代码优先直接保存 `entity_id`。
* 对外部系统暴露实体时，不暴露组件指针；暴露 `entity_id` 或复制出的只读快照。

## 兼容接口迁移

| 旧接口 | 新接口 | 迁移说明 |
|--------|--------|----------|
| `create_entity_deferred` | `spawn` | 创建后仍需 `commit()` 才能查询组件 |
| `mark_expired` | `destroy` | 成功后实体立即对查询和 `try_get` 不可见 |
| `do_deferred` | `commit` | 统一提交 destroy 和 spawn |
| `sliced_each` | `each` | 新代码使用更短的遍历入口 |
| `entity_ref` / `entity_pin` | `entity_id` | 新代码直接保存 generation handle |
