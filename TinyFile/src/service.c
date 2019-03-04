#include <sys/types.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <stdio.h>
#include <string.h>
#include "shm_ipc.h"
#include "snappy.h"

ipc_shared_info* ipc_shared;

extern void compress(char* input, size_t input_length, char* compressed, size_t* compressed_length) {
	struct snappy_env* env;
	if (snappy_init_env(env) < 0) perror("At snappy_init_env() in compress()");
	if (snappy_compress(env, input, input_length, compressed, compressed_length) < 0) perror("At snappy_compress() in compress()");
	snappy_free_env(env);
}

int main(int argc, char** argv) {
	while (1) {
	size_t cursor_pos[argc - 1]; // bytes written in the buffer for each orginal file
	char* buffer[argc - 1]; // original file data
	char** data_buffers[argc - 1]; // compressed file data
	// read a request record from the request message queue
	mymsg* msgp;
	if (msgrcv(ipc_shared->msgq_id, msgp, sizeof(char)*SIZE_T_BYTES*4, REQUEST_TYPE, MSG_NOERROR | IPC_NOWAIT) != -1) {
		// parse message data
		size_t file_id, cur_chunk, tot_chunks, bytes_read;
		memcpy(&file_id, msgp->data, SIZE_T_BYTES);
		memcpy(&cur_chunk, msgp->data + SIZE_T_BYTES, SIZE_T_BYTES);
		memcpy(&tot_chunks, msgp->data + SIZE_T_BYTES*2, SIZE_T_BYTES);
		memcpy(&bytes_read, msgp->data + SIZE_T_BYTES*3, SIZE_T_BYTES);

		// read original data from shared memory segments to buffer
		if (cur_chunk == 0) cursor_pos[file_id] = 0;
		char *filename = argv[file_id + 1], *filename_copy = filename; // argv[0] is the executable name
		char* appendix = "";
		sprintf(appendix, "%ld", cur_chunk);
		key_t shm_key = ftok(strcat(filename_copy, appendix), 1);
		int shmid = shmget(shm_key, 0, 0);
		data_buffers[file_id][cur_chunk] = shmat(shmid, NULL, 0);
		memcpy(buffer[file_id] + cursor_pos[file_id], data_buffers[file_id][cur_chunk], bytes_read);
		cursor_pos[file_id] += bytes_read;

		// if all the original data is read, compress the buffer
		if (cur_chunk == tot_chunks) {
			char* compressed;
			size_t bytes_compressed = 0, cur_bytes = 0;
			compress(buffer[file_id], cursor_pos[file_id], compressed, &bytes_compressed);
			char* app = "";
			for (int i = 0; i < tot_chunks; ++i) {
				char* fname = filename;
				sprintf(app, "%d", i);
				int bytes_copy = 0;
				if (cur_bytes < bytes_compressed) {
					bytes_copy = (bytes_compressed - cur_bytes) < ipc_shared->seg_size? (bytes_compressed - cur_bytes): ipc_shared->seg_size;
				}
				memcpy(compressed + cur_bytes, data_buffers[file_id][i], bytes_copy);
				cur_bytes += bytes_copy;
			}
		}
	}
	}

	return 0;
}