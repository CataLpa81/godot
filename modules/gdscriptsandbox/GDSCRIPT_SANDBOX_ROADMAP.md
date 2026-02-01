# GDScript 沙盒完善路线图

目标：让引擎内的 GDScript 沙盒（`modules/sandbox`）在能力上对齐现有 JS 沙盒（GodotJsSandbox），使其能够安全运行不可信的用户脚本（UGC / MOD / 教学关卡等）。

---

## 一、能力对比总览

| 能力 | JSSandbox | GDScript 沙盒现状 | 目标 |
|------|-----------|-------------------|------|
| 类实例化限制 | SafeWrapper + SandboxConfig | ClassDB 回调 ✓ | 保持，并增加继承感知 |
| 方法调用限制 | SafeWrapper 统一入口 | Object::callp 回调 ✓ | 保持，并增加继承感知 |
| **属性读写限制** | SafeWrapper get/set_property | **未拦截** | **需新增** |
| 沙盒全局变量 | context 独立全局 | GDScriptLanguage 回调 ✓ | 保持 |
| 路径/资源隔离 | load_resource 校验 | sandbox_root_path + SandboxBridge ✓ | 保持，并修复 load() |
| **全局 load()** | 经 SafeWrapper | **直接调 ResourceLoader，绕过沙盒** | **需拦截** |
| **类/方法黑名单继承** | is_class_or_parent_blocked | **仅精确类名** | **需继承感知** |
| 每帧 API 限流 | write_ops / heavy_ops per frame | 无 | 可选实现 |
| 执行超时 | execution_limiter timeout | 无 | 可选/后续 |
| 内存上限 | memory_limit_mb | 无 | 可选/后续 |
| 渲染/场景隔离 | 无 | SubViewport ✓ | 保持 |

---

## 二、待办事项与优先级

### P0：必须完成（安全与语义对齐）

1. **拦截全局 `load()`**  
   沙盒内脚本若直接写 `load("res://...")`，当前会绕过 `_resolve_sandbox_path`，可访问任意路径。必须让沙盒内的 `load()` 走沙盒路径解析。

2. **属性访问沙盒检查**  
   当前仅对**方法调用**做了检查，`obj.xxx = value` / `obj.xxx` 未经过沙盒。需在 `Object::get` / `Object::set` 中增加沙盒回调，使属性读写与 JSSandbox 的 SafeWrapper 行为对齐。

3. **类/方法黑名单继承感知**  
   当前只按**精确类名**判断，例如禁止 `Node` 仍可实例化 `Node2D`。需改为：若某类或其任意父类在黑名单中，则禁止；方法同理（按对象实际类及其父类检查方法黑名单）。

### P1：建议完成（策略完整性）

4. **每帧 API 限流（可选）**  
   JSSandbox 有 write_ops_per_frame、heavy_ops_per_frame。可在沙盒回调中维护“当前帧”的写操作/重量级操作计数，每帧清零，超限则拒绝调用，避免单帧内滥用 API。

### P2：后续/可选

5. **执行超时**  
   需在 VM 或驱动层做“本帧/本次调用”的执行时间或指令数限制，实现成本较高，可列为后续迭代。

6. **内存上限**  
   按沙盒统计内存较难，且 GDScript 无独立堆，可列为可选或后续。

---

## 三、分步实现计划

### 步骤 1：拦截沙盒内的全局 `load()`

**目标**：沙盒内执行 `load(path)` 时，路径必须经沙盒解析，且仅能访问 `sandbox_root_path` 下资源。

**方案 A（推荐）**：在 GDScript 的 `load` 工具函数中按“是否在沙盒”分支。

- **文件**：`modules/gdscript/gdscript_utility_functions.cpp`
- **位置**：`GDScriptUtilityFunctions::load` 实现（约 233–236 行，内部调用 `ResourceLoader::load(*p_args[0])`）。
- **做法**：
  1. 在调用 `ResourceLoader::load` 之前，判断当前是否处于沙盒上下文（例如通过 `SandboxInstance::get_current_sandbox()` 或由 sandbox 模块提供的 `is_in_sandbox()` 查询接口）。
  2. 若在沙盒内，则不直接调用 `ResourceLoader::load`，改为调用沙盒提供的安全加载接口（例如 `SandboxBridge::load_resource(path)` 或 `SandboxInstance` 的等价接口），其内部会做 `_resolve_sandbox_path` 与 `_is_path_allowed`。
  3. 若不在沙盒内，保持现有 `ResourceLoader::load` 行为。
- **依赖**：gdscript 模块需要能访问 sandbox 的“当前是否沙盒 + 安全 load”接口，可通过引擎层的小型桥接 API 或 sandbox 模块注册的回调实现，避免 gdscript 直接依赖 sandbox 类型（例如通过 `Engine` 或 `ClassDB` 的扩展、或全局/单例查询“沙盒服务”）。

