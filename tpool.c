#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include "tpool.h"

//Local Function Prototypes
static int enqueue_task(int job);
static int dequeue_task();
static void *tpool_remove_task();

//Thread Pool Object
tpool_t thrpool;

//Queue Size Variables
int QUEUE_MAX;


void print_queue(){
	
	for(int index = 0; index < QUEUE_MAX; index++){
		printf(" %d ", thrpool.job_queue[index]);
	}
	printf("\n");
}


static int enqueue_task(int job){
	
	thrpool.job_queue[thrpool.queue_head] = job;
	thrpool.queue_head = (thrpool.queue_head + 1) % QUEUE_MAX;
	return 0;
}


static int dequeue_task(){
	
	int temp_job = thrpool.job_queue[thrpool.queue_tail];
	thrpool.job_queue[thrpool.queue_tail] = 0;
	thrpool.queue_tail = (thrpool.queue_tail + 1) % QUEUE_MAX;
	return temp_job;
}


int tpool_init(void (*process_task) (int)){
	
	//Setup Process Task Function
	thrpool.profunction = process_task;
	
	//Queue Size Constants
	int const NUMBER_OF_WORKERS = sysconf(_SC_NPROCESSORS_ONLN) -1;
	QUEUE_MAX = NUMBER_OF_WORKERS * TASKS_PER_THREAD;
	
	//Mutex and Semaphore Initialization
	thrpool.queue_free_sem = QUEUE_MAX;
	pthread_mutex_init(&thrpool.queue_free_mtx, NULL);
	pthread_cond_init(&thrpool.queue_free_cond, NULL);
	
	thrpool.queue_avail_sem = 0;
	pthread_mutex_init(&thrpool.queue_avail_mtx, NULL);
	pthread_cond_init(&thrpool.queue_avail_cond, NULL);
	
	//Queue Entry Point Initialization
	thrpool.queue_head = 0;
	
	//Create Job Queue
	if((thrpool.job_queue = malloc(sizeof(int) * QUEUE_MAX)) == NULL){
		perror("Could not create queue to hold jobs\n");
		return -1;
	}
	
	//Create Worker Threads
	pthread_t worker_ids[NUMBER_OF_WORKERS];
	
	for(int index = 0; index < NUMBER_OF_WORKERS; index++){
		if(pthread_create(&worker_ids[index], NULL, tpool_remove_task, NULL) == -1){
			perror("Error Creating Worker Thread\n");
			return -1;
		}
	}
	return 0;
}


int tpool_add_task(int newtask){
	
	//Wait For Open Slot in Queue
	pthread_mutex_lock(&thrpool.queue_free_mtx);
	while (thrpool.queue_free_sem == 0){
		pthread_cond_wait(&thrpool.queue_free_cond, &thrpool.queue_free_mtx);
	}
	thrpool.queue_free_sem--;
	pthread_mutex_unlock(&thrpool.queue_free_mtx);

	//Acquire Lock on Queue to Add Job
	pthread_mutex_lock(&thrpool.queue_op_mtx); 		//Lock Queue
	
	//Add Job to the Task Queue
	enqueue_task(newtask);
						
	pthread_mutex_unlock(&thrpool.queue_op_mtx); 	//Release Queue

	//Update Added Job to Size and Free Lock
	pthread_mutex_lock(&thrpool.queue_avail_mtx);
	thrpool.queue_avail_sem++;
	pthread_mutex_unlock(&thrpool.queue_avail_mtx);
	pthread_cond_signal(&thrpool.queue_avail_cond);
	
	return 0;
}

static void *tpool_remove_task(){
	while(1){
		int job;  //Holds Task to Process

		//Wait For Nonempty Queue
		pthread_mutex_lock(&thrpool.queue_avail_mtx);
		while (thrpool.queue_avail_sem == 0){
			pthread_cond_wait(&thrpool.queue_avail_cond, &thrpool.queue_avail_mtx);
		}
		thrpool.queue_avail_sem--;
		pthread_mutex_unlock(&thrpool.queue_avail_mtx);

		//Acquire Lock on Queue to Remove Job
		pthread_mutex_lock(&thrpool.queue_op_mtx); 				//Lock Queue
		if((job = dequeue_task()) < 0){
			perror("Could not process job\n");
		}
		pthread_mutex_unlock(&thrpool.queue_op_mtx); 			//Release Queue

		//Increment free count and signal:
		pthread_mutex_lock(&thrpool.queue_free_mtx);
		thrpool.queue_free_sem++;
		pthread_mutex_unlock(&thrpool.queue_free_mtx);
		pthread_cond_signal(&thrpool.queue_free_cond);
		
		//Process Task With Given Function
		thrpool.profunction(job);
	}
	pthread_exit(NULL);
}