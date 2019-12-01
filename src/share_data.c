
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include<pthread.h>
#include <errno.h>
#include <unistd.h>
#include <sys/file.h>

#include "share_data.h"


static share_data_mgr_entry_t * g_sd_mgr_tbl;
static share_data_entry_t  g_sd_tbl[MAX_SHARE_DATA_NUMBER];

static int shm_mgr_fd = -1;
static char shm_mgr_path[256];
#define SHM_MGR_NAME "share_data_mgr" 


static inline int safe_lock(pthread_mutex_t *lock)
{
	int r = pthread_mutex_lock(lock);
	if (r == EOWNERDEAD)
	  pthread_mutex_consistent(lock);
	else if (r != 0)
		return -1;
	return 0;
}

static inline int safe_unlock(pthread_mutex_t *lock)
{
	int r = pthread_mutex_unlock(lock);
	return r;
}

static int share_data_open_shm(char * name, unsigned long size, void **shm_base)
{
	int shm_fd;
	void *ptr;
	int ret ;

	/* create the shared memory segment */
	shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
	if (shm_fd < 0 ) {
		printf("shm open: %s failed\n",name);
		return -1;
	}
	/* configure the size of the shared memory segment */
	ret = ftruncate(shm_fd, size);
	if ( ret != 0)
		return -1;

	/* now map the shared memory segment in the address space of the process */
	ptr = mmap(0, size , PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if (ptr == MAP_FAILED) {
		printf("Map failed\n");
		close(shm_fd);
		return -1;
	}
	*shm_base = ptr;
	return shm_fd;

}


static int share_data_close_shm(int shm_fd, void * shm_base, unsigned long size)
{
	if (munmap(shm_base, size) == -1) {
	  printf("cons: Unmap failed: %s\n", strerror(errno));
	}
	
	/* close the shared memory segment as if it was a file */
	if (close(shm_fd) == -1) {
	  printf("cons: Close failed: %s\n", strerror(errno));
	}
	return 0;
}


static int share_data_init()
{
	mkdir(SHARE_DATA_DIR, 666);
	sprintf(shm_mgr_path, "%s/%s", SHARE_DATA_DIR, SHM_MGR_NAME);
	shm_mgr_fd = share_data_open_shm(shm_mgr_path, sizeof(share_data_mgr_entry_t) * MAX_SHARE_DATA_NUMBER, (void **)&g_sd_mgr_tbl);
	if (shm_mgr_fd < 0){
		printf("open shm mgr file :%s, fail ", shm_mgr_path);
		return -1;
	}
	return 0;
}

int share_data_register(unsigned int index, unsigned long size, SD_INIT_FUNC_T init, SD_WRITE_FUNC_T write, SD_READ_FUNC_T  read)
{
	int ret;
	if (index < 0 || index >= MAX_SHARE_DATA_NUMBER)
		return -1;
	if (!g_sd_mgr_tbl) {
		if (share_data_init() < 0)
			return -1;
	}
	share_data_entry_t * ptr = &g_sd_tbl[index];
	
	ptr->index = index;
	ptr->size = size;
	ptr->init = init;
	ptr->write = write;
	ptr->read= read;

	mkdir(SHARE_DATA_DIR, 666);

	sprintf(ptr->path, "%s/share_data.%u", SHARE_DATA_DIR, index );
	
	ptr->shm_fd = share_data_open_shm(ptr->path, size, &ptr->addr);
	if (ptr->shm_fd < 0)
		printf("open share data %s fail \n", ptr->path);

	
	sprintf(ptr->init_lock_path, "%s/share_data.%u.initlock", SHARE_DATA_DIR, index );


	if(access(ptr->init_lock_path, R_OK)==0){
		return 0;
	}

	int lockfd = open(ptr->init_lock_path, O_CREAT|O_RDWR, 666);
	if (lockfd< 0)
		return -1;
	ret = flock(lockfd, LOCK_EX|LOCK_NB);
	if ( ret >= 0)  {
		pthread_mutexattr_t mutexattr;
		pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED);
		pthread_mutexattr_setrobust(&mutexattr, PTHREAD_MUTEX_ROBUST);
		pthread_mutex_init(&g_sd_mgr_tbl[index].lock, &mutexattr);
		if (ptr->init)
			(*ptr->init)(ptr->addr, ptr->size);
	}
	flock (lockfd , LOCK_UN );
	close(lockfd);
	return ptr->shm_fd;
}

