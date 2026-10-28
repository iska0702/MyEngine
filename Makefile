# Makefile for MyEngine Dedicated Server (Linux x86_64 / ARM64)
# Fully headless, zero GPU / DirectX dependencies.

CXX ?= g++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -pthread -DNDEBUG -DENGINE_SERVER=1 -Isrc
LDFLAGS ?= -pthread

SRC = \
    src/Server/ServerMain.cpp \
    src/Server/DedicatedServer.cpp \
    src/Level/LevelSystem.cpp \
    src/Core/Timer.cpp \
    src/Networking/BitWriter.cpp \
    src/Networking/BitReader.cpp \
    src/Networking/Packet.cpp \
    src/Networking/NetworkSocket.cpp \
    src/Networking/NetworkConnection.cpp \
    src/Networking/NetworkObject.cpp \
    src/Networking/NetworkReplication.cpp \
    src/Networking/ServerAntiCheat.cpp \
    src/Networking/NetworkServer.cpp

OBJ_DIR = bin/linux_obj
OBJS = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(SRC))
TARGET = bin/MyEngineServer

all: server

server: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)
	@echo "=================================================="
	@echo "Build successful: $(TARGET)"
	@echo "Run: ./$(TARGET) --port 7777"
	@echo "=================================================="

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all server clean
