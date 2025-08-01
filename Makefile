# code details
EXES    := server
SRCS    := $(EXES:=.c)
OBJS    := $(EXES:=.o) 

# generic build details
CC     = gcc
CFLAGS = -std=c99 -Wall
CLIBS  = 
CLINK  = -lws2_32

.PHONY: all clean lint

# Default target: build everything
all: lint $(EXES)

# compile to object code
%.o: %.c
	$(CC) -c $(CFLAGS) $(CLIBS) $< -o $@

# link
server: server.o
	$(CC) $^ $(CLINK) -o $@

# Linting
lint:
	@echo "Running cppcheck..."
	cppcheck --enable=all --inconclusive --std=c99 --suppress=missingIncludeSystem --quiet $(SRCS)
	@echo "Running clang-tidy..."
	clang-tidy $(SRCS) -- -std=c99

# clean up and remove object code and executable: type 'make clean'
clean:
	rm -f $(OBJS) $(EXES)
