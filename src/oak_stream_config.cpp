#include "godot_oak/oak_stream_config.hpp"

namespace godot_oak {

void OakStreamConfig::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("set_width", "value"), &OakStreamConfig::set_width);
    godot::ClassDB::bind_method(godot::D_METHOD("get_width"), &OakStreamConfig::get_width);
    godot::ClassDB::bind_method(godot::D_METHOD("set_height", "value"), &OakStreamConfig::set_height);
    godot::ClassDB::bind_method(godot::D_METHOD("get_height"), &OakStreamConfig::get_height);
    godot::ClassDB::bind_method(godot::D_METHOD("set_fps", "value"), &OakStreamConfig::set_fps);
    godot::ClassDB::bind_method(godot::D_METHOD("get_fps"), &OakStreamConfig::get_fps);

    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::INT, "width", godot::PROPERTY_HINT_RANGE, "1,7680,1"), "set_width", "get_width");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::INT, "height", godot::PROPERTY_HINT_RANGE, "1,4320,1"), "set_height", "get_height");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::INT, "fps", godot::PROPERTY_HINT_RANGE, "1,240,1"), "set_fps", "get_fps");
}

void OakStreamConfig::set_width(int value) { width_ = value; }
int OakStreamConfig::get_width() const { return width_; }

void OakStreamConfig::set_height(int value) { height_ = value; }
int OakStreamConfig::get_height() const { return height_; }

void OakStreamConfig::set_fps(int value) { fps_ = value; }
int OakStreamConfig::get_fps() const { return fps_; }

} // namespace godot_oak
