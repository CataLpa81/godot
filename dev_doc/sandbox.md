# GDScript 沙盒运行环境设计方案

## 1. 概述

### 1.1 目标
在Godot引擎中实现一个GDScript沙盒运行环境，允许Host游戏在运行时创建隔离的沙盒，在沙盒中安全地运行独立的小游戏（Guest），且Guest无法访问或影响Host的运行环境。

### 1.2 核心需求
- **脚本隔离**：沙盒中的GDScript不能访问Host的全局变量、Autoload和单例
- **场景隔离**：沙盒有独立的场景树，与Host场景树隔离
- **资源隔离**：沙盒有独立的资源路径空间，无法访问Host资源
- **API安全**：限制沙盒访问敏感API（文件系统、网络、OS等）
- **通信机制**：提供Host与沙盒之间的安全通信接口

---

## 2. 源码分析

### 2.1 GDScript 语言系统

**关键文件**: `modules/gdscript/`

#### 2.1.1 GDScriptLanguage 单例
```cpp
// gdscript.h:411
class GDScriptLanguage : public ScriptLanguage {
    static GDScriptLanguage *singleton;

    // 全局变量存储
    Variant *_global_array = nullptr;
    Vector<Variant> global_array;
    HashMap<StringName, int> globals;           // 通过索引访问
    HashMap<StringName, Variant> named_globals; // 通过名称访问（Autoload用这个）

    // 调用栈（thread_local）
    static thread_local CallLevel *_call_stack;
    static thread_local uint32_t _call_stack_size;
};
```

**问题**：`GDScriptLanguage` 是单例，所有GDScript共享同一个全局变量空间。

#### 2.1.2 Autoload 初始化
```cpp
// main/main.cpp:4367-4426
// Autoload 通过 ScriptServer 的每个语言实例注册全局常量
for (int i = 0; i < ScriptServer::get_language_count(); i++) {
    ScriptServer::get_language(i)->add_global_constant(info.name, Variant());
}
// ...
ScriptServer::get_language(i)->add_global_constant(info.name, n);
```

#### 2.1.3 全局变量访问（VM层）
```cpp
// gdscript_vm.cpp:3799-3807
OPCODE(OPCODE_STORE_NAMED_GLOBAL) {
    const StringName *globalname = &_global_names_ptr[globalname_idx];
    *dst = GDScriptLanguage::get_singleton()->get_named_globals_map()[*globalname];
}
```

### 2.2 场景树系统

**关键文件**: `scene/main/`

#### 2.2.1 SceneTree 单例
```cpp
// scene_tree.h:85
class SceneTree : public MainLoop {
    static SceneTree *singleton;
    Window *root = nullptr;
    Node *current_scene = nullptr;
    HashMap<StringName, Group> group_map;
    // ...
};
```

**问题**：`SceneTree` 是单例，只有一个根节点和一套场景管理。

#### 2.2.2 SubViewport
```cpp
// viewport.h:867
class SubViewport : public Viewport {
    // 可以作为独立的渲染目标
    // 有自己的 World2D 和 World3D
};
```

**优势**：`SubViewport` 可以拥有独立的渲染世界。

### 2.3 资源系统

**关键文件**: `core/io/`

#### 2.3.1 ResourceLoader
```cpp
// resource_loader.h:105
class ResourceLoader {
    static Ref<ResourceFormatLoader> loader[MAX_LOADERS];
    static HashMap<String, Vector<String>> translation_remaps;

    // 资源缓存机制
    static HashMap<String, ThreadLoadTask> thread_load_tasks;
};
```

#### 2.3.2 FileAccess
```cpp
// file_access.h:48
enum AccessType : int32_t {
    ACCESS_RESOURCES,  // res://
    ACCESS_USERDATA,   // user://
    ACCESS_FILESYSTEM, // 绝对路径
    ACCESS_PIPE,
    ACCESS_MAX
};
```

### 2.4 Engine 单例系统

```cpp
// engine.h:43
class Engine {
    static inline Engine *singleton = nullptr;
    List<Singleton> singletons;
    HashMap<StringName, Object *> singleton_ptrs;
};
```

---

## 3. 沙盒架构设计

### 3.1 整体架构

