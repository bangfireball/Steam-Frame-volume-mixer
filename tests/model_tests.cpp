#include "model.h"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace mixer;
#define CHECK(...) do { if (!(__VA_ARGS__)) throw std::runtime_error("Check failed at line " + std::to_string(__LINE__) + ": " #__VA_ARGS__); ++checks; } while (false)
int main() {
    int checks=0;
    try {
        CHECK(scaleVolume({normalVolume,normalVolume/2},50) == std::vector<std::uint32_t>({normalVolume/2,normalVolume/4}));
        CHECK(scaleVolume({0,0},100) == std::vector<std::uint32_t>({normalVolume,normalVolume}));
        CHECK(scaleVolume({normalVolume},-10) == std::vector<std::uint32_t>({0}));
        CHECK(scaleVolume({normalVolume},120) == std::vector<std::uint32_t>({normalVolume}));
        CHECK(scaleVolume({},70).empty());
        CHECK(scaleVolume({UINT32_MAX,0},100) == std::vector<std::uint32_t>({normalVolume,0}));
        Stream a{{1,1,17},"Chromium","Music","app",{normalVolume,normalVolume/2},true,false,true,false};
        CHECK(volumePercent(a)==100);
        CHECK(a.muted);
        auto over=a; over.channels={normalVolume*2}; CHECK(volumePercent(over)==200);
        CHECK(safeLabel("hello\nworld\t") == "helloworld");
        CHECK(safeLabel("\xff") == "?");
        CHECK(safeLabel("a\xc3\xa9",2)=="a");
        CHECK(safeLabel("a\xc3\xa9",3)=="a\xc3\xa9");
        CHECK(safeLabel("\xc0\x80") == "??");
        CHECK(safeLabel("") == "Unknown application");
        Mailbox box;
        for (int i=0;i<1000;++i) CHECK(box.push({a.key,CommandKind::Volume,i}));
        CHECK(box.size()==1);
        auto commands=box.take(); CHECK(commands.size()==1 && commands[0].value==999); CHECK(box.size()==0);
        for (int i=0;i<128;++i) CHECK(box.push({Key{1,static_cast<std::uint64_t>(i),static_cast<std::uint32_t>(i)},CommandKind::Volume,30}));
        CHECK(!box.push({Key{2,999,999},CommandKind::Mute,1}));
        box.clear(); CHECK(box.size()==0);
        Snapshot s; s.connected=true; s.streams={a};
        const int row = Interaction::rowY+10, masterRow = Interaction::masterY+10;
        Interaction ui; ui.update(s);
        auto c=ui.down(Interaction::sliderX,row); CHECK(c && c->key==a.key && c->value==0);
        c=ui.move(10000); CHECK(c && c->value==100);
        CHECK(!ui.move(std::numeric_limits<float>::quiet_NaN()));
        c=ui.up(725,row); CHECK(c && c->value==50); CHECK(!ui.dragging());
        CHECK(a.muted); // Volume command has no mute side effect.
        CHECK(!ui.down(500,row)); c=ui.up(500,row); CHECK(c && c->kind==CommandKind::Mute && c->value==0);
        ui.down(500,row); CHECK(!ui.up(500,row+Interaction::rowHeight));
        ui.down(725,row); ui.cancel(); CHECK(!ui.move(800));
        ui.down(725,row); s.streams[0].key.incarnation=2; ui.update(s); CHECK(!ui.dragging()); CHECK(!ui.move(800));
        s.streams[0].writable=false; ui.update(s); CHECK(!ui.down(725,row));
        s.connected=false; ui.update(s); CHECK(!ui.down(500,row));
        s.connected=true; s.streams.clear();
        for (int i=0;i<14;++i) { auto v=a; v.key.incarnation=i+1; s.streams.push_back(v); }
        ui.update(s); ui.page(1); CHECK(ui.offset()==5); ui.page(1); CHECK(ui.offset()==10); ui.page(1); CHECK(ui.offset()==10);
        ui.page(-1); CHECK(ui.offset()==5); ui.down(725,row); CHECK(ui.dragging()==s.streams[5].key);
        // Creation order insertion/removal does not retarget an ongoing drag.
        s.streams.erase(s.streams.begin()); ui.update(s); CHECK(ui.dragging().has_value());
        c=ui.move(800); CHECK(c && c->key.incarnation==6);
        s.streams.clear(); ui.update(s); CHECK(ui.offset()==0); CHECK(!ui.dragging());
        CHECK(!findStream(s,a.key));
        CHECK(!ui.down(725,masterRow)); // No default output: never fall back to an app.
        Sink master; master.key={1,900,17}; master.channels={normalVolume/2,normalVolume/4}; master.writable=true;
        s.master=master; ui.update(s);
        CHECK(volumePercent(*s.master)==50);
        c=ui.down(725,masterRow); CHECK(c && c->key==master.key && c->kind==CommandKind::MasterVolume && c->value==50);
        CHECK(ui.draggingMaster()); c=ui.up(870,masterRow); CHECK(c && c->value==100);
        ui.down(500,masterRow); c=ui.up(500,masterRow); CHECK(c && c->kind==CommandKind::MasterMute && c->value==1);
        ui.down(500,masterRow); CHECK(!ui.up(500,row));
        ui.down(725,masterRow); s.master->key.incarnation++; ui.update(s); CHECK(!ui.dragging()); CHECK(!ui.move(800));
        ui.down(725,masterRow); s.master->writable=false; ui.update(s); CHECK(!ui.dragging()); CHECK(!ui.down(725,masterRow));
        ui.down(500,masterRow); s.connected=false; ui.update(s); CHECK(!ui.up(500,masterRow)); s.connected=true;
        s.master.reset(); ui.update(s); CHECK(!ui.down(500,masterRow));
        CHECK(isMasterCommand(CommandKind::MasterVolume)); CHECK(!isMasterCommand(CommandKind::Volume));

        Snapshot graph; graph.connected=true;
        Stream firefox=a; firefox.key={1,1,200}; firefox.application="Firefox"; firefox.media="Music";
        firefox.applicationIdentified=true; firefox.sinkIndex=202; firefox.routing.nodeName="Firefox";
        Stream filter=a; filter.key={1,2,203}; filter.application="Unknown"; filter.applicationIdentified=false; filter.sinkIndex=151; filter.muted=false;
        filter.routing={"Firefox.200.spatialize_filter_chain.playback","","filter-group","176",true,true};
        Stream independent=a; independent.key={1,3,217}; independent.application="Valve VRLink"; independent.sinkIndex=151;
        Sink capture; capture.key={1,4,202}; capture.routing={"Firefox.200.spatialize_filter_chain.capture","","filter-group","176",true,false};
        graph.streams={firefox,filter,independent}; graph.sinks={capture};
        auto matches=matchedSpatializers(graph); CHECK(matches.size()==1 && matches.at(filter.key)==firefox.key);
        Interaction filtered; filtered.update(graph); CHECK(filtered.visibleStreams().size()==2); CHECK(filtered.snapshot().streams.size()==3);
        CHECK(filtered.visibleStreams()[1].key==independent.key);
        filtered.toggleInternals(); CHECK(filtered.visibleStreams().size()==3); CHECK(filtered.visibleStreams()[1].application=="Firefox (spatializer)");
        c=filtered.down(725,row+Interaction::rowHeight); CHECK(c && c->key==filter.key);
        filtered.toggleInternals(); CHECK(!filtered.dragging()); CHECK(filtered.visibleStreams().size()==2);
        graph.streams[1].channels={normalVolume*35/100}; filtered.update(graph);
        CHECK(filtered.visibleStreams()[0].media.find("Spatializer 35%")!=std::string::npos);
        graph.streams[1].muted=true; filtered.update(graph); CHECK(filtered.visibleStreams()[0].media.find("Spatializer muted")!=std::string::npos);
        auto uncertain=graph; uncertain.sinks.clear(); CHECK(matchedSpatializers(uncertain).empty());
        filtered.update(uncertain); CHECK(filtered.visibleStreams().size()==3);
        c=filtered.down(725,row+Interaction::rowHeight); CHECK(c && c->key==filter.key);
        filtered.update(graph); CHECK(!filtered.dragging()); // Matching arrival cancels a now-hidden row drag.
        filtered.down(725,row); CHECK(filtered.dragging().has_value());
        auto readonly=graph; readonly.streams[0].writable=false; filtered.update(readonly); CHECK(!filtered.dragging());
        uncertain=graph; uncertain.streams[0].sinkIndex=999; CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.streams[0].key.index=201; CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.streams[0].writable=false; CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.streams[1].applicationIdentified=true; CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.streams[1].routing.virtualNode=false; CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.streams[1].routing.passiveNode=false; CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.streams[1].routing.linkGroup.clear(); CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.sinks[0].routing.linkGroup="other"; CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.sinks[0].routing.clientId="other"; CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.sinks[0].key.generation=2; CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.streams[0].key.generation=2; CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.streams[1].key.generation=2; CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.sinks[0].routing.clientId.clear(); CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.sinks[0].routing.virtualNode=false; CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.streams[1].sinkIndex=202; CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.streams[2].sinkIndex=202; CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; auto secondFilter=filter; secondFilter.key.incarnation=100; uncertain.streams.push_back(secondFilter); CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.sinks.push_back(capture); CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.connected=false; CHECK(matchedSpatializers(uncertain).empty());
        uncertain=graph; uncertain.streams.erase(uncertain.streams.begin()); CHECK(matchedSpatializers(uncertain).empty());
        // Other virtual or Unknown streams are retained; names alone never match.
        uncertain=graph; uncertain.streams[1].routing.nodeName="SomeOtherFilter.playback"; CHECK(matchedSpatializers(uncertain).empty());
        std::cout << "PASS " << checks << " model checks\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
