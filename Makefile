INCLUDES        = -I. -I/usr/include

LIBS		= libsocklib.a \
			-ldl -lpthread -lm

COMPILE_FLAGS   = ${INCLUDES} -c
COMPILE         = gcc ${COMPILE_FLAGS}
LINK            = gcc -g -o

C_SRCS		= \
		passivesock.c \
		connectsock.c \
		structures.c \
		server.c

SOURCE          = ${C_SRCS}

OBJS            = ${SOURCE:.c=.o}

EXEC		= server

.SUFFIXES       :       .o .c .h

all		:	library server

.c.o            :	${SOURCE}
			@echo "    Compiling $< . . .  "
			@${COMPILE}  $<

library		:	passivesock.o connectsock.o
			ar rv libsocklib.a passivesock.o connectsock.o

server	:	server.o structures.o
			${LINK} $@ server.o structures.o ${LIBS}

clean           :
			@echo "    Cleaning ..."
			rm -f tags core *.out *.o *.lis *.a ${EXEC} libsocklib.a