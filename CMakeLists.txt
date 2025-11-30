cmake_minimum_required(VERSION 3.20)
project(SysButler)

# Use C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the packages installed by vcpkg
find_package(imgui CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

# Create the executable from your main file
add_executable(Butler src/main.cpp)

# Link the libraries to the executable
target_link_libraries(Butler PRIVATE imgui::imgui glfw opengl32)