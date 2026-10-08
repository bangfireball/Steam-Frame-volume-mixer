#include "model.h"
#include <cmath>
#include <limits>

namespace mixer {
std::vector<std::uint32_t> scaleVolume(const std::vector<std::uint32_t>& channels, int percent) {
    if (channels.empty()) return {};
    const auto target = static_cast<std::uint32_t>(std::clamp(percent, 0, 100) * static_cast<std::uint64_t>(normalVolume) / 100);
    const auto max = *std::max_element(channels.begin(), channels.end());
    auto result = channels;
    for (auto& v : result) v = max ? static_cast<std::uint32_t>(static_cast<std::uint64_t>(v) * target / max) : target;
    return result;
}
namespace {
int channelPercent(const std::vector<std::uint32_t>& channels) {
    if (channels.empty()) return 0;
    return static_cast<int>((static_cast<std::uint64_t>(*std::max_element(channels.begin(), channels.end())) * 100 + normalVolume / 2) / normalVolume);
}
}
int volumePercent(const Stream& s) { return channelPercent(s.channels); }
int volumePercent(const Sink& s) { return channelPercent(s.channels); }
bool isMasterCommand(CommandKind kind) { return kind == CommandKind::MasterVolume || kind == CommandKind::MasterMute; }
std::map<Key, Key> matchedSpatializers(const Snapshot& snapshot) {
    std::map<Key, Key> matches;
    if (!snapshot.connected) return matches;
    for (const auto& output : snapshot.streams) {
        if (output.applicationIdentified || !output.routing.virtualNode || !output.routing.passiveNode || output.routing.linkGroup.empty() || output.routing.clientId.empty()) continue;
        const Stream* source = nullptr;
        unsigned candidates = 0;
        for (const auto& sink : snapshot.sinks) {
            if (!sink.routing.virtualNode || sink.routing.linkGroup != output.routing.linkGroup || sink.routing.clientId != output.routing.clientId || sink.key.generation != output.key.generation) continue;
            for (const auto& upstream : snapshot.streams) {
                if (upstream.key == output.key || upstream.key.generation != output.key.generation || upstream.sinkIndex != sink.key.index || !upstream.applicationIdentified || !upstream.writable || upstream.routing.nodeName.empty()) continue;
                const auto stem = upstream.routing.nodeName + "." + std::to_string(upstream.key.index) + ".spatialize_filter_chain";
                if (sink.routing.nodeName != stem + ".capture" || output.routing.nodeName != stem + ".playback") continue;
                // Never hide a shared/ambiguous pipeline or a self-linked output.
                const auto inputs = std::count_if(snapshot.streams.begin(), snapshot.streams.end(), [&](const Stream& s) { return s.sinkIndex == sink.key.index; });
                const auto outputs = std::count_if(snapshot.streams.begin(), snapshot.streams.end(), [&](const Stream& s) { return s.routing.linkGroup == output.routing.linkGroup && s.routing.nodeName == output.routing.nodeName; });
                if (inputs != 1 || outputs != 1 || output.sinkIndex == sink.key.index) continue;
                source = &upstream; ++candidates;
            }
        }
        if (candidates == 1 && source) matches.emplace(output.key, source->key);
    }
    return matches;
}
// Validate UTF-8, remove control characters, and truncate only at codepoint boundaries.
std::string safeLabel(std::string text, std::size_t limit) {
    std::string out;
    for (std::size_t i = 0; i < text.size() && out.size() < limit;) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 32 || c == 127) { ++i; continue; }
        std::size_t n = c < 128 ? 1 : c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        bool valid = n && i + n <= text.size();
        for (std::size_t j = 1; valid && j < n; ++j) valid = (static_cast<unsigned char>(text[i+j]) & 0xc0) == 0x80;
        if (valid && n >= 3) {
            const auto d = static_cast<unsigned char>(text[i+1]);
            valid = !(c == 0xe0 && d < 0xa0) && !(c == 0xed && d >= 0xa0) && !(c == 0xf0 && d < 0x90) && !(c == 0xf4 && d >= 0x90);
        }
        if (!valid) { out += '?'; ++i; continue; }
        if (out.size() + n > limit) break;
        out.append(text, i, n); i += n;
    }
    return out.empty() ? "Unknown application" : out;
}
const Stream* findStream(const Snapshot& s, Key k) {
    auto it = std::find_if(s.streams.begin(), s.streams.end(), [&](const Stream& v) { return v.key == k; });
    return it == s.streams.end() ? nullptr : &*it;
}
bool Mailbox::push(Command c) {
    auto key = std::pair(c.key, c.kind);
    if (commands_.size() >= 128 && !commands_.contains(key)) return false;
    commands_[key] = c; return true;
}
std::vector<Command> Mailbox::take() {
    std::vector<Command> result;
    for (const auto& [k, v] : commands_) { (void)k; result.push_back(v); }
    commands_.clear(); return result;
}
bool Interaction::visible(Key key) const {
    return std::any_of(visibleStreams_.begin(), visibleStreams_.end(), [&](const Stream& s) { return s.key == key; });
}
void Interaction::rebuildVisible() {
    const auto matches = matchedSpatializers(snapshot_);
    visibleStreams_.clear();
    for (auto stream : snapshot_.streams) {
        auto match = matches.find(stream.key);
        if (match != matches.end()) {
            if (!showInternals_) continue;
            const auto* source = findStream(snapshot_, match->second);
            stream.application = source->application + " (spatializer)";
            stream.media = "Internal spatial-audio playback";
        } else {
            for (const auto& [child, parent] : matches) if (parent == stream.key) {
                const auto* filter = findStream(snapshot_, child);
                if (filter->muted) stream.media = "Spatializer muted · " + stream.media;
                else if (volumePercent(*filter) != 100) stream.media = "Spatializer " + std::to_string(volumePercent(*filter)) + "% · " + stream.media;
            }
        }
        visibleStreams_.push_back(std::move(stream));
    }
    if (offset_ >= visibleStreams_.size()) offset_ = visibleStreams_.empty() ? 0 : ((visibleStreams_.size()-1) / rows) * rows;
}
void Interaction::update(Snapshot s) {
    snapshot_ = std::move(s); rebuildVisible();
    if (!snapshot_.connected) { cancel(); return; }
    const auto* draggedStream = drag_ ? findStream(snapshot_, *drag_) : nullptr;
    if (drag_ && (dragMaster_ ? !snapshot_.master || snapshot_.master->key != *drag_ || !snapshot_.master->writable : !draggedStream || !draggedStream->writable || !visible(*drag_))) cancel();
    if (mutePress_ && (muteMaster_ ? !snapshot_.master || snapshot_.master->key != *mutePress_ : !visible(*mutePress_))) mutePress_.reset();
}
void Interaction::toggleInternals() { cancel(); showInternals_ = !showInternals_; rebuildVisible(); }
std::optional<Command> Interaction::down(float x, float y) {
    cancel();
    if (!snapshot_.connected || !std::isfinite(x) || !std::isfinite(y)) return {};
    if (y >= masterY && y < masterY + rowHeight) {
        if (!snapshot_.master) return {};
        const auto& s = *snapshot_.master;
        if (x >= sliderX-15 && x <= sliderEnd+15 && s.writable) { drag_ = s.key; dragMaster_ = true; return dragTo(x); }
        if (x >= 465 && x <= 550) { mutePress_ = s.key; muteMaster_ = true; }
        return {};
    }
    if (y < rowY || y >= rowY + rows*rowHeight) return {};
    auto index = offset_ + static_cast<std::size_t>((y-rowY)/rowHeight);
    if (index >= visibleStreams_.size()) return {};
    const auto& s = visibleStreams_[index];
    if (x >= sliderX-15 && x <= sliderEnd+15 && s.writable) { drag_ = s.key; return dragTo(x); }
    if (x >= 465 && x <= 550) mutePress_ = s.key;
    return {};
}
std::optional<Command> Interaction::dragTo(float x) {
    if (!drag_ || !std::isfinite(x)) return {};
    bool writable = false;
    if (dragMaster_) writable = snapshot_.master && snapshot_.master->key == *drag_ && snapshot_.master->writable;
    else if (const auto* s = findStream(snapshot_, *drag_)) writable = s->writable && visible(s->key);
    if (!writable || !snapshot_.connected) { cancel(); return {}; }
    dragPercent_ = static_cast<int>(std::lround(std::clamp((x-sliderX)/(sliderEnd-sliderX), 0.0f, 1.0f)*100));
    return Command{*drag_, dragMaster_ ? CommandKind::MasterVolume : CommandKind::Volume, dragPercent_};
}
std::optional<Command> Interaction::move(float x) { return dragTo(x); }
std::optional<Command> Interaction::up(float x, float y) {
    auto result = dragTo(x);
    if (mutePress_ && std::isfinite(x) && std::isfinite(y) && x >= 465 && x <= 550) {
        if (muteMaster_ && y >= masterY && y < masterY+rowHeight && snapshot_.master && snapshot_.master->key == *mutePress_) result = Command{*mutePress_, CommandKind::MasterMute, !snapshot_.master->muted};
        else if (!muteMaster_ && y >= rowY && y < rowY+rows*rowHeight) {
            auto i = offset_ + static_cast<std::size_t>((y-rowY)/rowHeight);
            if (i < visibleStreams_.size() && visibleStreams_[i].key == *mutePress_) result = Command{*mutePress_, CommandKind::Mute, !visibleStreams_[i].muted};
        }
    }
    cancel(); return result;
}
void Interaction::cancel() { drag_.reset(); mutePress_.reset(); dragMaster_ = muteMaster_ = false; }
void Interaction::page(int direction) {
    cancel();
    if (direction > 0 && offset_+rows < visibleStreams_.size()) offset_ += rows;
    if (direction < 0 && offset_ >= rows) offset_ -= rows;
}
}
