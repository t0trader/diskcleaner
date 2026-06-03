cmake_minimum_required(VERSION 3.20)
project(DiskCleaner VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows specific
if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
    add_compile_options(/utf-8 /EHsc)
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/scanner.cpp
    src/cleaner.cpp
    src/gui.cpp
    src/utils.cpp
    src/elevator.cpp
)

# Resource files
set(RESOURCES
    resources/app.rc
)

# Add executable with Windows subsystem
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${RESOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    shell32
    ole32
    comctl32
    shlwapi
    uuid
)

# Set output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    VS_DPI_AWARE "ON"
)