cmake_minimum_required(VERSION 3.10.2)
project(Llvm-SeaHorn)
if(CMAKE_VERSION VERSION_GREATER "3.13.0")
  cmake_policy(SET CMP0074 NEW)
  cmake_policy(SET CMP0077 NEW)
endif()


if (CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR )
  message (FATAL_ERROR
    "In-source builds are not allowed. Please clean your source tree and try again.")
endif()

find_package (LLVM 11 REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# We incorporate the CMake features provided by LLVM:
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
include(HandleLLVMOptions)
set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LLVM_CXXFLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LLVM_LDFLAGS}")

include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

if (NOT LLVM_BUILD_TYPE STREQUAL CMAKE_BUILD_TYPE)
  message(WARNING
    "LLVM_BUILD_TYPE and CMAKE_BUILD_TYPE differ:\n"
    "\tLLMV_BUILD_TYPE=${LLVM_BUILD_TYPE}\n"
    "\tCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}\n"
    "Runtime errors might occur.")
  if (LLVM_BUILD_TYPE MATCHES "Release|RelWithDebInfo" AND 
      CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo")
    message(STATUS "Assuming that mixing Release and RelWithDebInfo is allowed.")
  else()
    message(FATAL_ERROR "Incompatible mix of LLVM_BUILD_TYPE and CMAKE_BUILD_TYPE")
  endif()
endif()



include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
add_subdirectory(lib)
add_subdirectory(tools/opt)
