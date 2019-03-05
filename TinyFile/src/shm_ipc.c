#include <sys/types.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h> // strcat()
#include <stdlib.h> // stoi()
#include "shm_ipc.h"

ipc_shared_info ipc_shared;

/***********************************************/
/** service process creation and initialzation **/
/***********************************************/
extern void shm_ipc_init(size_t num, long size, ipc_shared_info* shared_info) {
	// create a message queues to store the request for the compression service
	key_t msgq_key = ftok("msgq", 1);
	shared_info->msgq_id = msgget(msgq_key, IPC_CREAT);
	if (shared_info->msgq_id == -1) perror("At msgget() for requests in shm_ipc_init()");

	// configure the shared memory parameters
	shared_info->seg_num = num;
	shared_info->seg_size = size;
	shared_info->remaining_num = num;
	pthread_mutex_init(&(shared_info->mutex), NULL);
}


/****************************************/
/** API for client to call the service **/
/****************************************/
extern void call_service(char* filename, size_t file_id, char* result, size_t* compressed_size) {
	// allocate the shared memory segments for the input file, and write file data to them
	// if the file is larger than one shared memory segment, separate it into multiple segments
	ipc_shared_info* shared_info = &ipc_shared;
	printf("In call_service(), trying to open %s\n", filename);
	FILE* fs = fopen(filename, "r");
	if (!fs) printf("cannot open file\n");
	fseek(fs, 0L, SEEK_END);
	long int file_end = ftell(fs);
	fseek(fs, 0L, SEEK_SET);
	long int file_cur = ftell(fs);
	printf("file_end: %ld, file_cur: %ld, shared_info->seg_size: %ld\n", file_end, file_cur, shared_info->seg_size);
	size_t chunk_cnt = 0, chunk_tot = (file_end - file_cur)/shared_info->seg_size + ((file_end - file_cur)%shared_info->seg_size == 0? 0: 1);
	printf("chunk_tot: %ld\n", chunk_tot);
	char appendix[10];
	char** buffers = (char**) malloc(chunk_tot*sizeof(char*)); // [cur_chunk][bytes], store the attached addresses of allocated shared memory segments
	int* shmids = (int*) malloc(chunk_tot*(sizeof(int))); // store the shared memory ids so that we can destroy them later
	
	while(file_cur < file_end) {
		pthread_mutex_lock(&(shared_info->mutex));
		if (shared_info->remaining_num > 0) {
			printf("segments remaining: %ld\n", shared_info->remaining_num);
			key_t file_key = ftok(filename, chunk_cnt + 1); // proj_id must be non-zero
			printf("file_key: %d\n", file_key);
			shmids[chunk_cnt] = shmget(file_key, shared_info->seg_size, IPC_CREAT);
			if (shmids[chunk_cnt] == -1) perror("At shmget() in call_service()");
			buffers[chunk_cnt] = shmat(shmids[chunk_cnt], NULL, 0);
			size_t bytes_read = (file_end - file_cur) < shared_info->seg_size? (file_end - file_cur): shared_info->seg_size;
			printf("bytes read: %ld\n", bytes_read);
			buffers[chunk_cnt] = (char*) malloc(shared_info->seg_size*sizeof(char));
			fread(buffers[chunk_cnt], bytes_read, 1, fs);
			file_cur += bytes_read;

			// send the request record via the request message queue
			mymsg msg;
			mymsg* req_msg = &msg;
			req_msg->type = REQUEST_TYPE;
			memcpy(req_msg->data, &file_id, sizeof(size_t)/sizeof(char)); // file id (later will be used by service as message type)
			memcpy(req_msg->data + SIZE_T_BYTES, &chunk_cnt, SIZE_T_BYTES); // current chunk
			memcpy(req_msg->data + SIZE_T_BYTES*2, &chunk_tot, SIZE_T_BYTES); // total chunks
			memcpy(req_msg->data + SIZE_T_BYTES*3, &bytes_read, SIZE_T_BYTES); // bytes to read in this segment
			msgsnd(shared_info->msgq_id, req_msg, sizeof(mymsg), 0);
			printf("message transmission for current chunk done\n");

			shared_info->remaining_num--;
			chunk_cnt++;
		}
		pthread_mutex_unlock(&(shared_info->mutex));
	}
	fclose(fs);

	// check if the file have been compressed and passed back via shared memory
	size_t received_cnt = 0;
	compressed_size = 0;
	while (received_cnt < chunk_tot) {
		mymsg* msgp;
		// the msgtyp 0 is used for any type, and msgtyp i is used for request type, so the msgtyp here should be file_id + 2
		if (msgrcv(shared_info->msgq_id, msgp, sizeof(char)*SIZE_T_BYTES*4, file_id + 2, MSG_NOERROR | IPC_NOWAIT) != -1) {
			received_cnt++;
			// parse the message data
			size_t cur_chunk, tot_chunk, read_bytes;
			memcpy(&cur_chunk, msgp->data, SIZE_T_BYTES);
			memcpy(&read_bytes, msgp->data + SIZE_T_BYTES*2, SIZE_T_BYTES);
			// copy the compressed data into the result buffer
			memcpy(result + *compressed_size, buffers[cur_chunk], read_bytes);
			shmdt(buffers[cur_chunk]);
			shmctl(shmids[cur_chunk], IPC_RMID, NULL);

			*compressed_size += read_bytes;
		}
	}
}