```
+------------------------------------------------------------------+
|                         Host Game                                 |
|  +--------------------------------------------------------------+ |
|  |                     SceneTree (主场景树)                       | |
|  |  +----------------------------------------------------------+ | |
|  |  |                    Root Window                            | | |
|  |  |  +-----------------+  +-------------------------------+  | | |
|  |  |  |   Host Scene    |  |      SandboxContainer         |  | | |
|  |  |  |  (主游戏场景)    |  |  +-------------------------+  |  | | |
|  |  |  |                 |  |  |     SubViewport         |  |  | | |
|  |  |  |                 |  |  |  +-------------------+  |  |  | | |
|  |  |  |                 |  |  |  |  Guest Scene     |  |  |  | | |
|  |  |  |                 |  |  |  |  (沙盒小游戏)     |  |  |  | | |
|  |  |  |                 |  |  |  +-------------------+  |  |  | | |
|  |  |  |                 |  |  +-------------------------+  |  | | |
|  |  |  +-----------------+  +-------------------------------+  | | |
|  |  +----------------------------------------------------------+ | |
|  +--------------------------------------------------------------+ |
+------------------------------------------------------------------+

+------------------------------------------------------------------+
|                      Sandbox Runtime Layer                        |
|  +--------------+  +----------------+  +-----------------------+  |
|  |SandboxScript |  |SandboxResource |  |  SandboxAPIFilter     |  |
|  |  Language    |  |   Loader       |  |                       |  |
|  +--------------+  +----------------+  +-----------------------+  |
+------------------------------------------------------------------+
```

### 3.2 核心组件

#### 3.2.1 SandboxInstance 类
沙盒实例的主控制器。

```cpp
// 新文件: modules/sandbox/sandbox_instance.h

class SandboxInstance : public RefCounted {
    GDCLASS(SandboxInstance, RefCounted);

private:
    // 沙盒标识
    StringName sandbox_id;

    // 独立的全局变量空间
    HashMap<StringName, Variant> sandbox_globals;
    HashMap<StringName, int> sandbox_global_indices;
    Vector<Variant> sandbox_global_array;

    // 沙盒的SubViewport（渲染隔离）
    SubViewport *sandbox_viewport = nullptr;

    // 沙盒资源加载器
    Ref<SandboxResourceLoader> resource_loader;

    // API白名单/黑名单
    HashSet<StringName> allowed_classes;
    HashSet<StringName> blocked_methods;

    // 沙盒状态
    bool is_running = false;

public:
    // 创建沙盒
    Error initialize(const String &p_sandbox_root_path);

    // 加载并运行沙盒场景
    Error load_scene(const String &p_scene_path);

    // 获取/设置沙盒全局变量
    void set_sandbox_global(const StringName &p_name, const Variant &p_value);
    Variant get_sandbox_global(const StringName &p_name) const;

    // Host与沙盒通信
    void send_message(const StringName &p_message, const Array &p_args);
    Signal on_message_received; // 沙盒向Host发送消息

    // 生命周期
    void start();
    void stop();
    void pause();
    void resume();

    // API访问控制
    void set_allowed_classes(const TypedArray<StringName> &p_classes);
    void block_method(const StringName &p_class, const StringName &p_method);
    bool is_api_allowed(const StringName &p_class, const StringName &p_method) const;

    SubViewport *get_viewport() const { return sandbox_viewport; }
};
```

#### 3.2.2 SandboxScriptInstance 类
替代标准GDScriptInstance，在沙盒环境中运行。

```cpp
// 新文件: modules/sandbox/sandbox_script_instance.h

class SandboxScriptInstance : public ScriptInstance {
private:
    SandboxInstance *sandbox = nullptr;  // 所属沙盒
    Ref<GDScript> script;
    Object *owner = nullptr;
    Vector<Variant> members;

public:
    // 重写方法调用，添加安全检查
    virtual Variant callp(const StringName &p_method, const Variant **p_args,
                          int p_argcount, Callable::CallError &r_error) override;

    // 重写属性访问
    virtual bool set(const StringName &p_name, const Variant &p_value) override;
    virtual bool get(const StringName &p_name, Variant &r_ret) const override;

    // 获取沙盒上下文中的全局变量
    Variant get_sandbox_global(const StringName &p_name) const;
};
```

