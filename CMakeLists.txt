cmake_minimum_required(VERSION 3.21)
project(evidence VERSION 0.1.0 LANGUAGES NONE)

find_program(DOTNET_EXECUTABLE dotnet REQUIRED)

set(AVALONIA_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/app/Evidence.Avalonia/Evidence.csproj")

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration" FORCE)
endif()

add_custom_target(evidence ALL
    COMMAND "${DOTNET_EXECUTABLE}" build "${AVALONIA_PROJECT}" --configuration "$<CONFIG>"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building evidence Avalonia application"
    VERBATIM
)