/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/object/object.h"
#include "gdscript_sandbox_bridge.h"
#include "gdscript_sandbox_instance.h"
#include "modules/gdscript/gdscript.h"

static GDScriptSandboxBridge *gdscript_sandbox_bridge = nullptr;

void initialize_gdscriptsandbox_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(GDScriptSandboxInstance);
	GDREGISTER_CLASS(GDScriptSandboxBridge);

	// Register sandbox API check callbacks
	Object::set_sandbox_api_check_callback(&GDScriptSandboxInstance::_sandbox_api_check);
	ClassDB::set_sandbox_class_check_callback(&GDScriptSandboxInstance::_sandbox_class_check);
	GDScriptLanguage::set_sandbox_global_callback(&GDScriptSandboxInstance::_sandbox_global_callback);

	// Register sandbox property check callback
	Object::set_sandbox_property_check_callback(&GDScriptSandboxInstance::_sandbox_property_check);

	// Register sandbox load callback
	GDScriptLanguage::set_sandbox_load_callback(&GDScriptSandboxInstance::_sandbox_load_callback);

	// Register GDScriptSandboxBridge as a singleton
	gdscript_sandbox_bridge = memnew(GDScriptSandboxBridge);
	Engine::get_singleton()->add_singleton(Engine::Singleton("GDScriptSandboxBridge", GDScriptSandboxBridge::get_singleton()));
}

void uninitialize_gdscriptsandbox_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	// Clear sandbox API check callbacks
	Object::set_sandbox_api_check_callback(nullptr);
	ClassDB::set_sandbox_class_check_callback(nullptr);
	GDScriptLanguage::set_sandbox_global_callback(nullptr);

	// Clear sandbox property check callback
	Object::set_sandbox_property_check_callback(nullptr);

	// Clear sandbox load callback
	GDScriptLanguage::set_sandbox_load_callback(nullptr);

	// Remove singleton and cleanup
	if (gdscript_sandbox_bridge) {
		Engine::get_singleton()->remove_singleton("GDScriptSandboxBridge");
		memdelete(gdscript_sandbox_bridge);
		gdscript_sandbox_bridge = nullptr;
	}
}
