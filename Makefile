run : llscript
	./llscript

llscript : llscript.c lls.h lls.o
	cc -o llscript llscript.c lls.o 

lls.o : lls.c
	cc -o lls.o -c lls.c
