#pragma once
#include "model.h"
#include <memory>

namespace mixer {
class PulseBackend {
public:
    // Opt-in only for the interactive mixer; diagnostics must remain read-only.
    explicit PulseBackend(bool resetVolumesOnExit = false);
    ~PulseBackend();
    PulseBackend(const PulseBackend&) = delete;
    PulseBackend& operator=(const PulseBackend&) = delete;
    Snapshot snapshot() const;
    bool submit(Command command);
    void reconnect();
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
