CC = gcc
CFLAGS = -g
LIBS = .
SRC = src/shm_ipc.c
OBJ = $(SRC:.c=.o) 

OUT = bin/libtinyfile.a

.c.o:
	$(CC) $(CFLAG) -pthread -c $< -o $@

$(OUT): $(OBJ)
	ar rcs $(OUT) $(OBJ)

all: service test_service

service: src/service.c
	$(CC) $(CFLAGS) src/service.c src/shm_ipc.c src/libsnappy.a -o bin/service

test_service: src/test_service.c
	$(CC) $(CFLAGS) -pthread src/test_service.c $(OUT) -o bin/test_service

clean:
	@rm src/*.o bin/service
	@echo Cleaned!
