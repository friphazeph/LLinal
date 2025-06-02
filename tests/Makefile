LLN_EXEC = ../lln
TESTS := $(basename $(wildcard *.lln))

.PHONY: all run setup expected clean

all: run

run: $(TESTS:%=run-%)

setup: $(TESTS:%=%.o)

expected: $(TESTS:%=%.exp)

run-%: %.lln %.o %.exp
	@echo "Running test: $*"
	@$(LLN_EXEC) -ro $*.lln $*.o | diff -u $*.exp -

%.o: %.c
	$(LLN_EXEC) -co $< $@

%.exp: %.lln %.o
	$(LLN_EXEC) -ro $*.lln $*.o > $@

clean:
	rm -f *.o *.exp
