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

SOURCES = uber-graph.c uber-graph.h main.c

uber-graph: $(SOURCES)
	$(CC) -g -o $@.o $(WARNINGS) $(INCLUDES) $(SOURCES) $(shell pkg-config --libs --cflags gtk+-2.0)
	@mv $@.o $@

clean:
	rm -f uber-graph

run: uber-graph
	./uber-graph
