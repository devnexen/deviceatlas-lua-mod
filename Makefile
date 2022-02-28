CC?=gcc
.PHONY: clean

dalua.so:
	$(CC) -shared -fPIC -o dalua.so dalua.c $(CFLAGS) $(LDFLAGS) -lda
clean:
	rm -f dalua.so
