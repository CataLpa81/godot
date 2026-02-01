/**************************************************************************/
/*  gdscript_sandbox_instance.h                                           */
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

#ifndef GDSCRIPT_SANDBOX_INSTANCE_H
#define GDSCRIPT_SANDBOX_INSTANCE_H

#include "core/io/resource.h"
#include "core/object/ref_counted.h"
#include "core/string/string_name.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/variant.h"

class SubViewport;
class Node;
class PackedScene;

class GDScriptSandboxInstance : public RefCounted {
	GDCLASS(GDScriptSandboxInstance, RefCounted);

public:
	enum SandboxState {
		STATE_STOPPED,
		STATE_RUNNING,
		STATE_PAUSED,
	};

private:
	// Sandbox identification
	StringName sandbox_id;
	static uint32_t sandbox_id_counter;

	// Isolated global variables space
	HashMap<StringName, Variant> sandbox_globals;
	HashMap<StringName, int> sandbox_global_indices;
	Vector<Variant> sandbox_global_array;

	// Sandbox viewport (rendering isolation)
	SubViewport *sandbox_viewport = nullptr;
	Node *sandbox_root_node = nullptr;

	// Resource root path
	String sandbox_root_path;

	// API whitelist/blacklist
	HashSet<StringName> blocked_classes;
	HashMap<StringName, HashSet<StringName>> blocked_methods;
	HashMap<StringName, HashSet<StringName>> blocked_properties; // Property blacklist
	HashSet<StringName> allowed_classes; // If not empty, only these classes are allowed

	// Sandbox state
	SandboxState state = STATE_STOPPED;

	// Internal methods
	void _setup_default_blocked_apis();
	String _resolve_sandbox_path(const String &p_path) const;
	bool _is_path_allowed(const String &p_path) const;
	void _cleanup();

protected:
	static void _bind_methods();

public:
	// Thread-local current sandbox context
	static thread_local GDScriptSandboxInstance *current_sandbox;

	// Static context management
	static void set_current_sandbox(GDScriptSandboxInstance *p_sandbox);
	static GDScriptSandboxInstance *get_current_sandbox();

	// Static callbacks for Object and ClassDB hooks
	static bool _sandbox_api_check(Object *p_object, const StringName &p_method);
	static bool _sandbox_class_check(const StringName &p_class);

	// Static callback for GDScript named global access
	// Returns true if the global is handled by sandbox and sets r_value, false to use host globals
	static bool _sandbox_global_callback(const StringName &p_name, Variant &r_value);

	// Static callback for property access check
	// Returns true if access is allowed, false to block
	static bool _sandbox_property_check(Object *p_object, const StringName &p_property, bool p_is_set);

	// Static callback for GDScript load() interception
	// Returns true if handled by sandbox (r_resource is set), false to use default loading
	static bool _sandbox_load_callback(const String &p_path, Ref<Resource> &r_resource);

	// Initialization
	Error initialize(const String &p_sandbox_root_path);

	// Scene management
	Error load_scene(const String &p_scene_path);
	Error load_packed_scene(const Ref<PackedScene> &p_scene);

	// Lifecycle
	void start();
	void stop();
	void pause();
	void resume();

	// Global variable management for sandbox
	void set_sandbox_global(const StringName &p_name, const Variant &p_value);
	Variant get_sandbox_global(const StringName &p_name) const;
	bool has_sandbox_global(const StringName &p_name) const;
	void remove_sandbox_global(const StringName &p_name);

	// API access control - classes
	void block_class(const StringName &p_class);
	void unblock_class(const StringName &p_class);
	void set_allowed_classes(const TypedArray<StringName> &p_classes);
	void clear_allowed_classes();
	bool is_class_allowed(const StringName &p_class) const;

	// API access control - methods
	void block_method(const StringName &p_class, const StringName &p_method);
	void unblock_method(const StringName &p_class, const StringName &p_method);
	bool is_method_allowed(const StringName &p_class, const StringName &p_method) const;

	// API access control - properties
	void block_property(const StringName &p_class, const StringName &p_property);
	void unblock_property(const StringName &p_class, const StringName &p_property);
	bool is_property_allowed(const StringName &p_class, const StringName &p_property) const;

	// Combined API check
	bool is_api_allowed(const StringName &p_class, const StringName &p_method) const;

	// Host-Sandbox communication
	void send_message(const StringName &p_message, const Array &p_args);
	void emit_to_host(const StringName &p_message, const Array &p_args);

	// Getters
	SubViewport *get_viewport() const;
	Node *get_root_node() const;
	String get_sandbox_root_path() const;
	SandboxState get_state() const;
	StringName get_sandbox_id() const;
	bool is_running() const;
	bool is_paused() const;

	// Resource loading (sandbox-aware)
	Ref<Resource> load_resource(const String &p_path, const String &p_type_hint = "");
	bool resource_exists(const String &p_path) const;

	GDScriptSandboxInstance();
	~GDScriptSandboxInstance();
};

VARIANT_ENUM_CAST(GDScriptSandboxInstance::SandboxState);

#endif // GDSCRIPT_SANDBOX_INSTANCE_H
