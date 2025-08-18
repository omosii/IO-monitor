CC = gcc
CFLAGS = -Wall -O2 -pthread
LDFLAGS = -pthread
TARGET = analyze_io_log
OBJS = analyze_io_log.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

analyze_io_log.o: analyze_io_log.c analyze_io_log.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean