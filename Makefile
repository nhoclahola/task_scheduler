CC = gcc
CFLAGS = -Wall -Wextra -g -std=c99 -D_GNU_SOURCE
LDFLAGS = -pthread -lreadline -lsqlite3 -lcurl -lcjson

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

.PHONY: all clean directories debug ls check-c

all: directories $(EXECUTABLE)

check-c:
	@echo "Checking if C compiler works..."
	@echo 'int main(void) { return 0; }' > test.c
	$(CC) test.c -o test
	@echo "C compiler is working!"
	@rm -f test.c test

ls:
	@echo "Listing source directories:"
	@ls -la $(SRC_DIR) || echo "$(SRC_DIR) không tồn tại"
	@ls -la $(SRC_DIR)/core || echo "$(SRC_DIR)/core không tồn tại"
	@ls -la $(SRC_DIR)/db || echo "$(SRC_DIR)/db không tồn tại"
	@ls -la $(SRC_DIR)/cli || echo "$(SRC_DIR)/cli không tồn tại"
	@ls -la $(SRC_DIR)/utils || echo "$(SRC_DIR)/utils không tồn tại"

debug:
	@echo "Sources to compile: $(SOURCES)"
	@echo "Objects to create: $(OBJECTS)"
	@echo "Try compiling main manually..."
	$(CC) -c $(SRC_DIR)/main.c -o $(OBJ_DIR)/main.o -I$(INCLUDE_DIR) $(CFLAGS)

directories:
	mkdir -p $(BIN_DIR)
	mkdir -p $(OBJ_DIR)/core
	mkdir -p $(OBJ_DIR)/db
	mkdir -p $(OBJ_DIR)/cli
	mkdir -p $(OBJ_DIR)/utils
	mkdir -p $(LIB_DIR)
	mkdir -p $(DATA_DIR)

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

run: all
	$(EXECUTABLE) 