#### 3.2.3 SandboxResourceLoader 类
虚拟文件系统，限制资源访问范围。

```cpp
// 新文件: modules/sandbox/sandbox_resource_loader.h

class SandboxResourceLoader : public RefCounted {
    GDCLASS(SandboxResourceLoader, RefCounted);

private:
    String sandbox_root;  // 沙盒资源根目录
    HashMap<String, Ref<Resource>> resource_cache;
    SandboxInstance *sandbox = nullptr;

public:
    void set_sandbox_root(const String &p_root);

    // 将沙盒路径转换为实际路径
    String resolve_path(const String &p_sandbox_path) const;

    // 检查路径是否在沙盒范围内
    bool is_path_allowed(const String &p_path) const;

    // 加载资源（只能加载沙盒范围内的资源）
    Ref<Resource> load(const String &p_path, const String &p_type_hint = "");

    // 沙盒内的 res:// 重定向
    // sandbox://scene.tscn -> {sandbox_root}/scene.tscn
};
```

#### 3.2.4 SandboxAPIFilter 类
API访问过滤器。

```cpp
// 新文件: modules/sandbox/sandbox_api_filter.h

class SandboxAPIFilter : public RefCounted {
    GDCLASS(SandboxAPIFilter, RefCounted);

private:
    // 完全禁止的类
    static const HashSet<StringName> BLOCKED_CLASSES;

    // 部分禁止的方法 (类名 -> 方法列表)
    static const HashMap<StringName, HashSet<StringName>> BLOCKED_METHODS;

    // 用户自定义白名单
    HashSet<StringName> user_allowed_classes;
    HashMap<StringName, HashSet<StringName>> user_blocked_methods;

public:
    // 默认禁止的危险类
    static void initialize_default_blocks();

    // 检查API是否允许
    bool is_class_allowed(const StringName &p_class) const;
    bool is_method_allowed(const StringName &p_class, const StringName &p_method) const;

    // 在方法调用前检查
    Error check_method_call(Object *p_object, const StringName &p_method,
                           const Variant **p_args, int p_argcount);
};
```

**默认禁止的类和方法**:
```cpp
const HashSet<StringName> SandboxAPIFilter::BLOCKED_CLASSES = {
    "OS",                    // 操作系统访问
    "FileAccess",           // 文件系统访问
    "DirAccess",            // 目录访问
    "Thread",               // 线程创建
    "Mutex",                // 互斥锁
    "Semaphore",            // 信号量
    "TCPServer",            // TCP服务器
    "StreamPeerTCP",        // TCP流
    "UDPServer",            // UDP服务器
    "PacketPeerUDP",        // UDP包
    "HTTPClient",           // HTTP客户端
    "HTTPRequest",          // HTTP请求
    "WebSocketPeer",        // WebSocket
    "MultiplayerAPI",       // 多人游戏API
    "ENetMultiplayerPeer",  // ENet多人游戏
    "GDExtension",          // GD扩展
    "GDExtensionManager",   // 扩展管理器
    "JavaScriptBridge",     // JS桥接
    "JavaScriptObject",     // JS对象
};

const HashMap<StringName, HashSet<StringName>> SandboxAPIFilter::BLOCKED_METHODS = {
    {"Engine", {"get_singleton", "register_singleton", "unregister_singleton"}},
    {"ClassDB", {"instantiate", "class_set_reloading"}},
    {"Object", {"call_deferred", "set_meta", "get_meta"}},  // 可选限制
    {"Node", {"set_process_mode", "queue_free"}},  // 可选限制
    {"ResourceLoader", {"load", "load_threaded_request"}},  // 使用沙盒版本
};
```

---

## 4. 实现方案

### 4.1 Phase 1: 基础沙盒框架

#### 4.1.1 创建sandbox模块目录结构

```
modules/sandbox/
├── SCsub
├── config.py
├── register_types.h
├── register_types.cpp
├── sandbox_instance.h
├── sandbox_instance.cpp
├── sandbox_script_instance.h
├── sandbox_script_instance.cpp
├── sandbox_resource_loader.h
├── sandbox_resource_loader.cpp
├── sandbox_api_filter.h
├── sandbox_api_filter.cpp
├── sandbox_globals.h          # 沙盒全局变量管理
├── sandbox_globals.cpp
└── doc_classes/
    └── SandboxInstance.xml
```

