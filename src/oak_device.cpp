#include "godot_oak/oak_device.hpp"

#include <depthai/depthai.hpp>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace godot_oak {
namespace {

struct CpuFrame {
    mutable std::mutex mutex;
    std::vector<std::uint8_t> data;
    int width = 0;
    int height = 0;
    bool ready = false;
    std::chrono::steady_clock::time_point arrival_time{};
    std::chrono::steady_clock::time_point capture_time{};

    std::atomic<int64_t> count{0};
    std::atomic<int64_t> presented{0};
    std::atomic<int64_t> dropped{0};
    std::atomic<double> capture_to_host_ms{0.0};
    std::atomic<double> host_latency_ms{0.0};
    std::atomic<double> texture_update_ms{0.0};

    godot::Ref<godot::ImageTexture> texture;
};

godot::Ref<godot::Texture2D> update_texture(
    CpuFrame& frame,
    godot::Image::Format format
) {
    std::vector<std::uint8_t> data;
    int width = 0;
    int height = 0;
    std::chrono::steady_clock::time_point arrival_time;

    {
        std::scoped_lock lock(frame.mutex);
        if(!frame.ready) {
            return frame.texture;
        }

        // Latest-frame-wins: transfer ownership of the newest CPU buffer
        // instead of copying it into a second std::vector.
        data.swap(frame.data);
        width = frame.width;
        height = frame.height;
        arrival_time = frame.arrival_time;
        frame.ready = false;
    }

    const auto update_start = std::chrono::steady_clock::now();

    godot::PackedByteArray bytes;
    bytes.resize(static_cast<int64_t>(data.size()));
    std::memcpy(bytes.ptrw(), data.data(), data.size());

    const godot::Ref<godot::Image> image = godot::Image::create_from_data(
        width,
        height,
        false,
        format,
        bytes
    );

    if(frame.texture.is_null()) {
        frame.texture = godot::ImageTexture::create_from_image(image);
    } else {
        frame.texture->update(image);
    }

    const auto update_end = std::chrono::steady_clock::now();
    const double host_latency =
        std::chrono::duration<double, std::milli>(
            update_end - arrival_time
        ).count();
    const double texture_update =
        std::chrono::duration<double, std::milli>(
            update_end - update_start
        ).count();

    frame.host_latency_ms.store(host_latency);
    frame.texture_update_ms.store(texture_update);
    frame.presented.fetch_add(1);

    return frame.texture;
}

void configure_frame(CpuFrame& frame, int width, int height, int channels) {
    std::scoped_lock lock(frame.mutex);
    frame.width = width;
    frame.height = height;
    frame.ready = false;
    frame.count.store(0);
    frame.presented.store(0);
    frame.dropped.store(0);
    frame.capture_to_host_ms.store(0.0);
    frame.host_latency_ms.store(0.0);
    frame.texture_update_ms.store(0.0);
    frame.data.clear();
    frame.data.reserve(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) *
        static_cast<std::size_t>(channels)
    );
}

} // namespace

struct OakDevice::Impl {
    std::unique_ptr<dai::Pipeline> pipeline;

    std::shared_ptr<dai::MessageQueue> rgb_queue;
    std::shared_ptr<dai::MessageQueue> left_ir_queue;
    std::shared_ptr<dai::MessageQueue> right_ir_queue;

    std::thread rgb_worker;
    std::thread left_ir_worker;
    std::thread right_ir_worker;
    std::thread stereo_ir_worker;

    std::atomic_bool running{false};
    std::atomic_bool opened{false};

    CpuFrame rgb;
    CpuFrame left_ir;
    CpuFrame right_ir;

    std::atomic_bool stereo_pair_active{false};
    std::atomic<int64_t> stereo_pair_count{0};
    std::atomic<int64_t> stereo_mismatch_count{0};
    std::atomic<double> stereo_timestamp_skew_ms{0.0};

    mutable std::mutex error_mutex;
    std::string last_error;

    void set_error(std::string message) {
        std::scoped_lock lock(error_mutex);
        last_error = std::move(message);
    }
};

OakDevice::OakDevice() : impl_(std::make_unique<Impl>()) {
    rgb_config_.instantiate();
    rgb_config_->set_width(1280);
    rgb_config_->set_height(720);
    rgb_config_->set_fps(30);

    left_ir_config_.instantiate();
    left_ir_config_->set_width(640);
    left_ir_config_->set_height(400);
    left_ir_config_->set_fps(60);

    right_ir_config_.instantiate();
    right_ir_config_->set_width(640);
    right_ir_config_->set_height(400);
    right_ir_config_->set_fps(60);
}

