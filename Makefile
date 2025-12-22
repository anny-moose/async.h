CC := clang
CFLAGS := -O1 -Werror -Wall -Wextra -g -fsanitize=thread,undefined
LFLAGS := -lpthread -lm

all: test 

test: test.c async.h
	${CC} ${CFLAGS} ${LFLAGS} test.c -o test

.PHONY: tidy
tidy:
	-rm -rf test
