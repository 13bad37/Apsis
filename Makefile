CC = gcc

CFLAGS = -std=c11 -Wall -Wextra -pedantic $(shell pkg-config --cflags sdl2 SDL2_ttf)
LDFLAGS = $(shell pkg-config --libs sdl2 SDL2_ttf) -lm

TARGET = apsis
TEST_TARGET = collision_tests

SRC = main.c simulation.c scenes.c render.c state_io.c benchmark_io.c
OBJ = main.o simulation.o scenes.o render.o state_io.o benchmark_io.o

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

$(TEST_TARGET): tests/collision_tests.c simulation.c scenes.c
	$(CC) $(CFLAGS) tests/collision_tests.c simulation.c scenes.c -o $(TEST_TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

run: all
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_TARGET)
