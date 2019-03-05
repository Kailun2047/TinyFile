#include <sys/types.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shm_ipc.h"
#include "snappy.h"

ipc_shared_info ipc_shared;

extern void compress(char* input, size_t input_length, char* compressed, size_t* compressed_length) {
	struct snappy_env* env;
	if (snappy_init_env(env) < 0) perror("At snappy_init_env() in compress()");
	if (snappy_compress(env, input, input_length, compressed, compressed_length) < 0) perror("At snappy_compress() in compress()");
	snappy_free_env(env);
}

int main(int argc, char** argv) {
	size_t seg_num = atol(argv[1]);
	long seg_size = atol(argv[2]);
	int file_num = argc - 3; // the fir 3 args are executable name, seg_num and seg_size 
	size_t* cursor_pos = (size_t*) malloc(file_num*sizeof(size_t)); // number of bytes read for each original file
	char*** compressed_buffers = (char***) malloc(file_num*sizeof(char**)); // [file_id][bytes], buffer[file_id] holds the original data of the whole file
	char** buffer = (char**) malloc(file_num*sizeof(char*)); // [file_id][cur_chunk][bytes], compressed_buffer[file_id][cur_chunk] points to the shared memory of a file chunk

	key_t msgq_key = ftok("msgq", 1);
	int msgq_id = msgget(msgq_key, 0);
	while (1) {
	// read a request record from the request message queue
	mymsg* msgp;
	if (msgrcv(msgq_id, msgp, sizeof(char)*SIZE_T_BYTES*4, REQUEST_TYPE, MSG_NOERROR | IPC_NOWAIT) != -1) {
		printf("recieving request message from msg queue\n");
		// parse message data
		size_t file_id, cur_chunk, tot_chunks, bytes_read;
		memcpy(&file_id, msgp->data, SIZE_T_BYTES);
		memcpy(&cur_chunk, msgp->data + SIZE_T_BYTES, SIZE_T_BYTES);
		memcpy(&tot_chunks, msgp->data + SIZE_T_BYTES*2, SIZE_T_BYTES);
		memcpy(&bytes_read, msgp->data + SIZE_T_BYTES*3, SIZE_T_BYTES);
		printf("file_id: %ld, cur_chunk: %ld, tot_chunks: %ld, bytes_read: %ld\n", file_id, cur_chunk, tot_chunks, bytes_read);

		if (cur_chunk == 0) {
			cursor_pos[file_id] = 0;
			compressed_buffers[file_id] = (char**) malloc(tot_chunks*sizeof(char*));
		}

		// read original data from shared memory segments to buffer
		char *filename = argv[file_id + 3];
		key_t shm_key = ftok(filename, cur_chunk + 1); // proj_id must be non-zero
		int shmid = shmget(shm_key, 0, 0);
		// now the compressed_buffers should hold the uncompressed data
		compressed_buffers[file_id][cur_chunk] = shmat(shmid, NULL, 0);
		memcpy(buffer[file_id] + cursor_pos[file_id], compressed_buffers[file_id][cur_chunk], bytes_read);
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
					bytes_copy = (bytes_compressed - cur_bytes) < seg_size? (bytes_compressed - cur_bytes): seg_size;
				}
				memcpy(compressed + cur_bytes, compressed_buffers[file_id][i], bytes_copy);
				cur_bytes += bytes_copy;
			}
		}
	}
	}

	return 0;
}