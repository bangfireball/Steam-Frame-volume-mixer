#include "pulse_backend.h"
#include <pulse/simple.h>
#include <pulse/pulseaudio.h>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace mixer;
namespace {
void wait(const std::function<bool()>& condition,const char* message) {
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(6);
    while (std::chrono::steady_clock::now()<end) { if (condition()) return; std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    throw std::runtime_error(message);
}
struct Playback {
    pa_simple* stream{};
    explicit Playback(const char* name) {
        pa_sample_spec spec{PA_SAMPLE_S16LE,48000,2}; int error{};
        stream=pa_simple_new(nullptr,name,PA_STREAM_PLAYBACK,nullptr,"Synthetic silence",&spec,nullptr,nullptr,&error);
        if (!stream) throw std::runtime_error("Cannot create private test playback stream");
        std::int16_t silence[1920]{};
        if (pa_simple_write(stream,silence,sizeof(silence),&error)<0) throw std::runtime_error("Cannot write test stream");
    }
    ~Playback() { if (stream) pa_simple_free(stream); }
};
Stream named(PulseBackend& backend,const char* name) {
    Stream result;
    wait([&] { auto s=backend.snapshot(); for (auto& v:s.streams) if(v.application==name) {result=v; return true;} return false; },"Stream missing");
    return result;
}
struct External {
    pa_mainloop* loop=pa_mainloop_new();
    pa_context* ctx=pa_context_new(pa_mainloop_get_api(loop),"External Test Mixer");
    External() {
        if(pa_context_connect(ctx,nullptr,PA_CONTEXT_NOAUTOSPAWN,nullptr)<0) throw std::runtime_error("External connection failed");
        wait([&] { pa_mainloop_iterate(loop,0,nullptr); return pa_context_get_state(ctx)==PA_CONTEXT_READY; },"External ready timeout");
    }
    ~External() { pa_context_disconnect(ctx); pa_context_unref(ctx); pa_mainloop_free(loop); }
    void complete(pa_operation* op, const char* message) {
        if (!op) throw std::runtime_error(message);
        wait([&] {pa_mainloop_iterate(loop,0,nullptr); return pa_operation_get_state(op)!=PA_OPERATION_RUNNING;},message);
        pa_operation_unref(op);
    }
    Sink readSink(const char* name = "mixer_test") {
        Sink sink;
        auto* op=pa_context_get_sink_info_by_name(ctx,name,[](pa_context*,const pa_sink_info* i,int,void* data) {
            if (!i) return;
            auto& sink = *static_cast<Sink*>(data);
            sink.key.index = i->index; sink.name = i->name; sink.muted = i->mute;
            sink.channels.assign(i->volume.values,i->volume.values+i->volume.channels);
        },&sink);
        complete(op,"Sink query failed/timed out");
        if (sink.channels.empty()) throw std::runtime_error("Private sink not found");
        return sink;
    }
    std::vector<std::uint32_t> sinkVolume() { return readSink().channels; }
    void sinkVolume(int percent, const char* name = "mixer_test") {
        pa_cvolume value; pa_cvolume_set(&value,2,normalVolume*percent/100);
        complete(pa_context_set_sink_volume_by_name(ctx,name,&value,nullptr,nullptr),"External sink volume failed");
    }
    void sinkMute(bool muted, const char* name = "mixer_test") {
        complete(pa_context_set_sink_mute_by_name(ctx,name,muted,nullptr,nullptr),"External sink mute failed");
    }
    void defaultSink(const char* name) {
        complete(pa_context_set_default_sink(ctx,name,nullptr,nullptr),"Default sink switch failed");
    }
    std::uint32_t loadSink(const std::string& name, const std::string& properties) {
        std::uint32_t index=PA_INVALID_INDEX;
        const auto args="sink_name="+name+" sink_properties='"+properties+"'";
        complete(pa_context_load_module(ctx,"module-null-sink",args.c_str(),[](pa_context*,std::uint32_t i,void* data) {
            *static_cast<std::uint32_t*>(data)=i;
        },&index),"Synthetic sink load failed");
        if (index==PA_INVALID_INDEX) throw std::runtime_error("Synthetic sink module rejected");
        return index;
    }
    void unloadSink(std::uint32_t index) { complete(pa_context_unload_module(ctx,index,nullptr,nullptr),"Synthetic sink unload failed"); }
    void moveStream(std::uint32_t index,const std::string& sink) {
        complete(pa_context_move_sink_input_by_name(ctx,index,sink.c_str(),nullptr,nullptr),"Synthetic stream move failed");
    }
    void volume(std::uint32_t index,int percent) {
        pa_cvolume value; pa_cvolume_set(&value,2,normalVolume*percent/100);
        auto* op=pa_context_set_sink_input_volume(ctx,index,&value,nullptr,nullptr);
        if(!op) throw std::runtime_error("External volume failed");
        wait([&] { pa_mainloop_iterate(loop,0,nullptr); return pa_operation_get_state(op)!=PA_OPERATION_RUNNING; },"External write timeout");
        pa_operation_unref(op);
    }
};
struct SyntheticStream {
    External& external;
    pa_stream* stream{};
    SyntheticStream(External& ext,const std::string& node,const std::string& application,const char* sink,bool filter=false) : external(ext) {
        auto* props=pa_proplist_new();
        pa_proplist_sets(props,PA_PROP_APPLICATION_NAME,application.c_str());
        pa_proplist_sets(props,PA_PROP_APPLICATION_PROCESS_BINARY,application.c_str());
        pa_proplist_sets(props,PA_PROP_APPLICATION_ID,application.c_str());
        pa_proplist_sets(props,PA_PROP_MEDIA_NAME,filter ? "Synthetic filter playback" : "Synthetic app playback");
        pa_proplist_sets(props,"node.name",node.c_str());
        if (filter) {
            pa_proplist_sets(props,"node.virtual","true"); pa_proplist_sets(props,"node.passive","true");
            pa_proplist_sets(props,"node.link-group","synthetic-filter-group"); pa_proplist_sets(props,"client.id","synthetic-owner");
        }
        pa_sample_spec spec{PA_SAMPLE_S16LE,48000,2};
        stream=pa_stream_new_with_proplist(ext.ctx,"Synthetic topology",&spec,nullptr,props);
        pa_proplist_free(props);
        if (!stream || pa_stream_connect_playback(stream,sink,nullptr,PA_STREAM_START_CORKED,nullptr,nullptr)<0) throw std::runtime_error("Synthetic stream create failed");
        wait([&] {pa_mainloop_iterate(ext.loop,0,nullptr); return pa_stream_get_state(stream)==PA_STREAM_READY;},"Synthetic stream ready timeout");
    }
    ~SyntheticStream() { if(stream) {pa_stream_disconnect(stream); pa_stream_unref(stream);} }
    std::uint32_t index() const { return pa_stream_get_index(stream); }
};
}
int main(int argc, char** argv) {
    // Must be explicitly opted into a private server; never use a desktop default.
    const char* server=getenv("PULSE_SERVER");
    if (!server || std::string(server).find("mixer-test-pulse") == std::string::npos) { std::cerr<<"Refusing integration test outside private mixer-test-pulse server\n"; return 2; }
    try {
        if (argc == 2 && std::string(argv[1]) == "--unavailable-reset") {
            auto start = std::chrono::steady_clock::now();
            { PulseBackend unavailable(true); }
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(3)) throw std::runtime_error("Unavailable exit reset exceeded shutdown bound");
            std::cout << "PASS unavailable exit reset is bounded\n";
            return 0;
        }
        PulseBackend backend;
        auto first=std::make_unique<Playback>("Mixer Test A"); Playback second("Mixer Test B");
        auto a=named(backend,"Mixer Test A"), b=named(backend,"Mixer Test B");
        int originalB=volumePercent(b);
        External external;
        auto originalSink=external.sinkVolume();
        if(!backend.submit({a.key,CommandKind::Volume,35})) throw std::runtime_error("Volume enqueue failed");
        wait([&] {auto s=backend.snapshot(); auto* v=findStream(s,a.key); return v && volumePercent(*v)==35 && !v->pending;},"Volume readback failed");
        if(volumePercent(named(backend,"Mixer Test B"))!=originalB) throw std::runtime_error("Unrelated stream changed");
        backend.submit({a.key,CommandKind::Mute,1});
        wait([&] {auto s=backend.snapshot(); auto* v=findStream(s,a.key); return v && v->muted;},"Mute failed");
        backend.submit({a.key,CommandKind::Volume,65});
        wait([&] {auto s=backend.snapshot(); auto* v=findStream(s,a.key); return v && v->muted && volumePercent(*v)==65;},"Volume changed mute state");
        if (external.sinkVolume()!=originalSink) throw std::runtime_error("Master sink volume changed");
        external.volume(a.key.index,80);
        wait([&] {auto s=backend.snapshot(); auto* v=findStream(s,a.key); return v && volumePercent(*v)==80;},"External change not observed");
        first.reset();
        wait([&] {return !findStream(backend.snapshot(),a.key);},"Removed stream retained");
        if(backend.submit({a.key,CommandKind::Volume,100})) throw std::runtime_error("Removed token accepted");
        first=std::make_unique<Playback>("Mixer Test A"); auto replacement=named(backend,"Mixer Test A");
        if(replacement.key==a.key) throw std::runtime_error("Recreated stream token reused");
        for(int i=0;i<1000;++i) backend.submit({replacement.key,CommandKind::Volume,i%101});
        backend.submit({replacement.key,CommandKind::Volume,42});
        wait([&] {auto s=backend.snapshot(); auto* v=findStream(s,replacement.key); return v && volumePercent(*v)==42 && !v->pending;},"Coalesced final value lost");
        backend.reconnect();
        wait([&] {auto s=backend.snapshot(); for(auto& v:s.streams) if(v.application=="Mixer Test A" && v.key.generation!=replacement.key.generation) return true; return false;},"Reconnect discovery failed");
        if(backend.submit({replacement.key,CommandKind::Mute,1})) throw std::runtime_error("Old connection token accepted");
        auto currentA = named(backend,"Mixer Test A");
        b = named(backend,"Mixer Test B");
        external.volume(currentA.key.index,35);
        external.volume(b.key.index,15);
        backend.submit({currentA.key,CommandKind::Mute,1});
        wait([&] {auto s=backend.snapshot(); auto* v=findStream(s,currentA.key); return v && v->muted && volumePercent(*v)==35;},"Shutdown fixture not ready");
        {
            PulseBackend readOnly;
            named(readOnly,"Mixer Test A");
        }
        if (volumePercent(named(backend,"Mixer Test A")) != 35) throw std::runtime_error("Read-only backend reset a volume");
        {
            PulseBackend interactive(true);
            auto target = named(interactive,"Mixer Test A");
            // Leave commands queued/in flight as Quit is pressed. Reset must be last.
            for (int i=0; i<1000; ++i) interactive.submit({target.key,CommandKind::Volume,i%20});
        }
        wait([&] {
            auto s=backend.snapshot(); auto* va=findStream(s,currentA.key); auto* vb=findStream(s,b.key);
            return va && vb && volumePercent(*va)==100 && volumePercent(*vb)==100 && va->muted && !vb->muted;
        },"Exit reset failed, replayed a drag, or changed mute state");
        if (external.sinkVolume()!=originalSink) throw std::runtime_error("Exit reset changed master sink");
        {
            SyntheticStream source(external,"ProbeBrowser","ProbeBrowser","mixer_test");
            const auto stem="ProbeBrowser."+std::to_string(source.index())+".spatialize_filter_chain";
            const auto captureName="synthetic_capture_"+std::to_string(source.index());
            const auto module=external.loadSink(captureName,"node.name="+stem+".capture node.virtual=true node.link-group=synthetic-filter-group client.id=synthetic-owner");
            external.moveStream(source.index(),captureName);
            {
                SyntheticStream filter(external,stem+".playback","","mixer_test",true);
                Key filterKey;
                wait([&] {
                    auto s=backend.snapshot();
                    for (const auto& v:s.streams) if (v.key.index==filter.index()) {
                        filterKey=v.key;
                        return matchedSpatializers(s).contains(v.key);
                    }
                    return false;
                },"Synthetic spatializer route not matched from Pulse metadata");
                auto snapshot=backend.snapshot(); Interaction view; view.update(snapshot);
                if (view.visibleStreams().size()+1 != snapshot.streams.size()) throw std::runtime_error("Matched spatializer not hidden only from UI");
                backend.submit({filterKey,CommandKind::Volume,23});
                wait([&] {auto s=backend.snapshot(); auto* v=findStream(s,filterKey); return v && volumePercent(*v)==23;},"Synthetic filter gain fixture failed");
                {
                    PulseBackend interactive(true);
                    wait([&] {return !matchedSpatializers(interactive.snapshot()).empty();},"Interactive route match missing");
                }
                wait([&] {auto s=backend.snapshot(); auto* v=findStream(s,filterKey); return v && volumePercent(*v)==100;},"Hidden spatializer excluded from exit reset");
                external.moveStream(source.index(),"mixer_test");
                wait([&] {return matchedSpatializers(backend.snapshot()).empty();},"Route change retained stale spatializer match");
                view.update(backend.snapshot());
                if (view.visibleStreams().size()!=view.snapshot().streams.size()) throw std::runtime_error("Unmatched filter not restored to UI");
            }
            external.unloadSink(module);
        }
        wait([&] {pa_mainloop_iterate(external.loop,0,nullptr); return backend.snapshot().streams.size()==2;},"Synthetic topology cleanup not observed");
        wait([&] {auto s=backend.snapshot(); return s.master && s.master->name=="mixer_test";},"Default output missing");
        auto master = *backend.snapshot().master;
        if (!backend.submit({master.key,CommandKind::MasterVolume,41})) throw std::runtime_error("Master enqueue failed");
        wait([&] {auto s=backend.snapshot(); return s.master && volumePercent(*s.master)==41 && !s.master->pending;},"Master volume readback failed");
        if (volumePercent(named(backend,"Mixer Test A")) != 100 || volumePercent(named(backend,"Mixer Test B")) != 100) throw std::runtime_error("Master volume changed app gain");
        backend.submit({master.key,CommandKind::MasterMute,1});
        wait([&] {auto s=backend.snapshot(); return s.master && s.master->muted;},"Master mute failed");
        backend.submit({master.key,CommandKind::MasterVolume,37});
        wait([&] {auto s=backend.snapshot(); return s.master && s.master->muted && volumePercent(*s.master)==37;},"Master gain changed mute");
        external.sinkVolume(68); external.sinkMute(false);
        wait([&] {auto s=backend.snapshot(); return s.master && !s.master->muted && volumePercent(*s.master)==68;},"External master edit not reflected");
        if (backend.submit({master.key,CommandKind::Volume,10}) || backend.submit({currentA.key,CommandKind::MasterVolume,10})) throw std::runtime_error("Master/app target namespaces mixed");
        external.defaultSink("mixer_test_alt");
        wait([&] {auto s=backend.snapshot(); return s.master && s.master->name=="mixer_test_alt";},"New default output not followed");
        auto alternate = *backend.snapshot().master;
        if (backend.submit({master.key,CommandKind::MasterVolume,1})) throw std::runtime_error("Previous default output token accepted");
        backend.submit({alternate.key,CommandKind::MasterVolume,29});
        wait([&] {auto s=backend.snapshot(); return s.master && volumePercent(*s.master)==29;},"Alternate master write failed");
        if (volumePercent(external.readSink()) != 68) throw std::runtime_error("Master edited previous default output");
        external.defaultSink("mixer_test");
        wait([&] {auto s=backend.snapshot(); return s.master && s.master->name=="mixer_test";},"Original default output not restored");
        if (backend.snapshot().master->key==master.key || backend.submit({master.key,CommandKind::MasterVolume,1})) throw std::runtime_error("A-B-A default change reused a stale target epoch");
        auto beforeReconnect=backend.snapshot().master->key;
        backend.reconnect();
        wait([&] {auto s=backend.snapshot(); return s.connected && s.master && s.master->key.generation!=beforeReconnect.generation;},"Master reconnect failed");
        if (backend.submit({beforeReconnect,CommandKind::MasterMute,1}) || volumePercent(external.readSink())!=68) throw std::runtime_error("Reconnect accepted stale master target or changed gain");
        currentA=named(backend,"Mixer Test A");
        {
            PulseBackend interactive(true);
            named(interactive,"Mixer Test A");
            wait([&] {return interactive.snapshot().master.has_value();},"Interactive default output missing");
            auto m = *interactive.snapshot().master;
            interactive.submit({m.key,CommandKind::MasterVolume,54});
            interactive.submit({m.key,CommandKind::MasterMute,1});
            wait([&] {auto s=interactive.snapshot(); return s.master && s.master->muted && volumePercent(*s.master)==54 && !s.master->pending;},"Master exit fixture failed");
            auto target = named(interactive,"Mixer Test A");
            interactive.submit({target.key,CommandKind::Volume,12});
            wait([&] {auto s=interactive.snapshot(); auto* v=findStream(s,target.key); return v && volumePercent(*v)==12;},"App exit fixture failed");
        }
        wait([&] {auto s=backend.snapshot(); auto* v=findStream(s,currentA.key); return v && volumePercent(*v)==100 && s.master && volumePercent(*s.master)==54 && s.master->muted;},"Exit reset changed master volume/mute or failed to restore app");
        if (volumePercent(external.readSink("mixer_test_alt")) != 29) throw std::runtime_error("Exit changed non-default sink");
        std::cout<<"PASS app/reset, spatializer matching/route changes/hidden reset, master volume/mute/external edits/default changes/stale-target rejection, master preserved on exit\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
