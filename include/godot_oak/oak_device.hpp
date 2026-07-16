#pragma once

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace godot_oak {

class OakDevice final : public godot::RefCounted {
    GDCLASS(OakDevice, godot::RefCounted)

public:
    OakDevice();
    ~OakDevice() override;

    bool open();
    bool start_rgb(int width = 640, int height = 360, int fps = 30);
    void stop();
    void close();

    bool is_open() const;
    bool is_streaming() const;
    godot::Ref<godot::ImageTexture> get_texture();
    godot::String get_last_error() const;
    int64_t get_frame_count() const;

protected:
    static void _bind_methods();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace godot_oak
