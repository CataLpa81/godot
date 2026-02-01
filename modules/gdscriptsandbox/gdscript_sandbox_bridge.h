/**************************************************************************/
/*  gdscript_sandbox_bridge.h                                             */
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

#ifndef GDSCRIPT_SANDBOX_BRIDGE_H
#define GDSCRIPT_SANDBOX_BRIDGE_H

#include "core/io/resource.h"
#include "core/object/object.h"
#include "core/variant/variant.h"

class GDScriptSandboxInstance;

// GDScriptSandboxBridge provides an interface for sandbox scripts to communicate with the host
// This class is registered as a singleton within the sandbox context
class GDScriptSandboxBridge : public Object {
	GDCLASS(GDScriptSandboxBridge, Object);

private:
	static GDScriptSandboxBridge *singleton;

protected:
	static void _bind_methods();

public:
	static GDScriptSandboxBridge *get_singleton();

	// Send a message to the host application
	void send_to_host(const StringName &p_message, const Array &p_args = Array());

	// Get information about the current sandbox
	Dictionary get_sandbox_info() const;

	// Check if currently running in a sandbox
	bool is_in_sandbox() const;

	// Get the sandbox root path (useful for relative resource loading)
	String get_sandbox_root() const;

	// Load a resource within the sandbox context (safe wrapper)
	Ref<Resource> load_resource(const String &p_path, const String &p_type_hint = "");

	// Check if a resource exists in the sandbox
	bool resource_exists(const String &p_path) const;

	// Get/set sandbox-local global variables
	void set_global(const StringName &p_name, const Variant &p_value);
	Variant get_global(const StringName &p_name) const;
	bool has_global(const StringName &p_name) const;

	GDScriptSandboxBridge();
	~GDScriptSandboxBridge();
};

#endif // GDSCRIPT_SANDBOX_BRIDGE_H
