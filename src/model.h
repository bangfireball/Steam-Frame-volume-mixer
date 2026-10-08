#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mixer {
constexpr std::uint32_t normalVolume = 65536;
struct Key {
    std::uint64_t generation{}, incarnation{};
    std::uint32_t index{};
    auto operator<=>(const Key&) const = default;
};
struct RoutingMetadata {
    std::string nodeName, description, linkGroup, clientId;
    bool virtualNode{}, passiveNode{};
    auto operator<=>(const RoutingMetadata&) const = default;
};
struct Stream {
    Key key;
    std::string application, media, applicationId;
    std::vector<std::uint32_t> channels;
    bool muted{}, corked{}, writable{};
    bool pending{};
    std::uint32_t sinkIndex{UINT32_MAX};
    bool applicationIdentified{};
    RoutingMetadata routing{};
    auto operator<=>(const Stream&) const = default;
};
struct Sink {
    Key key;
    std::string name, description;
    RoutingMetadata routing{};
    std::vector<std::uint32_t> channels;
    bool muted{}, writable{}, pending{};
    auto operator<=>(const Sink&) const = default;
};
struct Snapshot {
    std::uint64_t revision{};
    bool connected{};
    std::string status{"Connecting to audio server"}, error;
    std::vector<Stream> streams; // Always includes internal streams for diagnostics/reset.
    std::vector<Sink> sinks;
    std::optional<Sink> master; // Current default output only; absence disables controls.
};
enum class CommandKind { Volume, Mute, MasterVolume, MasterMute };
struct Command { Key key; CommandKind kind; int value{}; };
std::vector<std::uint32_t> scaleVolume(const std::vector<std::uint32_t>& channels, int percent);
int volumePercent(const Stream& stream);
int volumePercent(const Sink& sink);
bool isMasterCommand(CommandKind kind);
// Conservative matches: internal playback key -> real upstream application key.
std::map<Key, Key> matchedSpatializers(const Snapshot& snapshot);
std::string safeLabel(std::string text, std::size_t maxBytes = 160);
const Stream* findStream(const Snapshot& snapshot, Key key);
// Bounded latest-value mailbox. Tokens must still be checked at dispatch.
class Mailbox {
public:
    bool push(Command command);
    std::vector<Command> take();
    void clear() { commands_.clear(); }
    std::size_t size() const { return commands_.size(); }
private:
    std::map<std::pair<Key, CommandKind>, Command> commands_;
};
// Portable dashboard interaction: a fixed master row and five app rows per page.
class Interaction {
public:
    static constexpr int width = 1000, height = 700, masterY = 110, rowY = 218, rowHeight = 70, rows = 5;
    static constexpr int sliderX = 580, sliderEnd = 870;
    void update(Snapshot snapshot);
    std::optional<Command> down(float x, float y);
    std::optional<Command> move(float x);
    std::optional<Command> up(float x, float y);
    void cancel();
    void page(int direction);
    const Snapshot& snapshot() const { return snapshot_; }
    const std::vector<Stream>& visibleStreams() const { return visibleStreams_; }
    void toggleInternals();
    bool showInternals() const { return showInternals_; }
    bool draggingMaster() const { return drag_ && dragMaster_; }
    std::size_t offset() const { return offset_; }
    std::optional<Key> dragging() const { return drag_; }
    int dragPercent() const { return dragPercent_; }
private:
    std::optional<Command> dragTo(float x);
    void rebuildVisible();
    bool visible(Key key) const;
    Snapshot snapshot_;
    std::vector<Stream> visibleStreams_;
    bool showInternals_{}, dragMaster_{}, muteMaster_{};
    std::size_t offset_{};
    std::optional<Key> drag_, mutePress_;
    int dragPercent_{};
};
}
