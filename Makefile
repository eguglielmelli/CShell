CC = gcc
CFLAGS = -g -Wall

TARGET = shell
SRCS = shell.c input_parser.c utils.c
HEADERS = input_parser.h shell.h utils.h

.PHONY: clean all

default: $(TARGET)

all: default

$(TARGET): $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) $(SRCS) -g -o $(TARGET)

val: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET)

clean:
	rm -f $(TARGET)
