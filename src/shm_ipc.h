#pragma once

typedef struct mymsg_t {
	long mtype;
	int mcont; // name of file to be processed by service]
} mymsg;

typedef struct ipc_config_t {
	int seg_num; // the upper bound of the available shared memory segments
	long seg_size; // the size of an indivifual shared memory segment in size_t
	int remaining_num; // the segments that have already been allocated
	int req_msgq_id; // message queue for the requests
	int res_msgq_id; // message queue for the results
	int completed_shmid; // shared memory id for the completed mask
} ipc_config;

extern ipc_config* ipc_conf;