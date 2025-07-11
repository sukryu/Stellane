cmake_minimum_required(VERSION 3.20)

# 프로젝트 정의

project(stellane
VERSION 0.1.0
DESCRIPTION “High-performance C++20/23/26 backend framework for game servers and real-time applications”
LANGUAGES CXX
)

# =============================================================================

# 전역 설정

# =============================================================================

# C++ 표준 설정

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 빌드 타입 기본값

if(NOT CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE Release)
endif()

# Position Independent Code 활성화

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 컴파일러별 설정

if(CMAKE_CXX_COMPILER_ID STREQUAL “GNU”)
set(CMAKE_CXX_FLAGS_DEBUG “-g -O0 -Wall -Wextra -Wpedantic”)
set(CMAKE_CXX_FLAGS_RELEASE “-O3 -DNDEBUG -march=native”)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO “-O2 -g -DNDEBUG”)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL “Clang”)
set(CMAKE_CXX_FLAGS_DEBUG “-g -O0 -Wall -Wextra -Wpedantic”)
set(CMAKE_CXX_FLAGS_RELEASE “-O3 -DNDEBUG -march=native”)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO “-O2 -g -DNDEBUG”)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL “MSVC”)
set(CMAKE_CXX_FLAGS_DEBUG “/Od /Zi /W4”)
set(CMAKE_CXX_FLAGS_RELEASE “/O2 /DNDEBUG”)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO “/O2 /Zi /DNDEBUG”)
endif()

# =============================================================================

# 옵션 설정

# =============================================================================

option(STELLANE_BUILD_TESTS “Build tests” ON)
option(STELLANE_BUILD_EXAMPLES “Build examples” ON)
option(STELLANE_BUILD_BENCHMARKS “Build benchmarks” OFF)
option(STELLANE_ENABLE_COVERAGE “Enable code coverage” OFF)
option(STELLANE_ENABLE_SANITIZERS “Enable sanitizers” OFF)
option(STELLANE_ENABLE_LTO “Enable Link Time Optimization” OFF)
option(STELLANE_USE_VCPKG “Use vcpkg for dependencies” ON)
option(STELLANE_BUILD_SHARED_LIBS “Build shared libraries” OFF)

# Backend 지원 옵션

option(STELLANE_ENABLE_LIBUV “Enable libuv backend” ON)
option(STELLANE_ENABLE_EPOLL “Enable epoll backend (Linux only)” ON)
option(STELLANE_ENABLE_IO_URING “Enable io_uring backend (Linux 5.1+)” OFF)

# =============================================================================

# 의존성 관리

# =============================================================================

# CMake 모듈 경로 추가

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# vcpkg 설정 (선택사항)

if(STELLANE_USE_VCPKG)
find_package(PkgConfig REQUIRED)
endif()

# 필수 의존성

find_package(Threads REQUIRED)

# JSON 라이브러리

find_package(nlohmann_json REQUIRED)

# 로깅 라이브러리

find_package(spdlog REQUIRED)

# libuv (비동기 I/O)

if(STELLANE_ENABLE_LIBUV)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUV REQUIRED libuv)
add_compile_definitions(STELLANE_HAS_LIBUV)
endif()

# io_uring (Linux 5.1+)

if(STELLANE_ENABLE_IO_URING AND CMAKE_SYSTEM_NAME STREQUAL “Linux”)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING liburing)
if(LIBURING_FOUND)
add_compile_definitions(STELLANE_HAS_IO_URING)
else()
message(WARNING “liburing not found, disabling io_uring support”)
set(STELLANE_ENABLE_IO_URING OFF)
endif()
endif()

# 테스트 프레임워크

if(STELLANE_BUILD_TESTS)
find_package(Catch2 3 REQUIRED)
endif()

# 벤치마크 라이브러리

if(STELLANE_BUILD_BENCHMARKS)
find_package(benchmark REQUIRED)
endif()

# =============================================================================

# 컴파일러 기능 검사

# =============================================================================

include(CheckCXXSourceCompiles)

# Coroutines 지원 확인

check_cxx_source_compiles(
“#include <coroutine>
int main() {
return 0;
}”
STELLANE_HAS_COROUTINES
)

if(NOT STELLANE_HAS_COROUTINES)
message(FATAL_ERROR “Compiler does not support C++20 coroutines”)
endif()

# Concepts 지원 확인

check_cxx_source_compiles(
“#include <concepts>
template<typename T>
concept Integral = std::integral<T>;
int main() {
return 0;
}”
STELLANE_HAS_CONCEPTS
)

if(STELLANE_HAS_CONCEPTS)
add_compile_definitions(STELLANE_HAS_CONCEPTS)
endif()

# =============================================================================

# 타겟 정의

# =============================================================================

# 메인 라이브러리

add_library(stellane)

# 공개 헤더 디렉토리

