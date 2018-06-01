CC = g++
IDIR = ./lib
CFLAGS = -I ${IDIR} -std=c++11 -Wall -pedantic -O3 -pthread
OBJ = RaspServer.o gateway.o

.PHONY : clean

all : RaspServer.o gateway.o

RaspServer.o : RaspServer.h RaspServer.cpp
	${CC} ${CFLAGS} -c RaspServer.cpp

gateway.o :RaspServer.o gateway.cpp
	${CC} ${CFLAGS} RaspServer.o gateway.cpp -o gateway.o

clean :
	${RM} ${OBJ}
