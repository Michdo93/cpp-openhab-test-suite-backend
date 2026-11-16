cmake_minimum_required(VERSION 3.16)
project(OpenHABTestSuiteBackendCpp VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(OPENHAB_REST_CLIENT_DIR "" CACHE PATH "Source directory of cpp-openhab-rest-client (optional)")
set(OPENHAB_TEST_SUITE_DIR "" CACHE PATH "Source directory of cpp-openhab-test-suite (optional)")

# cpp-openhab-test-suite 2.0.0 – fetches cpp-openhab-rest-client 2.0.0 (nlohmann/json, libcurl) itself
set(OPENHAB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(OPENHAB_TESTSUITE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
if(OPENHAB_REST_CLIENT_DIR)
  add_subdirectory(${OPENHAB_REST_CLIENT_DIR} cpp-openhab-rest-client)
endif()
if(OPENHAB_TEST_SUITE_DIR)
  add_subdirectory(${OPENHAB_TEST_SUITE_DIR} cpp-openhab-test-suite)
else()
  FetchContent_Declare(cpp_openhab_test_suite
    GIT_REPOSITORY https://github.com/Michdo93/cpp-openhab-test-suite.git
    GIT_TAG        9a043243e346edc1ea400366c607fdbd3215c03b)   # 2.0.0
  FetchContent_MakeAvailable(cpp_openhab_test_suite)
endif()

# cpp-httplib (header only, no OpenSSL needed – Render terminates TLS)
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.18.3)
FetchContent_MakeAvailable(httplib)

find_package(Threads REQUIRED)

add_executable(openhab_test_suite_backend src/main.cpp)
target_link_libraries(openhab_test_suite_backend PRIVATE openhab::test_suite_cpp httplib::httplib
                      Threads::Threads)
