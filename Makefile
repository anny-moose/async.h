CC := clang
CFLAGS := -Werror -Wall -Wextra -g -fsanitize=address,undefined
LFLAGS := -lpthread -lm

all: test 

test: test.c
	${CC} ${CFLAGS} ${LFLAGS} test.c -o test

.PHONY: tidy
tidy:
	-rm -rf test
