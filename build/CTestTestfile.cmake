# CMake generated Testfile for 
# Source directory: /Users/thoth/Classes/cen4907c/Project64/app/native
# Build directory: /Users/thoth/Classes/cen4907c/Project64/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("ChessGame" "/Users/thoth/Classes/cen4907c/Project64/build/ChessGameTests")
set_tests_properties("ChessGame" PROPERTIES  _BACKTRACE_TRIPLES "/Users/thoth/Classes/cen4907c/Project64/app/native/CMakeLists.txt;85;add_test;/Users/thoth/Classes/cen4907c/Project64/app/native/CMakeLists.txt;0;")
add_test("Stockfish" "/Users/thoth/Classes/cen4907c/Project64/build/StockfishTests" "/Users/thoth/Classes/cen4907c/Project64/app/native/Vendor/stockfish")
set_tests_properties("Stockfish" PROPERTIES  TIMEOUT "90" _BACKTRACE_TRIPLES "/Users/thoth/Classes/cen4907c/Project64/app/native/CMakeLists.txt;86;add_test;/Users/thoth/Classes/cen4907c/Project64/app/native/CMakeLists.txt;0;")
add_test("HardwareFlow" "/opt/homebrew/Frameworks/Python.framework/Versions/3.14/bin/python3.14" "/Users/thoth/Classes/cen4907c/Project64/app/native/Tests/run_native_tests.py" "--hardware" "/Users/thoth/Classes/cen4907c/Project64/build/HardwareFlowTests")
set_tests_properties("HardwareFlow" PROPERTIES  TIMEOUT "60" _BACKTRACE_TRIPLES "/Users/thoth/Classes/cen4907c/Project64/app/native/CMakeLists.txt;90;add_test;/Users/thoth/Classes/cen4907c/Project64/app/native/CMakeLists.txt;0;")
add_test("OnlineFlow" "/opt/homebrew/Frameworks/Python.framework/Versions/3.14/bin/python3.14" "/Users/thoth/Classes/cen4907c/Project64/app/native/Tests/run_native_tests.py" "--online" "/Users/thoth/Classes/cen4907c/Project64/build/OnlineFlowTests")
set_tests_properties("OnlineFlow" PROPERTIES  TIMEOUT "60" _BACKTRACE_TRIPLES "/Users/thoth/Classes/cen4907c/Project64/app/native/CMakeLists.txt;91;add_test;/Users/thoth/Classes/cen4907c/Project64/app/native/CMakeLists.txt;0;")
add_test("NativeFlow" "/opt/homebrew/Frameworks/Python.framework/Versions/3.14/bin/python3.14" "/Users/thoth/Classes/cen4907c/Project64/app/native/Tests/run_native_tests.py" "--session" "/Users/thoth/Classes/cen4907c/Project64/build/NativeFlowTests" "--networks" "/Users/thoth/Classes/cen4907c/Project64/app/native/Vendor/stockfish")
set_tests_properties("NativeFlow" PROPERTIES  TIMEOUT "60" _BACKTRACE_TRIPLES "/Users/thoth/Classes/cen4907c/Project64/app/native/CMakeLists.txt;92;add_test;/Users/thoth/Classes/cen4907c/Project64/app/native/CMakeLists.txt;0;")
