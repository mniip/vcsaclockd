CC=gcc
CPPFLAGS=
CFLAGS=
LDFLAGS= -lpthread
all: vcsaclockd.c
	$(CC) -o vcsaclockd vcsaclockd.c $(CFLAGS) $(CPPFLAGS) $(LDFLAGS)
