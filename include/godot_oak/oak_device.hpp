#pragma once

#include "godot_oak/oak_stream_config.hpp"

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <cstdint>
#include <memory>

namespace godot_oak {

class OakDevice final : public godot::RefCounted {
    GDCLASS(OakDevice, godot::RefCounted)

public:
    OakDevice();
    ~OakDevice() override;

    bool open();
    bool start_rgb(const godot::Ref<OakStreamConfig>& config);
    bool start_rgb_values(int width = 640, int height = 360, int fps = 30);
    void stop();
    void close();

    [[nodiscard]] bool is_open() const;
    [[nodiscard]] bool is_streaming() const;
    godot::Ref<godot::ImageTexture> get_texture();
    [[nodiscard]] godot::String get_last_error() const;
    [[nodiscard]] int64_t get_frame_count() const;
    [[nodiscard]] int get_active_width() const;
    [[nodiscard]] int get_active_height() const;
    [[nodiscard]] int get_active_fps() const;

protected:
    static void _bind_methods();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace godot_oak
