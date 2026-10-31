cmake_minimum_required(VERSION 3.21)
project(evidence VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Sql Pdf)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

qt_add_executable(evidence
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
    src/markdown_highlighter.cpp
    src/markdown_highlighter.h
    src/search_index.cpp
    src/search_index.h
    src/secure_note.cpp
    src/secure_note.h
)

target_link_libraries(evidence PRIVATE Qt6::Widgets Qt6::Sql Qt6::Pdf OpenSSL::Crypto)

install(TARGETS evidence RUNTIME DESTINATION .)

if(WIN32)
    set_target_properties(evidence PROPERTIES WIN32_EXECUTABLE ON)
endif()

if(UNIX AND NOT APPLE)
    target_compile_options(evidence PRIVATE -Wall -Wextra -Wpedantic)
endif()