OakDevice::~OakDevice() {
    close();
}

void OakDevice::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("open"), &OakDevice::open);
    godot::ClassDB::bind_method(
        godot::D_METHOD("start_streams"),
        &OakDevice::start_streams
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("start_rgb", "config"),
        &OakDevice::start_rgb
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("start_left_ir", "config"),
        &OakDevice::start_left_ir
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("start_right_ir", "config"),
        &OakDevice::start_right_ir
    );
    godot::ClassDB::bind_method(godot::D_METHOD("stop"), &OakDevice::stop);
    godot::ClassDB::bind_method(godot::D_METHOD("close"), &OakDevice::close);

    godot::ClassDB::bind_method(
        godot::D_METHOD("is_open"),
        &OakDevice::is_open
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("is_streaming"),
        &OakDevice::is_streaming
    );

    godot::ClassDB::bind_method(
        godot::D_METHOD("get_rgb_texture"),
        &OakDevice::get_rgb_texture
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_left_ir_texture"),
        &OakDevice::get_left_ir_texture
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_right_ir_texture"),
        &OakDevice::get_right_ir_texture
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("update_stereo_ir_textures"),
        &OakDevice::update_stereo_ir_textures
    );

    godot::ClassDB::bind_method(
        godot::D_METHOD("set_rgb_config", "config"),
        &OakDevice::set_rgb_config
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_rgb_config"),
        &OakDevice::get_rgb_config
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_left_ir_config", "config"),
        &OakDevice::set_left_ir_config
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_left_ir_config"),
        &OakDevice::get_left_ir_config
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_right_ir_config", "config"),
        &OakDevice::set_right_ir_config
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_right_ir_config"),
        &OakDevice::get_right_ir_config
    );

    godot::ClassDB::bind_method(
        godot::D_METHOD("set_auto_open", "enabled"),
        &OakDevice::set_auto_open
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_auto_open"),
        &OakDevice::get_auto_open
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_auto_start_rgb", "enabled"),
        &OakDevice::set_auto_start_rgb
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_auto_start_rgb"),
        &OakDevice::get_auto_start_rgb
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_auto_start_left_ir", "enabled"),
        &OakDevice::set_auto_start_left_ir
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_auto_start_left_ir"),
        &OakDevice::get_auto_start_left_ir
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_auto_start_right_ir", "enabled"),
        &OakDevice::set_auto_start_right_ir
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_auto_start_right_ir"),
        &OakDevice::get_auto_start_right_ir
    );

    godot::ClassDB::bind_method(
        godot::D_METHOD("get_last_error"),
        &OakDevice::get_last_error
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_rgb_frame_count"),
        &OakDevice::get_rgb_frame_count
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_left_ir_frame_count"),
        &OakDevice::get_left_ir_frame_count
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_right_ir_frame_count"),
        &OakDevice::get_right_ir_frame_count
    );

    godot::ClassDB::bind_method(
        godot::D_METHOD("get_rgb_presented_count"),
        &OakDevice::get_rgb_presented_count
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_left_ir_presented_count"),
        &OakDevice::get_left_ir_presented_count
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_right_ir_presented_count"),
        &OakDevice::get_right_ir_presented_count
    );

    godot::ClassDB::bind_method(
        godot::D_METHOD("get_rgb_dropped_count"),
        &OakDevice::get_rgb_dropped_count
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_left_ir_dropped_count"),
        &OakDevice::get_left_ir_dropped_count
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_right_ir_dropped_count"),
        &OakDevice::get_right_ir_dropped_count
    );

    godot::ClassDB::bind_method(
        godot::D_METHOD("get_rgb_host_latency_ms"),
        &OakDevice::get_rgb_host_latency_ms
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_left_ir_host_latency_ms"),
        &OakDevice::get_left_ir_host_latency_ms
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_right_ir_host_latency_ms"),
        &OakDevice::get_right_ir_host_latency_ms
    );

    godot::ClassDB::bind_method(
        godot::D_METHOD("get_rgb_texture_update_ms"),
        &OakDevice::get_rgb_texture_update_ms
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_left_ir_texture_update_ms"),
        &OakDevice::get_left_ir_texture_update_ms
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_right_ir_texture_update_ms"),
        &OakDevice::get_right_ir_texture_update_ms
    );

    godot::ClassDB::bind_method(
        godot::D_METHOD("get_rgb_capture_to_host_ms"),
        &OakDevice::get_rgb_capture_to_host_ms
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_left_ir_capture_to_host_ms"),
        &OakDevice::get_left_ir_capture_to_host_ms
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_right_ir_capture_to_host_ms"),
        &OakDevice::get_right_ir_capture_to_host_ms
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_stereo_timestamp_skew_ms"),
        &OakDevice::get_stereo_timestamp_skew_ms
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_stereo_pair_count"),
        &OakDevice::get_stereo_pair_count
    );
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_stereo_mismatch_count"),
        &OakDevice::get_stereo_mismatch_count
    );

    ADD_GROUP("Startup", "");
    ADD_PROPERTY(
        godot::PropertyInfo(godot::Variant::BOOL, "auto_open"),
        "set_auto_open",
        "get_auto_open"
    );
    ADD_PROPERTY(
        godot::PropertyInfo(godot::Variant::BOOL, "auto_start_rgb"),
        "set_auto_start_rgb",
        "get_auto_start_rgb"
    );
    ADD_PROPERTY(
        godot::PropertyInfo(godot::Variant::BOOL, "auto_start_left_ir"),
        "set_auto_start_left_ir",
        "get_auto_start_left_ir"
    );
    ADD_PROPERTY(
        godot::PropertyInfo(godot::Variant::BOOL, "auto_start_right_ir"),
        "set_auto_start_right_ir",
        "get_auto_start_right_ir"
    );

    ADD_GROUP("Streams", "");
    ADD_PROPERTY(
        godot::PropertyInfo(
            godot::Variant::OBJECT,
            "rgb_config",
            godot::PROPERTY_HINT_RESOURCE_TYPE,
            "OakStreamConfig"
        ),
        "set_rgb_config",
        "get_rgb_config"
    );
    ADD_PROPERTY(
        godot::PropertyInfo(
            godot::Variant::OBJECT,
            "left_ir_config",
            godot::PROPERTY_HINT_RESOURCE_TYPE,
            "OakStreamConfig"
        ),
        "set_left_ir_config",
        "get_left_ir_config"
    );
    ADD_PROPERTY(
        godot::PropertyInfo(
            godot::Variant::OBJECT,
            "right_ir_config",
            godot::PROPERTY_HINT_RESOURCE_TYPE,
            "OakStreamConfig"
        ),
        "set_right_ir_config",
        "get_right_ir_config"
    );
}

