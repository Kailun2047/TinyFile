#include <sys/types.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <stdio.h>
#include <string.h> // strcat
#include "shm_ipc.h"

#define SIZE_T_BYTES sizeof(size_t)/sizeof(char)

ipc_config* ipc_conf;

/***********************************************/
/** service process creation and initialzation **/
/***********************************************/
extern void shm_ipc_init(int num, long size, int file_cnt) {
	// create two message queues to store the request for the service and the compression result respectively
	key_t req_msgq_key = ftok("request_msg", 1);
	ipc_conf->req_msgq_id = msgget(req_msgq_key, IPC_CREAT);
	if (ipc_conf->req_msgq_id == -1) perror("At msgget() for requests in shm_ipc_init()");
	key_t res_msgq_key = ftok("result_msg", 1);
	ipc_conf->res_msgq_id = msgget(res_msgq_key, IPC_CREAT);
	if (ipc_conf->res_msgq_id == -1) perror("At msgget() for results in shm_ipc_init()");
	// configure the shared memory parameters
	ipc_conf->seg_num = num;
	ipc_conf->seg_size = size;
	ipc_conf->remaining_num = num;
	// allocate memory to store the completed flags of each file to be processed
	key_t completed_key = ftok("completed_mask", sizeof(char)*file_cnt/SIZE_T_BYTES + 1, 1);
	ipc_conf->completed_shmid = shmget(completed_key, IPC_CREAT);
	if (ipc_conf->completed_shmid == -1) perror("At shmget() for completed mask in shm_ipc_init()");
}


/****************************************/
/** API for client to call the service **/
/****************************************/
extern void call_service(char* filename, int file_num, char* result) {
	// allocate the shared memory segments for the input file, and write file data to them
	int seg_size_T_bytes = SIZE_T_BYTES*ipc_conf->seg_size;
	FILE* fs = fopen(filename, "r");
	fseek(fs, 0L, SEEK_END);
	long int file_end = ftell(fs);
	fseek(fs, 0L, SEEK_SET);
	long int file_cur = ftell(fs);
	char* appendix = "";
	// if the file is larger than one shared memory segment, separate it into multiple segments
	while(ipc_conf->remaining_num > 0 && file_cur < file_end) {
		key_t file_key = ftok(strcat(filename, appendix), 1);
		int shmid = shmget(file_key, ipc_conf->seg_size, 0);
		if (shmid == -1) perror("At shmget() in call_service()");
		void* buffer = shmat(shmid, NULL, 0);
		int size_read = (file_end - file_cur) < seg_size_T_bytes? (file_end - file_cur): seg_size_T_bytes;
		fread(buffer, size_read, 1, fs);
		file_cur = ftell(fs);

		// send the request record via the request message queue
		mymsg* req;
		req->mcont = shmid;
		req->mtype = 1;
		msgsnd(ipc_conf->req_msgq_id, req, sizeof(mymsg), 0);

		strcat(appendix, "#");
		ipc_conf->remaining_num--;
	}

	// check if all the files have been compressed and passed back via shared memory

}