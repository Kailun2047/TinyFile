#include <sys/types.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shm_ipc.h"
#include "snappy.h"

extern void compress(char* input, size_t input_length, char* compressed, size_t* compressed_length) {
	printf("in compresssion\n");
	struct snappy_env env;
	struct snappy_env* env_ptr = &env;
	if (snappy_init_env(env_ptr) < 0) printf("snappy env init failed\n");
	if (snappy_compress(env_ptr, input, input_length, compressed, compressed_length) < 0) printf("compression failed\n");
	//snappy_free_env(env_ptr);
	printf("compression done!\n");
}

static int find_file_id(key_t* keys, int file_num, long file_key) {
	int file_id = file_num + 1;
	for (int i = 0; i < file_num; ++i) {
		if ((long) keys[i] == file_key) {
			file_id = i;
			break;
		}
	}
	return file_id;
}

int main(int argc, char** argv) {
	size_t seg_tot = atol(argv[1]);
	long seg_size = atol(argv[2]);
	ipc_shared_info ipc_shared;
	ipc_shared_info* shared_info = &ipc_shared;
	shm_ipc_init(seg_tot, seg_size, shared_info);

	int file_num = argc - 3; // the fir 3 args are executable name, seg_num and seg_size 
	printf("file_num: %d\n", file_num);
	size_t cursor_pos[file_num]; // number of bytes read for each original file
	//memset(cursor_pos, 0, file_num);
	char* orig_buffers[file_num]; // buffer to hold the original data of files for later compression
	char*** shmaddrs = (char***) malloc(file_num*sizeof(char**)); // shm addr of each shm segment for each file
	key_t keys[file_num];
	char* filenames[file_num];
	size_t seg_nums[file_num];
	for (int i = 0; i < file_num; ++i) {
		filenames[i] = argv[i + 3];
		keys[i] = ftok(filenames[i], 65);
	}

	int completed_files = 0;
	while (completed_files < file_num) {
		/* get and handle segment requests from request msgq */
		mymsg request;
		mymsg* request_msg = &request;
		while (shared_info->remaining > 0 && msgrcv(shared_info->msgq_ids[0], request_msg, SIZE_T_BYTES*4, 0, IPC_NOWAIT) != -1) {
			long type = request_msg->type;
			printf("Received request in request queue of type %ld\n", type);
			size_t seg_needed = 0;
			memcpy(&seg_needed, request_msg->data, SIZE_T_BYTES);
			printf("Number of segments requested: %ld\n", seg_needed);
			size_t seg_num = seg_needed > shared_info->remaining? shared_info->remaining: seg_needed;
			int file_id = find_file_id(keys, file_num, type - 1);
			printf("file_id is: %d\n", file_id);
			seg_nums[file_id] = seg_num;
			shmaddrs[file_id] = (char**) malloc(seg_num*sizeof(char*));
			shared_info->remaining -= seg_num;
			mymsg response;
			mymsg* response_msg = &response;
			response_msg->type = type;
			memcpy(response_msg->data, &seg_num, SIZE_T_BYTES);
			msgsnd(shared_info->msgq_ids[1], response_msg, SIZE_T_BYTES*4, 0);
			printf("\n");
		}
		/* get and handle original file data */
		mymsg orig;
		mymsg* orig_msg = &orig;
		while (msgrcv(shared_info->msgq_ids[2], orig_msg, SIZE_T_BYTES*4, 0, IPC_NOWAIT) != -1) {
			printf("Received orig message from orig queue\n");
			/* read orig message */
			long type = orig_msg->type;
			printf("orig message type: %ld\n", type);
			size_t segs, seg_needed, bytes_to_read;
			memcpy(&segs, orig_msg->data, SIZE_T_BYTES);
			memcpy(&seg_needed, orig_msg->data + SIZE_T_BYTES, SIZE_T_BYTES);
			memcpy(&bytes_to_read, orig_msg->data + SIZE_T_BYTES*2, SIZE_T_BYTES);
			printf("segs: %ld, seg_needed: %ld, bytes_to_read: %ld\n", segs, seg_needed, bytes_to_read);
			/* write data into corresponding buffer */
			int file_id = find_file_id(keys, file_num, type - 1);
			printf("file_id: %d, filename: %s\n", file_id, filenames[file_id]);
			if (segs == 0) {
				cursor_pos[file_id] = 0;
				orig_buffers[file_id] = (char*) malloc(MAX_FILE_SIZE);
			}

			if (segs < seg_nums[file_id]) {
				key_t shm_key = ftok(filenames[file_id], 65 + segs);
				int shmid = shmget(shm_key, 0, 0666);
				printf("shm_key: %d, shmid: %d, cursor_pos: %ld\n", shm_key, shmid, cursor_pos[file_id]);
				shmaddrs[file_id][segs] = shmat(shmid, NULL, 0);
				printf("shm segment %ld attached to server\n", segs);
			}
			memcpy(orig_buffers[file_id] + cursor_pos[file_id], shmaddrs[file_id][segs%seg_nums[file_id]], bytes_to_read);
			cursor_pos[file_id] += bytes_to_read;
			
			/* send server acknowledgement */
			mymsg server_ack;
			mymsg* server_ack_msg = &server_ack;
			server_ack_msg->type = type;
			memcpy(server_ack_msg->data, &segs, SIZE_T_BYTES);
			msgsnd(shared_info->msgq_ids[3], server_ack_msg, SIZE_T_BYTES*4, 0);
			printf("server ack message of type %ld sent (server_ackq_id: %d)\n", type, shared_info->msgq_ids[3]);

			/* if all original file data is collected, compress the buffer */
			if (segs == seg_needed - 1) {
				char compressed[MAX_FILE_SIZE];
				size_t compressed_length, cur_bytes = 0;
				compress(orig_buffers[file_id], cursor_pos[file_id], compressed, &compressed_length);
				printf("file size afer compression: %ld\n", compressed_length);
				size_t seg_sent = 0;
				while (seg_sent < seg_needed) {
					size_t bytes_written = compressed_length - cur_bytes > shared_info->seg_size? shared_info->seg_size: compressed_length - cur_bytes;
					/* write compressed data into shared memory */
					memcpy(shmaddrs[file_id][seg_sent%seg_nums[file_id]], compressed + cur_bytes, bytes_written);
					printf("copied %ld bytes of compressed data into shm segment %ld\n", bytes_written, seg_sent%seg_nums[file_id]);
					cur_bytes += bytes_written;
					seg_sent++;
					/* send result message */
					mymsg result;
					mymsg* result_msg = &result;
					result_msg->type = type;
					memcpy(result_msg->data, &bytes_written, SIZE_T_BYTES);
					msgsnd(shared_info->msgq_ids[4], result_msg, SIZE_T_BYTES*4, 0);
					/* wait for acknowledgement fromm client */
					mymsg client_ack;
					mymsg* client_ack_msg = &client_ack;
					printf("waiting for client ack message\n\n");
					while(msgrcv(shared_info->msgq_ids[5], client_ack_msg, SIZE_T_BYTES*4, type, IPC_NOWAIT) == -1);
				}

				/* detach and release shm sgements */
				for (int i = 0; i < seg_nums[file_id]; ++i) {
					shmdt(shmaddrs[file_id][i]);
					shmctl(shmget(ftok(filenames[file_id], 65 + i), 0, 0666), IPC_RMID, NULL);
					//free(shmaddrs[file_id][i]);
				}
				shared_info->remaining += seg_nums[file_id];
				seg_nums[file_id] = 0;
				cursor_pos[file_id] = 0;

				completed_files++;
			}
			printf("\n");
		}
	}

	return 0;
}