void OakDevice::_ready() {
    if(auto_open_ && !open()) {
        return;
    }

    if(auto_start_rgb_ || auto_start_left_ir_ || auto_start_right_ir_) {
        start_streams();
    }
}

bool OakDevice::open() {
    if(impl_->opened.load()) {
        return true;
    }

    try {
        const auto devices = dai::Device::getAllAvailableDevices();
        if(devices.empty()) {
            impl_->set_error("No se ha encontrado ninguna cámara OAK.");
            return false;
        }

        impl_->opened.store(true);
        impl_->set_error("");
        return true;
    } catch(const std::exception& error) {
        impl_->set_error(error.what());
        return false;
    }
}

bool OakDevice::start_rgb(const godot::Ref<OakStreamConfig>& config) {
    set_rgb_config(config);
    auto_start_rgb_ = true;
    auto_start_left_ir_ = false;
    auto_start_right_ir_ = false;
    return start_streams();
}

bool OakDevice::start_left_ir(const godot::Ref<OakStreamConfig>& config) {
    set_left_ir_config(config);
    auto_start_rgb_ = false;
    auto_start_left_ir_ = true;
    auto_start_right_ir_ = false;
    return start_streams();
}

bool OakDevice::start_right_ir(const godot::Ref<OakStreamConfig>& config) {
    set_right_ir_config(config);
    auto_start_rgb_ = false;
    auto_start_left_ir_ = false;
    auto_start_right_ir_ = true;
    return start_streams();
}

