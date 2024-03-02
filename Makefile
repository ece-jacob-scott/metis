PROJECT_NAME=metis
PROJECT_DIR=src
BIN_DIR=bin

SOURCE_FILES=$(wildcard $(PROJECT_DIR)/*.c)
INCLUDE_DIRS=$(PROJECT_DIR)

# CC=gcc
CC=clang
CFLAGS=-g -Wall -Wextra -Werror -Wpedantic -std=c99 -I$(INCLUDE_DIRS)

build: $(SOURCE_FILES)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/$(PROJECT_NAME) $(SOURCE_FILES)

@PHONY: clean
clean:
	rm -f $(BIN_DIR)/$(PROJECT_NAME)
