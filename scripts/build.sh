#!/bin/sh
# Run inside scripts/Dockerfile's build image.
set -eu
cmake -S . -B out/linux-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/linux-x64 -j "${JOBS:-4}"
ctest --test-dir out/linux-x64 --output-on-failure
cmake -S . -B out/linux-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/linux-arm64.cmake
cmake --build out/linux-arm64 -j "${JOBS:-4}"
# Match the arm64 distro loader to its multiarch glibc, not the older cross sysroot loader.
run_arm() {
    timeout -k 2s 30s qemu-aarch64 -L / -E LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu:/usr/aarch64-linux-gnu/lib "$@"
}
run_arm out/linux-arm64/model_tests
run_arm out/linux-arm64/instance_tests
run_arm out/linux-arm64/frame-volume-mixer --version
run_arm out/linux-arm64/frame-volume-mixer --preview out/mixer-arm64.ppm
python3 scripts/generate-icons.py
version=$(out/linux-x64/frame-volume-mixer --version)
name="frame-volume-mixer-arm64-$version"
mkdir -p out/packages
rm -rf "out/stage/$name"
cmake --install out/linux-arm64 --prefix "/work/out/stage/$name"
find "out/stage/$name" -type d -exec chmod 755 {} +
find "out/stage/$name" -type f -exec chmod 644 {} +
chmod 755 "out/stage/$name/frame-volume-mixer" "out/stage/$name/launch.sh" "out/stage/$name/install.sh" "out/stage/$name/uninstall.sh"
file "out/stage/$name/frame-volume-mixer"
tar -czf "out/packages/$name.tar.gz" -C out/stage "$name"
sha256sum "out/packages/$name.tar.gz" >"out/packages/$name.tar.gz.sha256"
printf 'Package: out/packages/%s.tar.gz\n' "$name"
