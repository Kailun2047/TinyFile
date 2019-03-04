#include <sys/types.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <stdio.h>
#include <string.h> // strcat()
#include <stdlib.h> // stoi()
#include "shm_ipc.h"

ipc_shared_info* ipc_shared;

/***********************************************/
/** service process creation and initialzation **/
/***********************************************/
extern void shm_ipc_init(size_t num, long size) {
	// create a message queues to store the request for the compression service
	key_t msgq_key = ftok("request_msg", 1);
	ipc_shared->msgq_id = msgget(msgq_key, IPC_CREAT);
	if (ipc_shared->msgq_id == -1) perror("At msgget() for requests in shm_ipc_init()");

	// configure the shared memory parameters
	ipc_shared->seg_num = num;
	ipc_shared->seg_size = size;
	ipc_shared->remaining_num = num;
}


/****************************************/
/** API for client to call the service **/
/****************************************/
extern void call_service(char* filename, size_t file_id, char* result, size_t* compressed_size) {
	// allocate the shared memory segments for the input file, and write file data to them
	// if the file is larger than one shared memory segment, separate it into multiple segments
	FILE* fs = fopen(filename, "r");
	fseek(fs, 0L, SEEK_END);
	long int file_end = ftell(fs);
	fseek(fs, 0L, SEEK_SET);
	long int file_cur = ftell(fs);
	size_t chunk_cnt = 0, chunk_tot = (file_end - file_cur)/ipc_shared->seg_size + ((file_end - file_cur)%ipc_shared->seg_size == 0? 0: 1);
	char* appendix = "";
	char* buffers[chunk_tot]; // store the attached addresses of allocated shared memory segments
	int shmids[chunk_tot]; // also store the shared memory ids to destroy them later
	while(file_cur < file_end) {
		pthread_mutex_lock(ipc_shared->mutex);
		if (ipc_shared->remaining_num > 0) {
			sprintf(appendix, "%ld", chunk_cnt);
			char* filename_cpy = filename;
			key_t file_key = ftok(strcat(filename_cpy, appendix), 1);
			shmids[chunk_cnt] = shmget(file_key, ipc_shared->seg_size, 0);
			if (shmids[chunk_cnt] == -1) perror("At shmget() in call_service()");
			buffers[chunk_cnt] = shmat(shmids[chunk_cnt], NULL, 0);
			size_t bytes_read = (file_end - file_cur) < ipc_shared->seg_size? (file_end - file_cur): ipc_shared->seg_size;
			fread(buffers[chunk_cnt], bytes_read, 1, fs);
			file_cur += SIZE_T_BYTES;

			// send the request record via the request message queue
			mymsg* req_msg;
			req_msg->type = REQUEST_TYPE;
			memcpy(req_msg->data, &file_id, sizeof(size_t)/sizeof(char)); // file id (later will be used by service as message type)
			memcpy(req_msg->data + SIZE_T_BYTES, &chunk_cnt, SIZE_T_BYTES); // current chunk
			memcpy(req_msg->data + SIZE_T_BYTES*2, &chunk_tot, SIZE_T_BYTES); // total chunks
			memcpy(req_msg->data + SIZE_T_BYTES*3, &bytes_read, SIZE_T_BYTES); // bytes to read in this segment
			msgsnd(ipc_shared->msgq_id, req_msg, sizeof(mymsg), 0);

			ipc_shared->remaining_num--;
			chunk_cnt++;
		}
		pthread_mutex_unlock(ipc_shared->mutex);
	}
	fclose(fs);

	// check if the file have been compressed and passed back via shared memory
	size_t received_cnt = 0;
	compressed_size = 0;
	while (received_cnt < chunk_tot) {
		mymsg* msgp;
		// the msgtyp 0 is used for any type, and msgtyp i is used for request type, so the msgtyp here should be file_id + 2
		if (msgrcv(ipc_shared->msgq_id, msgp, sizeof(char)*SIZE_T_BYTES*4, file_id + 2, MSG_NOERROR | IPC_NOWAIT) != -1) {
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