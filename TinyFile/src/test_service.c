#include <stdio.h>
#include <pthread.h>
#include <stdlib.h> // atol()
#include <string.h> // strcat()
#include "shm_ipc.h"


typedef struct __pthread_arg {
	char* filename;
	size_t file_id;
} pthread_arg_t;


static void* sync_call(void* arg) {
	pthread_arg_t* p_arg = (pthread_arg_t*) arg;
	char* compressed;
	size_t compressed_size;
	call_service(p_arg->filename, p_arg->file_id, compressed, &compressed_size);
	char* fname = p_arg->filename;
	FILE* fs = fopen(strcat(fname, ".zip"), "w");
	fwrite(compressed, compressed_size, 1, fs);
	fclose(fs);

	return fs;
}


int main(int argc, char** argv) {
	size_t seg_num = atol(argv[1]);
	long seg_size = atol(argv[2]);
	shm_ipc_init(seg_num, seg_size);
	for (int i = 3; i < argc; ++i) {
		pthread_t thing;
		pthread_arg_t* arg;
		arg->filename = argv[i];
		arg->file_id = i - 1; // file id starts from 2
		pthread_create(&thing, NULL, sync_call, arg);
	}

	return 0;
}