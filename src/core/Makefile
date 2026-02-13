CC = gcc
CFLAGS = -Wformat-truncation=0
TARGET = main

SRC = main.c ../libs/json/cJSON.c

daemon: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
