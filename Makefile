CFLAGS+=-std=gnu11
ifdef DEBUG
	CFLAGS+=-ggdb3 -DDEBUG -Og -Wall -pedantic
else
	CFLAGS+=-DNDEBUG -O2
endif

EXE=dht21-ftdi
CFLAGS+=$(shell libftdi-config --cflags)
LDLIBS+=$(shell libftdi-config --libs)

all: main-build

main-build: astyle
	$(MAKE) --no-print-directory $(EXE)

SRCS = $(wildcard *.c)
OBJS = $(SRCS:%.c=%.o)

$(EXE): $(OBJS)

.PHONY:	clean astyle

CPPFLAGS += -MMD
-include $(SRCS:.c=.d)

.PHONY:	clean astyle

clean:
	rm -rf *.o *.d $(EXE)

install: $(EXE)
	install $(EXE) $(DESTDIR)/usr/bin

astyle:
	astyle --style=linux --indent=tab --unpad-paren --pad-header --pad-oper *.c
