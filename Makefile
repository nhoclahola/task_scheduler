CC = gcc
CFLAGS = -Wall -Wextra -g -std=c99 -D_GNU_SOURCE
LDFLAGS = -pthread -lreadline -lsqlite3

SRC_DIR = src
INCLUDE_DIR = include
BIN_DIR = bin
OBJ_DIR = obj
LIB_DIR = lib
DATA_DIR = data

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))
EXECUTABLE = $(BIN_DIR)/taskscheduler

.PHONY: all clean directories

all: directories $(EXECUTABLE)

directories:
	mkdir -p $(BIN_DIR) $(OBJ_DIR) $(LIB_DIR) $(DATA_DIR)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c -o $@ $<

clean:
	rm -rf $(BIN_DIR) $(OBJ_DIR)

install: all
	mkdir -p $(DESTDIR)/usr/local/bin
	install -m 755 $(EXECUTABLE) $(DESTDIR)/usr/local/bin/taskscheduler

uninstall:
	rm -f $(DESTDIR)/usr/local/bin/taskscheduler

run: all
	$(EXECUTABLE) 