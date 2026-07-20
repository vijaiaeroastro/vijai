cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(VIJAI_VCPKG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/vcpkg")
  set(VIJAI_VCPKG_BOOTSTRAP "${VIJAI_VCPKG_ROOT}/bootstrap-vcpkg.sh")
  if(WIN32)
    set(VIJAI_VCPKG_EXECUTABLE "${VIJAI_VCPKG_ROOT}/vcpkg.exe")
    set(VIJAI_VCPKG_BOOTSTRAP "${VIJAI_VCPKG_ROOT}/bootstrap-vcpkg.bat")
  else()
    set(VIJAI_VCPKG_EXECUTABLE "${VIJAI_VCPKG_ROOT}/vcpkg")
  endif()

  if(NOT EXISTS "${VIJAI_VCPKG_BOOTSTRAP}")
    message(FATAL_ERROR
      "Missing external/vcpkg. Initialize Vijai's pinned dependencies with:\n"
      "  git submodule update --init --recursive")
  endif()

  if(NOT EXISTS "${VIJAI_VCPKG_EXECUTABLE}")
    message(STATUS "Bootstrapping pinned vcpkg")
    if(WIN32)
      execute_process(
        COMMAND cmd /c bootstrap-vcpkg.bat -disableMetrics
        WORKING_DIRECTORY "${VIJAI_VCPKG_ROOT}"
        RESULT_VARIABLE VIJAI_VCPKG_BOOTSTRAP_RESULT)
    else()
      execute_process(
        COMMAND "${VIJAI_VCPKG_BOOTSTRAP}" -disableMetrics
        WORKING_DIRECTORY "${VIJAI_VCPKG_ROOT}"
        RESULT_VARIABLE VIJAI_VCPKG_BOOTSTRAP_RESULT)
    endif()
    if(NOT VIJAI_VCPKG_BOOTSTRAP_RESULT EQUAL 0)
      message(FATAL_ERROR "vcpkg bootstrap failed (${VIJAI_VCPKG_BOOTSTRAP_RESULT})")
    endif()
  endif()

  set(CMAKE_TOOLCHAIN_FILE "${VIJAI_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      CACHE FILEPATH "Vijai dependency toolchain")
endif()

project(Vijai VERSION 0.1.0 LANGUAGES C CXX)

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

# Typst and BibTeX ship generated C parsers, so they are compiled directly just
# like C++. The maintained LaTeX grammar is vendored too, but upstream does not
# commit parser.c; it remains available for the later deterministic generator
# integration rather than requiring npm during a normal CMake build.
add_library(tree-sitter-typst STATIC
  third_party/tree-sitter-typst/src/parser.c
  third_party/tree-sitter-typst/src/scanner.c
)
target_include_directories(tree-sitter-typst PRIVATE third_party/tree-sitter-typst/src)
set_target_properties(tree-sitter-typst PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)

add_library(tree-sitter-bibtex STATIC third_party/tree-sitter-bibtex/src/parser.c)
target_include_directories(tree-sitter-bibtex PRIVATE third_party/tree-sitter-bibtex/src)
set_target_properties(tree-sitter-bibtex PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)

file(GLOB VIJAI_LIBVTERM_SOURCES CONFIGURE_DEPENDS third_party/libvterm/src/*.c)
add_library(libvterm STATIC ${VIJAI_LIBVTERM_SOURCES})
target_include_directories(libvterm PUBLIC third_party/libvterm/include)
target_include_directories(libvterm PRIVATE third_party/libvterm/src)
set_target_properties(libvterm PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)

option(VIJAI_BUILD_TESTS "Build Vijai tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(vijai_core
  src/cli.cpp
  src/cpp_tests.cpp
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
  src/tool_panels.cpp
  src/workspace_ftxui.cpp
)

target_include_directories(vijai_core PUBLIC include)
target_include_directories(vijai_core PRIVATE
  third_party/tree-sitter-cpp/bindings/c
  third_party/tree-sitter-typst/bindings/c
  third_party/tree-sitter-bibtex/bindings/c
)

if(WIN32)
  target_compile_definitions(vijai_core PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

target_compile_features(vijai_core PUBLIC cxx_std_20)
target_link_libraries(vijai_core PUBLIC
  nlohmann_json::nlohmann_json
  Boost::process
  clip::clip
  ftxui::screen
  ftxui::dom
  ftxui::component
  libgit2::libgit2package
  tree-sitter
  tree-sitter-cpp
  tree-sitter-typst
  tree-sitter-bibtex
  libvterm
)

if(UNIX AND NOT APPLE)
  target_link_libraries(vijai_core PRIVATE util)
endif()

if(MSVC)
  target_compile_options(vijai_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(vijai_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_executable(vijai src/main.cpp)
target_link_libraries(vijai PRIVATE vijai_core)

# Produce self-contained editor bundles from CI. The executable and its own
# shared library are installed together; platform system libraries remain the
# responsibility of the supported operating system.
include(GNUInstallDirs)
if(APPLE)
  set_target_properties(vijai PROPERTIES
    INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}"
  )
  set_target_properties(vijai_core PROPERTIES
    INSTALL_RPATH "@loader_path"
  )
elseif(UNIX)
  set_target_properties(vijai PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
  )
  set_target_properties(vijai_core PROPERTIES
    INSTALL_RPATH "$ORIGIN"
  )
endif()
install(TARGETS vijai vijai_core tree-sitter
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

set(CPACK_PACKAGE_NAME "vijai")
set(CPACK_PACKAGE_VENDOR "Vijai")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "vijai-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)

if(VIJAI_BUILD_TESTS)
  include(CTest)
  if(BUILD_TESTING)
    add_executable(vijai_tests
      tests/cli_tests.cpp
      tests/cpp_tests_tests.cpp
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
      tests/tool_panels_tests.cpp
      tests/tooling_tests.cpp
    )
    target_link_libraries(vijai_tests PRIVATE vijai_core)
    add_test(NAME vijai_tests COMMAND vijai_tests)
  endif()
endif()