#### 4.1.2 模块配置

```python
# modules/sandbox/config.py
def can_build(env, platform):
    return True

def configure(env):
    pass

def get_doc_classes():
    return [
        "SandboxInstance",
        "SandboxResourceLoader",
    ]

def get_doc_path():
    return "doc_classes"
```

```python
# modules/sandbox/SCsub
Import("env")
Import("env_modules")

env_sandbox = env_modules.Clone()

env_sandbox.add_source_files(env.modules_sources, "*.cpp")
```

### 4.2 Phase 2: GDScript 全局变量隔离

#### 4.2.1 修改 GDScriptLanguage

需要修改 `modules/gdscript/gdscript.h` 和 `modules/gdscript/gdscript.cpp`，添加沙盒支持：

```cpp
// 在 gdscript.h 中添加
class GDScriptLanguage : public ScriptLanguage {
    // ... 现有代码 ...

    // 新增：当前沙盒上下文（thread_local）
    static thread_local SandboxInstance *_current_sandbox;

public:
    // 新增：设置/获取当前沙盒上下文
    static void set_current_sandbox(SandboxInstance *p_sandbox);
    static SandboxInstance *get_current_sandbox();

    // 新增：沙盒感知的全局变量访问
    Variant get_global_for_sandbox(const StringName &p_name) const;
};
```

```cpp
// 在 gdscript.cpp 中实现
thread_local SandboxInstance *GDScriptLanguage::_current_sandbox = nullptr;

void GDScriptLanguage::set_current_sandbox(SandboxInstance *p_sandbox) {
    _current_sandbox = p_sandbox;
}

SandboxInstance *GDScriptLanguage::get_current_sandbox() {
    return _current_sandbox;
}

Variant GDScriptLanguage::get_global_for_sandbox(const StringName &p_name) const {
    // 如果在沙盒上下文中，从沙盒获取全局变量
    if (_current_sandbox) {
        if (_current_sandbox->has_sandbox_global(p_name)) {
            return _current_sandbox->get_sandbox_global(p_name);
        }
        // 沙盒中不存在且不在白名单中的全局变量返回null
        return Variant();
    }
    // 非沙盒上下文，正常访问
    return get_any_global_constant(p_name);
}
```

#### 4.2.2 修改 GDScript VM

修改 `modules/gdscript/gdscript_vm.cpp` 中的全局变量访问：

```cpp
// 修改 OPCODE_STORE_NAMED_GLOBAL
OPCODE(OPCODE_STORE_NAMED_GLOBAL) {
    CHECK_SPACE(3);
    int globalname_idx = _code_ptr[ip + 2];
    GD_ERR_BREAK(globalname_idx < 0 || globalname_idx >= _global_names_count);
    const StringName *globalname = &_global_names_ptr[globalname_idx];

    GET_VARIANT_PTR(dst, 0);

    // 新增：沙盒感知的全局变量访问
    *dst = GDScriptLanguage::get_singleton()->get_global_for_sandbox(*globalname);

    ip += 3;
}
DISPATCH_OPCODE;
```

### 4.3 Phase 3: 资源系统隔离

#### 4.3.1 SandboxResourceLoader 实现

