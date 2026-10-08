#pragma once
#include <string>
namespace mixer {
class Instance {
public:
    // Returns false after successfully asking an existing instance to show.
    bool acquire(const std::string& runtimeDirectory);
    bool pollShow();
    ~Instance();
    Instance() = default;
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
private:
    int lock_{-1}, socket_{-1};
    std::string socketPath_;
    bool owner_{};
};
}
