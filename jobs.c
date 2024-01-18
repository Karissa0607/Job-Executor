#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "executor.h"

extern struct executor tassadar;


/**
 * Populate the job lists by parsing a file where each line has
 * the following structure:
 *
 * <id> <type> <num_resources> <resource_id_0> <resource_id_1> ...
 *
 * Each job is added to the queue that corresponds with its job type.
 */
void parse_jobs(char *file_name) {
    int id;
    struct job *cur_job;
    struct admission_queue *cur_queue;
    enum job_type jtype;
    int num_resources, i;
    FILE *f = fopen(file_name, "r");

    /* parse file */
    while (fscanf(f, "%d %d %d", &id, (int*) &jtype, (int*) &num_resources) == 3) {

        /* construct job */
        cur_job = malloc(sizeof(struct job));
        cur_job->id = id;
        cur_job->type = jtype;
        cur_job->num_resources = num_resources;
        cur_job->resources = malloc(num_resources * sizeof(int));

        int resource_id; 
				for(i = 0; i < num_resources; i++) {
				    fscanf(f, "%d ", &resource_id);
				    cur_job->resources[i] = resource_id;
				    tassadar.resource_utilization_check[resource_id]++;
				}
				
				assign_processor(cur_job);

        /* append new job to head of corresponding list */
        cur_queue = &tassadar.admission_queues[jtype];
        cur_job->next = cur_queue->pending_jobs;
        cur_queue->pending_jobs = cur_job;
        cur_queue->pending_admission++;
    }

    fclose(f);
}

/*
 * Magic algorithm to assign a processor to a job.
 */
void assign_processor(struct job* job) {
    int i, proc = job->resources[0];
    for(i = 1; i < job->num_resources; i++) {
        if(proc < job->resources[i]) {
            proc = job->resources[i];
        }
    }
    job->processor = proc % NUM_PROCESSORS;
}


void do_stuff(struct job *job) {
    /* Job prints its id, its type, and its assigned processor */
    printf("%d %d %d\n", job->id, job->type, job->processor);
}



/**
 * TODO: Fill in this function
 *
 * Do all of the work required to prepare the executor
 * before any jobs start coming
 * 
 */
void init_executor() {
	int i, j;
    	struct admission_queue *cur_queue;
    	struct processor_record *cur_processor_record;
	//Initializing each resource
    	for (i = 0; i < NUM_RESOURCES; i++) {
        	tassadar.resource_utilization_check[i] = 0; // this gets updated in parse_jobs
    	}
	//Initializing each admission queue
    	for (i = 0; i < NUM_QUEUES; i++) {
        	cur_queue = &tassadar.admission_queues[i];
		//list is empty so the head and tail are both 0
        	cur_queue->head = 0; //head stays at index 0. 
        	cur_queue->tail = 0; //tail index can go to a maximum of 9 as the queue length is 10. Append to list.
        	cur_queue->capacity = QUEUE_LENGTH;
        	cur_queue->num_admitted = 0;
        	cur_queue->pending_admission = 0;
		cur_queue->pending_jobs = NULL; //Nothing is in the pending_jobs list when initialized so it is NULL
		//Allocate memory for the admitted_jobs list using malloc
        	cur_queue->admitted_jobs = (struct job **) malloc(sizeof(struct job *)*QUEUE_LENGTH);
		if (cur_queue->admitted_jobs == NULL) { //Error checking for malloc
			exit(1); //Exit with status code 1 as there has been an error with malloc.
		}
		//Admitted_jobs is an array of job pointers
		for(j = 0; j <QUEUE_LENGTH; j++) {
			cur_queue->admitted_jobs[j] = NULL; //Initialize each job pointer to NULL for initialization
		}
    	}
	//Initializing each processor record
    	for (i = 0; i < NUM_PROCESSORS; i++) {
        	cur_processor_record = &tassadar.processor_records[i];
        	cur_processor_record->num_completed = 0;
		cur_processor_record->completed_jobs = NULL;
    	}
}


/**
 * TODO: Fill in this function
 *
 * Handles an admission queue passed in through the arg (see the executor.c file). 
 * Bring jobs into this admission queue as room becomes available in it. 
 * As new jobs are added to this admission queue (and are therefore ready to be taken
 * for execution), the corresponding execute thread must become aware of this.
 * 
 */
