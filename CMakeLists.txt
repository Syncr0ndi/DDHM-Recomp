cmake_minimum_required(VERSION 3.24)
project(ddhm_port LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Self-contained executables: no Visual C++ redistributable / libc++ DLLs needed next to ddhm.exe.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra)
endif()
if(MINGW)
  add_link_options(-static)
endif()

# --- Asset library (platform independent, no SDL) --------------------------
add_library(dd2 STATIC src/dd2/dd2.cpp src/dd2/exe.cpp src/dd2/tables.cpp src/dd2/assets.cpp)
target_include_directories(dd2 PUBLIC src)

# --- Game logic (player simulation; platform independent, no SDL) -----------------
add_library(ddhm_game STATIC src/game/object.cpp src/game/player.cpp src/game/enemy.cpp src/game/world.cpp
            src/game/screen.cpp src/game/machine.cpp src/game/flow.cpp)
target_include_directories(ddhm_game PUBLIC src)
target_link_libraries(ddhm_game PUBLIC dd2)
find_package(Threads REQUIRED)
target_link_libraries(ddhm_game PUBLIC Threads::Threads)

# --- Command-line tools ------------------------------------------------------
add_executable(dd2tool tools/dd2tool.cpp)
target_link_libraries(dd2tool PRIVATE dd2)

add_executable(zootrace tools/zootrace.cpp)      # a synthetic room with every ported enemy, rendered to PNG
target_link_libraries(zootrace PRIVATE ddhm_game)

add_executable(enemytrace tools/enemytrace.cpp)  # a bot plays level 1 and the scene is rendered to PNG
target_link_libraries(enemytrace PRIVATE ddhm_game)

add_executable(playtrace tools/playtrace.cpp)    # renders a scripted walk/jump/shot on level 1 to PNG
target_link_libraries(playtrace PRIVATE ddhm_game)

add_executable(ddhm_run tools/ddhm_run.cpp)       # the whole game headless: scripted keys -> PNG / WAV / hashes
target_link_libraries(ddhm_run PRIVATE ddhm_game)

add_executable(unlzexe tools/unlzexe.c)          # unpacks LZEXE-compressed DOS EXEs for disassembly
if(MSVC)
  target_compile_definitions(unlzexe PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# --- Tests -------------------------------------------------------------------
enable_testing()
add_executable(test_assets tests/test_assets.cpp)
target_link_libraries(test_assets PRIVATE dd2)
add_executable(test_enemy tests/test_enemy.cpp)
target_link_libraries(test_enemy PRIVATE ddhm_game)
add_test(NAME enemy COMMAND test_enemy ${DDHM_GAME_DIR})
add_executable(test_player tests/test_player.cpp)
target_link_libraries(test_player PRIVATE ddhm_game)
add_test(NAME player COMMAND test_player ${DDHM_GAME_DIR})
set(DDHM_GAME_DIR "" CACHE PATH "Folder containing the original game's .DD2 files (for tests/viewer)")
add_executable(test_flow tests/test_flow.cpp)
target_link_libraries(test_flow PRIVATE ddhm_game)
add_executable(test_no_game_content tests/test_no_game_content.cpp)   # copyright check of the shipped exes
target_link_libraries(test_no_game_content PRIVATE dd2)
add_executable(test_screens tests/test_screens.cpp)  # screenshot regression (CRCs in tests/screens.golden)
target_link_libraries(test_screens PRIVATE ddhm_game)
if(DDHM_GAME_DIR)
  add_test(NAME assets COMMAND test_assets ${DDHM_GAME_DIR})
  add_test(NAME flow COMMAND test_flow ${DDHM_GAME_DIR})
  if(DDHM_BUILD_VIEWER AND WIN32)
    add_test(NAME no_game_content COMMAND test_no_game_content ${DDHM_GAME_DIR} $<TARGET_FILE:ddhm> $<TARGET_FILE:ddhm_launcher>)
  endif()
  add_test(NAME screens COMMAND test_screens ${DDHM_GAME_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests/screens.golden)
endif()

# --- SDL3 viewer / game front end ---------------------------------------------
option(DDHM_BUILD_VIEWER "Build the SDL3 viewer" ON)
if(DDHM_BUILD_VIEWER)
  find_package(SDL3 CONFIG QUIET)
  if(NOT SDL3_FOUND)
    include(FetchContent)
    set(DDHM_SDL3_TAG "release-3.2.0" CACHE STRING "SDL3 git tag to fetch (bump to the latest 3.x release)")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                              GIT_TAG ${DDHM_SDL3_TAG} GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
  endif()
  set(DDHM_GAME_SOURCES src/main.cpp)
  if(WIN32)                                           # exe icon: an original design generated at build time
    enable_language(RC)
    add_executable(make_icon tools/make_icon.cpp)
    set(DDHM_ICO ${CMAKE_CURRENT_BINARY_DIR}/ddhm.ico)
    add_custom_command(OUTPUT ${DDHM_ICO} COMMAND make_icon ${DDHM_ICO} DEPENDS make_icon VERBATIM)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ddhm.rc "1 ICON \"${DDHM_ICO}\"
")
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/ddhm.rc PROPERTIES OBJECT_DEPENDS ${DDHM_ICO})
    list(APPEND DDHM_GAME_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/ddhm.rc)
  endif()
  add_executable(ddhm WIN32 ${DDHM_GAME_SOURCES})     # the game
  target_link_libraries(ddhm PRIVATE ddhm_game dd2 SDL3::SDL3)

  if(WIN32)                                           # DDHM Launcher: Game -> Assets, options, starts ddhm.exe
    set(DDHM_LAUNCHER_RC ${CMAKE_CURRENT_BINARY_DIR}/launcher.rc)
    if(MSVC)                                          # MSVC adds its own manifest (see the pragma in the source)
      file(WRITE ${DDHM_LAUNCHER_RC} "1 ICON \"${DDHM_ICO}\"
")
    else()
      file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/launcher.manifest
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>
<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">
  <dependency><dependentAssembly><assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" version=\"6.0.0.0\" processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/></dependentAssembly></dependency>
</assembly>
")
      file(WRITE ${DDHM_LAUNCHER_RC} "1 ICON \"${DDHM_ICO}\"
1 24 \"${CMAKE_CURRENT_BINARY_DIR}/launcher.manifest\"
")
    endif()
    set_source_files_properties(${DDHM_LAUNCHER_RC} PROPERTIES OBJECT_DEPENDS ${DDHM_ICO})
    add_executable(ddhm_launcher WIN32 src/launcher_main.cpp ${DDHM_LAUNCHER_RC})
    target_link_libraries(ddhm_launcher PRIVATE dd2 shell32 ole32)
    set_target_properties(ddhm_launcher PROPERTIES OUTPUT_NAME "DDHM Launcher")
    target_compile_definitions(ddhm_launcher PRIVATE UNICODE _UNICODE)
    if(MINGW)
      target_link_options(ddhm_launcher PRIVATE -municode)
    endif()
  endif()
  add_executable(ddhm_viewer WIN32 src/viewer_main.cpp) # developer tool: level / collision / entity viewer
  target_link_libraries(ddhm_viewer PRIVATE ddhm_game dd2 SDL3::SDL3)
endif()
