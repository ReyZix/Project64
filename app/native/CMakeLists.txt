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

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    add_library(Project64Services STATIC
        Services/Hardware/P64Hardware.mm
        Services/Online/P64LichessService.mm
        Services/Online/P64TokenStore.mm)
    target_compile_options(Project64Services PRIVATE -fobjc-arc)
    target_link_libraries(Project64Services PUBLIC "-framework Foundation" "-framework CoreBluetooth" "-framework Security")
    add_library(Project64Session STATIC App/P64GameSession.mm)
    target_compile_options(Project64Session PRIVATE -fobjc-arc)
    target_link_libraries(Project64Session PUBLIC Project64Chess Project64Stockfish Project64Services)
    add_executable(HardwareFlowTests Services/Hardware/Tests/P64HardwareTests.mm)
    target_link_libraries(HardwareFlowTests PRIVATE Project64Services)
    add_executable(OnlineFlowTests Services/Online/Tests/P64LichessServiceTests.mm)
    target_link_libraries(OnlineFlowTests PRIVATE Project64Services)
    add_executable(NativeFlowTests Tests/NativeFlowTests.mm)
    target_link_libraries(NativeFlowTests PRIVATE Project64Session)
    foreach(testTarget HardwareFlowTests OnlineFlowTests NativeFlowTests)
        target_compile_options(${testTarget} PRIVATE -fobjc-arc -UNDEBUG)
    endforeach()
endif()

# Also build these as simulator executables so the same core tests can run
# against the iOS runtime via simctl spawn, without an account or board.
add_executable(ChessGameTests Tests/ChessGameTests.cpp)
target_link_libraries(ChessGameTests PRIVATE Project64Chess)
target_compile_options(ChessGameTests PRIVATE -UNDEBUG)
add_executable(StockfishTests Tests/StockfishTests.cpp)
target_link_libraries(StockfishTests PRIVATE Project64Stockfish)
target_compile_options(StockfishTests PRIVATE -UNDEBUG)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(Project64Native MACOSX_BUNDLE App/main.mm App/P64HomeViewController.mm App/P64PieceView.mm ${STOCKFISH_NETWORKS})
    target_link_libraries(Project64Native PRIVATE Project64Session "-framework UIKit" "-framework CoreGraphics" "-framework QuartzCore")
    target_compile_options(Project64Native PRIVATE -fobjc-arc)
    set_source_files_properties(${STOCKFISH_NETWORKS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(Project64Native PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/App/Info.plist"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "edu.project64.native"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic)
    add_executable(UISmokeTests MACOSX_BUNDLE Tests/UISmokeTests.mm App/P64HomeViewController.mm App/P64PieceView.mm)
    target_compile_options(UISmokeTests PRIVATE -fobjc-arc -UNDEBUG)
    target_link_libraries(UISmokeTests PRIVATE Project64Session "-framework UIKit" "-framework CoreGraphics" "-framework QuartzCore")
    set_target_properties(UISmokeTests PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/App/Info.plist"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "edu.project64.native.ui-tests"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)
    if(CMAKE_OSX_SYSROOT MATCHES "simulator|Simulator")
        # Xcode injects simulated entitlements into the simulator executable.
        # Device builds instead obtain their app identity from provisioning.
        foreach(appTarget Project64Native UISmokeTests)
            set_target_properties(${appTarget} PROPERTIES
                XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/App/Simulator.entitlements"
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
                XCODE_ATTRIBUTE_CODE_SIGN_STYLE Manual)
        endforeach()
    endif()
else()
    enable_testing()
    add_test(NAME ChessGame COMMAND ChessGameTests)
    add_test(NAME Stockfish COMMAND StockfishTests "${CMAKE_CURRENT_SOURCE_DIR}/Vendor/stockfish")
    set_tests_properties(Stockfish PROPERTIES TIMEOUT 90)
    if(APPLE)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME HardwareFlow COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/Tests/run_native_tests.py" --hardware $<TARGET_FILE:HardwareFlowTests>)
        add_test(NAME OnlineFlow COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/Tests/run_native_tests.py" --online $<TARGET_FILE:OnlineFlowTests>)
        add_test(NAME NativeFlow COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/Tests/run_native_tests.py" --session $<TARGET_FILE:NativeFlowTests> --networks "${CMAKE_CURRENT_SOURCE_DIR}/Vendor/stockfish")
        set_tests_properties(HardwareFlow OnlineFlow NativeFlow PROPERTIES TIMEOUT 60)
    endif()
endif()