void *admit_jobs(void *arg) {
	struct admission_queue *q = arg;
	struct job *curr;
	struct job *old_head;
	int i;
	
	pthread_mutex_lock(&q->lock); //Lock the admission thread that belongs to the current admission_queue

	//The admission thread will keep running as long as its corresponding admission_queue has jobs that are pending admission
	while(q->pending_admission != 0) {
		pthread_mutex_unlock(&q->lock); //Unlock the admission_thread after while loop condition checking to allow for other threads to execute
    		pthread_mutex_lock(&q->lock); //Lock the admission_thread

		//When the number of jobs admitted is equal to the capacity of the admission queue (ie admission queue is full), it must wait
    		while (q->num_admitted == q->capacity) { // queue is full
        		pthread_cond_wait(&q->admission_cv, &q->lock); // wait for queue to free up so wait for a job to be executed
    		}
  	
		//Get the last pending job
		curr = q->pending_jobs;
		//Remove the last pending job
		q->pending_jobs = q->pending_jobs->next;
		q->pending_admission--;
		//Head is 0. Tail increases indexes. The first job is at the end of the list (at the tail index)
		//Current/last job admitted is at the head index so index 0 at the front of the admitted_jobs list. 
		//Shift all jobs accordingly.
		//Add pending job to the admitted_jobs array based on above logic.
		old_head = q->admitted_jobs[q->head];
		if (old_head == NULL){ // list is empty
			curr->next = curr; //refers to itself (makes list circular)
			q->admitted_jobs[q->head] = curr;
		}
		else{//List is not empty
			q->tail++;
			//Shifting jobs to their new indexes in the admitted_jobs array. 
			for(i = 0; i <= q->tail; i++) {
				if (i==0) {
					q->admitted_jobs[i] = curr;
				} else {
					q->admitted_jobs[i] = old_head;
					q->admitted_jobs[i-1]->next = q->admitted_jobs[i]; //Makes list circular
					old_head = old_head->next;
				}
			}
			q->admitted_jobs[q->tail]->next = q->admitted_jobs[q->head]; //Makes the new tail refer to the new head. (makes list circular)
		}
		q->num_admitted++; //One job has been admitted into this queue. 
		pthread_cond_signal(&q->execution_cv); //signal execution that there is a job admitted waiting to be executed
    		pthread_mutex_unlock(&q->lock); //Unlock admission thread to allow for other threads to continue
		pthread_mutex_lock(&q->lock); //Lock admission thread to prevent data race when checking the while loop condition.
	}
	pthread_mutex_unlock(&q->lock); //Unlock admission thread after while loop condition fails
    	return NULL;
}


/**
 * TODO: Fill in this function
 *
 * Moves jobs from a single admission queue of the executor. 
 * Jobs must acquire the required resource locks before being able to execute. 
 *
 * Note: You do not need to spawn any new threads in here to simulate the processors.
 * When a job acquires all its required resources, it will execute do_stuff.
 * When do_stuff is finished, the job is considered to have completed.
 *
 * Once a job has completed, the admission thread must be notified since room
 * just became available in the queue. Be careful to record the job's completion
 * on its assigned processor and keep track of resources utilized. 
 *
 * Note: No printf statements are allowed in your final jobs.c code, 
 * other than the one from do_stuff!
 */
void *execute_jobs(void *arg) {
	int i, j; //used in for loops
    	int processor_record_num; //used for processor_record
	struct job *completed_job; 
	struct admission_queue *q = arg;
	
	pthread_mutex_lock(&q->lock); //Lock execution thread that belongs to the current admission_queue
	while (q->pending_admission != 0 || q->num_admitted != 0) {
		pthread_mutex_unlock(&q->lock); //Unlock execution thread after while loop to allow for other threads to have a chance to execute
		pthread_mutex_lock(&q->lock); //Lock execution thread
		
		//When there are no jobs admitted into the queue, the execution thread must wait
    		while (q->num_admitted == 0) {
        		pthread_cond_wait(&q->execution_cv, &q->lock);
    		}

		//Locks and updates each resource that the job requires in increasing order in order to prevent lock order errors. 
    		for (j = 0; j < NUM_RESOURCES; j++) {
			for (i = 0; i < q->admitted_jobs[q->tail]->num_resources; i++) {
				if (q->admitted_jobs[q->tail]->resources[i] == j) {
					pthread_mutex_lock(&tassadar.resource_locks[j]);
					tassadar.resource_utilization_check[j]--;
				}
    			}
		}

    		do_stuff(q->admitted_jobs[q->tail]); //execute first job added which is at the tail index due to FIFO
		
		//Take the current job (q->admitted_jobs[q->tail]) off of the admission_jobs list as it has been completed.
		completed_job = q->admitted_jobs[q->tail];
		completed_job->next = NULL;
		if (q->tail == 0) {//Special case for when the head and tail index are the same (ie 0). This means the list has only one job admitted.
			q->admitted_jobs[q->tail] = NULL;
		}else{	
			q->tail--; //Current job is being removed so decrease tail index by 1 
			q->admitted_jobs[q->tail]->next = q->admitted_jobs[q->head]; //change the job that the new tail points to to the head job - circular list
		}
		q->num_admitted--; //Current job is being removed so decrease num_admitted by 1				     
		
		//Update processor_record information
    		processor_record_num = completed_job->processor;
		pthread_mutex_lock(&tassadar.processor_records[processor_record_num].lock); //Lock the processor record for this processor
    		tassadar.processor_records[processor_record_num].num_completed++;
    		//Append completed job onto the head of the processor_record completed_jobs list.
		completed_job->next = tassadar.processor_records[processor_record_num].completed_jobs;
       		tassadar.processor_records[processor_record_num].completed_jobs = completed_job;
		pthread_mutex_unlock(&tassadar.processor_records[processor_record_num].lock); //Unlock the processor record for this processor
		
		//Unlocks the resources required from the job in reverse order from locking. This is to ensure that the lock order is maintained and deadlocks are prevented
		for (j = NUM_RESOURCES - 1; j >= 0; j--) {
    			for(i = completed_job->num_resources-1; i >=0; i--) {
				if (completed_job->resources[i] == j) {
					pthread_mutex_unlock(&tassadar.resource_locks[j]);
				}
    			}	
		}
		pthread_cond_signal(&q->admission_cv); //Signal to admission thread that this admission queue has executed and removed a job from the admitted_jobs list so there is room free to admit another job
    		pthread_mutex_unlock(&q->lock); //Unlock the execution thread
		pthread_mutex_lock(&q->lock); //Lock the execution thread to prevent data race when evaluating the while loop condition
	}
	pthread_mutex_unlock(&q->lock); //Unlock the execution thread after while loop condition fails.
	free(q->admitted_jobs); //Free the admitted_jobs list for this admission queue since there are no jobs left that are pending or admitted
    	return NULL;
}
