cmake_minimum_required(VERSION 3.21)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake CACHE STRING "Vcpkg toolchain file")

project(ImGuiPlayer C CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(FFMPEG REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_executable(ImGuiPlayer
main.cpp)

include_directories(ImGuiPlayer ${FFMPEG_INCLUDE_DIRS})

target_link_libraries(ImGuiPlayer PRIVATE ${FFMPEG_LIBRARIES})
target_link_libraries(ImGuiPlayer PRIVATE GLEW::GLEW)
target_link_libraries(ImGuiPlayer PRIVATE glfw)
target_link_libraries(ImGuiPlayer PRIVATE imgui::imgui)

if(WIN32)
  target_link_libraries(ImGuiPlayer PRIVATE opengl32)
endif()
