# Installation paths
PREFIX     ?= /usr
BINDIR     = $(PREFIX)/bin
INCLUDEDIR = $(PREFIX)/include/lln
LIBDIR     = $(PREFIX)/lib

# Build targets
lln: lln-cli.c lln.o
	cc -Wall -Wextra -o lln lln-cli.c lln.o

lln.o: lln.c lln.h
	cc -c -Wall -Wextra -o lln.o lln.c

liblln.so: lln.c lln.h
	cc -fPIC -shared -Wl,-soname,liblln.so.1 -o liblln.so lln.c

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
	cd $(LIBDIR) && ln -sf liblln.so liblln.so.1
	ldconfig

# Clean generated files
clean:
	rm -f lln lln.o liblln.so

# Uninstall everything
uninstall:
	rm -f $(BINDIR)/lln
	rm -f $(LIBDIR)/liblln.so
	rm -rf $(INCLUDEDIR)

tests: lln
	cd tests && make
