CFLAGS=-Wall -Werror
all: libtrafficker.a

libtrafficker.a: buffer.o hash.o ssl.o libtrafficker.o
	$(AR) rc $@ buffer.o hash.o ssl.o libtrafficker.o

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) libtrafficker.a *.o
