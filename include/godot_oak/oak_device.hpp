#pragma once

#include "godot_oak/oak_stream_config.hpp"

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <memory>

namespace godot_oak {

class OakDevice final : public godot::Node {
    GDCLASS(OakDevice, godot::Node)

public:
    OakDevice();
    ~OakDevice() override;

    void _ready() override;

    bool open();
    bool start_rgb(const godot::Ref<OakStreamConfig>& config);
    bool start_rgb_values(int width = 640, int height = 360, int fps = 30);
    void stop();
    void close();

    [[nodiscard]] bool is_open() const;
    [[nodiscard]] bool is_streaming() const;

    godot::Ref<godot::Texture2D> get_texture();

    void set_rgb_config(const godot::Ref<OakStreamConfig>& config);
    [[nodiscard]] godot::Ref<OakStreamConfig> get_rgb_config() const;

    void set_auto_open(bool enabled);
    [[nodiscard]] bool get_auto_open() const;

    void set_auto_start_rgb(bool enabled);
    [[nodiscard]] bool get_auto_start_rgb() const;

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

    godot::Ref<OakStreamConfig> rgb_config_;
    bool auto_open_ = true;
    bool auto_start_rgb_ = true;
};

} // namespace godot_oak
