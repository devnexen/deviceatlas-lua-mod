CC?=gcc
dalua.so:
	$(CC) -shared -fPIC -o dalua.so dalua.c $(CFLAGS) $(LDFLAGS) -lda
clean:
	rm dalua.so
