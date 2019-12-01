#ifndef __SHARE_DATA__
#define __SHARE_DATA__
#ifdef __cplusplus
extern "C"  {
#endif



#define MAX_SHARE_DATA_NUMBER   512
#define SHARE_DATA_DIR "/tmp/sharedata"

typedef int (*SD_INIT_FUNC_T) (void * addr, long size);
typedef int (*SD_WRITE_FUNC_T) (void * addr, long size);
typedef int (*SD_READ_FUNC_T) (void * addr, long size);


typedef struct share_data_mgr_entry {
	pthread_mutex_t lock;
} share_data_mgr_entry_t;
typedef struct share_data_entry{
	unsigned int index;
	char path[256];
	char init_lock_path[256];
	unsigned long size;	
	void * addr;
	int shm_fd;
	SD_INIT_FUNC_T init;
	SD_WRITE_FUNC_T write;
	SD_READ_FUNC_T  read;
} share_data_entry_t;


int share_data_register(unsigned int index, unsigned long size, SD_INIT_FUNC_T init, SD_WRITE_FUNC_T write, SD_READ_FUNC_T  read);
int share_data_unregister(unsigned int index);

int share_data_set(unsigned int index , void * data);
int share_data_get(unsigned int index, void **pdata );

void * share_data_get_addr_lock(unsigned int index );
int share_data_free_addr_unlock(unsigned int index);

int share_data_set_rt(unsigned int index , void * buf, long size);
int share_data_get_rt(unsigned int index, void **pdata );


int share_data_load(unsigned int index);
int _share_data_save(unsigned int index); /*no lock*/
int share_data_save(unsigned int index);









#ifdef __cplusplus
}
#endif
#endif
