#ifndef TPOOL_H
#define TPOOL_H

#define TASKS_PER_THREAD  5
//Function Pointer Task
typedef void (*Task)(int job);

//Thread Pool Struct Declaration
typedef struct tpool {
	int queue_head;
	int queue_tail;
	int *job_queue;
	Task profunction;
	
	pthread_mutex_t queue_op_mtx;

	int queue_free_sem;
	pthread_mutex_t queue_free_mtx;
	pthread_cond_t queue_free_cond;
	
	int queue_avail_sem;
	pthread_mutex_t queue_avail_mtx;
	pthread_cond_t queue_avail_cond;
	
}tpool_t;

//Function Prototypes
int tpool_init(void (*process_task) (int));
int tpool_add_task(int newtask);

//Test Function Prototypes
void print_queue();

#endif