**方案 B（备选）**：在沙盒初始化时，通过 `set_sandbox_global("load", callable)` 注入一个安全 `load`。  
缺点：GDScript 编译器会把 `load(...)` 编译为 `CALL_GDSCRIPT_UTILITY`，不会走 named global，因此除非修改编译器对 `load` 的解析，否则方案 B 无法生效。**不推荐**，除非同时改 VM/编译器。

**产出**：沙盒内所有 `load(path)` 都经沙盒路径校验，与 JSSandbox 的 `load_resource` 行为一致。

---

### 步骤 2：属性读写的沙盒检查

**目标**：沙盒内对任意对象的属性 `get`/`set` 也经过策略检查，与 JSSandbox 的 get_property/set_property 语义对齐。

**做法**：

1. **扩展 Object 的沙盒接口**  
   - **文件**：`core/object/object.h`、`core/object/object.cpp`  
   - 当前已有 `_sandbox_api_check_callback(Object*, StringName method)`，仅用于方法。  
   - 新增**属性检查回调**，例如：  
     `typedef bool (*SandboxPropertyCheckCallback)(Object *p_object, const StringName &p_property, bool p_is_set);`  
     并增加 `set_sandbox_property_check_callback` / `get_sandbox_property_check_callback`，以及静态成员 `_sandbox_property_check_callback`。

2. **在 Object::get / Object::set 中调用**  
   - **文件**：`core/object/object.cpp`  
   - 在 `Object::get` 中，在真正执行属性读取前（例如在 `script_instance->get`、`ClassDB::get_property` 等分支之前），若 `_sandbox_property_check_callback` 存在且当前线程处于沙盒上下文，则先调用该回调；若返回 false，则视作不允许访问（可设 `r_valid = false` 并返回空 Variant）。  
   - 在 `Object::set` 中同理：在真正写入前调用属性检查回调（p_is_set = true）；若返回 false，则拒绝写入并返回。

3. **Sandbox 模块实现并注册回调**  
   - **文件**：`modules/sandbox/sandbox_instance.cpp`、`sandbox_instance.h`  
   - 实现静态方法，例如 `SandboxInstance::_sandbox_property_check(Object*, StringName property, bool is_set)`：  
     - 若 `current_sandbox == nullptr`，返回 true（允许）。  
     - 否则用 `p_object->get_class_name()` 及继承链（见步骤 3）判断该类是否允许；若需要“按属性名”的黑名单，可扩展 `blocked_properties`（与现有 `blocked_methods` 类似），并做继承感知。  
   - 在 `register_types.cpp` 的 `initialize_sandbox_module` 中调用 `Object::set_sandbox_property_check_callback(&SandboxInstance::_sandbox_property_check)`，在 `uninitialize_sandbox_module` 中置为 nullptr。

**产出**：沙盒内对任意对象的属性访问都会经过同一套策略，无法通过“只改属性不调方法”绕过限制。

---

### 步骤 3：类/方法黑名单的继承感知

**目标**：禁止某类时，其子类也被禁止；禁止某类某方法时，子类继承的同一方法也被禁止。

**做法**：

1. **类允许判断（is_class_allowed）**  
   - **文件**：`modules/sandbox/sandbox_instance.cpp`  
   - 在 `is_class_allowed` 中：  
     - 若使用白名单（allowed_classes 非空）：保持现有逻辑（仅检查 p_class 是否在 allowed_classes）。  
     - 若使用黑名单：不仅检查 `blocked_classes.has(p_class)`，还遍历 `blocked_classes`，对每个 blocked_class 调用 `ClassDB::is_parent_class(p_class, blocked_class)`；若任一为 true，表示 p_class 继承自被禁类，则返回 false。  
   - 使用 `ClassDB::is_parent_class`（`core/object/class_db.cpp` 已存在）。

2. **方法允许判断（is_method_allowed）**  
   - **文件**：`modules/sandbox/sandbox_instance.cpp`  
   - 在 `is_method_allowed` 中：  
     - 先做**类**的继承感知检查（复用或内联上述 is_class_allowed 逻辑）。  
     - 再检查方法：不仅检查 `blocked_methods.has(p_class)`，还沿 p_class 的父类链向上（`ClassDB::get_parent_class(p_class)` 循环），对当前类名检查 `blocked_methods[当前类].has(p_method)`；若任一父类或自身禁止了该方法，则返回 false。

3. **ClassDB 实例化检查（_sandbox_class_check）**  
   - 已调用 `is_class_allowed(p_class)`，只需保证 `is_class_allowed` 已做继承感知即可，无需改 ClassDB。

4. **Object 方法检查（_sandbox_api_check）**  
   - 当前传入的是 `p_object->get_class_name()` 和 p_method。  
   - 保持传入“对象实际类名”，在 `is_method_allowed` 内部做父类链方法黑名单检查即可，无需改 Object。

