# Installation paths
PREFIX     ?= /usr/local
BINDIR     = $(PREFIX)/bin
INCLUDEDIR = $(PREFIX)/include/lls
LIBDIR     = $(PREFIX)/lib

# Build targets
lls: lls-cli.c lls.o
	cc -o lls lls-cli.c lls.o

lls.o: lls.c lls.h
	cc -c -o lls.o lls.c

liblls.so: lls.c lls.h
	cc -fPIC -shared -o liblls.so lls.c

# Install everything
install: lls liblls.so
	@echo "Installing binary to $(BINDIR)..."
	mkdir -p $(BINDIR)
	cp lls $(BINDIR)/lls

	@echo "Installing header to $(INCLUDEDIR)..."
	mkdir -p $(INCLUDEDIR)
	cp lls.h $(INCLUDEDIR)/

	@echo "Installing shared library to $(LIBDIR)..."
	mkdir -p $(LIBDIR)
	cp liblls.so $(LIBDIR)/

# Clean generated files
clean:
	rm -f lls lls.o liblls.so

# Uninstall everything
uninstall:
	rm -f $(BINDIR)/lls
	rm -f $(LIBDIR)/liblls.so
	rm -rf $(INCLUDEDIR)
