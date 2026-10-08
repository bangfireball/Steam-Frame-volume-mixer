#pragma once
#include "model.h"
#include "vk_texture.h"
#include <openvr.h>
#include <string>
#include <vector>
namespace mixer {
class Dashboard {
public:
    enum class Action { None, Previous, Next, Internals, Reconnect, Quit };
    struct Event { enum Kind { Down, Move, Up, Cancel, Scroll, Close } kind; float x{}, y{}; };
    ~Dashboard();
    bool initialize(const std::string& manifest, std::string& error);
    void shutdown();
    bool health(std::string& error, bool& recreated);
    bool render(const std::vector<std::uint8_t>& pixels, std::string& error);
    std::vector<Event> events();
    bool visible() const;
    void show();
    bool runtimeQuit() const { return quit_; }
    static Action footer(float x, float y);
private:
    bool create(std::string& error);
    void releaseHandles();
    VulkanContext vulkan_;
    OverlayTexture panel_, icon_;
    vr::VROverlayHandle_t main_{}, thumbnail_{};
    bool initialized_{}, quit_{};
};
}
