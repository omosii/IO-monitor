CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -pthread
TARGET = analyze_io_log

all: $(TARGET)

$(TARGET): analyze_io_log.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean