#include <stdio.h>
#include <pthread.h>
#include <stdlib.h> // atol()
#include <string.h> // strcat(), strstr(), strncpy()
#include <unistd.h> // fork()
#include "shm_ipc.h"

static void* sync_call(char* filename, size_t seg_size) {
	char compressed[MAX_FILE_SIZE];
	size_t compressed_size = 0;
	call_service(filename, seg_size, compressed, &compressed_size);
	char* fname = filename;

	/* produce the name of compressed file */
	char* pch = strstr(fname, ".");
	if (pch) {
		strncpy(pch, ".snp", 4);
	}
	else {
		strcat(fname, ".snp");
	}
	printf("name of compressed file: %s\n", fname);

	FILE* fs = fopen(fname, "w");
	fwrite(compressed, compressed_size, 1, fs);
	fclose(fs);

	return fs;
}

/* arguments: argv[1]->size of a shm segment, argv[2]->call type (sync or async), argv[3+]->filenames */
int main(int argc, char** argv) {
	size_t seg_size = atol(argv[1]);
	int call_type = atoi(argv[2]);
	int file_tot = argc - 3, mid_pos = 3 + file_tot/2;

	printf("call type: %d\n", call_type);
	/* synchronous call */
	if (call_type == 0) {
		pid_t pid = fork();
		if (pid == 0) {
			for (int i = 3; i < mid_pos; ++i) sync_call(argv[i], seg_size);
		}
		else if (pid > 0) {
			for (int i = mid_pos; i < argc; ++i) sync_call(argv[i], seg_size);
		}
		else printf("client process creation failed\n");
	}

	/* asynchonous call */
	else if (call_type == 1) {
			for (int i = 3; i < argc; ++i) {
				p_arg* handle = initiate_service(argv[i], seg_size);
				int tmp = 0;
				while (tmp < 10000) tmp++;
				while (!handle->has_result);
				/* produce the name of compressed file */
				char* fname = argv[i];
				char* pch = strstr(fname, ".");
				if (pch) {
					strncpy(pch, ".snp", 4);
				}
				else {
					strcat(fname, ".snp");
				}
				printf("name of compressed file: %s\n", fname);

				FILE* fs = fopen(fname, "w");
				fwrite(handle->result_buf, handle->compressed_size, 1, fs);
				fclose(fs);
			}
	}

	else printf("invalid call type\n");

	return 0;
}