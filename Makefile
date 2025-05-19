run : llscript
	./llscript

trans : lls.h lls.c llspreproc.c
	cc -o llspreproc -ggdb llspreproc.c
	./llspreproc

llscript : main.c lls.h lls.o
	cc -o llscript main.c lls.o 

lls.o : lls.c
	cc -o lls.o -c lls.c
