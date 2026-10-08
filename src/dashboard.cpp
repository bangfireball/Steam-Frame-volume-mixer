#include "dashboard.h"
#include "panel.h"
#include <chrono>
#include <thread>
#include <unistd.h>
namespace mixer {
namespace { constexpr const char* key = "org.steamframe.volume-mixer.dashboard"; }
Dashboard::~Dashboard() { shutdown(); }
bool Dashboard::initialize(const std::string& manifest, std::string& error) {
    shutdown();
    vr::EVRInitError initError = vr::VRInitError_None;
    // Background probe avoids launching SteamVR merely because this utility starts.
    vr::VR_Init(&initError, vr::VRApplication_Background);
    if (initError != vr::VRInitError_None) { error = vr::VR_GetVRInitErrorAsEnglishDescription(initError); return false; }
    vr::VR_Shutdown();
    vr::VR_Init(&initError, vr::VRApplication_Overlay);
    if (initError != vr::VRInitError_None) { error = vr::VR_GetVRInitErrorAsEnglishDescription(initError); return false; }
    initialized_ = true; quit_ = false;
    if (!vr::VROverlay() || !vr::VRCompositor()) { error = "OpenVR overlay/compositor unavailable"; shutdown(); return false; }
    if (!manifest.empty() && vr::VRApplications()) {
        auto result = vr::VRApplications()->AddApplicationManifest(manifest.c_str(), true);
        if (result != vr::VRApplicationError_None) { error = "OpenVR manifest registration failed"; shutdown(); return false; }
        result = vr::VRApplications()->IdentifyApplication(static_cast<std::uint32_t>(getpid()), "org.steamframe.volume-mixer");
        if (result != vr::VRApplicationError_None) { error = "OpenVR process identification failed"; shutdown(); return false; }
    }
    if (!vulkan_.init(error) || !panel_.create(vulkan_,Interaction::width,Interaction::height,error) || !icon_.create(vulkan_,128,128,error) || !create(error)) { shutdown(); return false; }
    return true;
}
bool Dashboard::create(std::string& error) {
    auto* api = vr::VROverlay();
    auto check = [&](vr::EVROverlayError result) {
        if (result == vr::VROverlayError_None) return true;
        error = api->GetOverlayErrorNameFromEnum(result); return false;
    };
    if (!check(api->CreateDashboardOverlay(key,"Frame Volume Mixer",&main_,&thumbnail_))) return false;
    vr::HmdVector2_t scale{{static_cast<float>(Interaction::width),static_cast<float>(Interaction::height)}};
    if (!check(api->SetOverlayInputMethod(main_,vr::VROverlayInputMethod_Mouse)) || !check(api->SetOverlayMouseScale(main_,&scale)) || !check(api->SetOverlayWidthInMeters(main_,1.65f))) { releaseHandles(); return false; }
    api->SetOverlayFlag(main_,vr::VROverlayFlags_SendVRDiscreteScrollEvents,true);
    if (!check(api->SetOverlayFlag(main_,vr::VROverlayFlags_EnableControlBarClose,true))) { releaseHandles(); return false; }
    if (!icon_.update(thumbnail_,renderIcon(128).data(),error)) { releaseHandles(); return false; }
    return true;
}
void Dashboard::releaseHandles() {
    if (!initialized_ || !vr::VROverlay()) return;
    auto* api = vr::VROverlay();
    // Never destroy an overlay if its key now belongs to a replacement handle.
    vr::VROverlayHandle_t found{};
    if (main_ && api->FindOverlay(key,&found) == vr::VROverlayError_None && found == main_) {
        api->HideOverlay(main_); api->ClearOverlayTexture(main_);
        if (thumbnail_) api->ClearOverlayTexture(thumbnail_);
        api->DestroyOverlay(main_);
    }
    main_ = thumbnail_ = vr::k_ulOverlayHandleInvalid;
}
void Dashboard::shutdown() {
    if (initialized_) {
        releaseHandles();
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        vr::VR_Shutdown(); initialized_ = false;
    }
    panel_.destroy(); icon_.destroy(); vulkan_.destroy();
}
bool Dashboard::health(std::string& error, bool& recreated) {
    recreated = false;
    if (!initialized_) return false;
    vr::VROverlayHandle_t found{};
    if (vr::VROverlay()->FindOverlay(key,&found) == vr::VROverlayError_None && found == main_) return true;
    releaseHandles();
    recreated = true;
    return create(error);
}
bool Dashboard::render(const std::vector<std::uint8_t>& pixels, std::string& error) { return panel_.update(main_,pixels.data(),error); }
bool Dashboard::visible() const { return initialized_ && vr::VROverlay()->IsOverlayVisible(main_); }
void Dashboard::show() { if (initialized_) vr::VROverlay()->ShowDashboard(key); }
std::vector<Dashboard::Event> Dashboard::events() {
    std::vector<Event> result;
    vr::VREvent_t event{};
    for (int n=0; n<128 && vr::VRSystem()->PollNextEvent(&event,sizeof(event)); ++n) {
        if (event.eventType == vr::VREvent_Quit) { quit_ = true; vr::VRSystem()->AcknowledgeQuit_Exiting(); }
    }
    for (int n=0; n<128 && vr::VROverlay()->PollNextOverlayEvent(main_,&event,sizeof(event)); ++n) {
        float x = event.data.mouse.x, y = Interaction::height-event.data.mouse.y;
        switch (event.eventType) {
        case vr::VREvent_MouseMove: result.push_back({Event::Move,x,y}); break;
        case vr::VREvent_MouseButtonDown: if (event.data.mouse.button == vr::VRMouseButton_Left) result.push_back({Event::Down,x,y}); break;
        case vr::VREvent_MouseButtonUp: if (event.data.mouse.button == vr::VRMouseButton_Left) result.push_back({Event::Up,x,y}); break;
        case vr::VREvent_ScrollDiscrete: result.push_back({Event::Scroll,0,event.data.scroll.ydelta}); break;
        case vr::VREvent_OverlayClosed: result.push_back({Event::Close,0,0}); break;
        case vr::VREvent_FocusLeave:
        case vr::VREvent_OverlayHidden:
        case vr::VREvent_DashboardDeactivated: result.push_back({Event::Cancel,0,0}); break;
        default: break;
        }
    }
    return result;
}
Dashboard::Action Dashboard::footer(float x,float y) {
    if (y<630 || y>678) return Action::None;
    if (x>=32 && x<=177) return Action::Previous;
    if (x>=185 && x<=330) return Action::Next;
    if (x>=355 && x<=500) return Action::Internals;
    if (x>=645 && x<=790) return Action::Reconnect;
    if (x>=812 && x<=957) return Action::Quit;
    return Action::None;
}
}
