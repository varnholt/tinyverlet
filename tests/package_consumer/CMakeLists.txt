# Builds against an installed tinyverlet to check find_package works.
#   cmake --install <build> --prefix <prefix>
#   cmake -S tests/package_consumer -B <out> -DCMAKE_PREFIX_PATH=<prefix>
cmake_minimum_required(VERSION 3.21)
project(tinyverlet_package_consumer LANGUAGES CXX)

find_package(tinyverlet 1 REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE tinyverlet::tinyverlet)
