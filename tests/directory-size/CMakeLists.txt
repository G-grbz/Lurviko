cmake_minimum_required(VERSION 3.21)
project(DirectorySizeIndexTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Sql Test)
add_executable(directory-size-tests directory_size_tests.cpp
    ../../src/directorysizeindex.cpp ../../src/directorysizestore.cpp
    ../../src/directorysizeworker.cpp)
target_include_directories(directory-size-tests PRIVATE ../../src)
target_link_libraries(directory-size-tests PRIVATE Qt6::Core Qt6::Concurrent Qt6::Sql Qt6::Test)