int share_data_unregister(unsigned int index)
{
	share_data_entry_t * ptr = &g_sd_tbl[index];
	share_data_close_shm(ptr->shm_fd, ptr->addr, ptr->size);
	return 0;
}


int __share_data_set(share_data_entry_t * ptr, void * data)
{
	memcpy(ptr->addr, data, ptr->size);
	return 0;
}

int share_data_set(unsigned int index , void * data)
{
	share_data_entry_t * ptr = &g_sd_tbl[index];
	if (safe_lock(&g_sd_mgr_tbl[index].lock) < 0)
		return -1;
	__share_data_set(ptr, data);
	safe_unlock(&g_sd_mgr_tbl[index].lock);
	return 0;
}


int __share_data_get(share_data_entry_t * ptr, void **pdata )
{
	memcpy(*pdata, ptr->addr, ptr->size);
	return 0;
}

int share_data_get(unsigned int index, void **pdata )
{
	share_data_entry_t * ptr = &g_sd_tbl[index];
	
	if (safe_lock(&g_sd_mgr_tbl[index].lock) < 0)
		return -1;
	__share_data_get(ptr, pdata);
	
	safe_unlock(&g_sd_mgr_tbl[index].lock);
	return 0;
}

void * share_data_get_addr_lock(unsigned int index )
{
	share_data_entry_t * ptr = &g_sd_tbl[index];
	
	if (safe_lock(&g_sd_mgr_tbl[index].lock) < 0)
		return NULL;
	return ptr->addr;
}
int share_data_free_addr_unlock(unsigned int index)
{
	safe_unlock(&g_sd_mgr_tbl[index].lock);
	return 0;
}

int __share_data_load(share_data_entry_t * ptr)
{

	int ret = 0 ;
	if(ptr->read)
		ret = (*ptr->read)(ptr->addr, ptr->size);
	return ret;
}

int share_data_load(unsigned int index)
{
	share_data_entry_t * ptr = &g_sd_tbl[index];

	int ret = 0 ;
	
	if (safe_lock(&g_sd_mgr_tbl[index].lock) < 0)
		return -1;
	ret = __share_data_load(ptr);
	
	safe_unlock(&g_sd_mgr_tbl[index].lock);
	
	return ret;
}

int __share_data_save(share_data_entry_t * ptr)
{
	int ret = 0;
	if (ptr->write)
		ret = (*ptr->write)(ptr->addr, ptr->size);
	return ret;


}

int _share_data_save(unsigned int index)
{
	share_data_entry_t * ptr = &g_sd_tbl[index];
	return __share_data_save( ptr);

}

int share_data_save(unsigned int index)
{
	share_data_entry_t * ptr = &g_sd_tbl[index];
	int ret = 0;
	if (ptr->write) {
		if (safe_lock(&g_sd_mgr_tbl[index].lock) < 0)
			return -1;
		ret = (*ptr->write)(ptr->addr, ptr->size);
		safe_unlock(&g_sd_mgr_tbl[index].lock);
	}
	return ret;


}


int share_data_set_rt(unsigned int index , void * data, long size)
{
	share_data_entry_t * ptr = &g_sd_tbl[index];
	int ret ;
	
	if (safe_lock(&g_sd_mgr_tbl[index].lock) < 0)
		return -1;
	__share_data_set(ptr, data);
	ret = __share_data_save(ptr);
	
	safe_unlock(&g_sd_mgr_tbl[index].lock);
	return ret;
}
int share_data_get_rt(unsigned int index, void **pdata )
{
	share_data_entry_t * ptr = &g_sd_tbl[index];
	int ret ;
	
	if (safe_lock(&g_sd_mgr_tbl[index].lock) < 0)
		return -1;
	ret = __share_data_load(ptr);
	__share_data_get(ptr, pdata);
	
	safe_unlock(&g_sd_mgr_tbl[index].lock);
	
	return ret;
}








