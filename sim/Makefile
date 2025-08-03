CC     = cc
CFLAGS = -Os -ansi -Wall -Wpedantic
#CFLAGS = -O0 -g -ansi -Wall -Wpedantic
SRC    = sim.c posix.c
OBJ    = ${SRC:.c=.o}

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

all: sim

config.h:
	cp config.def.h $@

${OBJ}: config.h sim.h

sim: ${OBJ}
	${CC} ${OBJ} -o $@

%.o: %.c
	${CC} ${CFLAGS} -c $< -o $@

install: sim
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f sim ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/sim
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	cp -f sim.1 ${DESTDIR}${MANPREFIX}/man1/sim.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/sim.1

nuke:
	rm -f ${DESTDIR}${PREFIX}/bin/sim
	rm -f ${DESTDIR}${MANPREFIX}/man1/sim.1

clean:
	rm -f sim *.o

.PHONY: all install nuke clean
