MFLAGS=-lm
CFLAGS=-Wall -Werror -ggdb -I. -Ilibtrafficker/
NIDSFLAGS=-lnids
TARGETS=gmaps-profile gmaps-trafficker

all: libtrafficker/libtrafficker.a $(TARGETS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

libtrafficker/libtrafficker.a:
	$(MAKE) -C libtrafficker/

gmaps-trafficker: $(LIBTR) map.o list.o utils.o gmaps-utils.o gmaps-trafficker.c gmaps.h
	$(CC) $(CFLAGS) gmaps-trafficker.c map.o list.o utils.o gmaps-utils.o libtrafficker/libtrafficker.a $(NIDSFLAGS) $(MFLAGS) -o $@

gmaps-profile: map.o list.o utils.o gmaps-utils.o gmaps-profile.c gmaps.h
	$(CC) $(CFLAGS) gmaps-profile.c map.o list.o utils.o gmaps-utils.o $(MFLAGS) -o $@

clean:
	$(RM) $(TARGETS) *.o
	$(MAKE) -C libtrafficker clean

count:
	find . -iname "*.[c|h]" -exec cat \{\} \; | wc -l
