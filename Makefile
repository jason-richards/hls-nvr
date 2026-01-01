# Compiler and flags
CXX := g++
CXXFLAGS := -std=c++20 $(shell pkg-config --cflags libavcodec libavformat libavutil libswresample libswscale yaml-cpp)
LDLIBS := $(shell pkg-config --libs libavcodec libavformat libavutil libswresample libswscale yaml-cpp)
BUILD_TYPE ?= release

ifeq ($(BUILD_TYPE), release)
    CXXFLAGS += -O3
else ifeq ($(BUILD_TYPE), debug)
    CXXFLAGS += -DDEBUG -g
endif

# Source and object files
SRCS := ConfigSources.cpp ConfigPluginDir.cpp M3U8Generator.cpp PluginManager.cpp main.cpp
OBJS := $(SRCS:.cpp=.o)

# Output executable
TARGET := hls-nvr
INSTALL_DIR = /etc/$(TARGET)

# Default rule
all: $(TARGET)

# Link
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET) $(LDLIBS)

# Compile
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean
clean:
	rm -f $(OBJS) $(TARGET)

# Phony targets
.PHONY: all clean

install: $(TARGET)
	sudo install -Dm644 etc/systemd/system/hls-nvr.service /etc/systemd/system
	sudo install -Dm755 $(TARGET) $(INSTALL_DIR)/bin/$(TARGET)
	sudo systemctl daemon-reload
	sudo systemctl enable hls-nvr.service
	sudo systemctl start hls-nvr.service

install_config: $(TARGET)
	sudo install -Dm644 etc/hls-nvr/config.yaml /etc/hls-nvr/config.yaml
