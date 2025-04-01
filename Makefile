CC = gcc
CFLAGS = -Wall -Wextra -g -std=c99 -D_GNU_SOURCE
LDFLAGS = -pthread -lreadline -lsqlite3

SRC_DIR = src
INCLUDE_DIR = include
BIN_DIR = bin
OBJ_DIR = obj
LIB_DIR = lib
DATA_DIR = data

# Tìm tất cả các file nguồn trong thư mục src và các thư mục con
SOURCES = $(SRC_DIR)/main.c \
          $(wildcard $(SRC_DIR)/core/*.c) \
          $(wildcard $(SRC_DIR)/db/*.c) \
          $(wildcard $(SRC_DIR)/cli/*.c) \
          $(wildcard $(SRC_DIR)/utils/*.c)

# Tạo danh sách các file đối tượng tương ứng
OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))

EXECUTABLE = $(BIN_DIR)/taskscheduler

.PHONY: all clean directories

all: directories $(EXECUTABLE)

directories:
	mkdir -p $(BIN_DIR) $(OBJ_DIR) $(OBJ_DIR)/core $(OBJ_DIR)/db $(OBJ_DIR)/cli $(OBJ_DIR)/utils $(LIB_DIR) $(DATA_DIR)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Quy tắc cho file main.c
$(OBJ_DIR)/main.o: $(SRC_DIR)/main.c
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c -o $@ $<

# Quy tắc cho các file trong thư mục core
$(OBJ_DIR)/core/%.o: $(SRC_DIR)/core/%.c
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c -o $@ $<

# Quy tắc cho các file trong thư mục db
$(OBJ_DIR)/db/%.o: $(SRC_DIR)/db/%.c
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c -o $@ $<

# Quy tắc cho các file trong thư mục cli
$(OBJ_DIR)/cli/%.o: $(SRC_DIR)/cli/%.c
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c -o $@ $<

# Quy tắc cho các file trong thư mục utils
$(OBJ_DIR)/utils/%.o: $(SRC_DIR)/utils/%.c
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