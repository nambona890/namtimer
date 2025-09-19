SOURCES := $(shell find . -name "*.cpp" -o -name "*.c")

all:
	g++ $(SOURCES) -o namtimer -lm -Ofast -Iinclude -lSDL2 -std=c++20
