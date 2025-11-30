cmake_minimum_required(VERSION 3.20)
project(SysButler)

add_compile_definitions(NOMINMAX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(imgui CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(Butler 
    src/main.cpp 
    src/Core/Logger.cpp
    src/Core/Logger.h
    src/Jobs/TransferManager.cpp
    src/Jobs/TransferManager.h
    src/UI/FileBrowser.cpp
    src/UI/FileBrowser.h
)

target_include_directories(Butler PRIVATE 
    src/Core
    src/Jobs
    src/UI
)

target_link_libraries(Butler PRIVATE imgui::imgui glfw opengl32)