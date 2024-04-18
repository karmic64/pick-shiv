ifdef COMSPEC
DOTEXE:=.exe
else
DOTEXE:=
endif


#############################################################################


.PHONY: default

default: pixiv$(DOTEXE)


#############################################################################

CFLAGS:=-Ofast -Wall -Wextra -Wpedantic $(shell curl-config --cflags)
LDFLAGS:=-s
LDLIBS:=$(shell curl-config --libs) -lcjson

%$(DOTEXE): %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)
