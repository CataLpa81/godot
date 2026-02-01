/**************************************************************************/
/*  gdscript_sandbox_instance.cpp                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "gdscript_sandbox_instance.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "scene/main/viewport.h"
#include "scene/resources/packed_scene.h"
#include "scene/resources/world_2d.h"

// Static members
uint32_t GDScriptSandboxInstance::sandbox_id_counter = 0;
thread_local GDScriptSandboxInstance *GDScriptSandboxInstance::current_sandbox = nullptr;

void GDScriptSandboxInstance::set_current_sandbox(GDScriptSandboxInstance *p_sandbox) {
	current_sandbox = p_sandbox;
}

GDScriptSandboxInstance *GDScriptSandboxInstance::get_current_sandbox() {
	return current_sandbox;
}

bool GDScriptSandboxInstance::_sandbox_api_check(Object *p_object, const StringName &p_method) {
	// If not in sandbox context, allow all
	if (!current_sandbox) {
		return true;
	}

	// Get the class name of the object
	StringName class_name = p_object->get_class_name();

	// Check if the method is allowed in the current sandbox
	return current_sandbox->is_method_allowed(class_name, p_method);
}

bool GDScriptSandboxInstance::_sandbox_class_check(const StringName &p_class) {
	// If not in sandbox context, allow all
	if (!current_sandbox) {
		return true;
	}

	// Check if the class is allowed in the current sandbox
	return current_sandbox->is_class_allowed(p_class);
}

bool GDScriptSandboxInstance::_sandbox_global_callback(const StringName &p_name, Variant &r_value) {
	// If not in sandbox context, don't intercept
	if (!current_sandbox) {
		return false;
	}

	// Check if sandbox has this global
	if (current_sandbox->has_sandbox_global(p_name)) {
		r_value = current_sandbox->get_sandbox_global(p_name);
		return true;
	}

	// For named globals that are Objects (typically Autoloads), block access
	// This will be handled by the caller returning an error or empty Variant
	return false;
}

bool GDScriptSandboxInstance::_sandbox_property_check(Object *p_object, const StringName &p_property, bool p_is_set) {
	// If not in sandbox context, allow all
	if (!current_sandbox) {
		return true;
	}

	StringName class_name = p_object->get_class_name();
	return current_sandbox->is_property_allowed(class_name, p_property);
}

bool GDScriptSandboxInstance::_sandbox_load_callback(const String &p_path, Ref<Resource> &r_resource) {
	// If not in sandbox context, use default loading
	if (!current_sandbox) {
		return false;
	}

	// Use sandbox's safe load method
	r_resource = current_sandbox->load_resource(p_path);
	return true; // Handled by sandbox
}

void GDScriptSandboxInstance::_setup_default_blocked_apis() {
	// Block dangerous classes by default
	// File system access
	blocked_classes.insert("FileAccess");
	blocked_classes.insert("DirAccess");

	// Operating system access
	blocked_classes.insert("OS");

	// Threading (could be used to bypass sandbox)
	blocked_classes.insert("Thread");
	blocked_classes.insert("Mutex");
	blocked_classes.insert("Semaphore");

	// Network access
	blocked_classes.insert("TCPServer");
	blocked_classes.insert("StreamPeerTCP");
	blocked_classes.insert("UDPServer");
	blocked_classes.insert("PacketPeerUDP");
	blocked_classes.insert("HTTPClient");
	blocked_classes.insert("HTTPRequest");
	blocked_classes.insert("WebSocketPeer");

	// Multiplayer (could leak data)
	blocked_classes.insert("MultiplayerAPI");
	blocked_classes.insert("MultiplayerPeer");
	blocked_classes.insert("ENetMultiplayerPeer");
	blocked_classes.insert("WebRTCMultiplayerPeer");

	// Extensions (could load native code)
	blocked_classes.insert("GDExtension");
	blocked_classes.insert("GDExtensionManager");

	// JavaScript bridge
	blocked_classes.insert("JavaScriptBridge");
	blocked_classes.insert("JavaScriptObject");

	// Block specific methods on Engine class
	HashSet<StringName> engine_blocked;
	engine_blocked.insert("get_singleton");
	engine_blocked.insert("register_singleton");
	engine_blocked.insert("unregister_singleton");
	engine_blocked.insert("get_singletons");
	blocked_methods.insert("Engine", engine_blocked);

	// Block specific methods on ClassDB
	HashSet<StringName> classdb_blocked;
	classdb_blocked.insert("instantiate");
	blocked_methods.insert("ClassDB", classdb_blocked);

	// Block ResourceLoader direct access (use sandbox version)
	HashSet<StringName> resloader_blocked;
	resloader_blocked.insert("load");
	resloader_blocked.insert("load_threaded_request");
	resloader_blocked.insert("load_threaded_get");
	blocked_methods.insert("ResourceLoader", resloader_blocked);
}

String GDScriptSandboxInstance::_resolve_sandbox_path(const String &p_path) const {
	String path = p_path;

	// Handle sandbox:// prefix
	if (path.begins_with("sandbox://")) {
		path = path.substr(10);
	}
	// Handle res:// prefix within sandbox context
	else if (path.begins_with("res://")) {
		path = path.substr(6);
	}

	// Security: prevent path traversal
	if (path.find("..") != -1) {
		ERR_FAIL_V_MSG("", "Path traversal is not allowed in sandbox.");
	}

	// Ensure path doesn't start with /
	if (path.begins_with("/")) {
		path = path.substr(1);
	}

	return sandbox_root_path.path_join(path);
}

bool GDScriptSandboxInstance::_is_path_allowed(const String &p_path) const {
	String resolved = _resolve_sandbox_path(p_path);
	if (resolved.is_empty()) {
		return false;
	}

	// Verify the resolved path is within sandbox root
	String simplified = resolved.simplify_path();
	String root_simplified = sandbox_root_path.simplify_path();

	if (!simplified.begins_with(root_simplified)) {
		return false;
	}

	return true;
}

void GDScriptSandboxInstance::_cleanup() {
	if (sandbox_root_node) {
		sandbox_root_node->queue_free();
		sandbox_root_node = nullptr;
	}

	if (sandbox_viewport) {
		sandbox_viewport->queue_free();
		sandbox_viewport = nullptr;
	}

	sandbox_globals.clear();
	sandbox_global_indices.clear();
	sandbox_global_array.clear();

	state = STATE_STOPPED;
}

void GDScriptSandboxInstance::_bind_methods() {
	// Initialization
	ClassDB::bind_method(D_METHOD("initialize", "sandbox_root_path"), &GDScriptSandboxInstance::initialize);

	// Scene management
	ClassDB::bind_method(D_METHOD("load_scene", "scene_path"), &GDScriptSandboxInstance::load_scene);
	ClassDB::bind_method(D_METHOD("load_packed_scene", "scene"), &GDScriptSandboxInstance::load_packed_scene);

	// Lifecycle
	ClassDB::bind_method(D_METHOD("start"), &GDScriptSandboxInstance::start);
	ClassDB::bind_method(D_METHOD("stop"), &GDScriptSandboxInstance::stop);
	ClassDB::bind_method(D_METHOD("pause"), &GDScriptSandboxInstance::pause);
	ClassDB::bind_method(D_METHOD("resume"), &GDScriptSandboxInstance::resume);

	// Global variables
	ClassDB::bind_method(D_METHOD("set_sandbox_global", "name", "value"), &GDScriptSandboxInstance::set_sandbox_global);
	ClassDB::bind_method(D_METHOD("get_sandbox_global", "name"), &GDScriptSandboxInstance::get_sandbox_global);
	ClassDB::bind_method(D_METHOD("has_sandbox_global", "name"), &GDScriptSandboxInstance::has_sandbox_global);
	ClassDB::bind_method(D_METHOD("remove_sandbox_global", "name"), &GDScriptSandboxInstance::remove_sandbox_global);

	// API control - classes
	ClassDB::bind_method(D_METHOD("block_class", "class_name"), &GDScriptSandboxInstance::block_class);
	ClassDB::bind_method(D_METHOD("unblock_class", "class_name"), &GDScriptSandboxInstance::unblock_class);
	ClassDB::bind_method(D_METHOD("set_allowed_classes", "classes"), &GDScriptSandboxInstance::set_allowed_classes);
	ClassDB::bind_method(D_METHOD("clear_allowed_classes"), &GDScriptSandboxInstance::clear_allowed_classes);
	ClassDB::bind_method(D_METHOD("is_class_allowed", "class_name"), &GDScriptSandboxInstance::is_class_allowed);

	// API control - methods
	ClassDB::bind_method(D_METHOD("block_method", "class_name", "method_name"), &GDScriptSandboxInstance::block_method);
	ClassDB::bind_method(D_METHOD("unblock_method", "class_name", "method_name"), &GDScriptSandboxInstance::unblock_method);
	ClassDB::bind_method(D_METHOD("is_method_allowed", "class_name", "method_name"), &GDScriptSandboxInstance::is_method_allowed);

	// API control - properties
	ClassDB::bind_method(D_METHOD("block_property", "class_name", "property_name"), &GDScriptSandboxInstance::block_property);
	ClassDB::bind_method(D_METHOD("unblock_property", "class_name", "property_name"), &GDScriptSandboxInstance::unblock_property);
	ClassDB::bind_method(D_METHOD("is_property_allowed", "class_name", "property_name"), &GDScriptSandboxInstance::is_property_allowed);

	// Communication
	ClassDB::bind_method(D_METHOD("send_message", "message", "args"), &GDScriptSandboxInstance::send_message);

	// Getters
	ClassDB::bind_method(D_METHOD("get_viewport"), &GDScriptSandboxInstance::get_viewport);
	ClassDB::bind_method(D_METHOD("get_root_node"), &GDScriptSandboxInstance::get_root_node);
	ClassDB::bind_method(D_METHOD("get_sandbox_root_path"), &GDScriptSandboxInstance::get_sandbox_root_path);
	ClassDB::bind_method(D_METHOD("get_state"), &GDScriptSandboxInstance::get_state);
	ClassDB::bind_method(D_METHOD("get_sandbox_id"), &GDScriptSandboxInstance::get_sandbox_id);
	ClassDB::bind_method(D_METHOD("is_running"), &GDScriptSandboxInstance::is_running);
	ClassDB::bind_method(D_METHOD("is_paused"), &GDScriptSandboxInstance::is_paused);

	// Resource loading
	ClassDB::bind_method(D_METHOD("load_resource", "path", "type_hint"), &GDScriptSandboxInstance::load_resource, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("resource_exists", "path"), &GDScriptSandboxInstance::resource_exists);

	// Signals
	ADD_SIGNAL(MethodInfo("message_received",
			PropertyInfo(Variant::STRING_NAME, "message"),
			PropertyInfo(Variant::ARRAY, "args")));

	ADD_SIGNAL(MethodInfo("sandbox_started"));
	ADD_SIGNAL(MethodInfo("sandbox_stopped"));
	ADD_SIGNAL(MethodInfo("sandbox_paused"));
	ADD_SIGNAL(MethodInfo("sandbox_resumed"));

	// Enums
	BIND_ENUM_CONSTANT(STATE_STOPPED);
	BIND_ENUM_CONSTANT(STATE_RUNNING);
	BIND_ENUM_CONSTANT(STATE_PAUSED);
}

Error GDScriptSandboxInstance::initialize(const String &p_sandbox_root_path) {
	ERR_FAIL_COND_V_MSG(state != STATE_STOPPED, ERR_ALREADY_IN_USE,
			"Sandbox is already initialized. Call stop() first.");

	// Validate and normalize path
	String root_path = p_sandbox_root_path;
	if (!root_path.ends_with("/")) {
		root_path += "/";
	}

	// Check if path exists
	if (!DirAccess::exists(root_path)) {
		ERR_FAIL_V_MSG(ERR_FILE_NOT_FOUND,
				vformat("Sandbox root path does not exist: %s", root_path));
	}

	sandbox_root_path = root_path;

	// Create SubViewport for rendering isolation
	sandbox_viewport = memnew(SubViewport);
	sandbox_viewport->set_name("SandboxViewport");
	sandbox_viewport->set_handle_input_locally(true);

	// Create isolated World2D for 2D rendering
	Ref<World2D> isolated_world_2d;
	isolated_world_2d.instantiate();
	sandbox_viewport->set_world_2d(isolated_world_2d);

#ifndef _3D_DISABLED
	sandbox_viewport->set_use_own_world_3d(true);
#endif

	// Setup default blocked APIs
	_setup_default_blocked_apis();

	return OK;
}

Error GDScriptSandboxInstance::load_scene(const String &p_scene_path) {
	ERR_FAIL_COND_V_MSG(sandbox_viewport == nullptr, ERR_UNCONFIGURED,
			"Sandbox not initialized. Call initialize() first.");

	if (!_is_path_allowed(p_scene_path)) {
		ERR_FAIL_V_MSG(ERR_FILE_NO_PERMISSION,
				vformat("Scene path is not allowed in sandbox: %s", p_scene_path));
	}

	String actual_path = _resolve_sandbox_path(p_scene_path);

	// Set sandbox context for loading
	GDScriptSandboxInstance *prev_sandbox = current_sandbox;
	current_sandbox = this;

	Ref<PackedScene> scene = ResourceLoader::load(actual_path, "PackedScene");

	current_sandbox = prev_sandbox;

	if (scene.is_null()) {
		ERR_FAIL_V_MSG(ERR_CANT_OPEN,
				vformat("Failed to load scene: %s", actual_path));
	}

	return load_packed_scene(scene);
}

Error GDScriptSandboxInstance::load_packed_scene(const Ref<PackedScene> &p_scene) {
	ERR_FAIL_COND_V_MSG(sandbox_viewport == nullptr, ERR_UNCONFIGURED,
			"Sandbox not initialized. Call initialize() first.");
	ERR_FAIL_COND_V_MSG(p_scene.is_null(), ERR_INVALID_PARAMETER,
			"Scene is null.");

	// Remove existing root node if any
	if (sandbox_root_node) {
		sandbox_viewport->remove_child(sandbox_root_node);
		sandbox_root_node->queue_free();
		sandbox_root_node = nullptr;
	}

	// Set sandbox context for instantiation
	GDScriptSandboxInstance *prev_sandbox = current_sandbox;
	current_sandbox = this;

	sandbox_root_node = p_scene->instantiate();

	current_sandbox = prev_sandbox;

	if (sandbox_root_node == nullptr) {
		ERR_FAIL_V_MSG(ERR_CANT_CREATE,
				"Failed to instantiate scene.");
	}

	sandbox_viewport->add_child(sandbox_root_node);

	return OK;
}

void GDScriptSandboxInstance::start() {
	ERR_FAIL_COND_MSG(sandbox_viewport == nullptr,
			"Sandbox not initialized. Call initialize() first.");
	ERR_FAIL_COND_MSG(state == STATE_RUNNING,
			"Sandbox is already running.");

	state = STATE_RUNNING;

	// Enable processing on viewport
	sandbox_viewport->set_process_mode(Node::PROCESS_MODE_INHERIT);

	emit_signal("sandbox_started");
}

void GDScriptSandboxInstance::stop() {
	if (state == STATE_STOPPED) {
		return;
	}

	_cleanup();

	emit_signal("sandbox_stopped");
}

void GDScriptSandboxInstance::pause() {
	ERR_FAIL_COND_MSG(state != STATE_RUNNING,
			"Sandbox is not running.");

	state = STATE_PAUSED;

	if (sandbox_viewport) {
		sandbox_viewport->set_process_mode(Node::PROCESS_MODE_DISABLED);
	}

	emit_signal("sandbox_paused");
}

void GDScriptSandboxInstance::resume() {
	ERR_FAIL_COND_MSG(state != STATE_PAUSED,
			"Sandbox is not paused.");

	state = STATE_RUNNING;

	if (sandbox_viewport) {
		sandbox_viewport->set_process_mode(Node::PROCESS_MODE_INHERIT);
	}

	emit_signal("sandbox_resumed");
}

void GDScriptSandboxInstance::set_sandbox_global(const StringName &p_name, const Variant &p_value) {
	sandbox_globals[p_name] = p_value;

	// Also update indexed access
	if (!sandbox_global_indices.has(p_name)) {
		sandbox_global_indices[p_name] = sandbox_global_array.size();
		sandbox_global_array.push_back(p_value);
	} else {
		sandbox_global_array.write[sandbox_global_indices[p_name]] = p_value;
	}
}

Variant GDScriptSandboxInstance::get_sandbox_global(const StringName &p_name) const {
	if (sandbox_globals.has(p_name)) {
		return sandbox_globals[p_name];
	}
	return Variant();
}

bool GDScriptSandboxInstance::has_sandbox_global(const StringName &p_name) const {
	return sandbox_globals.has(p_name);
}

void GDScriptSandboxInstance::remove_sandbox_global(const StringName &p_name) {
	sandbox_globals.erase(p_name);
	// Note: We don't remove from sandbox_global_indices/array to avoid
	// invalidating existing indices. The value becomes null.
	if (sandbox_global_indices.has(p_name)) {
		sandbox_global_array.write[sandbox_global_indices[p_name]] = Variant();
	}
}

void GDScriptSandboxInstance::block_class(const StringName &p_class) {
	blocked_classes.insert(p_class);
}

void GDScriptSandboxInstance::unblock_class(const StringName &p_class) {
	blocked_classes.erase(p_class);
}

void GDScriptSandboxInstance::block_method(const StringName &p_class, const StringName &p_method) {
	if (!blocked_methods.has(p_class)) {
		blocked_methods.insert(p_class, HashSet<StringName>());
	}
	blocked_methods[p_class].insert(p_method);
}

void GDScriptSandboxInstance::unblock_method(const StringName &p_class, const StringName &p_method) {
	if (blocked_methods.has(p_class)) {
		blocked_methods[p_class].erase(p_method);
		if (blocked_methods[p_class].is_empty()) {
			blocked_methods.erase(p_class);
		}
	}
}

void GDScriptSandboxInstance::block_property(const StringName &p_class, const StringName &p_property) {
	if (!blocked_properties.has(p_class)) {
		blocked_properties.insert(p_class, HashSet<StringName>());
	}
	blocked_properties[p_class].insert(p_property);
}

void GDScriptSandboxInstance::unblock_property(const StringName &p_class, const StringName &p_property) {
	if (blocked_properties.has(p_class)) {
		blocked_properties[p_class].erase(p_property);
		if (blocked_properties[p_class].is_empty()) {
			blocked_properties.erase(p_class);
		}
	}
}

void GDScriptSandboxInstance::set_allowed_classes(const TypedArray<StringName> &p_classes) {
	allowed_classes.clear();
	for (int i = 0; i < p_classes.size(); i++) {
		allowed_classes.insert(p_classes[i]);
	}
}

void GDScriptSandboxInstance::clear_allowed_classes() {
	allowed_classes.clear();
}

bool GDScriptSandboxInstance::is_class_allowed(const StringName &p_class) const {
	// Whitelist mode: if allowed_classes is not empty
	if (!allowed_classes.is_empty()) {
		// In whitelist mode, check if the class itself or any of its parents is in allowed list
		StringName current_class = p_class;
		while (current_class != StringName()) {
			if (allowed_classes.has(current_class)) {
				return true;
			}
			current_class = ClassDB::get_parent_class(current_class);
		}
		return false;
	}

	// Blacklist mode: check if the class or any of its parents is blocked
	StringName current_class = p_class;
	while (current_class != StringName()) {
		if (blocked_classes.has(current_class)) {
			return false; // Class or one of its parents is blocked
		}
		current_class = ClassDB::get_parent_class(current_class);
	}

	return true;
}

bool GDScriptSandboxInstance::is_method_allowed(const StringName &p_class, const StringName &p_method) const {
	// First check if class is allowed (includes inheritance awareness)
	if (!is_class_allowed(p_class)) {
		return false;
	}

	// Check method blacklist along the inheritance chain
	StringName current_class = p_class;
	while (current_class != StringName()) {
		if (blocked_methods.has(current_class)) {
			if (blocked_methods[current_class].has(p_method)) {
				return false; // Method is blocked on this class or a parent
			}
		}
		current_class = ClassDB::get_parent_class(current_class);
	}

	return true;
}

bool GDScriptSandboxInstance::is_property_allowed(const StringName &p_class, const StringName &p_property) const {
	// First check if class is allowed (includes inheritance awareness)
	if (!is_class_allowed(p_class)) {
		return false;
	}

	// Check property blacklist along the inheritance chain
	StringName current_class = p_class;
	while (current_class != StringName()) {
		if (blocked_properties.has(current_class)) {
			if (blocked_properties[current_class].has(p_property)) {
				return false; // Property is blocked on this class or a parent
			}
		}
		current_class = ClassDB::get_parent_class(current_class);
	}

	return true;
}

bool GDScriptSandboxInstance::is_api_allowed(const StringName &p_class, const StringName &p_method) const {
	return is_method_allowed(p_class, p_method);
}

void GDScriptSandboxInstance::send_message(const StringName &p_message, const Array &p_args) {
	ERR_FAIL_COND_MSG(state != STATE_RUNNING,
			"Sandbox is not running.");
	ERR_FAIL_COND_MSG(sandbox_root_node == nullptr,
			"Sandbox has no root node.");

	// Broadcast message to all nodes in sandbox that have the _on_host_message method
	Array args;
	args.push_back(p_message);
	args.push_back(p_args);
	sandbox_root_node->propagate_call("_on_host_message", args, false);
}

void GDScriptSandboxInstance::emit_to_host(const StringName &p_message, const Array &p_args) {
	emit_signal("message_received", p_message, p_args);
}

SubViewport *GDScriptSandboxInstance::get_viewport() const {
	return sandbox_viewport;
}

Node *GDScriptSandboxInstance::get_root_node() const {
	return sandbox_root_node;
}

String GDScriptSandboxInstance::get_sandbox_root_path() const {
	return sandbox_root_path;
}

GDScriptSandboxInstance::SandboxState GDScriptSandboxInstance::get_state() const {
	return state;
}

StringName GDScriptSandboxInstance::get_sandbox_id() const {
	return sandbox_id;
}

bool GDScriptSandboxInstance::is_running() const {
	return state == STATE_RUNNING;
}

bool GDScriptSandboxInstance::is_paused() const {
	return state == STATE_PAUSED;
}

Ref<Resource> GDScriptSandboxInstance::load_resource(const String &p_path, const String &p_type_hint) {
	if (!_is_path_allowed(p_path)) {
		ERR_FAIL_V_MSG(Ref<Resource>(),
				vformat("Resource path is not allowed in sandbox: %s", p_path));
	}

	String actual_path = _resolve_sandbox_path(p_path);

	// Set sandbox context for loading
	GDScriptSandboxInstance *prev_sandbox = current_sandbox;
	current_sandbox = this;

	Ref<Resource> res = ResourceLoader::load(actual_path, p_type_hint);

	current_sandbox = prev_sandbox;

	return res;
}

bool GDScriptSandboxInstance::resource_exists(const String &p_path) const {
	if (!_is_path_allowed(p_path)) {
		return false;
	}

	String actual_path = _resolve_sandbox_path(p_path);
	return FileAccess::exists(actual_path);
}

GDScriptSandboxInstance::GDScriptSandboxInstance() {
	sandbox_id = StringName(vformat("sandbox_%d", sandbox_id_counter++));
}

GDScriptSandboxInstance::~GDScriptSandboxInstance() {
	_cleanup();
}
