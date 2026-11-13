cmake_minimum_required(VERSION 3.22)
project(Project64Native VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)

add_library(Project64Chess STATIC Core/ChessGame.cpp)
target_include_directories(Project64Chess PUBLIC Core)

# Stockfish's public Engine API runs in-process. No main(), pipes, or UCI subprocess.
file(GLOB_RECURSE STOCKFISH_SOURCES CONFIGURE_DEPENDS Vendor/stockfish/*.cpp)
list(FILTER STOCKFISH_SOURCES EXCLUDE REGEX "/main\\.cpp$")
add_library(Project64Stockfish STATIC Core/StockfishEngine.cpp ${STOCKFISH_SOURCES})
target_include_directories(Project64Stockfish PUBLIC Core PRIVATE Vendor/stockfish)
target_compile_definitions(Project64Stockfish PRIVATE NNUE_EMBEDDING_OFF)
target_link_libraries(Project64Stockfish PUBLIC Project64Chess Threads::Threads)
file(GLOB STOCKFISH_NETWORKS CONFIGURE_DEPENDS Vendor/stockfish/*.nnue)

# Also build these as simulator executables so the same core tests can run
# against the iOS runtime via simctl spawn, without an account or board.
add_executable(ChessGameTests Tests/ChessGameTests.cpp)
target_link_libraries(ChessGameTests PRIVATE Project64Chess)
target_compile_options(ChessGameTests PRIVATE -UNDEBUG)
add_executable(StockfishTests Tests/StockfishTests.cpp)
target_link_libraries(StockfishTests PRIVATE Project64Stockfish)
target_compile_options(StockfishTests PRIVATE -UNDEBUG)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(OBJCXX)
    add_executable(Project64Native MACOSX_BUNDLE App/main.mm ${STOCKFISH_NETWORKS})
    target_link_libraries(Project64Native PRIVATE Project64Chess Project64Stockfish "-framework UIKit" "-framework Foundation")
    target_compile_options(Project64Native PRIVATE -fobjc-arc)
    set_source_files_properties(${STOCKFISH_NETWORKS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(Project64Native PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/App/Info.plist"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "edu.project64.native"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic)
else()
    enable_testing()
    add_test(NAME ChessGame COMMAND ChessGameTests)
    add_test(NAME Stockfish COMMAND StockfishTests "${CMAKE_CURRENT_SOURCE_DIR}/Vendor/stockfish")
    set_tests_properties(Stockfish PROPERTIES TIMEOUT 90)
endif()
