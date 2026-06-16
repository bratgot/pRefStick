cmake_minimum_required(VERSION 3.20)

# MSVC_RUNTIME_LIBRARY must reach the compile -> set the policy before any target.
cmake_policy(SET CMP0091 NEW)

project(pRefStick LANGUAGES CXX)

# Use Nuke's shipped NukeConfig.cmake. Point CMake at the Nuke root at configure time:
#   -DCMAKE_PREFIX_PATH="C:/Program Files/Nuke16.1v1"
# If find_package can't locate it, point straight at the cmake folder instead:
#   -DNuke_DIR="C:/Program Files/Nuke16.1v1/cmake"
find_package(Nuke REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Nuke LoadLibrary()s plugins; nothing links them -> no 'lib' prefix.
set(CMAKE_SHARED_MODULE_PREFIX "")

add_library(pRefStick MODULE pRefStick.cpp)

# Nuke::NDK brings the include dirs + DDImage; no manual find_library needed.
target_link_libraries(pRefStick PRIVATE Nuke::NDK)

# Where the plugin should land.
if(WIN32)
    set(PLUGIN_DEST "$ENV{USERPROFILE}/.nuke")
else()
    set(PLUGIN_DEST "$ENV{HOME}/.nuke")
endif()

if(WIN32)
    # fnVC.h #errors without NOMINMAX / _USE_MATH_DEFINES; the others quieten the build.
    target_compile_definitions(pRefStick PRIVATE
        NOMINMAX _USE_MATH_DEFINES WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    # Match Nuke's dynamic CRT (/MD); CMP0091 NEW (above) makes this stick.
    set_target_properties(pRefStick PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    target_compile_options(pRefStick PRIVATE /W3 /EHsc /wd4251)
elseif(APPLE)
    set_target_properties(pRefStick PROPERTIES SUFFIX ".dylib")
endif()

# Auto-copy the built plugin into ~/.nuke after every build (cross-platform).
add_custom_command(TARGET pRefStick POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_DEST}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:pRefStick>" "${PLUGIN_DEST}/"
    COMMENT "Copying pRefStick -> ${PLUGIN_DEST}")

# Also available explicitly via `cmake --install build`.
install(TARGETS pRefStick DESTINATION "${PLUGIN_DEST}")
