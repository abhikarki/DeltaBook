CXX = g++
CXXFLAGS = -std=c++17 -O2 -march=native -Wall -Wextra

# to link libraries for boost, OpenSSL and thread
LIBS = -lboost_system -lboost_json -lssl -lcrypto -pthread

#python bindings
PYTHON_INCLUDES = $(shell python3-config --includes)
PYBIND11_INCLUDES = $(shell python3 -m pybind11 --includes)
EXTENSION_SUFFIX = $(shell python3-config --extension-suffix)

CPP_TARGET = main_client
PY_TARGET = kalshi_orderbook$(EXTENSION_SUFFIX)
TEST_TARGET = run_tests

# automatically find our tests
TEST_SRCS = tests/test_main.cpp $(wildcard tests/*_tests.cpp)

all: $(CPP_TARGET) $(PY_TARGET)

$(CPP_TARGET) : main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o $(CPP_TARGET) $(LIBS)

$(PY_TARGET) : main.cpp bindings.cpp
	$(CXX) $(CXXFLAGS) -shared -fPIC -DKALSHI_PYBIND_BUILD $(PYTHON_INCLUDES) $(PYBIND11_INCLUDES) main.cpp bindings.cpp -o $(PY_TARGET) $(LIBS)


#Test Target
$(TEST_TARGET) : $(TEST_SRCS)
	$(CXX) $(CXXFLAGS) -I./tests $(TEST_SRCS) -o $(TEST_TARGET) $(LIBS)


clean:
	rm -f $(CPP_TARGET) $(TEST_TARGET) kalshi_orderbook*.so