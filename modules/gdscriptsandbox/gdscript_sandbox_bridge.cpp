/**************************************************************************/
/*  gdscript_sandbox_bridge.cpp                                           */
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

#include "gdscript_sandbox_bridge.h"

#include "gdscript_sandbox_instance.h"

GDScriptSandboxBridge *GDScriptSandboxBridge::singleton = nullptr;

GDScriptSandboxBridge *GDScriptSandboxBridge::get_singleton() {
	return singleton;
}

void GDScriptSandboxBridge::_bind_methods() {
	ClassDB::bind_method(D_METHOD("send_to_host", "message", "args"), &GDScriptSandboxBridge::send_to_host, DEFVAL(Array()));
	ClassDB::bind_method(D_METHOD("get_sandbox_info"), &GDScriptSandboxBridge::get_sandbox_info);
	ClassDB::bind_method(D_METHOD("is_in_sandbox"), &GDScriptSandboxBridge::is_in_sandbox);
	ClassDB::bind_method(D_METHOD("get_sandbox_root"), &GDScriptSandboxBridge::get_sandbox_root);
	ClassDB::bind_method(D_METHOD("load_resource", "path", "type_hint"), &GDScriptSandboxBridge::load_resource, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("resource_exists", "path"), &GDScriptSandboxBridge::resource_exists);
	ClassDB::bind_method(D_METHOD("set_global", "name", "value"), &GDScriptSandboxBridge::set_global);
	ClassDB::bind_method(D_METHOD("get_global", "name"), &GDScriptSandboxBridge::get_global);
	ClassDB::bind_method(D_METHOD("has_global", "name"), &GDScriptSandboxBridge::has_global);
}

void GDScriptSandboxBridge::send_to_host(const StringName &p_message, const Array &p_args) {
	GDScriptSandboxInstance *sandbox = GDScriptSandboxInstance::get_current_sandbox();
	if (sandbox) {
		sandbox->emit_to_host(p_message, p_args);
	} else {
		ERR_PRINT("GDScriptSandboxBridge::send_to_host called outside of sandbox context.");
	}
}

Dictionary GDScriptSandboxBridge::get_sandbox_info() const {
	Dictionary info;

	GDScriptSandboxInstance *sandbox = GDScriptSandboxInstance::get_current_sandbox();
	if (sandbox) {
		info["id"] = sandbox->get_sandbox_id();
		info["root_path"] = sandbox->get_sandbox_root_path();
		info["state"] = sandbox->get_state();
		info["is_running"] = sandbox->is_running();
		info["is_paused"] = sandbox->is_paused();
	} else {
		info["id"] = "";
		info["root_path"] = "";
		info["state"] = GDScriptSandboxInstance::STATE_STOPPED;
		info["is_running"] = false;
		info["is_paused"] = false;
	}

	return info;
}

bool GDScriptSandboxBridge::is_in_sandbox() const {
	return GDScriptSandboxInstance::get_current_sandbox() != nullptr;
}

String GDScriptSandboxBridge::get_sandbox_root() const {
	GDScriptSandboxInstance *sandbox = GDScriptSandboxInstance::get_current_sandbox();
	if (sandbox) {
		return sandbox->get_sandbox_root_path();
	}
	return "";
}

Ref<Resource> GDScriptSandboxBridge::load_resource(const String &p_path, const String &p_type_hint) {
	GDScriptSandboxInstance *sandbox = GDScriptSandboxInstance::get_current_sandbox();
	if (sandbox) {
		return sandbox->load_resource(p_path, p_type_hint);
	}

	ERR_FAIL_V_MSG(Ref<Resource>(),
			"GDScriptSandboxBridge::load_resource called outside of sandbox context.");
}

bool GDScriptSandboxBridge::resource_exists(const String &p_path) const {
	GDScriptSandboxInstance *sandbox = GDScriptSandboxInstance::get_current_sandbox();
	if (sandbox) {
		return sandbox->resource_exists(p_path);
	}
	return false;
}

void GDScriptSandboxBridge::set_global(const StringName &p_name, const Variant &p_value) {
	GDScriptSandboxInstance *sandbox = GDScriptSandboxInstance::get_current_sandbox();
	if (sandbox) {
		sandbox->set_sandbox_global(p_name, p_value);
	} else {
		ERR_PRINT("GDScriptSandboxBridge::set_global called outside of sandbox context.");
	}
}

Variant GDScriptSandboxBridge::get_global(const StringName &p_name) const {
	GDScriptSandboxInstance *sandbox = GDScriptSandboxInstance::get_current_sandbox();
	if (sandbox) {
		return sandbox->get_sandbox_global(p_name);
	}
	return Variant();
}

bool GDScriptSandboxBridge::has_global(const StringName &p_name) const {
	GDScriptSandboxInstance *sandbox = GDScriptSandboxInstance::get_current_sandbox();
	if (sandbox) {
		return sandbox->has_sandbox_global(p_name);
	}
	return false;
}

GDScriptSandboxBridge::GDScriptSandboxBridge() {
	singleton = this;
}

GDScriptSandboxBridge::~GDScriptSandboxBridge() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
