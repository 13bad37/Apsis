CC = gcc
EMCC ?= emcc

CFLAGS = -std=c11 -Wall -Wextra -pedantic $(shell pkg-config --cflags sdl2 SDL2_ttf)
LDFLAGS = $(shell pkg-config --libs sdl2 SDL2_ttf) -lm

TARGET = apsis
TEST_TARGET = collision_tests
WEB_DIST = web/dist
WEB_TARGET = $(WEB_DIST)/index.html
WEB_FLAGS = -std=gnu11 -O2 -Wall -Wextra \
	--use-port=sdl2 --use-port=sdl2_ttf \
	-sASYNCIFY -sSTACK_SIZE=4194304 -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1 \
	--preload-file assets/fonts/NotoSans-Regular.ttf@assets/fonts/NotoSans-Regular.ttf \
	--shell-file web/shell.html

SRC = main.c simulation.c scenes.c render.c state_io.c benchmark_io.c
OBJ = main.o simulation.o scenes.o render.o state_io.o benchmark_io.o

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

$(TEST_TARGET): tests/collision_tests.c simulation.c scenes.c
	$(CC) $(CFLAGS) tests/collision_tests.c simulation.c scenes.c -o $(TEST_TARGET) $(LDFLAGS)

web: $(WEB_TARGET)

$(WEB_TARGET): $(SRC) gravity.h render.h simulation.h scenes.h state_io.h benchmark_io.h \
		web/shell.html web/site.css web/favicon.svg assets/fonts/NotoSans-Regular.ttf
	mkdir -p $(WEB_DIST)/assets/fonts
	cp web/site.css web/favicon.svg $(WEB_DIST)/
	cp assets/fonts/NotoSans-Regular.ttf $(WEB_DIST)/assets/fonts/
	$(EMCC) $(WEB_FLAGS) $(SRC) -o $(WEB_TARGET) -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $<

run: all
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_TARGET)

clean-web:
	rm -rf $(WEB_DIST)