target_include_directories(stellane
PUBLIC
$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
$<INSTALL_INTERFACE:include>
PRIVATE
${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 소스 파일 추가

file(GLOB_RECURSE STELLANE_SOURCES
“src/*.cpp”
“src/*.h”
)
target_sources(stellane PRIVATE ${STELLANE_SOURCES})

# 헤더 파일 추가 (IDE 지원용)

file(GLOB_RECURSE STELLANE_HEADERS
“include/*.h”
“include/*.hpp”
)
target_sources(stellane PRIVATE ${STELLANE_HEADERS})

# 링크 라이브러리

target_link_libraries(stellane
PUBLIC
Threads::Threads
nlohmann_json::nlohmann_json
spdlog::spdlog
)

# 조건부 링크

if(STELLANE_ENABLE_LIBUV)
target_link_libraries(stellane PRIVATE ${LIBUV_LIBRARIES})
target_include_directories(stellane PRIVATE ${LIBUV_INCLUDE_DIRS})
endif()

if(STELLANE_ENABLE_IO_URING)
target_link_libraries(stellane PRIVATE ${LIBURING_LIBRARIES})
target_include_directories(stellane PRIVATE ${LIBURING_INCLUDE_DIRS})
endif()

# 컴파일 옵션

target_compile_features(stellane PUBLIC cxx_std_20)

# =============================================================================

# 정적 분석 및 코드 품질

# =============================================================================

# clang-tidy 설정

if(CMAKE_CXX_CLANG_TIDY)
set_target_properties(stellane PROPERTIES CXX_CLANG_TIDY ${CMAKE_CXX_CLANG_TIDY})
endif()

# 코드 커버리지

if(STELLANE_ENABLE_COVERAGE)
if(CMAKE_CXX_COMPILER_ID STREQUAL “GNU” OR CMAKE_CXX_COMPILER_ID STREQUAL “Clang”)
target_compile_options(stellane PRIVATE –coverage)
target_link_options(stellane PRIVATE –coverage)
endif()
endif()

# Sanitizers

if(STELLANE_ENABLE_SANITIZERS)
if(CMAKE_CXX_COMPILER_ID STREQUAL “GNU” OR CMAKE_CXX_COMPILER_ID STREQUAL “Clang”)
target_compile_options(stellane PRIVATE
-fsanitize=address
-fsanitize=undefined
-fno-omit-frame-pointer
)
target_link_options(stellane PRIVATE
-fsanitize=address
-fsanitize=undefined
)
endif()
endif()

# Link Time Optimization

if(STELLANE_ENABLE_LTO)
set_target_properties(stellane PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# =============================================================================

# 서브 디렉토리

# =============================================================================

# 테스트

if(STELLANE_BUILD_TESTS)
enable_testing()
add_subdirectory(tests)
endif()

# 예제

if(STELLANE_BUILD_EXAMPLES)
add_subdirectory(examples)
endif()

# =============================================================================

# 설치 구성

# =============================================================================

# 설치 타겟

install(TARGETS stellane
EXPORT stellane-targets
LIBRARY DESTINATION lib
ARCHIVE DESTINATION lib
RUNTIME DESTINATION bin
INCLUDES DESTINATION include
)

# 헤더 파일 설치

install(DIRECTORY include/stellane
DESTINATION include
FILES_MATCHING PATTERN “*.h” PATTERN “*.hpp”
)

# CMake config 파일

include(CMakePackageConfigHelpers)

configure_package_config_file(
“${CMAKE_CURRENT_SOURCE_DIR}/cmake/stellane-config.cmake.in”
“${CMAKE_CURRENT_BINARY_DIR}/stellane-config.cmake”
INSTALL_DESTINATION lib/cmake/stellane
)

write_basic_package_version_file(
“${CMAKE_CURRENT_BINARY_DIR}/stellane-config-version.cmake”
VERSION ${PROJECT_VERSION}
COMPATIBILITY SameMajorVersion
)

install(FILES
“${CMAKE_CURRENT_BINARY_DIR}/stellane-config.cmake”
“${CMAKE_CURRENT_BINARY_DIR}/stellane-config-version.cmake”
DESTINATION lib/cmake/stellane
)

install(EXPORT stellane-targets
FILE stellane-targets.cmake
NAMESPACE stellane::
DESTINATION lib/cmake/stellane
)

# =============================================================================

# 상태 출력

# =============================================================================

message(STATUS “=== Stellane Configuration Summary ===”)
message(STATUS “Version: ${PROJECT_VERSION}”)
message(STATUS “Build type: ${CMAKE_BUILD_TYPE}”)
message(STATUS “C++ standard: ${CMAKE_CXX_STANDARD}”)
message(STATUS “Build tests: ${STELLANE_BUILD_TESTS}”)
message(STATUS “Build examples: ${STELLANE_BUILD_EXAMPLES}”)
message(STATUS “Build benchmarks: ${STELLANE_BUILD_BENCHMARKS}”)
message(STATUS “Enable coverage: ${STELLANE_ENABLE_COVERAGE}”)
message(STATUS “Enable sanitizers: ${STELLANE_ENABLE_SANITIZERS}”)
message(STATUS “Enable LTO: ${STELLANE_ENABLE_LTO}”)
message(STATUS “”)
message(STATUS “Backend support:”)
message(STATUS “  libuv: ${STELLANE_ENABLE_LIBUV}”)
message(STATUS “  epoll: ${STELLANE_ENABLE_EPOLL}”)
message(STATUS “  io_uring: ${STELLANE_ENABLE_IO_URING}”)
message(STATUS “==========================================”)
