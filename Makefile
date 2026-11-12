CXX ?= c++
CXXFLAGS ?= -std=c++20 -O2 -pthread -Wall -Wextra -Wpedantic
CPPFLAGS += -I external/mcpxx/include
TARGET := bin/memory_mcp_tool_server
SOURCE := src/memory_mcp_tool_server.cpp
.PHONY: all clean
all: $(TARGET)
$(TARGET): $(SOURCE)
	mkdir -p bin
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
clean:
	rm -rf bin
