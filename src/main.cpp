#include "dashboard.h"
#include "instance.h"
#include "panel.h"
#include "pulse_backend.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

using namespace mixer;
namespace {
volatile std::sig_atomic_t stopRequested = 0;
void stop(int) { stopRequested = 1; }
Snapshot mockSnapshot() {
    Snapshot s; s.connected = true; s.revision = 1; s.status = "Demo";
    s.streams = {
        {{1,1,1},"Chromium","Music","chromium",{normalVolume,normalVolume},false,false,true,false},
        {{1,2,2},"VR Game","Game audio","game",{normalVolume*35/100,normalVolume*35/100},false,false,true,false},
        {{1,3,3},"PhoneCast","Phone playback","phonecast",{normalVolume*65/100,normalVolume*65/100},true,false,true,false},
        {{1,4,4},"Paused application","Dormant stream","paused",{normalVolume,normalVolume},false,true,true,false}
    };
    Sink master;
    master.key = {1,100,50}; master.name = "demo-speakers"; master.description = "Headset speakers";
    master.channels = {normalVolume/2,normalVolume/2}; master.writable = true;
    s.master = master;
    return s;
}
void help() {
    std::cout << "Frame Volume Mixer " << MIXER_VERSION << "\n"
              << "  frame-volume-mixer                 Open native dashboard mixer\n"
              << "  --mock                             Dashboard with fake streams (no writes)\n"
              << "  --preview FILE.ppm                 Render mock UI without audio or VR\n"
              << "  --list                             Read-only local playback stream list\n"
              << "  --version | --help\n";
}
}
int main(int argc,char** argv) {
    try {
        bool mock = false, list = false;
        std::string preview;
        for (int i=1; i<argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help") { help(); return 0; }
            if (arg == "--version") { std::cout << MIXER_VERSION << '\n'; return 0; }
            if (arg == "--mock") mock = true;
            else if (arg == "--list") list = true;
            else if (arg == "--preview" && i+1<argc) preview = argv[++i];
            else throw std::runtime_error("Unknown/incomplete option: "+arg);
        }
        if (!preview.empty()) { Interaction ui; ui.update(mockSnapshot()); writePpm(preview,renderPanel(ui,false,true),Interaction::width,Interaction::height); return 0; }
        if (list) {
            PulseBackend backend;
            auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(5);
            Snapshot snapshot;
            while (std::chrono::steady_clock::now()<deadline) {
                snapshot = backend.snapshot();
                if (snapshot.connected) { std::this_thread::sleep_for(std::chrono::milliseconds(300)); snapshot = backend.snapshot(); break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if (!snapshot.connected) { std::cerr << snapshot.status << ": " << snapshot.error << '\n'; return 2; }
            for (const auto& s : snapshot.streams) std::cout << s.key.index << '\t' << s.application << '\t' << s.media << '\t' << volumePercent(s) << "%\t" << (s.muted ? "muted" : "unmuted") << (s.corked ? "\tpaused" : "") << (s.writable ? "" : "\tvolume-read-only") << '\n';
            return 0;
        }
        const char* runtime = std::getenv("XDG_RUNTIME_DIR");
        if (!runtime || !*runtime) throw std::runtime_error("XDG_RUNTIME_DIR is missing; launch in the headset user's session");
        Instance instance;
        if (!instance.acquire(runtime)) return 0;
        std::signal(SIGINT,stop); std::signal(SIGTERM,stop);
        auto executable = std::filesystem::canonical("/proc/self/exe");
        auto manifest = executable.parent_path()/"frame-volume-mixer.vrmanifest";
        std::unique_ptr<PulseBackend> backend;
        if (!mock) backend = std::make_unique<PulseBackend>(true);
        Snapshot demo = mockSnapshot();
        Interaction ui; ui.update(mock ? demo : backend->snapshot());
        Dashboard dashboard;
        bool active = false, dirty = true, firstUpload = true, showPending = true, wasVisible = false, confirmQuit = false;
        Dashboard::Action pressed = Dashboard::Action::None;
        auto nextInit = std::chrono::steady_clock::now(), nextHealth = nextInit, quitDeadline = nextInit;
        auto submit = [&](std::optional<Command> c) {
            if (!c) return;
            if (mock) {
                if (isMasterCommand(c->kind) && demo.master && demo.master->key == c->key) {
                    if (c->kind == CommandKind::MasterVolume) demo.master->channels = scaleVolume(demo.master->channels,c->value);
                    else demo.master->muted = !!c->value;
                }
                for (auto& s : demo.streams) if (!isMasterCommand(c->kind) && s.key == c->key) {
                    if (c->kind == CommandKind::Volume) s.channels = scaleVolume(s.channels,c->value);
                    else s.muted = !!c->value;
                }
                ++demo.revision; ui.update(demo);
            } else if (!backend->submit(*c)) std::cerr << "Audio command discarded: stream unavailable\n";
        };
        while (!stopRequested) {
            auto now = std::chrono::steady_clock::now();
            showPending |= instance.pollShow();
            if (confirmQuit && now>=quitDeadline) { confirmQuit = false; dirty = true; }
            auto s = mock ? demo : backend->snapshot();
            if (s.revision != ui.snapshot().revision) { ui.update(std::move(s)); dirty = true; }
            std::string error;
            if (!active && now>=nextInit) {
                active = dashboard.initialize(std::filesystem::exists(manifest) ? manifest.string() : "",error);
                nextInit = now+std::chrono::seconds(3);
                if (!active) std::cerr << "Waiting for SteamVR: " << error << '\n';
                else { dirty = firstUpload = true; nextHealth = now+std::chrono::seconds(3); }
            }
            if (active) {
                for (const auto& event : dashboard.events()) {
                    if (event.kind == Dashboard::Event::Close) { ui.cancel(); stopRequested = 1; }
                    if (stopRequested) continue;
                    if (event.kind == Dashboard::Event::Cancel) { ui.cancel(); pressed = Dashboard::Action::None; dirty = true; }
                    if (!dashboard.visible()) continue;
                    switch (event.kind) {
                    case Dashboard::Event::Down: pressed = Dashboard::footer(event.x,event.y); submit(ui.down(event.x,event.y)); dirty = true; break;
                    case Dashboard::Event::Move: if (ui.dragging()) { submit(ui.move(event.x)); dirty = true; } break;
                    case Dashboard::Event::Up:
                        submit(ui.up(event.x,event.y));
                        if (pressed != Dashboard::Action::None && pressed == Dashboard::footer(event.x,event.y)) {
                            if (pressed == Dashboard::Action::Previous) ui.page(-1);
                            if (pressed == Dashboard::Action::Next) ui.page(1);
                            if (pressed == Dashboard::Action::Internals) ui.toggleInternals();
                            if (pressed == Dashboard::Action::Reconnect && backend) backend->reconnect();
                            if (pressed == Dashboard::Action::Quit) {
                                if (confirmQuit) stopRequested = 1;
                                else { confirmQuit = true; quitDeadline = now+std::chrono::seconds(4); }
                            } else confirmQuit = false;
                        }
                        pressed = Dashboard::Action::None; dirty = true; break;
                    case Dashboard::Event::Scroll: ui.page(event.y<0 ? 1 : -1); dirty = true; break;
                    default: break;
                    }
                }
                if (dashboard.runtimeQuit()) {
                    // Honor SteamVR's request to exit (including stopping this app).
                    // Backend teardown performs the same reset as the panel Quit button.
                    stopRequested = 1;
                    dashboard.shutdown(); active = false; ui.cancel(); pressed = Dashboard::Action::None; wasVisible = false;
                } else {
                    if (now>=nextHealth) {
                        bool recreated = false;
                        if (!dashboard.health(error,recreated)) { std::cerr << "Dashboard recovery: " << error << '\n'; dashboard.shutdown(); active = false; ui.cancel(); }
                        if (recreated) { dirty = firstUpload = true; ui.cancel(); pressed = Dashboard::Action::None; }
                        nextHealth = now+std::chrono::seconds(3);
                    }
                    if (active) {
                        if (showPending) { dashboard.show(); showPending = false; }
                        bool visible = dashboard.visible();
                        if (!visible && wasVisible) { ui.cancel(); pressed = Dashboard::Action::None; confirmQuit = false; }
                        if (visible && !wasVisible) dirty = true;
                        wasVisible = visible;
                        if ((visible && dirty) || firstUpload) {
                            if (!dashboard.render(renderPanel(ui,confirmQuit,mock),error)) {
                                std::cerr << "Overlay upload failed: " << error << '\n'; dashboard.shutdown(); active = false; ui.cancel(); nextInit = now+std::chrono::seconds(3);
                            } else { dirty = firstUpload = false; }
                        }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(active && dashboard.visible() ? 16 : 50));
        }
        dashboard.shutdown();
        return 0;
    } catch (const std::exception& e) { std::cerr << "Frame Volume Mixer: " << e.what() << '\n'; return 1; }
}
