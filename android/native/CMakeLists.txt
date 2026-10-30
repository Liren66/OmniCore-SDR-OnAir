# libonair.so for Android = the desktop receiver library (dect2core, from this repository) + a JNI bridge.
# The OnAir source tree is only read from here. By default it is this checkout itself (android/native/../..); ONAIR_SRC can point at
# another checkout or worktree. Built by android/tools/build_native.sh (see android/README.md and android/docs/USB.md).
cmake_minimum_required(VERSION 3.22)
project(onair_android LANGUAGES C CXX)

get_filename_component(_onair_repo "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(ONAIR_SRC "${_onair_repo}" CACHE PATH "OnAir repository root (holds the receiver library); default: this checkout")
if(NOT EXISTS "${ONAIR_SRC}/CMakeLists.txt" OR NOT EXISTS "${ONAIR_SRC}/core/include")
  message(FATAL_ERROR "ONAIR_SRC does not hold the OnAir sources: ${ONAIR_SRC}")
endif()

# USB radios (android/docs/USB.md). The radios come from the app as descriptors (UsbManager), never from a bus scan:
#  - RTL-SDR: the Rust driver over usbfs (DECT2_RUST_USB), always built.
#  - HackRF: OnAir's own driver on libusb. LIBUSB_SRC = a libusb 1.0.24+ source tree (git checkout of github.com/libusb/libusb, e.g. tag
#    v1.0.27): built here as libusb1.0.so with its Android config; without it the libusb stand-ins of src/stubs_ext.c are linked and a
#    HackRF says so when opened.
#  - Airspy R2/Mini, HF+: AIRSPY_SRC / AIRSPYHF_SRC = airspyone_host / airspyhf source trees (with airspy_open_fd / airspyhf_open_fd), built
#    as libairspy.so / libairspyhf.so against that libusb (they need LIBUSB_SRC). The core loads them at run time (source_airspy*.cpp).
set(LIBUSB_SRC "" CACHE PATH "libusb source tree (optional, enables HackRF and Airspy)")
set(AIRSPY_SRC "" CACHE PATH "airspyone_host source tree (optional, Airspy R2/Mini; needs LIBUSB_SRC)")
set(AIRSPYHF_SRC "" CACHE PATH "airspyhf source tree (optional, Airspy HF+; needs LIBUSB_SRC)")

# The core only: no desktop app, no tests, no SoapySDR or Hamlib (none of them exists for Android here). Rust is mandatory in the core
# (cmake/OnairRust.cmake): the target aarch64-linux-android must be installed (rustup target add aarch64-linux-android).
set(DECT2_BUILD_APP OFF CACHE BOOL "" FORCE)
set(DECT2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DECT2_WITH_SOAPY OFF CACHE BOOL "" FORCE)
set(DECT2_HAVE_HAMLIB OFF CACHE BOOL "" FORCE)
set(DECT2_RUST_USB "ON" CACHE STRING "" FORCE)   # the RTL-SDR on a descriptor (rtl_fd_shim.h)

set(_have_libusb OFF)
if(LIBUSB_SRC)
  if(NOT EXISTS "${LIBUSB_SRC}/libusb/core.c" OR NOT EXISTS "${LIBUSB_SRC}/android/config.h")
    message(FATAL_ERROR "LIBUSB_SRC is not a libusb source tree (libusb/core.c, android/config.h): ${LIBUSB_SRC}")
  endif()
  set(_lu "${LIBUSB_SRC}/libusb")
  # the file list of libusb's own android/jni/libusb.mk
  add_library(usb1.0 SHARED
    ${_lu}/core.c ${_lu}/descriptor.c ${_lu}/hotplug.c ${_lu}/io.c ${_lu}/sync.c ${_lu}/strerror.c
    ${_lu}/os/linux_usbfs.c ${_lu}/os/linux_netlink.c ${_lu}/os/events_posix.c ${_lu}/os/threads_posix.c)
  target_include_directories(usb1.0 PRIVATE "${LIBUSB_SRC}/android" "${_lu}" "${_lu}/os")
  target_include_directories(usb1.0 INTERFACE "${_lu}")
  target_compile_options(usb1.0 PRIVATE -O2 -fvisibility=default -Wno-unused-parameter)
  target_link_libraries(usb1.0 PRIVATE log)
  target_link_options(usb1.0 PRIVATE -Wl,-z,max-page-size=16384 -Wl,--build-id=none)
  set(_have_libusb ON)
  # (the core's HackRF driver compiles against the build machine's libusb.h, found by pkg-config: it must be 1.0.24 or later, the
  # version that has LIBUSB_OPTION_NO_DEVICE_DISCOVERY; Homebrew's is. The library ABI is libusb's stable one.)
endif()

add_subdirectory("${ONAIR_SRC}" "${CMAKE_BINARY_DIR}/onair-core" EXCLUDE_FROM_ALL)

# Linux pkg-config points at /usr/include/libhackrf, while core includes
# <libhackrf/hackrf.h>. Stage only that header instead of exposing host libc.
find_file(_android_hackrf_header hackrf.h HINTS ${HACKRF_INCLUDE_DIRS} PATH_SUFFIXES libhackrf NO_CMAKE_FIND_ROOT_PATH REQUIRED)
file(COPY "${_android_hackrf_header}" DESTINATION "${CMAKE_BINARY_DIR}/android-host-headers/libhackrf")
target_include_directories(dect2core PRIVATE "${CMAKE_BINARY_DIR}/android-host-headers")

option(ONAIR_ANDROID_OMNICORE "Build OmniCore Host with Android USB descriptor support" ON)
set(OMNICORE_HOST_SRC "" CACHE PATH "Unmodified OmniCore Host 0.4.8 source; downloaded when empty")
if(ONAIR_ANDROID_OMNICORE)
  if(NOT _have_libusb)
    message(FATAL_ERROR "OmniCore Android needs LIBUSB_SRC; its USB transport cannot use stubs")
  endif()
  if(NOT OMNICORE_HOST_SRC)
    include(FetchContent)
    FetchContent_Declare(omnicore_host_source
      URL https://github.com/Liren66/OmniCore-SDR-Host/archive/894ffd073ba0cf80312376f90e16e40ba1d88351.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_GetProperties(omnicore_host_source)
    if(NOT omnicore_host_source_POPULATED)
      FetchContent_Populate(omnicore_host_source)
    endif()
    set(OMNICORE_HOST_SRC "${omnicore_host_source_SOURCE_DIR}")
  endif()
  set(_och "${CMAKE_BINARY_DIR}/omnicore-host-android")
  # Patch a build-local copy. Never modify the user's desktop Host checkout.
  file(COPY "${OMNICORE_HOST_SRC}/src" "${OMNICORE_HOST_SRC}/include" DESTINATION "${_och}")
  foreach(_file src/core/api.cpp src/core/device.cpp src/transport/libusb.hpp)
    file(READ "${_och}/${_file}" _text)
    string(REPLACE "\r\n" "\n" _text "${_text}")
    file(WRITE "${_och}/${_file}" "${_text}")
  endforeach()
  find_program(PATCH_EXECUTABLE patch REQUIRED)
  execute_process(COMMAND "${PATCH_EXECUTABLE}" --batch -p1 -i "${CMAKE_CURRENT_SOURCE_DIR}/omnicore-host-android.patch"
    WORKING_DIRECTORY "${_och}" RESULT_VARIABLE _och_patch_result)
  if(NOT _och_patch_result EQUAL 0)
    message(FATAL_ERROR "OmniCore Android patch failed; use the pinned unmodified Host source")
  endif()
  add_library(omnicore SHARED
    ${_och}/src/core/api.cpp ${_och}/src/core/device.cpp ${_och}/src/core/configuration.cpp
    ${_och}/src/core/reconfigure.cpp ${_och}/src/core/stream.cpp ${_och}/src/core/rf.cpp
    ${_och}/src/core/power.cpp ${_och}/src/core/board.cpp
    ${_och}/src/dsp/codec.cpp ${_och}/src/dsp/framer.cpp)
  target_include_directories(omnicore PRIVATE "${_och}/include" "${_och}/src")
  target_compile_definitions(omnicore PRIVATE OCH_BUILD)
  target_compile_features(omnicore PRIVATE cxx_std_17)
  set_target_properties(omnicore PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
  target_link_libraries(omnicore PRIVATE usb1.0 log)
  target_link_options(omnicore PRIVATE -Wl,-z,max-page-size=16384 -Wl,--build-id=none)
endif()

if(_have_libusb AND AIRSPY_SRC)
  set(_as "${AIRSPY_SRC}/libairspy/src")
  add_library(airspy SHARED ${_as}/airspy.c ${_as}/iqconverter_float.c ${_as}/iqconverter_int16.c)
  target_include_directories(airspy PRIVATE "${_as}")
  target_compile_options(airspy PRIVATE -O2 -fvisibility=default)
  target_link_libraries(airspy PRIVATE usb1.0 log)
  target_link_options(airspy PRIVATE -Wl,-z,max-page-size=16384 -Wl,--build-id=none)
endif()
if(_have_libusb AND AIRSPYHF_SRC)
  set(_ah "${AIRSPYHF_SRC}/libairspyhf/src")
  add_library(airspyhf SHARED ${_ah}/airspyhf.c ${_ah}/iqbalancer.c)
  target_include_directories(airspyhf PRIVATE "${_ah}")
  target_compile_options(airspyhf PRIVATE -O2 -fvisibility=default)
  target_link_libraries(airspyhf PRIVATE usb1.0 log m)
  target_link_options(airspyhf PRIVATE -Wl,-z,max-page-size=16384 -Wl,--build-id=none)
endif()

add_library(onair SHARED
  src/jni_onair.cpp
  src/jni_radio.cpp
  src/results_json.cpp
  src/stubs_ext.c
)
set_target_properties(onair PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(onair PRIVATE "${ONAIR_SRC}/core/include" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(onair PRIVATE -O2 -fvisibility=hidden -Wall)
# the core is a very large static library: keep only what the bridge reaches (the engine reaches every mode), drop the rest
target_link_libraries(onair PRIVATE dect2core log android)
if(_have_libusb)
  target_compile_definitions(onair PRIVATE ONAIR_REAL_LIBUSB=1)   # stubs_ext.c leaves out its libusb stand-ins
  target_link_libraries(onair PRIVATE usb1.0)
endif()
# 16 KB pages (Android 15+ devices), no build id so two builds of the same tree give the same file
target_link_options(onair PRIVATE -Wl,--gc-sections -Wl,--exclude-libs,ALL -Wl,-z,max-page-size=16384 -Wl,--build-id=none)
