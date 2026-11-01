cmake_minimum_required(VERSION 3.24)
project(RE4OGAimWalk LANGUAGES C CXX)
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Configure an x86 build; bio4.exe is 32-bit.")
endif()
add_subdirectory(third_party/minhook)

function(configure_movement_module target)
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  target_link_libraries(${target} PRIVATE minhook user32 comctl32)
endfunction()

# Keep the regular ASI build available for the standalone non-COOP setup.
add_library(RE4OGAimWalk MODULE src/aim_walk.cpp)
configure_movement_module(RE4OGAimWalk)
set_target_properties(RE4OGAimWalk PROPERTIES PREFIX "" SUFFIX ".asi" OUTPUT_NAME "RE4OGAimWalk")

add_library(RE4BetterMovement MODULE src/aim_walk.cpp)
configure_movement_module(RE4BetterMovement)
set_target_properties(RE4BetterMovement PROPERTIES PREFIX "" SUFFIX ".dll" OUTPUT_NAME "re4bettermovement")

# Proxy the six dinput8 exports to COOP's original DLL and start the movement
# plugin from the proxy. This avoids relying on COOP to discover arbitrary ASI files.
add_library(RE4BetterMovementDInput8 SHARED src/dinput8_proxy.cpp src/dinput8_proxy.def)
target_compile_features(RE4BetterMovementDInput8 PRIVATE cxx_std_17)
target_compile_definitions(RE4BetterMovementDInput8 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
  target_compile_options(RE4BetterMovementDInput8 PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(RE4BetterMovementDInput8 PRIVATE -Wall -Wextra -Wpedantic)
  target_link_options(RE4BetterMovementDInput8 PRIVATE -static -static-libgcc -static-libstdc++)
endif()
set_target_properties(RE4BetterMovementDInput8 PROPERTIES PREFIX "" SUFFIX ".dll" OUTPUT_NAME "dinput8")