```cpp
// sandbox_resource_loader.cpp

void SandboxResourceLoader::set_sandbox_root(const String &p_root) {
    // 确保路径以 / 结尾
    sandbox_root = p_root.ends_with("/") ? p_root : p_root + "/";
}

String SandboxResourceLoader::resolve_path(const String &p_sandbox_path) const {
    String path = p_sandbox_path;

    // 处理沙盒内的 res:// 路径
    if (path.begins_with("res://")) {
        path = path.substr(6); // 移除 "res://"
    }
    // 处理沙盒内的 sandbox:// 路径
    else if (path.begins_with("sandbox://")) {
        path = path.substr(10); // 移除 "sandbox://"
    }

    // 安全检查：防止路径遍历攻击
    if (path.find("..") != -1) {
        ERR_FAIL_V_MSG("", "Path traversal not allowed in sandbox.");
    }

    return sandbox_root + path;
}

bool SandboxResourceLoader::is_path_allowed(const String &p_path) const {
    String resolved = resolve_path(p_path);

    // 检查解析后的路径是否在沙盒根目录内
    if (!resolved.begins_with(sandbox_root)) {
        return false;
    }

    // 检查路径是否存在
    return FileAccess::exists(resolved);
}

Ref<Resource> SandboxResourceLoader::load(const String &p_path, const String &p_type_hint) {
    if (!is_path_allowed(p_path)) {
        ERR_FAIL_V_MSG(Ref<Resource>(),
            vformat("Resource path '%s' is not allowed in sandbox.", p_path));
    }

    String actual_path = resolve_path(p_path);

    // 检查缓存
    if (resource_cache.has(actual_path)) {
        return resource_cache[actual_path];
    }

    // 设置沙盒上下文后加载
    GDScriptLanguage::set_current_sandbox(sandbox);
    Ref<Resource> res = ResourceLoader::load(actual_path, p_type_hint);
    GDScriptLanguage::set_current_sandbox(nullptr);

    if (res.is_valid()) {
        resource_cache[actual_path] = res;
    }

    return res;
}
```

### 4.4 Phase 4: API 安全过滤

#### 4.4.1 方法调用拦截

需要在 `core/object/object.cpp` 的方法调用路径中添加沙盒检查：

```cpp
// 修改 Object::callp 或在合适的位置添加检查

Variant Object::callp(const StringName &p_method, const Variant **p_args,
                      int p_argcount, Callable::CallError &r_error) {
    // 新增：沙盒API检查
    SandboxInstance *sandbox = GDScriptLanguage::get_current_sandbox();
    if (sandbox) {
        if (!sandbox->is_api_allowed(get_class_name(), p_method)) {
            r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
            ERR_FAIL_V_MSG(Variant(),
                vformat("Method '%s::%s' is not allowed in sandbox.",
                        get_class_name(), p_method));
        }
    }

    // ... 原有的方法调用逻辑 ...
}
```

#### 4.4.2 对象创建拦截

```cpp
// 修改 ClassDB::instantiate

Object *ClassDB::instantiate(const StringName &p_class) {
    // 新增：沙盒类创建检查
    SandboxInstance *sandbox = GDScriptLanguage::get_current_sandbox();
    if (sandbox) {
        if (!sandbox->is_class_creation_allowed(p_class)) {
            ERR_FAIL_V_MSG(nullptr,
                vformat("Class '%s' cannot be instantiated in sandbox.", p_class));
        }
    }

    // ... 原有的实例化逻辑 ...
}
```

### 4.5 Phase 5: Host-沙盒通信

#### 4.5.1 通信接口

```cpp
// sandbox_instance.cpp

// Host -> Sandbox 发送消息
void SandboxInstance::send_message(const StringName &p_message, const Array &p_args) {
    if (!is_running || !sandbox_viewport) {
        return;
    }

    // 在沙盒场景树中广播消息
    sandbox_viewport->propagate_call("_on_host_message",
        varray(p_message, p_args), false);
}

// Sandbox -> Host 发送消息（从沙盒脚本调用）
void SandboxInstance::emit_to_host(const StringName &p_message, const Array &p_args) {
    emit_signal("message_received", p_message, p_args);
}

// 绑定方法
void SandboxInstance::_bind_methods() {
    ClassDB::bind_method(D_METHOD("send_message", "message", "args"),
                         &SandboxInstance::send_message);
    ClassDB::bind_method(D_METHOD("start"), &SandboxInstance::start);
    ClassDB::bind_method(D_METHOD("stop"), &SandboxInstance::stop);
    ClassDB::bind_method(D_METHOD("pause"), &SandboxInstance::pause);
    ClassDB::bind_method(D_METHOD("resume"), &SandboxInstance::resume);
    ClassDB::bind_method(D_METHOD("load_scene", "path"), &SandboxInstance::load_scene);
    ClassDB::bind_method(D_METHOD("get_viewport"), &SandboxInstance::get_viewport);

    ADD_SIGNAL(MethodInfo("message_received",
        PropertyInfo(Variant::STRING_NAME, "message"),
        PropertyInfo(Variant::ARRAY, "args")));
}
```

