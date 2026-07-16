cmake_minimum_required(VERSION 3.10)

project(AuctionQueryThrottle LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# minhook declares an old cmake_minimum_required; promote its policy set so
# CMake 4.x doesn't warn on every reconfigure.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)

# Static CRT, for BOTH languages, BEFORE add_subdirectory. minhook is C, and its
# old policy scope (VERSION 3.0...3.5) predates CMP0091 — it ignores
# CMAKE_MSVC_RUNTIME_LIBRARY and instead takes the runtime from the C flags.
# Without an explicit /MT there, MSVC defaults minhook to the dynamic CRT (/MD),
# which collides with our /MT DLL at link (LNK4098 / __except_handler4_common).
# So we must pin the flag on the C flags too, not just CXX.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MT")
set(CMAKE_C_FLAGS_RELEASE "/MT /O2 /Ob2 /DNDEBUG")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MT")
set(CMAKE_CXX_FLAGS_RELEASE "/MT /O2 /Ob2 /DNDEBUG")

add_subdirectory(external/minhook)

add_library(${PROJECT_NAME} SHARED
    src/DllMain.cpp
    src/Offsets.h)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    external/minhook/include)

target_link_libraries(${PROJECT_NAME} PRIVATE minhook)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "AuctionQueryThrottle")
