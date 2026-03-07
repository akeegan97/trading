cmake_minimum_required(VERSION 3.24)

project(trading_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(trading_core src/app.cpp)
target_include_directories(trading_core PUBLIC include)
target_link_libraries(
  trading_core
  PUBLIC nlohmann_json::nlohmann_json
         spdlog::spdlog
)

add_executable(trading_engine engine/main.cpp)
target_link_libraries(trading_engine PRIVATE trading_core)

if(BUILD_TESTING)
  add_executable(trading_tests tests/trading_tests.cpp)
  target_link_libraries(trading_tests PRIVATE trading_core)
  add_test(NAME trading_tests COMMAND trading_tests)
endif()
