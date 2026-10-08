include(FetchContent)
# OpenVR SDK 2.15.6. Build the client for the target architecture, not a PC-only binary.
FetchContent_Declare(openvr
  GIT_REPOSITORY https://github.com/ValveSoftware/openvr.git
  GIT_TAG 0924064316de3effbcd1acf1e309182a2deb1c05
  SOURCE_SUBDIR _no_automatic_subdirectory)
FetchContent_MakeAvailable(openvr)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_FRAMEWORK OFF CACHE BOOL "" FORCE)
set(USE_LIBCXX OFF CACHE BOOL "" FORCE)
add_subdirectory("${openvr_SOURCE_DIR}" "${openvr_BINARY_DIR}" EXCLUDE_FROM_ALL)
