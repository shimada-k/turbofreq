# Makefile
objs = turbofreq.o bitops.o

turbofreq: Makefile $(objs)
	cc -Wall -o turbofreq $(objs) -lpthread

turbofreq.o: turbofreq.c
	cc -Wall -c turbofreq.c

bitops.o: bitops.c bitops.h
	cc -Wall -c bitops.c

clean:
	rm -f shalend *.o *~
