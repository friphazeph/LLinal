# Installation paths
PREFIX     ?= /usr/local
BINDIR     = $(PREFIX)/bin
INCLUDEDIR = $(PREFIX)/include/lln
LIBDIR     = $(PREFIX)/lib

# Build targets
lln: lln-cli.c lln.o
	cc -Wall -o lln lln-cli.c lln.o

lln.o: lln.c lln.h
	cc -c -Wall -o lln.o lln.c

liblln.so: lln.c lln.h
	cc -fPIC -shared -o liblln.so lln.c

# Install everything
install: lln liblln.so
	@echo "Installing binary to $(BINDIR)..."
	mkdir -p $(BINDIR)
	cp lln $(BINDIR)/lln

	@echo "Installing header to $(INCLUDEDIR)..."
	mkdir -p $(INCLUDEDIR)
	cp lln.h $(INCLUDEDIR)/

	@echo "Installing shared library to $(LIBDIR)..."
	mkdir -p $(LIBDIR)
	cp liblln.so $(LIBDIR)/

# Clean generated files
clean:
	rm -f lln lln.o liblln.so

# Uninstall everything
uninstall:
	rm -f $(BINDIR)/lln
	rm -f $(LIBDIR)/liblln.so
	rm -rf $(INCLUDEDIR)
