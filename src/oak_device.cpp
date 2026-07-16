#include "godot_oak/oak_device.hpp"

#include <depthai/depthai.hpp>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <atomic>
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
    std::atomic<int64_t> count{0};
    godot::Ref<godot::ImageTexture> texture;
};

godot::Ref<godot::Texture2D> update_texture(
    CpuFrame& frame,
    godot::Image::Format format
) {
    std::vector<std::uint8_t> data;
    int width = 0;
    int height = 0;

    {
        std::scoped_lock lock(frame.mutex);
        if(!frame.ready) {
            return frame.texture;
        }

        data = frame.data;
        width = frame.width;
        height = frame.height;
        frame.ready = false;
    }

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

    return frame.texture;
}

} // namespace

struct OakDevice::Impl {
    std::unique_ptr<dai::Pipeline> pipeline;

    std::shared_ptr<dai::MessageQueue> rgb_queue;
    std::shared_ptr<dai::MessageQueue> left_ir_queue;

    std::thread rgb_worker;
    std::thread left_ir_worker;

    std::atomic_bool running{false};
    std::atomic_bool opened{false};

    CpuFrame rgb;
    CpuFrame left_ir;

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
}

void OakDevice::_ready() {
    if(auto_open_ && !open()) {
        return;
    }

    if(auto_start_rgb_ || auto_start_left_ir_) {
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
    return start_streams();
}

bool OakDevice::start_left_ir(const godot::Ref<OakStreamConfig>& config) {
    set_left_ir_config(config);
    auto_start_rgb_ = false;
    auto_start_left_ir_ = true;
    return start_streams();
}

bool OakDevice::start_streams() {
    stop();

    if(!open()) {
        return false;
    }

    if(!auto_start_rgb_ && !auto_start_left_ir_) {
        impl_->set_error("No hay ningún stream habilitado.");
        return false;
    }

    try {
        impl_->pipeline = std::make_unique<dai::Pipeline>();

        if(auto_start_rgb_) {
            const int width = rgb_config_->get_width();
            const int height = rgb_config_->get_height();
            const int fps = rgb_config_->get_fps();

            auto rgb_camera = impl_->pipeline
                                  ->create<dai::node::Camera>()
                                  ->build(dai::CameraBoardSocket::CAM_A);

            auto rgb_output = rgb_camera->requestOutput(
                std::make_pair(width, height),
                dai::ImgFrame::Type::BGR888i,
                dai::ImgResizeMode::LETTERBOX,
                static_cast<float>(fps),
                std::nullopt
            );

            impl_->rgb_queue = rgb_output->createOutputQueue(2, false);

            std::scoped_lock lock(impl_->rgb.mutex);
            impl_->rgb.width = width;
            impl_->rgb.height = height;
            impl_->rgb.ready = false;
            impl_->rgb.count.store(0);
            impl_->rgb.data.resize(
                static_cast<std::size_t>(width) *
                static_cast<std::size_t>(height) *
                3U
            );
        }

        if(auto_start_left_ir_) {
            const int width = left_ir_config_->get_width();
            const int height = left_ir_config_->get_height();
            const int fps = left_ir_config_->get_fps();

            auto left_camera = impl_->pipeline
                                   ->create<dai::node::Camera>()
                                   ->build(dai::CameraBoardSocket::CAM_B);

            auto left_output = left_camera->requestOutput(
                std::make_pair(width, height),
                dai::ImgFrame::Type::GRAY8,
                dai::ImgResizeMode::LETTERBOX,
                static_cast<float>(fps),
                std::nullopt
            );

            impl_->left_ir_queue = left_output->createOutputQueue(2, false);

            std::scoped_lock lock(impl_->left_ir.mutex);
            impl_->left_ir.width = width;
            impl_->left_ir.height = height;
            impl_->left_ir.ready = false;
            impl_->left_ir.count.store(0);
            impl_->left_ir.data.resize(
                static_cast<std::size_t>(width) *
                static_cast<std::size_t>(height)
            );
        }

        impl_->pipeline->start();
        impl_->running.store(true);
        impl_->set_error("");

        if(impl_->rgb_queue) {
            impl_->rgb_worker = std::thread([this]() {
                try {
                    while(impl_->running.load()) {
                        auto frame = impl_->rgb_queue->get<dai::ImgFrame>();
                        if(!frame) {
                            continue;
                        }

                        const auto& bgr = frame->getData();
                        const std::size_t expected =
                            static_cast<std::size_t>(impl_->rgb.width) *
                            static_cast<std::size_t>(impl_->rgb.height) *
                            3U;

                        if(bgr.size() < expected) {
                            continue;
                        }

                        std::scoped_lock lock(impl_->rgb.mutex);
                        impl_->rgb.data.resize(expected);

                        for(std::size_t index = 0; index < expected; index += 3) {
                            impl_->rgb.data[index] = bgr[index + 2];
                            impl_->rgb.data[index + 1] = bgr[index + 1];
                            impl_->rgb.data[index + 2] = bgr[index];
                        }

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

        if(impl_->left_ir_queue) {
            impl_->left_ir_worker = std::thread([this]() {
                try {
                    while(impl_->running.load()) {
                        auto frame = impl_->left_ir_queue->get<dai::ImgFrame>();
                        if(!frame) {
                            continue;
                        }

                        const auto& gray = frame->getData();
                        const std::size_t expected =
                            static_cast<std::size_t>(impl_->left_ir.width) *
                            static_cast<std::size_t>(impl_->left_ir.height);

                        if(gray.size() < expected) {
                            continue;
                        }

                        std::scoped_lock lock(impl_->left_ir.mutex);
                        impl_->left_ir.data.assign(
                            gray.begin(),
                            gray.begin() + static_cast<std::ptrdiff_t>(expected)
                        );
                        impl_->left_ir.ready = true;
                        impl_->left_ir.count.fetch_add(1);
                    }
                } catch(const std::exception& error) {
                    if(impl_->running.load()) {
                        impl_->set_error(error.what());
                    }
                }
            });
        }

        return true;
    } catch(const std::exception& error) {
        impl_->set_error(error.what());
        stop();
        return false;
    }
}

void OakDevice::stop() {
    impl_->running.store(false);

    if(impl_->rgb_queue) {
        impl_->rgb_queue->close();
    }
    if(impl_->left_ir_queue) {
        impl_->left_ir_queue->close();
    }

    if(impl_->rgb_worker.joinable()) {
        impl_->rgb_worker.join();
    }
    if(impl_->left_ir_worker.joinable()) {
        impl_->left_ir_worker.join();
    }

    impl_->rgb_queue.reset();
    impl_->left_ir_queue.reset();

    if(impl_->pipeline) {
        try {
            impl_->pipeline->stop();
        } catch(...) {
        }
        impl_->pipeline.reset();
    }
}

void OakDevice::close() {
    stop();
    impl_->opened.store(false);
    impl_->rgb.texture.unref();
    impl_->left_ir.texture.unref();
}

bool OakDevice::is_open() const {
    return impl_->opened.load();
}

bool OakDevice::is_streaming() const {
    return impl_->running.load();
}

godot::Ref<godot::Texture2D> OakDevice::get_rgb_texture() {
    return update_texture(impl_->rgb, godot::Image::FORMAT_RGB8);
}

godot::Ref<godot::Texture2D> OakDevice::get_left_ir_texture() {
    return update_texture(impl_->left_ir, godot::Image::FORMAT_L8);
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

int64_t OakDevice::get_rgb_frame_count() const {
    return impl_->rgb.count.load();
}

int64_t OakDevice::get_left_ir_frame_count() const {
    return impl_->left_ir.count.load();
}

godot::String OakDevice::get_last_error() const {
    std::scoped_lock lock(impl_->error_mutex);
    return godot::String(impl_->last_error.c_str());
}

} // namespace godot_oak
