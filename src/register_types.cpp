#include "godot_oak/oak_device.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_godot_oak(ModuleInitializationLevel level) {
    if(level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    ClassDB::register_class<godot_oak::OakDevice>();
}

void uninitialize_godot_oak(ModuleInitializationLevel level) {
    if(level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
}

extern "C" {
GDExtensionBool GDE_EXPORT godot_oak_library_init(
    GDExtensionInterfaceGetProcAddress get_proc_address,
    GDExtensionClassLibraryPtr library,
    GDExtensionInitialization* initialization
) {
    GDExtensionBinding::InitObject init_obj(get_proc_address, library, initialization);
    init_obj.register_initializer(initialize_godot_oak);
    init_obj.register_terminator(uninitialize_godot_oak);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}
}
