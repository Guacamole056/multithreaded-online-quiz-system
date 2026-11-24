cmake_minimum_required(VERSION 3.16)
project(MultithreadedQuizSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(QUIZ_BUILD_GUI   "Build the Dear ImGui desktop application" ON)
option(QUIZ_BUILD_TESTS "Build the headless stress test"            ON)

find_package(Threads REQUIRED)

# ---- Concurrency core (header only, no GUI dependency) ----------------------
add_library(quiz_core INTERFACE)
target_include_directories(quiz_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(quiz_core INTERFACE Threads::Threads)
if(MSVC)
  target_compile_options(quiz_core INTERFACE /W4 /permissive-)
else()
  target_compile_options(quiz_core INTERFACE -Wall -Wextra)
endif()

# ---- Headless stress test ----------------------------------------------------
if(QUIZ_BUILD_TESTS)
  enable_testing()
  add_executable(stress_test tests/stress_test.cpp)
  target_link_libraries(stress_test PRIVATE quiz_core)
  add_test(NAME stress_test COMMAND stress_test)
endif()

# ---- GUI ---------------------------------------------------------------------
if(QUIZ_BUILD_GUI)
  include(FetchContent)
  find_package(OpenGL REQUIRED)

  find_package(glfw3 3.3 QUIET)
  if(NOT glfw3_FOUND)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glfw)
  endif()

  # Dear ImGui ships no CMakeLists, so we compile the sources we need.
  FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.9 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(imgui)
  add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL ${CMAKE_DL_LIBS})

  add_executable(quiz_gui src/main.cpp)
  target_link_libraries(quiz_gui PRIVATE quiz_core imgui_lib)
  # Run from the project root so questions.txt / submissions.log land next to the sources.
  set_target_properties(quiz_gui PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
