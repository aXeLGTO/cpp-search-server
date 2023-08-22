cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(search-server)

set(CMAKE_CXX_STANDARD 17)
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  set(
    CMAKE_CXX_FLAGS_DEBUG
    "${CMAKE_CXX_FLAGS_DEBUG} /JMC"
  )
else()
  set(
    CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Werror -Wno-unused-parameter -Wno-implicit-fallthrough"
  )
endif()

find_package(TBB REQUIRED)
find_package(Threads REQUIRED)

file(GLOB sources
  *.cpp
  *.h
)

add_executable(
  search-server
  ${sources}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  TBB::tbb
  Threads::Threads
)