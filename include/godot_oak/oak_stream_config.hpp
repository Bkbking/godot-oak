#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot_oak {

class OakStreamConfig final : public godot::Resource {
    GDCLASS(OakStreamConfig, godot::Resource)

public:
    void set_width(int value);
    [[nodiscard]] int get_width() const;

    void set_height(int value);
    [[nodiscard]] int get_height() const;

    void set_fps(int value);
    [[nodiscard]] int get_fps() const;

protected:
    static void _bind_methods();

private:
    int width_ = 640;
    int height_ = 360;
    int fps_ = 30;
};

} // namespace godot_oak
