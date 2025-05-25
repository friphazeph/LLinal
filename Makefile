# run : main
# 	./main ./test-cases/hello.lls

main : main.c lls.o cli
	./lls -c main.c main

cli : lls-cli.c
	cc -o lls lls-cli.c

lls.o : lls.h lls.c
	cc -c -o lls.o lls.c
