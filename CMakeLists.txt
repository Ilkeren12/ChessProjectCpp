cmake_minimum_required(VERSION 3.16)

project(OnlineChessCpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC normalde kaynak dosyaları Windows ANSI kod sayfasıyla okuyabilir.
# /utf-8 Türkçe yazıların ve Unicode satranç taşlarının doğru görünmesini sağlar.
if (MSVC)
    add_compile_options(/utf-8)
endif()

add_library(chess_core
    src/common/Chess.cpp
)
target_include_directories(chess_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if (WIN32)
    add_executable(ChessWin WIN32
        src/client/WinClient.cpp
    )
    target_link_libraries(ChessWin PRIVATE chess_core ws2_32 gdi32 user32 comdlg32)
    target_compile_definitions(ChessWin PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if (UNIX AND NOT APPLE)
    add_executable(ChessServer
        src/server/LinuxTcpServer.cpp
    )
    target_link_libraries(ChessServer PRIVATE chess_core pthread)
endif()
