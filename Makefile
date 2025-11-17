# Compiler and flags
CXX := g++
CXXFLAGS := -std=c++20 -g $(shell pkg-config --cflags libavcodec libavformat libavutil libswresample libswscale yaml-cpp)
LDLIBS := $(shell pkg-config --libs libavcodec libavformat libavutil libswresample libswscale yaml-cpp)

# Source and object files
SRCS := ConfigSources.cpp M3U8Generator.cpp main.cpp
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
	sudo install -Dm755 etc/hls-nvr/config.yaml      /etc/hls-nvr/config.yaml
	sudo install -Dm755 etc/systemd/system/hls-nvr.service      /etc/systemd/system
	sudo install -Dm755 $(TARGET) $(INSTALL_DIR)/bin/$(TARGET)
	sudo systemctl daemon-reload
	sudo systemctl enable hls-nvr.service
	sudo systemctl start hls-nvr.service
