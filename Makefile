CC = gcc
CFLAGS = -g -Wall

TARGET = shell
SRCS = shell.c input_parser.c
HEADERS = input_parser.h

.PHONY: clean all

default: $(TARGET)

all: default

$(TARGET): $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)