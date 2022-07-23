CC=g++
CFLAGS=-Wall -pedantic
OBJ = mcs.o

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

mcs: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)
