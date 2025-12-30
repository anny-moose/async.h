CC := clang
CFLAGS := -O1 -Werror -Wall -Wextra -g -fsanitize=thread,undefined
LFLAGS := -I ../ -lpthread -lm



all: pi_estimation 

pi_estimation: pi_estimation.c ../async.h
	${CC} ${CFLAGS} ${LFLAGS} pi_estimation.c -o pi_estimation

.PHONY: tidy
tidy:
	-rm -rf pi_estimation
