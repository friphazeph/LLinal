run : main
	./main

main : main.c lls.h lls.o
	cc -o main main.c lls.o

lls.o : lls.h lls.c llspreproc.c
	cc -o lls.o -c llspreproc.c
