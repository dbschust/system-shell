#Daniel Schuster


CC = gcc
DEBUG = -g
CFLAGS = -Wall -Wextra -Wshadow -Wunreachable-code -Wredundant-decls \
-Wmissing-declarations -Wold-style-definition -Wmissing-prototypes \
-Wdeclaration-after-statement -Wno-return-local-addr \
-Wunsafe-loop-optimizations -Wuninitialized -Werror -Wunused \
$(DEBUG)

PROGS = $(PROG1)
PROG1 = psush

TAR_FILE = ${LOGNAME}_lab4.tar.gz

ALL all All: $(PROGS)


$(PROG1): $(PROG1).o
	$(CC) -o $(PROG1) $(PROG1).o -lmd

$(PROG1).o: $(PROG1).c
	$(CC) $(CFLAGS) -c $(PROG1).c

clean cls:
	rm -f $(PROGS) *.o *~ \#*

tar: clean
	rm -f $(TAR_FILE)
	tar cvfa $(TAR_FILE) *.[ch] [Mm]akefile

