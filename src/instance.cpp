#include "instance.h"
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace mixer {
namespace {
void privateDirectory(const std::string& path, bool create) {
    if (create && mkdir(path.c_str(), 0700) < 0 && errno != EEXIST) throw std::runtime_error("Cannot create private runtime directory");
    struct stat st{};
    if (lstat(path.c_str(), &st) < 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 0077))
        throw std::runtime_error("Runtime directory must be a real, user-owned 0700 directory");
}
}
bool Instance::acquire(const std::string& runtimeDirectory) {
    if (lock_ >= 0) throw std::runtime_error("Instance already acquired");
    privateDirectory(runtimeDirectory, false);
    auto dir = runtimeDirectory + "/frame-volume-mixer";
    privateDirectory(dir, true);
    socketPath_ = dir + "/control.sock";
    sockaddr_un address{}; address.sun_family = AF_UNIX;
    if (socketPath_.size() >= sizeof(address.sun_path)) throw std::runtime_error("Runtime socket path too long");
    std::memcpy(address.sun_path, socketPath_.c_str(), socketPath_.size()+1);
    lock_ = open((dir+"/instance.lock").c_str(), O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW, 0600);
    struct stat st{};
    if (lock_ < 0 || fstat(lock_, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 0077))
        throw std::runtime_error("Cannot open private instance lock");
    if (flock(lock_, LOCK_EX|LOCK_NB) < 0) {
        if (errno != EWOULDBLOCK) throw std::runtime_error("Cannot lock mixer instance");
        for (int attempt=0; attempt<20; ++attempt) {
            int fd = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
            if (fd >= 0 && connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
                ucred credentials{}; socklen_t len = sizeof(credentials);
                bool trusted = getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &len) == 0 && credentials.uid == getuid();
                auto sent = trusted ? send(fd, "SHOW", 4, MSG_NOSIGNAL) : -1;
                close(fd);
                if (sent == 4) return false;
            } else if (fd >= 0) close(fd);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        throw std::runtime_error("Mixer is running but its control socket is unavailable");
    }
    owner_ = true;
    socket_ = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
    if (socket_ < 0) throw std::runtime_error("Cannot create control socket");
    unlink(socketPath_.c_str());
    if (bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || chmod(socketPath_.c_str(), 0600) < 0 || listen(socket_, 8) < 0)
        throw std::runtime_error("Cannot listen on private control socket");
    return true;
}
bool Instance::pollShow() {
    bool show = false;
    for (int i=0; i<8; ++i) {
        int fd = accept4(socket_, nullptr, nullptr, SOCK_CLOEXEC|SOCK_NONBLOCK);
        if (fd < 0) break;
        ucred credentials{}; socklen_t len = sizeof(credentials);
        char message[5]{};
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &len) == 0 && credentials.uid == getuid()) {
            // Launcher sends before returning; readiness may lag accept briefly.
            for (int retry=0; retry<3; ++retry) {
                auto n = recv(fd, message, sizeof(message), MSG_DONTWAIT);
                if (n >= 0) { show |= n == 4 && std::memcmp(message, "SHOW", 4) == 0; break; }
                if (errno != EAGAIN && errno != EWOULDBLOCK) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        close(fd);
    }
    return show;
}
Instance::~Instance() {
    if (socket_ >= 0) close(socket_);
    if (owner_ && !socketPath_.empty()) unlink(socketPath_.c_str());
    if (lock_ >= 0) close(lock_);
}
}
