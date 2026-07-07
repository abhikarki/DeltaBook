CXX = g++
CXXFLAGS = -std=c++17 -O2 -march=native -Wall -Wextra

TARGET = main_client

SRCS = main.cpp

# to link libraries for boost, OpenSSL and thread
LIBS = -lboost_system -lboost_json -lssl -lcrypto -pthread

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET)