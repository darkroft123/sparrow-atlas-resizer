cmake_minimum_required(VERSION 4.1)
project(sparrow_atlas_resizer)

set(CMAKE_CXX_STANDARD 20)

add_executable(sparrow_atlas_resizer main.cpp dialogs.cpp)

add_subdirectory(external/pugixml)
target_link_libraries(${PROJECT_NAME} pugixml)

add_subdirectory(external/raylib)
target_link_libraries(${PROJECT_NAME} raylib)

include_directories(external/raygui/src)