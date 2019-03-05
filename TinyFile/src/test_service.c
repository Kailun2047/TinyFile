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
	printf("file to be compressed: %s\n", p_arg->filename);
	char compressed[MAX_FILE_SIZE];
	size_t compressed_size = 0;
	printf("p_arg->filename: %s, p_arg->file_id: %ld\n", p_arg->filename, p_arg->file_id);
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
	printf("seg_num: %ld, seg_size: %ld\n", seg_num, seg_size);
	shm_ipc_init(seg_num, seg_size, &ipc_shared);

	int file_num = argc - 1;
	pthread_t* pthreads = (pthread_t*) malloc(file_num*sizeof(pthread_t));
	for (int i = 0; i < file_num; ++i) {
		pthread_arg_t arg;
		pthread_arg_t* arg_p = &arg;
		arg_p->filename = argv[i + 3];
		arg_p->file_id = i;
		pthreads[i] = 0;
		pthread_create(&pthreads[i], NULL, sync_call, arg_p);
		pthread_join(pthreads[i], NULL);
	}

	return 0;
}