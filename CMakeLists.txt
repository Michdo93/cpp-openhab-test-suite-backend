cmake_minimum_required(VERSION 3.16)
project(OpenHABTestSuiteBackend VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ───────────────────────────────────────────────────────────────

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# nlohmann/json
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(json)
endif()

# cpp-httplib (header-only — replaces Crow; no asio/boost dependency at all)
include(FetchContent)
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.15.3)
FetchContent_MakeAvailable(httplib)

# cpp-openhab-rest-client (git submodule)
add_subdirectory(extern/cpp-openhab-rest-client)

# cpp-openhab-test-suite (header-only, git submodule)
set(TEST_SUITE_INCLUDE
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/cpp-openhab-test-suite/include")

# ── Backend executable ─────────────────────────────────────────────────────────

add_executable(openhab_test_suite_backend src/main.cpp)

target_include_directories(openhab_test_suite_backend PRIVATE
  ${TEST_SUITE_INCLUDE})

target_link_libraries(openhab_test_suite_backend PRIVATE
  openhab_rest_client_static
  httplib::httplib
  ${CURL_LIBRARIES}
  nlohmann_json::nlohmann_json
  Threads::Threads)