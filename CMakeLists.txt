cmake_minimum_required(VERSION 3.16)
project(OpenHABTestSuiteBackend VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ───────────────────────────────────────────────────────────────

find_package(CURL REQUIRED)

# nlohmann/json
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(json)
endif()

# Crow – lightweight C++ HTTP server (header-only)
# Instruct Crow to use the system-installed Asio (libasio-dev) rather than
# trying to find Boost.Asio, and disable SSL so we don't need libssl linkage.
set(CROW_USE_BOOST      OFF CACHE BOOL "" FORCE)
set(CROW_ENABLE_SSL     OFF CACHE BOOL "" FORCE)
set(ASIO_INCLUDE_DIR    /usr/include CACHE PATH "" FORCE)

include(FetchContent)
FetchContent_Declare(crow
  GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
  GIT_TAG        v1.2.0)
FetchContent_MakeAvailable(crow)

# cpp-openhab-rest-client (git submodule in extern/)
add_subdirectory(extern/cpp-openhab-rest-client)

# cpp-openhab-test-suite headers (git submodule in extern/)
# It is header-only, so we just add its include directory directly.
set(TEST_SUITE_INCLUDE
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/cpp-openhab-test-suite/include")

# ── Backend executable ─────────────────────────────────────────────────────────

add_executable(openhab_test_suite_backend src/main.cpp)

target_include_directories(openhab_test_suite_backend PRIVATE
  ${TEST_SUITE_INCLUDE})

target_link_libraries(openhab_test_suite_backend PRIVATE
  Crow::Crow
  openhab_rest_client_static
  ${CURL_LIBRARIES}
  nlohmann_json::nlohmann_json)

if(UNIX)
  target_link_libraries(openhab_test_suite_backend PRIVATE pthread)
endif()
