#include "instance.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <sys/stat.h>
using namespace mixer;
int main() {
    char path[]="/tmp/mixer-instance-XXXXXX";
    auto* dir=mkdtemp(path);
    if (!dir) return 1;
    try {
        {
            Instance primary; if (!primary.acquire(dir)) throw std::runtime_error("Not primary");
            Instance secondary; if (secondary.acquire(dir)) throw std::runtime_error("Duplicate primary");
            if (!primary.pollShow()) throw std::runtime_error("SHOW not delivered");
            if (primary.pollShow()) throw std::runtime_error("SHOW delivered twice");
        }
        {
            // Stale socket after an unclean exit is replaced only by lock owner.
            Instance restarted; if (!restarted.acquire(dir)) throw std::runtime_error("Restart not primary");
        }
        chmod(dir,0777);
        bool rejected=false;
        try { Instance unsafe; unsafe.acquire(dir); } catch (...) { rejected=true; }
        if (!rejected) throw std::runtime_error("Unsafe runtime accepted");
        chmod(dir,0700); std::filesystem::remove_all(dir);
        std::cout<<"PASS instance lock, SHOW, restart, unsafe directory\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; chmod(dir,0700); std::filesystem::remove_all(dir); return 1; }
}