bool OakDevice::start_streams() {
    stop();

    impl_->stereo_pair_active.store(false);
    impl_->stereo_pair_count.store(0);
    impl_->stereo_mismatch_count.store(0);
    impl_->stereo_timestamp_skew_ms.store(0.0);

    if(!open()) {
        return false;
    }

    if(!auto_start_rgb_ && !auto_start_left_ir_ && !auto_start_right_ir_) {
        impl_->set_error("No hay ningún stream habilitado.");
        return false;
    }

    try {
        impl_->pipeline = std::make_unique<dai::Pipeline>();

        if(auto_start_rgb_) {
            const int width = rgb_config_->get_width();
            const int height = rgb_config_->get_height();
            const int fps = rgb_config_->get_fps();

            auto camera = impl_->pipeline
                              ->create<dai::node::Camera>()
                              ->build(dai::CameraBoardSocket::CAM_A);
            camera->setOutputsNumFramesPool(1);

            auto output = camera->requestOutput(
                std::make_pair(width, height),
                dai::ImgFrame::Type::RGB888i,
                dai::ImgResizeMode::LETTERBOX,
                static_cast<float>(fps),
                std::nullopt
            );

            impl_->rgb_queue = output->createOutputQueue(1, false);
            configure_frame(impl_->rgb, width, height, 3);
        }

        if(auto_start_left_ir_ && auto_start_right_ir_) {
            const int left_width = left_ir_config_->get_width();
            const int left_height = left_ir_config_->get_height();
            const int left_fps = left_ir_config_->get_fps();

            const int right_width = right_ir_config_->get_width();
            const int right_height = right_ir_config_->get_height();
            const int right_fps = right_ir_config_->get_fps();

            if(
                left_width != right_width ||
                left_height != right_height ||
                left_fps != right_fps
            ) {
                impl_->set_error(
                    "Los streams IR emparejados deben usar la misma resolución y FPS."
                );
                stop();
                return false;
            }

            // Low-latency preview path:
            // read both mono cameras directly. Do not route preview frames
            // through StereoDepth, rectification or dynamic calibration.
            auto left_camera = impl_->pipeline
                                   ->create<dai::node::Camera>()
                                   ->build(dai::CameraBoardSocket::CAM_B);
            auto right_camera = impl_->pipeline
                                    ->create<dai::node::Camera>()
                                    ->build(dai::CameraBoardSocket::CAM_C);

            left_camera->setOutputsNumFramesPool(1);
            right_camera->setOutputsNumFramesPool(1);

            dai::ImgFrameCapability left_capability;
            left_capability.size.fixed(
                std::make_pair(left_width, left_height)
            );
            left_capability.fps.fixed(static_cast<float>(left_fps));
            left_capability.type = dai::ImgFrame::Type::GRAY8;
            left_capability.resizeMode = dai::ImgResizeMode::CROP;

            dai::ImgFrameCapability right_capability;
            right_capability.size.fixed(
                std::make_pair(right_width, right_height)
            );
            right_capability.fps.fixed(static_cast<float>(right_fps));
            right_capability.type = dai::ImgFrame::Type::GRAY8;
            right_capability.resizeMode = dai::ImgResizeMode::CROP;

            auto left_output =
                left_camera->requestOutput(left_capability, false);
            auto right_output =
                right_camera->requestOutput(right_capability, false);

            impl_->left_ir_queue =
                left_output->createOutputQueue(1, false);
            impl_->right_ir_queue =
                right_output->createOutputQueue(1, false);
            impl_->stereo_pair_active.store(true);

            configure_frame(
                impl_->left_ir,
                left_width,
                left_height,
                1
            );
            configure_frame(
                impl_->right_ir,
                right_width,
                right_height,
                1
            );
        } else {
            if(auto_start_left_ir_) {
                const int width = left_ir_config_->get_width();
                const int height = left_ir_config_->get_height();
                const int fps = left_ir_config_->get_fps();

                auto camera = impl_->pipeline
                                  ->create<dai::node::Camera>()
                                  ->build(dai::CameraBoardSocket::CAM_B);
                camera->setOutputsNumFramesPool(1);

                auto output = camera->requestOutput(
                    std::make_pair(width, height),
                    dai::ImgFrame::Type::GRAY8,
                    dai::ImgResizeMode::LETTERBOX,
                    static_cast<float>(fps),
                    std::nullopt
                );

                impl_->left_ir_queue = output->createOutputQueue(1, false);
                configure_frame(impl_->left_ir, width, height, 1);
            }

            if(auto_start_right_ir_) {
                const int width = right_ir_config_->get_width();
                const int height = right_ir_config_->get_height();
                const int fps = right_ir_config_->get_fps();

                auto camera = impl_->pipeline
                                  ->create<dai::node::Camera>()
                                  ->build(dai::CameraBoardSocket::CAM_C);
                camera->setOutputsNumFramesPool(1);

                auto output = camera->requestOutput(
                    std::make_pair(width, height),
                    dai::ImgFrame::Type::GRAY8,
                    dai::ImgResizeMode::LETTERBOX,
                    static_cast<float>(fps),
                    std::nullopt
                );

                impl_->right_ir_queue = output->createOutputQueue(1, false);
                configure_frame(impl_->right_ir, width, height, 1);
            }
        }

        impl_->pipeline->start();
        impl_->running.store(true);
        impl_->set_error("");

        if(impl_->rgb_queue) {
            impl_->rgb_worker = std::thread([this]() {
                try {
                    while(impl_->running.load()) {
                        auto frame = impl_->rgb_queue->tryGet<dai::ImgFrame>();
                        if(!frame) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            continue;
                        }

                        const auto& rgb = frame->getData();
                        const std::size_t expected =
                            static_cast<std::size_t>(impl_->rgb.width) *
                            static_cast<std::size_t>(impl_->rgb.height) *
                            3U;

                        if(rgb.size() < expected) {
                            continue;
                        }

                        std::scoped_lock lock(impl_->rgb.mutex);
                        if(impl_->rgb.ready) {
                            impl_->rgb.dropped.fetch_add(1);
                        }

                        impl_->rgb.data.assign(
                            rgb.begin(),
                            rgb.begin() + static_cast<std::ptrdiff_t>(expected)
                        );
                        const auto arrival = std::chrono::steady_clock::now();
                        const auto capture = frame->getTimestamp();

                        impl_->rgb.arrival_time = arrival;
                        impl_->rgb.capture_time = capture;
                        impl_->rgb.capture_to_host_ms.store(
                            std::chrono::duration<double, std::milli>(
                                arrival - capture
                            ).count()
                        );
                        impl_->rgb.ready = true;
                        impl_->rgb.count.fetch_add(1);
                    }
                } catch(const std::exception& error) {
                    if(impl_->running.load()) {
                        impl_->set_error(error.what());
                    }
                }
            });
        }

        auto start_gray_worker = [this](
            std::shared_ptr<dai::MessageQueue> queue,
            CpuFrame& destination,
            std::thread& worker
        ) {
            if(!queue) {
                return;
            }

            worker = std::thread([this, queue, &destination]() {
                try {
                    while(impl_->running.load()) {
                        auto frame = queue->tryGet<dai::ImgFrame>();
                        if(!frame) {
                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(1)
                            );
                            continue;
                        }

                        const auto& gray = frame->getData();
                        const std::size_t expected =
                            static_cast<std::size_t>(destination.width) *
                            static_cast<std::size_t>(destination.height);

                        if(gray.size() < expected) {
                            continue;
                        }

                        const auto arrival = std::chrono::steady_clock::now();
                        const auto capture = frame->getTimestamp();

                        std::scoped_lock lock(destination.mutex);
                        if(destination.ready) {
                            destination.dropped.fetch_add(1);
                        }

                        destination.data.assign(
                            gray.begin(),
                            gray.begin() +
                                static_cast<std::ptrdiff_t>(expected)
                        );
                        destination.arrival_time = arrival;
                        destination.capture_time = capture;
                        destination.capture_to_host_ms.store(
                            std::chrono::duration<double, std::milli>(
                                arrival - capture
                            ).count()
                        );
                        destination.ready = true;
                        destination.count.fetch_add(1);
                    }
                } catch(const std::exception& error) {
                    if(impl_->running.load()) {
                        impl_->set_error(error.what());
                    }
                }
            });
        };

        if(impl_->stereo_pair_active.load()) {
            impl_->stereo_ir_worker = std::thread([this]() {
                std::shared_ptr<dai::ImgFrame> pending_left;
                std::shared_ptr<dai::ImgFrame> pending_right;

                try {
                    while(impl_->running.load()) {
                        if(!pending_left) {
                            pending_left =
                                impl_->left_ir_queue->tryGet<dai::ImgFrame>();
                        }
                        if(!pending_right) {
                            pending_right =
                                impl_->right_ir_queue->tryGet<dai::ImgFrame>();
                        }

                        if(!pending_left || !pending_right) {
                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(1)
                            );
                            continue;
                        }

                        const auto left_sequence =
                            pending_left->getSequenceNum();
                        const auto right_sequence =
                            pending_right->getSequenceNum();

                        if(left_sequence < right_sequence) {
                            impl_->left_ir.dropped.fetch_add(1);
                            impl_->stereo_mismatch_count.fetch_add(1);
                            pending_left.reset();
                            continue;
                        }
                        if(right_sequence < left_sequence) {
                            impl_->right_ir.dropped.fetch_add(1);
                            impl_->stereo_mismatch_count.fetch_add(1);
                            pending_right.reset();
                            continue;
                        }

                        const auto& left_data = pending_left->getData();
                        const auto& right_data = pending_right->getData();
                        const std::size_t left_expected =
                            static_cast<std::size_t>(impl_->left_ir.width) *
                            static_cast<std::size_t>(impl_->left_ir.height);
                        const std::size_t right_expected =
                            static_cast<std::size_t>(impl_->right_ir.width) *
                            static_cast<std::size_t>(impl_->right_ir.height);

                        if(
                            left_data.size() < left_expected ||
                            right_data.size() < right_expected
                        ) {
                            impl_->stereo_mismatch_count.fetch_add(1);
                            pending_left.reset();
                            pending_right.reset();
                            continue;
                        }

                        const auto arrival =
                            std::chrono::steady_clock::now();
                        const auto left_capture =
                            pending_left->getTimestamp();
                        const auto right_capture =
                            pending_right->getTimestamp();

                        const double skew_ms = std::abs(
                            std::chrono::duration<double, std::milli>(
                                left_capture - right_capture
                            ).count()
                        );

                        {
                            std::scoped_lock lock(
                                impl_->left_ir.mutex,
                                impl_->right_ir.mutex
                            );

                            if(
                                impl_->left_ir.ready ||
                                impl_->right_ir.ready
                            ) {
                                impl_->left_ir.dropped.fetch_add(1);
                                impl_->right_ir.dropped.fetch_add(1);
                            }

                            impl_->left_ir.data.assign(
                                left_data.begin(),
                                left_data.begin() +
                                    static_cast<std::ptrdiff_t>(
                                        left_expected
                                    )
                            );
                            impl_->right_ir.data.assign(
                                right_data.begin(),
                                right_data.begin() +
                                    static_cast<std::ptrdiff_t>(
                                        right_expected
                                    )
                            );

                            impl_->left_ir.arrival_time = arrival;
                            impl_->right_ir.arrival_time = arrival;
                            impl_->left_ir.capture_time = left_capture;
                            impl_->right_ir.capture_time = right_capture;
                            impl_->left_ir.capture_to_host_ms.store(
                                std::chrono::duration<double, std::milli>(
                                    arrival - left_capture
                                ).count()
                            );
                            impl_->right_ir.capture_to_host_ms.store(
                                std::chrono::duration<double, std::milli>(
                                    arrival - right_capture
                                ).count()
                            );

                            impl_->left_ir.ready = true;
                            impl_->right_ir.ready = true;
                            impl_->left_ir.count.fetch_add(1);
                            impl_->right_ir.count.fetch_add(1);
                        }

                        impl_->stereo_timestamp_skew_ms.store(skew_ms);
                        impl_->stereo_pair_count.fetch_add(1);

                        pending_left.reset();
                        pending_right.reset();
                    }
                } catch(const std::exception& error) {
                    if(impl_->running.load()) {
                        impl_->set_error(error.what());
                    }
                }
            });
        } else {
            start_gray_worker(
                impl_->left_ir_queue,
                impl_->left_ir,
                impl_->left_ir_worker
            );
            start_gray_worker(
                impl_->right_ir_queue,
                impl_->right_ir,
                impl_->right_ir_worker
            );
        }

        return true;
    } catch(const std::exception& error) {
        impl_->set_error(error.what());
        stop();
        return false;
    }
}

void OakDevice::stop() {
    // Los workers usan tryGet(), por lo que pueden salir sin cerrar las colas
    // mientras están bloqueados. Esto evita QueueException durante un apagado
    // normal o una reconstrucción del pipeline.
    impl_->running.store(false);

    if(impl_->rgb_worker.joinable()) {
        impl_->rgb_worker.join();
    }
    if(impl_->left_ir_worker.joinable()) {
        impl_->left_ir_worker.join();
    }
    if(impl_->right_ir_worker.joinable()) {
        impl_->right_ir_worker.join();
    }
    if(impl_->stereo_ir_worker.joinable()) {
        impl_->stereo_ir_worker.join();
    }

    if(impl_->pipeline) {
        try {
            impl_->pipeline->stop();
            impl_->pipeline->wait();
        } catch(...) {
            // El cierre del pipeline no debe sobrescribir el último error útil.
        }
    }

    impl_->rgb_queue.reset();
    impl_->left_ir_queue.reset();
    impl_->right_ir_queue.reset();
    impl_->stereo_pair_active.store(false);
    impl_->pipeline.reset();
}

void OakDevice::close() {
    stop();
    impl_->opened.store(false);
    impl_->rgb.texture.unref();
    impl_->left_ir.texture.unref();
    impl_->right_ir.texture.unref();
}