#### 4.5.2 沙盒内置类 SandboxBridge

```cpp
// 新文件: modules/sandbox/sandbox_bridge.h

// 提供给沙盒脚本使用的桥接类
class SandboxBridge : public Object {
    GDCLASS(SandboxBridge, Object);

private:
    SandboxInstance *sandbox = nullptr;

public:
    // 沙盒脚本调用此方法向Host发送消息
    void send_to_host(const StringName &p_message, const Array &p_args);

    // 获取沙盒配置信息
    Dictionary get_sandbox_info() const;

    static void _bind_methods();
};
```

---

## 5. 使用示例

### 5.1 Host 端 GDScript 代码

```gdscript
# host_game.gd
extends Node

var sandbox: SandboxInstance

func _ready():
    # 创建沙盒实例
    sandbox = SandboxInstance.new()

    # 设置沙盒资源根目录
    sandbox.initialize("user://downloaded_games/game1/")

    # 设置API限制（可选，有默认配置）
    sandbox.block_method("Node", "queue_free")  # 禁止删除节点

    # 连接消息信号
    sandbox.message_received.connect(_on_sandbox_message)

    # 加载沙盒场景
    sandbox.load_scene("res://main.tscn")  # 相对于沙盒根目录

    # 将沙盒viewport添加到场景
    var viewport = sandbox.get_viewport()
    $SandboxContainer.add_child(viewport)

    # 启动沙盒
    sandbox.start()

func _on_sandbox_message(message: StringName, args: Array):
    print("Received from sandbox: ", message, args)

    # 处理沙盒请求
    match message:
        "request_score":
            sandbox.send_message("score_response", [get_player_score()])
        "game_over":
            _handle_sandbox_game_over(args[0])

func send_to_sandbox(message: StringName, data: Array):
    sandbox.send_message(message, data)
```

### 5.2 Guest 端（沙盒内）GDScript 代码

```gdscript
# sandbox_game.gd (在沙盒内运行)
extends Node

func _ready():
    # SandboxBridge 是沙盒内可用的全局单例
    print("Sandbox game started!")

func _on_host_message(message: StringName, args: Array):
    # 接收来自Host的消息
    match message:
        "score_response":
            display_score(args[0])
        "pause":
            get_tree().paused = true

func request_score():
    # 向Host请求数据
    SandboxBridge.send_to_host("request_score", [])

func game_over(score: int):
    # 通知Host游戏结束
    SandboxBridge.send_to_host("game_over", [score])
```

---

## 6. 安全考虑

### 6.1 默认禁止的API

| 类别 | 禁止的类/方法 | 原因 |
|------|--------------|------|
| 文件系统 | FileAccess, DirAccess | 防止访问Host文件 |
| 网络 | HTTPClient, HTTPRequest, TCP*, UDP*, WebSocket* | 防止未授权网络访问 |
| 系统 | OS, Thread, Mutex, Semaphore | 防止系统级操作 |
| 引擎内部 | Engine.get_singleton, ClassDB.instantiate | 防止访问引擎单例 |
| 扩展 | GDExtension* | 防止加载原生代码 |

### 6.2 可选限制

可以根据需求配置的限制：
- `Node.queue_free()` - 限制节点删除
- `Object.call_deferred()` - 限制延迟调用
- `ResourceLoader` - 使用沙盒版本替代

### 6.3 资源安全

- 沙盒内的 `res://` 路径自动重定向到沙盒根目录
- 禁止路径遍历 (`../`)
- 禁止绝对路径访问
- 资源缓存独立于Host

---

## 7. 性能考虑

### 7.1 开销分析

| 操作 | 额外开销 | 优化建议 |
|------|---------|---------|
| 方法调用检查 | 哈希查找 O(1) | 使用缓存标记 |
| 全局变量访问 | thread_local 检查 | 编译时优化 |
| 资源加载 | 路径验证 | 预编译路径 |
| 场景渲染 | SubViewport开销 | 按需更新 |

### 7.2 优化策略

