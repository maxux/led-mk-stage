EXEC = staged

CFLAGS  = -std=gnu99 -W -Wall -O2 -I/opt/rpi_ws281x/
LDFLAGS = -L/opt/rpi_ws281x/ -lws2811 -lpng -lm

SRC=$(wildcard *.c)
OBJ=$(SRC:.c=.o)

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)

clean:
	rm -fv *.o

mrproper: clean
	rm -fv $(EXEC)

