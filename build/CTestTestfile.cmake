# CMake generated Testfile for 
# Source directory: /Users/thoth/Classes/cen4907c/Project64/app/native
# Build directory: /Users/thoth/Classes/cen4907c/Project64/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("ChessGame" "/Users/thoth/Classes/cen4907c/Project64/build/ChessGameTests")
set_tests_properties("ChessGame" PROPERTIES  _BACKTRACE_TRIPLES "/Users/thoth/Classes/cen4907c/Project64/app/native/CMakeLists.txt;44;add_test;/Users/thoth/Classes/cen4907c/Project64/app/native/CMakeLists.txt;0;")
add_test("Stockfish" "/Users/thoth/Classes/cen4907c/Project64/build/StockfishTests" "/Users/thoth/Classes/cen4907c/Project64/app/native/Vendor/stockfish")
set_tests_properties("Stockfish" PROPERTIES  TIMEOUT "90" _BACKTRACE_TRIPLES "/Users/thoth/Classes/cen4907c/Project64/app/native/CMakeLists.txt;45;add_test;/Users/thoth/Classes/cen4907c/Project64/app/native/CMakeLists.txt;0;")
