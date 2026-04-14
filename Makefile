CC = gcc
CFLAGS = -Wall -O2
TARGET = bclmtool

all: $(TARGET)

$(TARGET): bclmtool.c
	$(CC) $(CFLAGS) -o $(TARGET) bclmtool.c

clean:
	rm -f $(TARGET)

install:
	sudo cp $(TARGET) /usr/local/bin/