**产出**：禁止 `Node` 则 `Node2D`、`Sprite2D` 等均不可实例化；禁止 `Node.set_process` 则子类调用也会被拒绝。

---

### 步骤 4（可选）：每帧 API 限流

**目标**：与 JSSandbox 的 write_ops_per_frame / heavy_ops_per_frame 类似，限制沙盒内每帧的“写操作”和“重量级操作”次数。

**做法**：

1. **SandboxInstance 内增加限流状态**  
   - **文件**：`modules/sandbox/sandbox_instance.h`、`sandbox_instance.cpp`  
   - 增加成员：例如 `write_ops_this_frame`、`heavy_ops_this_frame`，以及配置项 `max_write_ops_per_frame`、`max_heavy_ops_per_frame`（可设默认值，如 500 / 50）。  
   - 提供 `set_max_write_ops_per_frame`、`set_max_heavy_ops_per_frame`、`reset_frame_counters()`，并在每帧开始时由宿主调用 `reset_frame_counters()`（或通过 SceneTree 的 idle 帧回调自动调用）。

2. **对“写/重量级”操作分类**  
   - 在 `_sandbox_api_check` 和 `_sandbox_property_check` 中，根据 class + method 或 class + property 判定为 WRITE 或 HEAVY（例如 set_position、add_child 为 WRITE，instantiate、queue_free 为 HEAVY）。可维护两张集合表（write_methods, heavy_methods）或从配置读取。  
   - 若为 WRITE：`write_ops_this_frame++`，若 `> max_write_ops_per_frame` 则返回 false。  
   - 若为 HEAVY：`heavy_ops_this_frame++`，若 `> max_heavy_ops_per_frame` 则返回 false。  
   - 读操作不计数（或仅统计不限制）。

3. **帧计数重置**  
   - 在 `SandboxInstance` 的 `start()` 或每帧更新处调用 `reset_frame_counters()`；若沙盒由 SceneTree 驱动，可在该沙盒的 SubViewport 或根节点的 `_process` 中调用，或通过引擎的帧回调注册。

**产出**：沙盒内单帧写/重量级调用有上限，降低滥用与 DoS 风险。

---

### 步骤 5：测试与验证

- **单元/集成测试**  
  - 沙盒内调用 `load("res://outside/path")` 应失败或仅能解析到沙盒根下；  
  - 禁止某类后，实例化其子类应失败；  
  - 禁止某方法后，子类调用该方法应失败；  
  - 属性黑名单后，`obj.property = x` / `obj.property` 应被拒绝。  
- **回归**  
  - 非沙盒场景下，`load()`、属性访问、类实例化、方法调用行为与修改前一致。  
- **文档**  
  - 在 `modules/sandbox` 下补充：沙盒内推荐使用 `SandboxBridge.load_resource()` 或直接 `load()`（步骤 1 完成后二者等价），以及如何配置黑名单/白名单、可选限流参数。

---

## 四、涉及文件一览

| 步骤 | 文件 | 修改要点 |
|------|------|----------|
| 1 | `modules/gdscript/gdscript_utility_functions.cpp` | `load()` 内分支：沙盒时走 SandboxBridge/沙盒 load |
| 1 | sandbox 与 gdscript 的桥接（见上） | 提供“是否沙盒 + 安全 load”的 API 或回调 |
| 2 | `core/object/object.h` | 新增属性检查回调类型与 set/get |
| 2 | `core/object/object.cpp` | 在 get/set 中调用属性检查回调 |
| 2 | `modules/sandbox/sandbox_instance.h/.cpp` | 实现并注册 _sandbox_property_check |
| 2 | `modules/sandbox/register_types.cpp` | 注册/注销属性检查回调 |
| 3 | `modules/sandbox/sandbox_instance.cpp` | is_class_allowed / is_method_allowed 继承感知 |
| 4 | `modules/sandbox/sandbox_instance.h/.cpp` | 限流计数器与 reset_frame_counters，在 api/property 回调中分类计数 |

---

## 五、与 JSSandbox 的最终对齐情况

完成 P0 + P1 后，GDScript 沙盒将具备：

- 类/方法/属性的黑名单（含继承），与 JSSandbox 的 SafeWrapper + SandboxConfig 语义对齐；  
- 沙盒内 `load()` 强制走沙盒路径，与 JSSandbox 的 load_resource 行为一致；  
- 可选每帧限流，与 JSSandbox 的 write/heavy ops 对齐；  
- 现有视口隔离、沙盒全局、SandboxBridge 通信保持不变。

执行超时与内存上限可作为后续版本或独立需求再设计。本文档会随实现进展做小幅修订（如具体函数名、行号），整体步骤与优先级保持不变。
