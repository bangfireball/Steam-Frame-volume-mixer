#include "pulse_backend.h"
#include <pulse/pulseaudio.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <list>
#include <mutex>
#include <thread>

namespace mixer {
using Clock = std::chrono::steady_clock;
class PulseBackend::Impl {
public:
    struct Request {
        Impl* self{};
        std::uint64_t version{};
        std::uint32_t index{};
        enum class Target { Stream, Sink, Server };
        Target target{Target::Stream};
        bool list{}, done{}, write{};
        Key key{};
        Clock::time_point started{Clock::now()};
        pa_operation* operation{};
        ~Request() { if (operation) pa_operation_unref(operation); }
    };
    mutable std::mutex mutex;
    Snapshot published;
    Mailbox mailbox;
    bool reconnectRequested{};
    const bool resetVolumesOnExit;
    std::atomic<bool> stopping{};
    std::thread worker;
    pa_mainloop* loop{};
    pa_context* context{};
    Snapshot current;
    std::map<std::uint32_t, Stream> streams;
    std::map<std::uint32_t, Sink> sinks;
    std::map<std::uint32_t, std::uint64_t> versions, sinkVersions;
    std::string defaultSinkName;
    std::uint64_t serverVersion{}, masterIncarnation{};
    bool serverKnown{}, streamsListed{}, sinksListed{};
    std::list<std::unique_ptr<Request>> requests;
    std::uint64_t generation{}, incarnation{}, sequence{};
    bool subscribed{}, ready{};
    Clock::time_point connectedAt{}, nextConnect{}, nextWrite{};

