#
# Projekt: IOS Proj 2
# Autor:   Ondrej Slampa
# 
# Pouziti:
#   - preklad:      make
#   - ladit:        make debug
#

# jmeno projektu
NAME=barbers
# prekladac jazyka C
CC=gcc
# parametry překladace
CFLAGS= -std=gnu99 -Wall -Wextra -Werror -pedantic -lrt

$(NAME): $(NAME).c
	$(CC) $(NAME).c $(CFLAGS) -o $(NAME)

.PHONY: debug

debug: $(NAME)
	export XEDITOR=gedit;ddd $(NAME)
