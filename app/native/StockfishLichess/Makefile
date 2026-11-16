# make        builds ./project64
# make test   builds and runs the tests
CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
LIBS      = -lcurl -lpthread

CORE = src/Json.cpp src/Game.cpp src/StockfishEngine.cpp src/LichessClient.cpp src/LichessSession.cpp
HEADERS = $(wildcard src/*.h)

project64: $(CORE) src/main.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CORE) src/main.cpp -o $@ $(LIBS)

run-tests: $(CORE) tests/tests.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CORE) tests/tests.cpp -o $@ $(LIBS)

test: run-tests
	./run-tests

clean:
	rm -f project64 run-tests

.PHONY: test clean
