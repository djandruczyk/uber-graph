all: uber-graph

WARNINGS =								\
	-Wall								\
	-Werror								\
	-Wold-style-definition						\
	-Wdeclaration-after-statement					\
	-Wredundant-decls						\
	-Wmissing-noreturn						\
	-Wshadow							\
	-Wcast-align							\
	-Wwrite-strings							\
	-Winline							\
	-Wformat-nonliteral						\
	-Wformat-security						\
	-Wswitch-enum							\
	-Wswitch-default						\
	-Winit-self							\
	-Wmissing-include-dirs						\
	-Wundef								\
	-Waggregate-return						\
	-Wmissing-format-attribute					\
	-Wnested-externs

INCLUDES = -DHAVE_CONFIG_H=0

uber-graph.o: uber-graph.c uber-graph.h
	$(CC) -g -c -o $@ $(WARNINGS) $(INCLUDES) uber-graph.c $(shell pkg-config --cflags gtk+-2.0)

uber-data-set.o: uber-data-set.c uber-data-set.h
	$(CC) -g -c -o $@ $(WARNINGS) $(INCLUDES) uber-data-set.c $(shell pkg-config --cflags gtk+-2.0)

main.o: main.c
	$(CC) -g -c -o $@ $(WARNINGS) $(INCLUDES) main.c $(shell pkg-config --cflags gtk+-2.0)

uber-graph: uber-graph.o uber-data-set.o main.o
	$(CC) -g -o $@ $(shell pkg-config --libs gtk+-2.0) uber-graph.o uber-data-set.o main.o

clean:
	rm -f uber-graph *.o

run: uber-graph
	./uber-graph
