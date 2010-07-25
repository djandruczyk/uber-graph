all: uber-graph

FILES = \
	../g-ring.c \
	../g-ring.h \
	uber-graph.c \
	uber-graph.h \
	main.c \
	$(NULL)

uber-graph: $(FILES) Makefile
	$(CC) -g -o $@ -Wall -Werror $(FILES) $(shell pkg-config --cflags --libs clutter-1.0)

clean:
	rm -rf uber-graph

run: uber-graph
	./uber-graph
