cmake_minimum_required(VERSION 3.20)
project(MusicCat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# 使用 spdlog 作为 logger
FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.15.3
)

FetchContent_MakeAvailable(spdlog)

add_executable(
        MusicCat
        main.cpp
        include/Mcat.cpp
        include/Mcat.h
        include/common/src/FileManager.cpp
        include/common/FileManager.h
        include/common/src/TypeParser.cpp
        include/common/TypeParser.h
        include/common/src/Path.cpp
        include/common/Path.h
        include/common/src/CommonsInit.cpp
        include/common/CommonsInit.h
        include/common/src/Logger.cpp
        include/common/Logger.h
        include/core/src/MyFfmpeg.cpp
        include/core/MyFfmpeg.h
        include/core/src/MyAppleMusic.cpp
        include/core/MyAppleMusic.h
        include/common/src/Listener.cpp
        include/common/Listener.h
        include/common/src/Cmd.cpp
        include/common/Cmd.h
        include/core/src/MySox.cpp
        include/core/MySox.h
        include/common/src/Entity.cpp
        include/common/Entity.h
        include/common/Public.h
)

target_include_directories(
        MusicCat PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/
        ${CMAKE_CURRENT_SOURCE_DIR}/include/
        ${CMAKE_CURRENT_SOURCE_DIR}/include/common/
        ${CMAKE_CURRENT_SOURCE_DIR}/include/core/
)

target_link_libraries(MusicCat PRIVATE spdlog::spdlog)


set_target_properties(MusicCat PROPERTIES
        OUTPUT_NAME "mcat"
)