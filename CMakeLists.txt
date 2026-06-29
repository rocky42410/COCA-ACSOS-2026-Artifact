cmake_minimum_required(VERSION 3.10)
project(COCA_Artifact VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Portable optimisation flags. -march=native is OFF by default because it
# breaks reproducibility/portability across reviewer machines. Enable with
# -DCOCA_NATIVE_ARCH=ON only if you build and run on the same CPU.
option(COCA_NATIVE_ARCH "Optimise for the local CPU (-march=native)" OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -O3)
  if(COCA_NATIVE_ARCH)
    add_compile_options(-march=native)
  endif()
elseif(MSVC)
  add_compile_options(/W4 /O2)
endif()

find_package(OpenMP)

include_directories(src)

set(COMMON_HEADERS
    src/coca_model.hpp
    src/io/csv_reader.hpp
    src/utils/config_parser.hpp
    src/utils/model_io.hpp
)

add_executable(coca_train apps/coca_train.cpp ${COMMON_HEADERS})
add_executable(coca_test  apps/coca_test.cpp  ${COMMON_HEADERS})
add_executable(coca_unit_tests tests/test_losses.cpp ${COMMON_HEADERS})

if(OpenMP_CXX_FOUND)
  target_link_libraries(coca_train      OpenMP::OpenMP_CXX)
  target_link_libraries(coca_test       OpenMP::OpenMP_CXX)
  target_link_libraries(coca_unit_tests OpenMP::OpenMP_CXX)
endif()

enable_testing()
add_test(NAME UnitTests COMMAND coca_unit_tests)

install(TARGETS coca_train coca_test DESTINATION bin)
install(FILES coca_config.yaml DESTINATION etc/coca)

message(STATUS "COCA artifact: C++${CMAKE_CXX_STANDARD}, build=${CMAKE_BUILD_TYPE}, OpenMP=${OpenMP_CXX_FOUND}, native_arch=${COCA_NATIVE_ARCH}")
