#CXXFLAGS = -O0 -g -Wall -rdynamic -D_XOPEN_SOURCE=500
CXXFLAGS = -O2 -Wall -D_XOPEN_SOURCE=500
LIBS =

all: kbdrec

kbdrec: main.o
	g++ -o $@ $^ $(LIBS)

clean:
	rm -f *.o kbdrec
