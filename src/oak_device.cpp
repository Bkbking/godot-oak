#include "godot_oak/oak_device.hpp"

#include <depthai/depthai.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>

namespace godot_oak {

struct OakDevice::Impl {
    std::unique_ptr<dai::Pipeline> pipeline;
    std::shared_ptr<dai::MessageQueue> rgb_queue;
    std::thread worker;
    std::atomic_bool running{false};
    std::atomic_bool opened{false};
    std::atomic<int64_t> frame_count{0};

    mutable std::mutex frame_mutex;
    std::vector<uint8_t> latest_rgb;
    int width = 0;
    int height = 0;
    int fps = 0;
    bool frame_ready = false;

    mutable std::mutex error_mutex;
    std::string last_error;

    godot::Ref<godot::ImageTexture> texture;

    void set_error(std::string message) {
        std::scoped_lock lock(error_mutex);
        last_error = std::move(message);
    }
};

OakDevice::OakDevice() : impl_(std::make_unique<Impl>()) {}
OakDevice::~OakDevice() { close(); }

void OakDevice::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("open"), &OakDevice::open);
    godot::ClassDB::bind_method(godot::D_METHOD("start_rgb", "config"), &OakDevice::start_rgb);
    godot::ClassDB::bind_method(godot::D_METHOD("start_rgb_values", "width", "height", "fps"), &OakDevice::start_rgb_values, 640, 360, 30);
    godot::ClassDB::bind_method(godot::D_METHOD("stop"), &OakDevice::stop);
    godot::ClassDB::bind_method(godot::D_METHOD("close"), &OakDevice::close);
    godot::ClassDB::bind_method(godot::D_METHOD("is_open"), &OakDevice::is_open);
    godot::ClassDB::bind_method(godot::D_METHOD("is_streaming"), &OakDevice::is_streaming);
    godot::ClassDB::bind_method(godot::D_METHOD("get_texture"), &OakDevice::get_texture);
    godot::ClassDB::bind_method(godot::D_METHOD("get_last_error"), &OakDevice::get_last_error);
    godot::ClassDB::bind_method(godot::D_METHOD("get_frame_count"), &OakDevice::get_frame_count);
    godot::ClassDB::bind_method(godot::D_METHOD("get_active_width"), &OakDevice::get_active_width);
    godot::ClassDB::bind_method(godot::D_METHOD("get_active_height"), &OakDevice::get_active_height);
    godot::ClassDB::bind_method(godot::D_METHOD("get_active_fps"), &OakDevice::get_active_fps);
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::OBJECT, "texture", godot::PROPERTY_HINT_RESOURCE_TYPE, "ImageTexture"), "", "get_texture");
}

bool OakDevice::open() {
    if(impl_->opened.load()) return true;
    try {
        const auto devices = dai::Device::getAllAvailableDevices();
        if(devices.empty()) {
            impl_->set_error("No se ha encontrado ninguna cámara OAK.");
            return false;
        }
        impl_->opened.store(true);
        impl_->set_error("");
        return true;
    } catch(const std::exception& e) {
        impl_->set_error(e.what());
        return false;
    }
}

bool OakDevice::start_rgb(const godot::Ref<OakStreamConfig>& config) {
    if(config.is_null()) {
        impl_->set_error("La configuración RGB no puede ser nula.");
        return false;
    }
    return start_rgb_values(config->get_width(), config->get_height(), config->get_fps());
}

bool OakDevice::start_rgb_values(int width, int height, int fps) {
    if(impl_->running.load()) return true;
    if(!open()) return false;
    if(width <= 0 || height <= 0 || fps <= 0) {
        impl_->set_error("Resolución o FPS no válidos.");
        return false;
    }

    try {
        impl_->pipeline = std::make_unique<dai::Pipeline>();
        auto camera = impl_->pipeline->create<dai::node::Camera>()->build(dai::CameraBoardSocket::CAM_A);
        auto output = camera->requestOutput(
            std::make_pair(width, height),
            dai::ImgFrame::Type::BGR888i,
            dai::ImgResizeMode::LETTERBOX,
            static_cast<float>(fps),
            std::nullopt
        );
        impl_->rgb_queue = output->createOutputQueue(2, false);
        impl_->pipeline->start();

        impl_->width = width;
        impl_->height = height;
        impl_->fps = fps;
        impl_->latest_rgb.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 3U);
        impl_->frame_ready = false;
        impl_->frame_count.store(0);
        impl_->running.store(true);

        impl_->worker = std::thread([this]() {
            try {
                while(impl_->running.load()) {
                    auto frame = impl_->rgb_queue->get<dai::ImgFrame>();
                    if(!frame) continue;
                    const auto& bgr = frame->getData();
                    const size_t expected = static_cast<size_t>(impl_->width) * static_cast<size_t>(impl_->height) * 3U;
                    if(bgr.size() < expected) continue;

                    std::scoped_lock lock(impl_->frame_mutex);
                    impl_->latest_rgb.resize(expected);
                    for(size_t i = 0; i < expected; i += 3) {
                        impl_->latest_rgb[i] = bgr[i + 2];
                        impl_->latest_rgb[i + 1] = bgr[i + 1];
                        impl_->latest_rgb[i + 2] = bgr[i];
                    }
                    impl_->frame_ready = true;
                    impl_->frame_count.fetch_add(1);
                }
            } catch(const std::exception& e) {
                impl_->set_error(e.what());
                impl_->running.store(false);
            }
        });

        return true;
    } catch(const std::exception& e) {
        impl_->set_error(e.what());
        stop();
        return false;
    }
}

void OakDevice::stop() {
    impl_->running.store(false);
    if(impl_->rgb_queue) impl_->rgb_queue->close();
    if(impl_->worker.joinable()) impl_->worker.join();
    impl_->rgb_queue.reset();
    if(impl_->pipeline) {
        try { impl_->pipeline->stop(); } catch(...) {}
        impl_->pipeline.reset();
    }
    std::scoped_lock lock(impl_->frame_mutex);
    impl_->frame_ready = false;
}

void OakDevice::close() {
    stop();
    impl_->opened.store(false);
    impl_->texture.unref();
}

bool OakDevice::is_open() const { return impl_->opened.load(); }
bool OakDevice::is_streaming() const { return impl_->running.load(); }
int64_t OakDevice::get_frame_count() const { return impl_->frame_count.load(); }
int OakDevice::get_active_width() const { return impl_->width; }
int OakDevice::get_active_height() const { return impl_->height; }
int OakDevice::get_active_fps() const { return impl_->fps; }

godot::String OakDevice::get_last_error() const {
    std::scoped_lock lock(impl_->error_mutex);
    return godot::String(impl_->last_error.c_str());
}

godot::Ref<godot::ImageTexture> OakDevice::get_texture() {
    std::vector<uint8_t> rgb;
    {
        std::scoped_lock lock(impl_->frame_mutex);
        if(!impl_->frame_ready) return impl_->texture;
        rgb = impl_->latest_rgb;
        impl_->frame_ready = false;
    }

    godot::PackedByteArray bytes;
    bytes.resize(static_cast<int64_t>(rgb.size()));
    std::memcpy(bytes.ptrw(), rgb.data(), rgb.size());
    const godot::Ref<godot::Image> image = godot::Image::create_from_data(
        impl_->width,
        impl_->height,
        false,
        godot::Image::FORMAT_RGB8,
        bytes
    );

    if(impl_->texture.is_null()) {
        impl_->texture = godot::ImageTexture::create_from_image(image);
    } else {
        impl_->texture->update(image);
    }
    return impl_->texture;
}

} // namespace godot_oak
