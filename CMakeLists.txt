cmake_minimum_required(VERSION 3.24)
project(godot_oak VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(GODOT_CPP_DIR "${CMAKE_SOURCE_DIR}/third_party/godot-cpp" CACHE PATH "Path to godot-cpp")
if(NOT EXISTS "${GODOT_CPP_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "godot-cpp not found at ${GODOT_CPP_DIR}. Run ./scripts/bootstrap.sh")
endif()

set(GODOTCPP_BUILD_PROFILE "${CMAKE_SOURCE_DIR}/config/godot-cpp-build-profile.json" CACHE FILEPATH "godot-cpp build profile")
set(BUILD_PROFILE "${GODOTCPP_BUILD_PROFILE}" CACHE FILEPATH "godot-cpp build profile" FORCE)
add_subdirectory("${GODOT_CPP_DIR}" "${CMAKE_BINARY_DIR}/godot-cpp" EXCLUDE_FROM_ALL)

find_package(depthai CONFIG REQUIRED)

add_library(godot_oak SHARED
  src/register_types.cpp
  src/oak_device.cpp
)

target_include_directories(godot_oak PRIVATE include)
target_link_libraries(godot_oak PRIVATE godot-cpp depthai::core)
target_compile_definitions(godot_oak PRIVATE GODOT_OAK_VERSION="${PROJECT_VERSION}")

if(APPLE)
  set_target_properties(godot_oak PROPERTIES
    OUTPUT_NAME "godot_oak.macos"
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/godot/addons/godot_oak/bin"
    BUILD_RPATH "@loader_path"
    INSTALL_RPATH "@loader_path"
  )
elseif(UNIX)
  set_target_properties(godot_oak PROPERTIES
    OUTPUT_NAME "godot_oak.linux"
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/godot/addons/godot_oak/bin"
  )
elseif(WIN32)
  set_target_properties(godot_oak PROPERTIES
    OUTPUT_NAME "godot_oak.windows"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/godot/addons/godot_oak/bin"
  )
endif()
