exectest: exectest.c
	$(CC) -D_GNU_SOURCE $^ -o $@