bool OakDevice::is_open() const {
    return impl_->opened.load();
}

bool OakDevice::is_streaming() const {
    return impl_->running.load();
}

bool OakDevice::update_stereo_ir_textures() {
    if(!impl_->stereo_pair_active.load()) {
        return false;
    }

    std::vector<std::uint8_t> left_data;
    std::vector<std::uint8_t> right_data;
    int left_width = 0;
    int left_height = 0;
    int right_width = 0;
    int right_height = 0;
    std::chrono::steady_clock::time_point left_arrival;
    std::chrono::steady_clock::time_point right_arrival;

    {
        std::scoped_lock lock(
            impl_->left_ir.mutex,
            impl_->right_ir.mutex
        );

        if(!impl_->left_ir.ready || !impl_->right_ir.ready) {
            return false;
        }

        left_data.swap(impl_->left_ir.data);
        right_data.swap(impl_->right_ir.data);
        left_width = impl_->left_ir.width;
        left_height = impl_->left_ir.height;
        right_width = impl_->right_ir.width;
        right_height = impl_->right_ir.height;
        left_arrival = impl_->left_ir.arrival_time;
        right_arrival = impl_->right_ir.arrival_time;
        impl_->left_ir.ready = false;
        impl_->right_ir.ready = false;
    }

    const auto update_start = std::chrono::steady_clock::now();

    auto update_one = [](
        CpuFrame& destination,
        const std::vector<std::uint8_t>& data,
        int width,
        int height
    ) {
        godot::PackedByteArray bytes;
        bytes.resize(static_cast<int64_t>(data.size()));
        std::memcpy(bytes.ptrw(), data.data(), data.size());

        const godot::Ref<godot::Image> image =
            godot::Image::create_from_data(
                width,
                height,
                false,
                godot::Image::FORMAT_L8,
                bytes
            );

        if(destination.texture.is_null()) {
            destination.texture =
                godot::ImageTexture::create_from_image(image);
        } else {
            destination.texture->update(image);
        }
    };

    update_one(impl_->left_ir, left_data, left_width, left_height);
    update_one(impl_->right_ir, right_data, right_width, right_height);

    const auto update_end = std::chrono::steady_clock::now();
    const double pair_update_ms =
        std::chrono::duration<double, std::milli>(
            update_end - update_start
        ).count();

    impl_->left_ir.host_latency_ms.store(
        std::chrono::duration<double, std::milli>(
            update_end - left_arrival
        ).count()
    );
    impl_->right_ir.host_latency_ms.store(
        std::chrono::duration<double, std::milli>(
            update_end - right_arrival
        ).count()
    );
    impl_->left_ir.texture_update_ms.store(pair_update_ms);
    impl_->right_ir.texture_update_ms.store(pair_update_ms);
    impl_->left_ir.presented.fetch_add(1);
    impl_->right_ir.presented.fetch_add(1);

    return true;
}

