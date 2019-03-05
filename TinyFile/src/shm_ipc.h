#pragma once
#include <pthread.h>

#define REQUEST_TYPE 1
#define MAX_FILE_SIZE 1024*1024
#define SIZE_T_BYTES sizeof(size_t)/sizeof(char)

typedef struct mymsg_t {
	long type;
	char data[SIZE_T_BYTES*4]; // <file_id, current chunk, total chunks, bytes to read> for request; <current chunk, bytes to read> for result
} mymsg;

typedef struct ipc_shared_info_t {
	size_t seg_num; // the upper bound of the available shared memory segments
	long seg_size; // the size of an indivifual shared memory segment in size_t
	size_t remaining_num; // the segments that have already been allocated
	int msgq_id; // message queue for the requests
	pthread_mutex_t mutex; // mutex for shared info among clients (like the remaining available shm segments)
} ipc_shared_info;

extern ipc_shared_info ipc_shared;

extern void shm_ipc_init(size_t num, long size, ipc_shared_info* shared_info);
extern void call_service(char* filename, size_t file_id, char* result, size_t* compressed_size);