    explicit Impl(bool resetOnExit) : resetVolumesOnExit(resetOnExit) { worker = std::thread([this] { run(); }); }
    ~Impl() { stopping = true; worker.join(); }
    void publish() {
        current.streams.clear();
        for (auto& [index, s] : streams) {
            (void)index;
            s.pending = std::any_of(requests.begin(), requests.end(), [&](const auto& r) { return r->write && !r->done && r->key == s.key; });
            current.streams.push_back(s);
        }
        // Stable creation order; names or changing volume never reorder a row.
        std::sort(current.streams.begin(), current.streams.end(), [](const Stream& a, const Stream& b) { return a.key.incarnation < b.key.incarnation; });
        current.sinks.clear();
        for (const auto& [index, sink] : sinks) { (void)index; current.sinks.push_back(sink); }
        current.master = masterSink();
        if (current.master) current.master->pending = std::any_of(requests.begin(), requests.end(), [&](const auto& r) { return r->write && !r->done && r->key == current.master->key; });
        std::lock_guard lock(mutex);
        if (published.connected != current.connected || published.status != current.status || published.error != current.error || published.streams != current.streams || published.sinks != current.sinks || published.master != current.master) {
            current.revision = published.revision + 1;
            published = current;
        }
    }
    static std::string property(pa_proplist* p, const char* name) {
        auto* value = p ? pa_proplist_gets(p, name) : nullptr;
        return value ? value : "";
    }
    static RoutingMetadata routing(pa_proplist* p) {
        RoutingMetadata result;
        auto identifier = [&](const char* key) {
            auto value = property(p, key);
            if (value.size() > 512 || std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; })) return std::string{};
            return value;
        };
        result.nodeName = identifier("node.name");
        result.description = identifier("node.description");
        result.linkGroup = identifier("node.link-group");
        result.clientId = identifier("client.id");
        result.virtualNode = property(p,"node.virtual") == "true" || property(p,"node.virtual") == "1";
        result.passiveNode = property(p,"node.passive") == "true" || property(p,"node.passive") == "1";
        return result;
    }
    void discovered() {
        if (!current.connected && streamsListed && sinksListed && serverKnown) {
            current.connected = true; current.status = "Connected — local playback streams"; current.error.clear();
        }
    }
    std::optional<Sink> masterSink() const {
        if (!serverKnown || defaultSinkName.empty()) return {};
        for (const auto& [index, sink] : sinks) if (sink.name == defaultSinkName) {
            (void)index;
            auto master = sink;
            // A default-route epoch prevents queued writes surviving A -> B -> A.
            master.key.incarnation = masterIncarnation;
            return master;
        }
        return {};
    }
    static void info(pa_context*, const pa_sink_input_info* i, int eol, void* data) {
        auto& r = *static_cast<Request*>(data);
        auto& self = *r.self;
        if (eol) {
            r.done = true;
            if (r.list) {
                if (eol < 0) { self.current.error = "Playback enumeration failed"; self.ready = false; }
                else { self.streamsListed = true; self.discovered(); }
            }
            return;
        }
        if (!i || (r.list ? self.versions[i->index] > r.version : self.versions[i->index] != r.version)) return;
        auto old = self.streams.find(i->index);
        Stream s;
        s.key = old == self.streams.end() ? Key{self.generation, ++self.incarnation, i->index} : old->second.key;
        auto app = property(i->proplist, PA_PROP_APPLICATION_NAME);
        if (app.empty()) app = property(i->proplist, PA_PROP_APPLICATION_PROCESS_BINARY);
        auto id = property(i->proplist, PA_PROP_APPLICATION_ID);
        if (id.empty()) id = property(i->proplist, "pipewire.access.portal.app_id");
        s.applicationIdentified = !app.empty() || !id.empty();
        s.applicationId = id.empty() ? "" : safeLabel(id);
        s.sinkIndex = i->sink;
        s.routing = routing(i->proplist);
        auto media = property(i->proplist, PA_PROP_MEDIA_NAME);
        if (media.empty() || media == "(null)") media = i->name ? i->name : "";
        if (media == "(null)") media.clear();
        if (app.empty()) app = id;
        if (app.empty()) app = media;
        if (app.empty()) app = s.routing.description;
        if (app.empty()) app = s.routing.nodeName;
        s.application = safeLabel(app);
        s.media = media.empty() ? "Playback stream" : safeLabel(media);
        s.channels.assign(i->volume.values, i->volume.values + i->volume.channels);
        s.muted = i->mute; s.corked = i->corked;
        s.writable = i->has_volume && i->volume_writable && !s.channels.empty();
        self.streams[i->index] = std::move(s);
    }
    static void sinkInfo(pa_context*, const pa_sink_info* i, int eol, void* data) {
        auto& r = *static_cast<Request*>(data);
        auto& self = *r.self;
        if (eol) {
            r.done = true;
            if (r.list) {
                if (eol < 0) { self.current.error = "Output enumeration failed"; self.ready = false; }
                else { self.sinksListed = true; self.discovered(); }
            } else if (eol < 0 && self.sinkVersions[r.index] == r.version) self.sinks.erase(r.index);
            return;
        }
        if (!i || (r.list ? self.sinkVersions[i->index] > r.version : self.sinkVersions[i->index] != r.version)) return;
        auto old = self.sinks.find(i->index);
        Sink sink;
        sink.key = old == self.sinks.end() ? Key{self.generation, ++self.incarnation, i->index} : old->second.key;
        sink.name = i->name ? i->name : "";
        sink.description = safeLabel(i->description ? i->description : "Default output");
        sink.routing = routing(i->proplist);
        sink.channels.assign(i->volume.values, i->volume.values + i->volume.channels);
        sink.muted = i->mute;
        sink.writable = pa_cvolume_valid(&i->volume);
        self.sinks[i->index] = std::move(sink);
    }
    static void serverInfo(pa_context*, const pa_server_info* i, void* data) {
        auto& r = *static_cast<Request*>(data); r.done = true;
        auto& self = *r.self;
        if (r.version != self.serverVersion) return;
        if (!i) { self.current.error = "Default output discovery failed"; return; }
        self.defaultSinkName = i->default_sink_name ? i->default_sink_name : "";
        self.serverKnown = true;
        self.discovered();
    }
    Request* request(bool list, std::uint32_t index, bool write = false, Key key = {}, Request::Target target = Request::Target::Stream) {
        if (requests.size() >= 256) { current.error = "Audio request limit reached; reconnecting"; ready = false; return nullptr; }
        auto r = std::make_unique<Request>();
        r->self = this; r->list = list; r->index = index; r->target = target;
        r->version = target == Request::Target::Server ? serverVersion : list ? sequence : target == Request::Target::Sink ? sinkVersions[index] : versions[index];
        r->write = write; r->key = key;
        auto* ptr = r.get(); requests.push_back(std::move(r)); return ptr;
    }
    void query(std::uint32_t index) {
        // Any earlier query must not overwrite this one.
        versions[index] = ++sequence;
        if (auto* r = request(false, index)) {
            r->operation = pa_context_get_sink_input_info(context, index, info, r);
            if (!r->operation) r->done = true;
        }
    }
    void querySink(std::uint32_t index) {
        sinkVersions[index] = ++sequence;
        if (auto* r = request(false, index, false, {}, Request::Target::Sink)) {
            r->operation = pa_context_get_sink_info_by_index(context, index, sinkInfo, r);
            if (!r->operation) r->done = true;
        }
    }
    void queryServer() {
        serverVersion = ++sequence;
        masterIncarnation = ++incarnation;
        serverKnown = false;
        if (auto* r = request(false, 0, false, {}, Request::Target::Server)) {
            r->operation = pa_context_get_server_info(context, serverInfo, r);
            if (!r->operation) r->done = true;
        }
    }
    static void event(pa_context*, pa_subscription_event_type_t event, std::uint32_t index, void* data) {
        auto& self = *static_cast<Impl*>(data);
        const auto facility = event & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
        const auto kind = event & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
        if (facility == PA_SUBSCRIPTION_EVENT_SERVER) { self.queryServer(); return; }
        if (facility == PA_SUBSCRIPTION_EVENT_SINK) {
            if (kind == PA_SUBSCRIPTION_EVENT_REMOVE || kind == PA_SUBSCRIPTION_EVENT_NEW) {
                const auto previous = self.sinks.find(index);
                const bool replacedDefault = previous != self.sinks.end() && previous->second.name == self.defaultSinkName;
                self.sinkVersions[index] = ++self.sequence;
                self.sinks.erase(index);
                // Firefox may create a new internal sink for each track. That
                // must not invalidate a drag on an unchanged default output.
                if (replacedDefault || !self.serverKnown || self.defaultSinkName.empty()) self.queryServer();
            }
            if (kind != PA_SUBSCRIPTION_EVENT_REMOVE) self.querySink(index);
            return;
        }
        if (facility != PA_SUBSCRIPTION_EVENT_SINK_INPUT) return;
        if (kind == PA_SUBSCRIPTION_EVENT_REMOVE || kind == PA_SUBSCRIPTION_EVENT_NEW) {
            self.versions[index] = ++self.sequence;
            self.streams.erase(index);
        }
        if (kind != PA_SUBSCRIPTION_EVENT_REMOVE) self.query(index);
    }
    static void subscribeDone(pa_context*, int success, void* data) {
        auto& self = *static_cast<Impl*>(data);
        if (!success) { self.ready = false; self.current.error = "Cannot subscribe to playback changes"; return; }
        if (auto* r = self.request(true, 0)) {
            r->operation = pa_context_get_sink_input_info_list(self.context, info, r);
            if (!r->operation) r->done = true;
        }
        if (auto* r = self.request(true, 0, false, {}, Request::Target::Sink)) {
            r->operation = pa_context_get_sink_info_list(self.context, sinkInfo, r);
            if (!r->operation) r->done = true;
        }
        self.queryServer();
        self.current.status = "Discovering local playback streams";
    }
    static void writeDone(pa_context*, int success, void* data) {
        auto& r = *static_cast<Request*>(data); r.done = true;
        auto& self = *r.self;
        if (r.target == Request::Target::Sink) {
            const auto master = self.masterSink();
            if (!success && master && master->key == r.key) self.current.error = "Master volume/mute change rejected by audio server";
            self.querySink(r.index);
            return;
        }
        auto it = self.streams.find(r.index);
        if (it == self.streams.end() || it->second.key != r.key) return;
        if (!success) self.current.error = "Volume/mute change rejected by audio server";
        self.query(r.index);
    }
    void disconnect() {
        if (context) {
            for (auto& r : requests) if (r->operation) pa_operation_cancel(r->operation);
            pa_context_set_subscribe_callback(context, nullptr, nullptr);
            pa_context_disconnect(context);
            pa_context_unref(context); context = nullptr;
        }
        requests.clear();
        if (loop) { pa_mainloop_free(loop); loop = nullptr; }
        streams.clear(); sinks.clear(); versions.clear(); sinkVersions.clear();
        defaultSinkName.clear(); serverKnown = streamsListed = sinksListed = false;
        ready = subscribed = false; current.connected = false;
        std::lock_guard lock(mutex); mailbox.clear();
    }
    void connect() {
        current.status = "Connecting to audio server";
        ++generation; loop = pa_mainloop_new();
        if (!loop) { current.error = "Cannot create Pulse mainloop"; nextConnect = Clock::now()+std::chrono::seconds(2); return; }
        context = pa_context_new(pa_mainloop_get_api(loop), "Frame Volume Mixer");
        if (!context || pa_context_connect(context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
            current.error = "Audio server unavailable"; return;
        }
        connectedAt = Clock::now(); ready = true;
    }
    void dispatch(const std::vector<Command>& commands) {
        for (const auto& c : commands) {
            if (isMasterCommand(c.kind)) {
                const auto master = masterSink();
                if (!current.connected || !master || master->key != c.key || (c.kind == CommandKind::MasterVolume && !master->writable)) continue;
                if (std::any_of(requests.begin(), requests.end(), [&](const auto& r) { return r->write && !r->done && r->key == c.key; })) {
                    std::lock_guard lock(mutex); mailbox.push(c); continue;
                }
                auto* r = request(false, c.key.index, true, c.key, Request::Target::Sink);
                if (!r) break;
                current.error.clear();
                if (c.kind == CommandKind::MasterMute) r->operation = pa_context_set_sink_mute_by_index(context, c.key.index, !!c.value, writeDone, r);
                else {
                    const auto values = scaleVolume(master->channels, c.value);
                    pa_cvolume volume{}; volume.channels = static_cast<std::uint8_t>(values.size());
                    std::copy(values.begin(), values.end(), volume.values);
                    r->operation = pa_context_set_sink_volume_by_index(context, c.key.index, &volume, writeDone, r);
                }
                if (!r->operation) { r->done = true; current.error = "Unable to submit master control"; }
                continue;
            }
            auto it = streams.find(c.key.index);
            if (it == streams.end() || it->second.key != c.key || !current.connected) continue;
            if (c.kind == CommandKind::Volume && !it->second.writable) continue;
            if (std::any_of(requests.begin(), requests.end(), [&](const auto& r) { return r->write && !r->done && r->key == c.key; })) {
                std::lock_guard lock(mutex); mailbox.push(c); continue;
            }
            auto* r = request(false, c.key.index, true, c.key);
            if (!r) break;
            current.error.clear();
            if (c.kind == CommandKind::Mute) r->operation = pa_context_set_sink_input_mute(context, c.key.index, !!c.value, writeDone, r);
            else {
                auto values = scaleVolume(it->second.channels, c.value);
                pa_cvolume volume{}; volume.channels = static_cast<std::uint8_t>(values.size());
                std::copy(values.begin(), values.end(), volume.values);
                r->operation = pa_context_set_sink_input_volume(context, c.key.index, &volume, writeDone, r);
            }
            if (!r->operation) { r->done = true; current.error = "Unable to submit audio control"; }
        }
    }
    void resetForExit() {
        // Shutdown owns the audio thread. Discard slider commands and cancel old
        // callbacks before fresh enumeration. Earlier submitted writes precede
        // these resets on the same connection; none can be dispatched afterwards.
        const auto deadline = Clock::now() + std::chrono::seconds(2);
        {
            std::lock_guard lock(mutex);
            mailbox.clear();
        }
        if (!context || pa_context_get_state(context) != PA_CONTEXT_READY) {
            disconnect();
            connect();
        }
        auto pump = [&] {
            if (!context || !loop || pa_mainloop_iterate(loop, 0, nullptr) < 0) return false;
            const auto state = pa_context_get_state(context);
            return state != PA_CONTEXT_FAILED && state != PA_CONTEXT_TERMINATED;
        };
        while (context && pa_context_get_state(context) != PA_CONTEXT_READY && Clock::now() < deadline) {
            if (!pump()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!context || pa_context_get_state(context) != PA_CONTEXT_READY) {
            std::fprintf(stderr, "Exit volume reset unavailable: audio server not ready\n");
            return;
        }
        pa_context_set_subscribe_callback(context, nullptr, nullptr);
        for (auto& r : requests) if (r->operation) pa_operation_cancel(r->operation);
        requests.clear();
        struct Reset {
            bool listed{};
            unsigned submitted{}, completed{}, failed{};
            std::vector<pa_operation*> operations;
        } reset;
        auto* enumeration = pa_context_get_sink_input_info_list(context,
            [](pa_context* ctx, const pa_sink_input_info* info, int eol, void* data) {
                auto& reset = *static_cast<Reset*>(data);
                if (eol) { reset.listed = true; if (eol < 0) ++reset.failed; return; }
                if (!info || !info->has_volume || !info->volume_writable || !info->volume.channels) return;
                const std::vector<std::uint32_t> current(info->volume.values, info->volume.values + info->volume.channels);
                const auto values = scaleVolume(current, 100);
                pa_cvolume volume{}; volume.channels = info->volume.channels;
                std::copy(values.begin(), values.end(), volume.values);
                auto* op = pa_context_set_sink_input_volume(ctx, info->index, &volume,
                    [](pa_context*, int success, void* data) {
                        auto& reset = *static_cast<Reset*>(data);
                        ++reset.completed;
                        if (!success) ++reset.failed;
                    }, &reset);
                if (op) { reset.operations.push_back(op); ++reset.submitted; }
                else ++reset.failed;
            }, &reset);
        if (!enumeration) ++reset.failed;
        while (enumeration && (!reset.listed || reset.completed < reset.submitted) && Clock::now() < deadline) {
            if (!pump()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        // Suppress callbacks before their stack-owned state disappears, including
        // on timeout/server loss. Mute, master output and device routing are untouched.
        if (enumeration) { pa_operation_cancel(enumeration); pa_operation_unref(enumeration); }
        for (auto* op : reset.operations) { pa_operation_cancel(op); pa_operation_unref(op); }
        std::fprintf(stderr, "Exit volume reset: %u acknowledged, %u rejected; %s\n",
                     reset.completed, reset.failed,
                     reset.listed && reset.completed == reset.submitted ? "complete" : "incomplete (server unavailable/timeout)");
    }
    void run() {
        while (!stopping) {
            auto now = Clock::now();
            bool reset;
            { std::lock_guard lock(mutex); reset = reconnectRequested; reconnectRequested = false; }
            if (reset) { disconnect(); nextConnect = now; current.error.clear(); }
            if (!context && now >= nextConnect) connect();
            if (context && ready) {
                // Bounded nonblocking iteration; never wait on audio from the VR loop.
                for (int n=0; n<8; ++n) {
                    int result{};
                    if (pa_mainloop_iterate(loop, 0, &result) < 0) { ready = false; break; }
                    if (!result) break;
                }
                auto state = pa_context_get_state(context);
                if (state == PA_CONTEXT_READY && !subscribed) {
                    subscribed = true;
                    pa_context_set_subscribe_callback(context, event, this);
                    auto* op = pa_context_subscribe(context, static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK_INPUT | PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER), subscribeDone, this);
                    if (op) pa_operation_unref(op); else ready = false;
                }
                if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED || (!current.connected && now-connectedAt > std::chrono::seconds(3))) {
                    current.error = "Audio connection lost/unavailable; retrying"; ready = false;
                }
                if (versions.size() + sinkVersions.size() > 4096) { current.error = "Refreshing audio discovery history"; ready = false; }
                bool timeout = false;
                for (const auto& r : requests) if (!r->done && now-r->started > std::chrono::seconds(3)) timeout = true;
                if (timeout) { current.error = "Audio request timed out; reconnecting"; ready = false; }
                requests.remove_if([](const auto& r) { return r->done; });
                if (current.connected && ready && now >= nextWrite) {
                    std::vector<Command> commands;
                    { std::lock_guard lock(mutex); commands = mailbox.take(); }
                    dispatch(commands); nextWrite = now + std::chrono::milliseconds(50);
                }
            }
            if (!ready && (context || loop)) {
                disconnect(); nextConnect = now + std::chrono::seconds(2);
                current.status = "Audio unavailable — retrying";
            }
            publish();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (resetVolumesOnExit) resetForExit();
        disconnect();
    }
};
PulseBackend::PulseBackend(bool resetVolumesOnExit) : impl_(std::make_unique<Impl>(resetVolumesOnExit)) {}
PulseBackend::~PulseBackend() = default;
Snapshot PulseBackend::snapshot() const { std::lock_guard lock(impl_->mutex); return impl_->published; }
bool PulseBackend::submit(Command c) {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->published.connected) return false;
    if (isMasterCommand(c.kind)) {
        if (!impl_->published.master || impl_->published.master->key != c.key) return false;
    } else if (!findStream(impl_->published, c.key)) return false;
    return impl_->mailbox.push(c);
}
void PulseBackend::reconnect() { std::lock_guard lock(impl_->mutex); impl_->reconnectRequested = true; }
}