1. **编译时标记**：在GDScript编译阶段标记沙盒脚本
2. **方法调用缓存**：缓存已验证的方法调用
3. **批量API检查**：预先验证类的所有方法
4. **懒加载**：沙盒资源按需加载

---

## 8. 实现路线图

### Phase 1: 基础框架
- [x] 创建 `modules/sandbox` 模块结构
- [x] 实现 `SandboxInstance` 基础类
- [x] 实现 `SubViewport` 集成

### Phase 2: 脚本隔离
- [x] 修改 `GDScriptLanguage` 添加沙盒上下文
- [x] 修改 `GDScript VM` 全局变量访问
- [ ] 实现 `SandboxScriptInstance` (集成到SandboxInstance中)

### Phase 3: 资源隔离
- [x] 实现 `SandboxResourceLoader` (集成到SandboxInstance中)
- [x] 实现路径重定向和验证
- [ ] 实现独立资源缓存

### Phase 4: API安全
- [x] 实现 `SandboxAPIFilter` (集成到SandboxInstance中)
- [x] 修改 `Object::callp` 添加检查
- [x] 修改 `ClassDB::instantiate` 添加检查

### Phase 5: 通信机制
- [x] 实现 Host-Sandbox 消息传递
- [x] 实现 `SandboxBridge` 类
- [x] 添加信号和事件支持

### Phase 6: 测试和文档
- [ ] 单元测试
- [ ] 集成测试
- [ ] 安全测试
- [ ] API文档

---

## 9. 替代方案对比

### 9.1 方案A: 修改GDScriptLanguage（本文档方案）
**优点**：
- 性能较好，最小化运行时开销
- 与现有引擎深度集成
- 完全控制隔离级别

**缺点**：
- 需要修改引擎核心代码
- 维护成本较高
- 升级引擎版本时需要合并修改

### 9.2 方案B: 创建独立的GDScript解释器
**优点**：
- 完全隔离，不影响Host的GDScript
- 可以完全控制所有行为

**缺点**：
- 实现复杂度极高
- 维护两套GDScript系统
- 功能可能不完整

### 9.3 方案C: 使用进程隔离
**优点**：
- 系统级别的完全隔离
- 不需要修改引擎核心

**缺点**：
- 通信开销大
- 共享数据困难
- 部署复杂

### 9.4 推荐
本方案（方案A）在安全性、性能和实现复杂度之间取得了较好的平衡。

---

## 10. 附录

### 10.1 相关源码文件

| 文件路径 | 说明 |
|---------|------|
| `modules/gdscript/gdscript.h/cpp` | GDScript语言核心 |
| `modules/gdscript/gdscript_vm.cpp` | GDScript虚拟机 |
| `modules/gdscript/gdscript_compiler.cpp` | GDScript编译器 |
| `scene/main/scene_tree.h/cpp` | 场景树 |
| `scene/main/viewport.h/cpp` | Viewport和SubViewport |
| `scene/main/node.h/cpp` | Node基类 |
| `core/io/resource_loader.h/cpp` | 资源加载 |
| `core/io/file_access.h/cpp` | 文件访问 |
| `core/object/class_db.h/cpp` | 类数据库 |
| `core/object/object.h/cpp` | Object基类和方法调用 |
| `core/config/engine.h/cpp` | 引擎单例 |
| `core/config/project_settings.h/cpp` | 项目设置和Autoload |
| `main/main.cpp` | Autoload初始化 |

### 10.2 关键代码位置

**GDScript全局变量访问**:
- `gdscript.h:418-421` - globals和named_globals定义
- `gdscript.cpp:2073-2079` - add_global_constant和add_named_global_constant
- `gdscript_vm.cpp:3799-3807` - OPCODE_STORE_NAMED_GLOBAL

**Autoload初始化**:
- `main/main.cpp:4367-4426` - Autoload加载和注册

**资源加载**:
- `resource_loader.h:244` - ResourceLoader::load
- `resource_loader.cpp` - 加载实现

**方法调用**:
- `object.cpp` - Object::callp
- `class_db.cpp` - ClassDB::instantiate

### 10.3 参考资料
- Godot Engine Documentation: https://docs.godotengine.org
- Godot Source Code: https://github.com/godotengine/godot
- GDScript Language Server Implementation
- Godot Module Development Guide
