CXX = g++
CXXFLAGS = -std=c++17 -O2 -march=native -Wall -Wextra

# to link libraries for boost, OpenSSL and thread
LIBS = -lboost_system -lboost_json -lssl -lcrypto -pthread

CPP_TARGET = main_client
PY_TARGET = kalshi_orderbook$(EXTENSION_SUFFIX)
TEST_TARGET = run_tests

# automatically find our tests
TEST_SRCS = tests/test_main.cpp $(wildcard tests/*_tests.cpp)

all: $(CPP_TARGET)

$(CPP_TARGET) : main.cpp orderbook.hpp
	$(CXX) $(CXXFLAGS) main.cpp -o $(CPP_TARGET) $(LIBS)

#Test Target
$(TEST_TARGET) : $(TEST_SRCS)
	$(CXX) $(CXXFLAGS) -I./tests $(TEST_SRCS) -o $(TEST_TARGET) $(LIBS)


clean:
	rm -f $(CPP_TARGET) $(TEST_TARGET)