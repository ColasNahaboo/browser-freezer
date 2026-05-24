# Variables
DESTDIR=/usr/local/bin
TARGET=browser-freezer
AUX=browser-freezer-signal
SRC=browser-freezer.c
CFLAGS=-O2
LDFLAGS=-lX11 -lXext

# Mark targets as non-files to avoid conflicts
.PHONY: all clean install restart

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

install: $(TARGET)
	@# Use standard install utility, safer than mv/cp
	@# 'install -m 755' ensures the binary is executable by all
	install -m 755 $(TARGET) $(DESTDIR)/$(TARGET)
	install -m 755 $(AUX)  $(DESTDIR)/$(AUX)

restart: install
	@# Graceful kill: try SIGTERM first, wait, then SIGKILL
	@echo "Restarting $(TARGET)..."
	-killall $(TARGET)
	@sleep 1
	@# Ensure the process is actually gone
	-pkill -9 $(TARGET) 2>/dev/null
	@# Restart detached
	$(DESTDIR)/$(TARGET) </dev/null >/dev/null 2>&1 &

clean:
	@# Remove the object file and the binary
	rm -f *.o $(TARGET)
	@# Optionally clean up the old versions
	rm -f $(TARGET).old