godot::Ref<godot::Texture2D> OakDevice::get_rgb_texture() {
    return update_texture(impl_->rgb, godot::Image::FORMAT_RGB8);
}

godot::Ref<godot::Texture2D> OakDevice::get_left_ir_texture() {
    if(impl_->stereo_pair_active.load()) {
        return impl_->left_ir.texture;
    }
    return update_texture(impl_->left_ir, godot::Image::FORMAT_L8);
}

godot::Ref<godot::Texture2D> OakDevice::get_right_ir_texture() {
    if(impl_->stereo_pair_active.load()) {
        return impl_->right_ir.texture;
    }
    return update_texture(impl_->right_ir, godot::Image::FORMAT_L8);
}

void OakDevice::set_rgb_config(const godot::Ref<OakStreamConfig>& config) {
    if(config.is_valid()) {
        rgb_config_ = config;
    }
}

godot::Ref<OakStreamConfig> OakDevice::get_rgb_config() const {
    return rgb_config_;
}

void OakDevice::set_left_ir_config(
    const godot::Ref<OakStreamConfig>& config
) {
    if(config.is_valid()) {
        left_ir_config_ = config;
    }
}

godot::Ref<OakStreamConfig> OakDevice::get_left_ir_config() const {
    return left_ir_config_;
}

void OakDevice::set_right_ir_config(
    const godot::Ref<OakStreamConfig>& config
) {
    if(config.is_valid()) {
        right_ir_config_ = config;
    }
}

godot::Ref<OakStreamConfig> OakDevice::get_right_ir_config() const {
    return right_ir_config_;
}

void OakDevice::set_auto_open(bool enabled) {
    auto_open_ = enabled;
}

bool OakDevice::get_auto_open() const {
    return auto_open_;
}

void OakDevice::set_auto_start_rgb(bool enabled) {
    auto_start_rgb_ = enabled;
}

bool OakDevice::get_auto_start_rgb() const {
    return auto_start_rgb_;
}

void OakDevice::set_auto_start_left_ir(bool enabled) {
    auto_start_left_ir_ = enabled;
}

bool OakDevice::get_auto_start_left_ir() const {
    return auto_start_left_ir_;
}

void OakDevice::set_auto_start_right_ir(bool enabled) {
    auto_start_right_ir_ = enabled;
}

bool OakDevice::get_auto_start_right_ir() const {
    return auto_start_right_ir_;
}

int64_t OakDevice::get_rgb_frame_count() const {
    return impl_->rgb.count.load();
}

int64_t OakDevice::get_left_ir_frame_count() const {
    return impl_->left_ir.count.load();
}

int64_t OakDevice::get_right_ir_frame_count() const {
    return impl_->right_ir.count.load();
}

int64_t OakDevice::get_rgb_presented_count() const {
    return impl_->rgb.presented.load();
}

int64_t OakDevice::get_left_ir_presented_count() const {
    return impl_->left_ir.presented.load();
}

int64_t OakDevice::get_right_ir_presented_count() const {
    return impl_->right_ir.presented.load();
}

int64_t OakDevice::get_rgb_dropped_count() const {
    return impl_->rgb.dropped.load();
}

int64_t OakDevice::get_left_ir_dropped_count() const {
    return impl_->left_ir.dropped.load();
}

int64_t OakDevice::get_right_ir_dropped_count() const {
    return impl_->right_ir.dropped.load();
}

double OakDevice::get_rgb_host_latency_ms() const {
    return impl_->rgb.host_latency_ms.load();
}

double OakDevice::get_left_ir_host_latency_ms() const {
    return impl_->left_ir.host_latency_ms.load();
}

double OakDevice::get_right_ir_host_latency_ms() const {
    return impl_->right_ir.host_latency_ms.load();
}

double OakDevice::get_rgb_texture_update_ms() const {
    return impl_->rgb.texture_update_ms.load();
}

double OakDevice::get_left_ir_texture_update_ms() const {
    return impl_->left_ir.texture_update_ms.load();
}

double OakDevice::get_right_ir_texture_update_ms() const {
    return impl_->right_ir.texture_update_ms.load();
}

double OakDevice::get_rgb_capture_to_host_ms() const {
    return impl_->rgb.capture_to_host_ms.load();
}

double OakDevice::get_left_ir_capture_to_host_ms() const {
    return impl_->left_ir.capture_to_host_ms.load();
}

double OakDevice::get_right_ir_capture_to_host_ms() const {
    return impl_->right_ir.capture_to_host_ms.load();
}

double OakDevice::get_stereo_timestamp_skew_ms() const {
    return impl_->stereo_timestamp_skew_ms.load();
}

int64_t OakDevice::get_stereo_pair_count() const {
    return impl_->stereo_pair_count.load();
}

int64_t OakDevice::get_stereo_mismatch_count() const {
    return impl_->stereo_mismatch_count.load();
}

godot::String OakDevice::get_last_error() const {
    std::scoped_lock lock(impl_->error_mutex);
    return godot::String(impl_->last_error.c_str());
}

} // namespace godot_oak
