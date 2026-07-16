#pragma once

#include "godot_oak/oak_stream_config.hpp"

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
    bool start_streams();
    bool start_rgb(const godot::Ref<OakStreamConfig>& config);
    bool start_left_ir(const godot::Ref<OakStreamConfig>& config);
    bool start_right_ir(const godot::Ref<OakStreamConfig>& config);
    void stop();
    void close();

    [[nodiscard]] bool is_open() const;
    [[nodiscard]] bool is_streaming() const;

    godot::Ref<godot::Texture2D> get_rgb_texture();
    godot::Ref<godot::Texture2D> get_left_ir_texture();
    godot::Ref<godot::Texture2D> get_right_ir_texture();
    bool update_stereo_ir_textures();

    void set_rgb_config(const godot::Ref<OakStreamConfig>& config);
    [[nodiscard]] godot::Ref<OakStreamConfig> get_rgb_config() const;

    void set_left_ir_config(const godot::Ref<OakStreamConfig>& config);
    [[nodiscard]] godot::Ref<OakStreamConfig> get_left_ir_config() const;

    void set_right_ir_config(const godot::Ref<OakStreamConfig>& config);
    [[nodiscard]] godot::Ref<OakStreamConfig> get_right_ir_config() const;

    void set_auto_open(bool enabled);
    [[nodiscard]] bool get_auto_open() const;

    void set_auto_start_rgb(bool enabled);
    [[nodiscard]] bool get_auto_start_rgb() const;

    void set_auto_start_left_ir(bool enabled);
    [[nodiscard]] bool get_auto_start_left_ir() const;

    void set_auto_start_right_ir(bool enabled);
    [[nodiscard]] bool get_auto_start_right_ir() const;

    [[nodiscard]] godot::String get_last_error() const;
    [[nodiscard]] int64_t get_rgb_frame_count() const;
    [[nodiscard]] int64_t get_left_ir_frame_count() const;
    [[nodiscard]] int64_t get_right_ir_frame_count() const;

    [[nodiscard]] int64_t get_rgb_presented_count() const;
    [[nodiscard]] int64_t get_left_ir_presented_count() const;
    [[nodiscard]] int64_t get_right_ir_presented_count() const;

    [[nodiscard]] int64_t get_rgb_dropped_count() const;
    [[nodiscard]] int64_t get_left_ir_dropped_count() const;
    [[nodiscard]] int64_t get_right_ir_dropped_count() const;

    [[nodiscard]] double get_rgb_host_latency_ms() const;
    [[nodiscard]] double get_left_ir_host_latency_ms() const;
    [[nodiscard]] double get_right_ir_host_latency_ms() const;

    [[nodiscard]] double get_rgb_texture_update_ms() const;
    [[nodiscard]] double get_left_ir_texture_update_ms() const;
    [[nodiscard]] double get_right_ir_texture_update_ms() const;

    [[nodiscard]] double get_rgb_capture_to_host_ms() const;
    [[nodiscard]] double get_left_ir_capture_to_host_ms() const;
    [[nodiscard]] double get_right_ir_capture_to_host_ms() const;
    [[nodiscard]] double get_stereo_timestamp_skew_ms() const;
    [[nodiscard]] int64_t get_stereo_pair_count() const;
    [[nodiscard]] int64_t get_stereo_mismatch_count() const;

protected:
    static void _bind_methods();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    godot::Ref<OakStreamConfig> rgb_config_;
    godot::Ref<OakStreamConfig> left_ir_config_;
    godot::Ref<OakStreamConfig> right_ir_config_;

    bool auto_open_ = true;
    bool auto_start_rgb_ = true;
    bool auto_start_left_ir_ = true;
    bool auto_start_right_ir_ = true;
};

} // namespace godot_oak
