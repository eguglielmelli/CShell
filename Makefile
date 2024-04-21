CC = gcc
CFLAGS = -g -Wall

TARGETS = shell

.PHONY: clean all 

default: shell

all: $(TARGETS)

temp_conversion: shell.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGETS) 