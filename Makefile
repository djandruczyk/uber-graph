all: uber-graph

DISABLE_DEBUG := 1

WARNINGS =								\
	-Wall								\
	-Werror								\
	-Wold-style-definition						\
	-Wdeclaration-after-statement					\
	-Wredundant-decls						\
	-Wmissing-noreturn						\
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

#	-Wshadow

DEBUG_INCLUDES =							\
	-DG_DISABLE_ASSERT						\
	-DG_DISABLE_CHECKS						\
	-DG_DISABLE_CAST_CHECKS						\
	-DDISABLE_DEBUG							\
	$(NULL)

INCLUDES =								\
	-DHAVE_CONFIG_H=0						\
	$(NULL)

ifeq ($(DISABLE_DEBUG),1)
	INCLUDES += $(DEBUG_INCLUDES)
endif

uber-graph.o: uber-graph.c uber-graph.h Makefile
	$(CC) -g -c -o $@ $(WARNINGS) $(INCLUDES) uber-graph.c $(shell pkg-config --cflags gtk+-2.0)

uber-buffer.o: uber-buffer.c uber-buffer.h Makefile
	$(CC) -g -c -o $@ $(WARNINGS) $(INCLUDES) uber-buffer.c $(shell pkg-config --cflags gtk+-2.0)

main.o: main.c Makefile
	$(CC) -g -c -o $@ $(WARNINGS) $(INCLUDES) main.c $(shell pkg-config --cflags gtk+-2.0)

uber-graph: uber-graph.o main.o uber-buffer.o Makefile
	$(CC) -g -o $@ $(shell pkg-config --libs gtk+-2.0 gthread-2.0) uber-graph.o main.o uber-buffer.o

clean:
	rm -f uber-graph *.o

run: uber-graph
	./uber-graph
