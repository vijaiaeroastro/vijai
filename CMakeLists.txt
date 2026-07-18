cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(HORCRUX_VCPKG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/vcpkg")
  set(HORCRUX_VCPKG_BOOTSTRAP "${HORCRUX_VCPKG_ROOT}/bootstrap-vcpkg.sh")
  if(WIN32)
    set(HORCRUX_VCPKG_EXECUTABLE "${HORCRUX_VCPKG_ROOT}/vcpkg.exe")
    set(HORCRUX_VCPKG_BOOTSTRAP "${HORCRUX_VCPKG_ROOT}/bootstrap-vcpkg.bat")
  else()
    set(HORCRUX_VCPKG_EXECUTABLE "${HORCRUX_VCPKG_ROOT}/vcpkg")
  endif()

  if(NOT EXISTS "${HORCRUX_VCPKG_BOOTSTRAP}")
    message(FATAL_ERROR
      "Missing external/vcpkg. Initialize Horcrux's pinned dependencies with:\n"
      "  git submodule update --init --recursive")
  endif()

  if(NOT EXISTS "${HORCRUX_VCPKG_EXECUTABLE}")
    message(STATUS "Bootstrapping pinned vcpkg")
    if(WIN32)
      execute_process(
        COMMAND cmd /c bootstrap-vcpkg.bat -disableMetrics
        WORKING_DIRECTORY "${HORCRUX_VCPKG_ROOT}"
        RESULT_VARIABLE HORCRUX_VCPKG_BOOTSTRAP_RESULT)
    else()
      execute_process(
        COMMAND "${HORCRUX_VCPKG_BOOTSTRAP}" -disableMetrics
        WORKING_DIRECTORY "${HORCRUX_VCPKG_ROOT}"
        RESULT_VARIABLE HORCRUX_VCPKG_BOOTSTRAP_RESULT)
    endif()
    if(NOT HORCRUX_VCPKG_BOOTSTRAP_RESULT EQUAL 0)
      message(FATAL_ERROR "vcpkg bootstrap failed (${HORCRUX_VCPKG_BOOTSTRAP_RESULT})")
    endif()
  endif()

  set(CMAKE_TOOLCHAIN_FILE "${HORCRUX_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      CACHE FILEPATH "Horcrux dependency toolchain")
endif()

project(Horcrux VERSION 0.1.0 LANGUAGES C CXX)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(boost_process CONFIG REQUIRED)
find_package(clip CONFIG REQUIRED)
find_package(ftxui CONFIG REQUIRED)
find_package(libgit2 CONFIG REQUIRED)

# Tree-sitter's runtime and grammars are pinned as official submodules. Keeping
# them together avoids a partial package-manager setup where only some language
# grammars are available on a platform.
#
# Tree-sitter's public C header does not annotate exports for MSVC, so a shared
# Windows build produces a DLL without an import library and downstream targets
# fail with LNK1181. Keep the upstream shared-library default on other systems.
if(WIN32)
  set(BUILD_SHARED_LIBS OFF)
endif()
add_subdirectory(third_party/tree-sitter EXCLUDE_FROM_ALL)

# The C++ grammar commits its generated parser and scanner sources. Build those
# directly instead of invoking the optional Tree-sitter CLI at build time.
add_library(tree-sitter-cpp STATIC
  third_party/tree-sitter-cpp/src/parser.c
  third_party/tree-sitter-cpp/src/scanner.c
)
target_include_directories(tree-sitter-cpp PRIVATE third_party/tree-sitter-cpp/src)
set_target_properties(tree-sitter-cpp PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)

file(GLOB HORCRUX_LIBVTERM_SOURCES CONFIGURE_DEPENDS third_party/libvterm/src/*.c)
add_library(libvterm STATIC ${HORCRUX_LIBVTERM_SOURCES})
target_include_directories(libvterm PUBLIC third_party/libvterm/include)
target_include_directories(libvterm PRIVATE third_party/libvterm/src)
set_target_properties(libvterm PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)

option(HORCRUX_BUILD_TESTS "Build Horcrux tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(horcrux_core
  src/cli.cpp
  src/config.cpp
  src/document.cpp
  src/editor_buffer.cpp
  src/file_tree.cpp
  src/git.cpp
  src/paths.cpp
  src/project.cpp
  src/project_search.cpp
  src/project_search_job.cpp
  src/recovery.cpp
  src/session.cpp
  src/syntax_highlighter.cpp
  src/system_clipboard.cpp
  src/terminal_session.cpp
  src/text_buffer.cpp
  src/task_runner.cpp
  src/tooling.cpp
  src/workspace_ftxui.cpp
)

target_include_directories(horcrux_core PUBLIC include)
target_include_directories(horcrux_core PRIVATE third_party/tree-sitter-cpp/bindings/c)

if(WIN32)
  target_compile_definitions(horcrux_core PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

target_compile_features(horcrux_core PUBLIC cxx_std_20)
target_link_libraries(horcrux_core PUBLIC
  nlohmann_json::nlohmann_json
  Boost::process
  clip::clip
  ftxui::screen
  ftxui::dom
  ftxui::component
  libgit2::libgit2package
  tree-sitter
  tree-sitter-cpp
  libvterm
)

if(UNIX AND NOT APPLE)
  target_link_libraries(horcrux_core PRIVATE util)
endif()

if(MSVC)
  target_compile_options(horcrux_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(horcrux_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_executable(horcrux src/main.cpp)
target_link_libraries(horcrux PRIVATE horcrux_core)

if(HORCRUX_BUILD_TESTS)
  include(CTest)
  if(BUILD_TESTING)
    add_executable(horcrux_tests
      tests/cli_tests.cpp
      tests/config_tests.cpp
      tests/document_tests.cpp
      tests/editor_buffer_tests.cpp
      tests/file_tree_tests.cpp
      tests/git_tests.cpp
      tests/project_tests.cpp
      tests/project_search_tests.cpp
      tests/project_search_job_tests.cpp
      tests/recovery_tests.cpp
      tests/session_tests.cpp
      tests/syntax_highlighter_tests.cpp
      tests/text_buffer_tests.cpp
      tests/task_runner_tests.cpp
      tests/terminal_session_tests.cpp
      tests/tooling_tests.cpp
    )
    target_link_libraries(horcrux_tests PRIVATE horcrux_core)
    add_test(NAME horcrux_tests COMMAND horcrux_tests)
  endif()
endif()
