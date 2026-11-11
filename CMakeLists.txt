cmake_minimum_required(VERSION 3.20)
project(DiskCleaner VERSION 1.0.0 LANGUAGES CXX)

# Use C++23 for std::format and modern features
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows specific
if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
    add_compile_options(/utf-8 /EHsc /Zc:inline /permissive-)
endif()

# Force /std:c++latest for newest language features
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest")
endif()

# Release optimizations (保留 CMake 默认的 /MD /O2 /DNDEBUG，追加 LTCG 与体积优化)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /GL /GF /Gy")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG /OPT:REF /OPT:ICF")

# Source files
set(SOURCES
    src/main.cpp
    src/scanner.cpp
    src/cleaner.cpp
    src/category_paths.cpp
    src/gui.cpp
    src/utils.cpp
    src/elevator.cpp
)

# Resource files (manifest 必须随目标一起链接，否则 DPI/长路径/提权声明不会生效)
set(RESOURCES
    resources/app.rc
    resources/app.manifest
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
    advapi32
    setupapi
    wtsapi32
    wevtapi
)

# Set output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
