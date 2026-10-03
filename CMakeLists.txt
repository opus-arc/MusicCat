cmake_minimum_required(VERSION 3.20)
project(MusicCat VERSION 0.3.0
    DESCRIPTION "macOS Apple Music recorder with optional MIDI and MusicXML transcription"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(musiccat_core STATIC
    src/musiccat.cpp
    src/musiccat.hpp
)
target_include_directories(musiccat_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(musiccat_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(MusicCat main.cpp)
target_link_libraries(MusicCat PRIVATE musiccat_core)
set_target_properties(MusicCat PROPERTIES OUTPUT_NAME mcat)

include(CTest)
if(BUILD_TESTING)
    add_executable(musiccat_state_tests tests/state_machine_tests.cpp)
    target_link_libraries(musiccat_state_tests PRIVATE musiccat_core)
    target_compile_options(musiccat_state_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME musiccat_state_tests COMMAND musiccat_state_tests)

    add_executable(musiccat_processor_tests tests/processor_tests.cpp)
    target_link_libraries(musiccat_processor_tests PRIVATE musiccat_core)
    target_compile_options(musiccat_processor_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME musiccat_processor_tests COMMAND musiccat_processor_tests)
endif()

install(TARGETS MusicCat RUNTIME DESTINATION bin)
