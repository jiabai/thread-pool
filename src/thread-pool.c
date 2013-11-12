#include "thread-pool.h"

static void *tp_work_thread(void *pthread);
static void *tp_manage_thread(void *pthread);

static TPBOOL tp_init(tp_thread_pool *this_);
static void tp_close(tp_thread_pool *this_);
static void tp_process_job(tp_thread_pool *this_, tp_work *worker, tp_work_desc *job);
static int  tp_get_thread_by_id(tp_thread_pool *this_, int id);
static TPBOOL tp_add_thread(tp_thread_pool *this_);
static TPBOOL tp_delete_thread(tp_thread_pool *this_);
static int  tp_get_tp_status(tp_thread_pool *this_);

/**
  * user interface. creat thread pool.
  * para:
  * 	num: min thread number to be created in the pool
  * return:
  * 	thread pool struct instance be created successfully
  */
tp_thread_pool *creat_thread_pool(int min_num, int max_num){
	tp_thread_pool *this_;
	this_ = (tp_thread_pool*)malloc(sizeof(tp_thread_pool));	

	memset(this_, 0, sizeof(tp_thread_pool));
	
	//init member function ponter
	this_->init = tp_init;
	this_->close = tp_close;
	this_->process_job = tp_process_job;
	this_->get_thread_by_id = tp_get_thread_by_id;
	this_->add_thread = tp_add_thread;
	this_->delete_thread = tp_delete_thread;
	this_->get_tp_status = tp_get_tp_status;

	//init member var
	this_->min_th_num = min_num;
	this_->cur_th_num = this_->min_th_num;
	this_->max_th_num = max_num;
	pthread_mutex_init(&this_->tp_lock, NULL);

	//malloc mem for num thread info struct
	if(NULL != this_->thread_info)
		free(this_->thread_info);
	this_->thread_info = (tp_thread_info*)malloc(sizeof(tp_thread_info)*this_->max_th_num);

	return this_;
}


/**
  * member function reality. thread pool init function.
  * para:
  * 	this_: thread pool struct instance ponter
  * return:
  * 	true: successful; false: failed
  */
TPBOOL tp_init(tp_thread_pool *this_){
	int i;
	int err;
	
	//creat work thread and init work thread info
	for(i=0;i<this_->min_th_num;i++){
		pthread_cond_init(&this_->thread_info[i].thread_cond, NULL);
		pthread_mutex_init(&this_->thread_info[i].thread_lock, NULL);
		
		err = pthread_create(&this_->thread_info[i].thread_id, NULL, tp_work_thread, this_);
		if(0 != err){
			//printf("tp_init: creat work thread failed\n");
			return FALSE;
		}
		//printf("tp_init: creat work thread %d\n", this_->thread_info[i].thread_id);
	}

	//creat manage thread
	err = pthread_create(&this_->manage_thread_id, NULL, tp_manage_thread, this_);
	if(0 != err){
		//printf("tp_init: creat manage thread failed\n");
		return FALSE;
	}
	//printf("tp_init: creat manage thread %d\n", this_->manage_thread_id);

	return TRUE;
}

/**
  * member function reality. thread pool entirely close function.
  * para:
  * 	this_: thread pool struct instance ponter
  * return:
  */
void tp_close(tp_thread_pool *this_){
	int i;
	
	//close work thread
	for(i=0;i<this_->cur_th_num;i++){
		kill(this_->thread_info[i].thread_id, SIGKILL);
		pthread_mutex_destroy(&this_->thread_info[i].thread_lock);
		pthread_cond_destroy(&this_->thread_info[i].thread_cond);
		//printf("tp_close: kill work thread %d\n", this_->thread_info[i].thread_id);
	}

	//close manage thread
	kill(this_->manage_thread_id, SIGKILL);
	pthread_mutex_destroy(&this_->tp_lock);
	//printf("tp_close: kill manage thread %d\n", this_->manage_thread_id);
	
	//free thread struct
	free(this_->thread_info);
}

/**
  * member function reality. main interface opened. 
  * after getting own worker and job, user may use the function to process the task.
  * para:
  * 	this_: thread pool struct instance ponter
  *	worker: user task reality.
  *	job: user task para
  * return:
  */
void tp_process_job(tp_thread_pool *this_, tp_work *worker, tp_work_desc *job){
	int i;
	int tmpid;

	//fill this_->thread_info's relative work key
	for(i=0;i<this_->cur_th_num;i++){
		pthread_mutex_lock(&this_->thread_info[i].thread_lock);
		if(!this_->thread_info[i].is_busy){
			//printf("tp_process_job: %d thread idle, thread id is %d\n", i, this_->thread_info[i].thread_id);
			//thread state be set busy before work
		  	this_->thread_info[i].is_busy = TRUE;
			pthread_mutex_unlock(&this_->thread_info[i].thread_lock);
			
			this_->thread_info[i].th_work = worker;
			this_->thread_info[i].th_job = job;
			
			//printf("tp_process_job: informing idle working thread %d, thread id is %d\n", i, this_->thread_info[i].thread_id);
			pthread_cond_signal(&this_->thread_info[i].thread_cond);

			return;
		}
		else 
			pthread_mutex_unlock(&this_->thread_info[i].thread_lock);		
	}//end of for

	//if all current thread are busy, new thread is created here
	pthread_mutex_lock(&this_->tp_lock);
	if( this_->add_thread(this_) ){
		i = this_->cur_th_num - 1;
		tmpid = this_->thread_info[i].thread_id;
		this_->thread_info[i].th_work = worker;
		this_->thread_info[i].th_job = job;
	}
	pthread_mutex_unlock(&this_->tp_lock);
	
	//send cond to work thread
	//printf("tp_process_job: informing idle working thread %d, thread id is %d\n", i, this_->thread_info[i].thread_id);
	pthread_cond_signal(&this_->thread_info[i].thread_cond);
	return;	
}

/**
  * member function reality. get real thread by thread id num.
  * para:
  * 	this_: thread pool struct instance ponter
  *	id: thread id num
  * return:
  * 	seq num in thread info struct array
  */
int tp_get_thread_by_id(tp_thread_pool *this_, int id){
	int i;

	for(i=0;i<this_->cur_th_num;i++){
		if(id == this_->thread_info[i].thread_id)
			return i;
	}

	return -1;
}

/**
  * member function reality. add new thread into the pool.
  * para:
  * 	this_: thread pool struct instance ponter
  * return:
  * 	true: successful; false: failed
  */
static TPBOOL tp_add_thread(tp_thread_pool *this_){
	int err;
	tp_thread_info *new_thread;
	
	if( this_->max_th_num <= this_->cur_th_num )
		return FALSE;
		
	//malloc new thread info struct
	new_thread = &this_->thread_info[this_->cur_th_num];
	
	//init new thread's cond & mutex
	pthread_cond_init(&new_thread->thread_cond, NULL);
	pthread_mutex_init(&new_thread->thread_lock, NULL);

	//init status is busy
	new_thread->is_busy = TRUE;

	//add current thread number in the pool.
	this_->cur_th_num++;
	
	err = pthread_create(&new_thread->thread_id, NULL, tp_work_thread, this_);
	if(0 != err){
		free(new_thread);
		return FALSE;
	}
	//printf("tp_add_thread: creat work thread %d\n", this_->thread_info[this_->cur_th_num-1].thread_id);
	
	return TRUE;
}

/**
  * member function reality. delete idle thread in the pool.
  * only delete last idle thread in the pool.
  * para:
  * 	this_: thread pool struct instance ponter
  * return:
  * 	true: successful; false: failed
  */
static TPBOOL tp_delete_thread(tp_thread_pool *this_){
	//current thread num can't < min thread num
	if(this_->cur_th_num <= this_->min_th_num) return FALSE;

	//if last thread is busy, do nothing
	if(this_->thread_info[this_->cur_th_num-1].is_busy) return FALSE;

	//kill the idle thread and free info struct
	kill(this_->thread_info[this_->cur_th_num-1].thread_id, SIGKILL);
	pthread_mutex_destroy(&this_->thread_info[this_->cur_th_num-1].thread_lock);
	pthread_cond_destroy(&this_->thread_info[this_->cur_th_num-1].thread_cond);

	//after deleting idle thread, current thread num -1
	this_->cur_th_num--;

	return TRUE;
}

/**
  * member function reality. get current thread pool status:idle, normal, busy, .etc.
  * para:
  * 	this_: thread pool struct instance ponter
  * return:
  * 	0: idle; 1: normal or busy(don't process)
  */
static int  tp_get_tp_status(tp_thread_pool *this_){
	float busy_num = 0.0;
	int i;

	//get busy thread number
	for(i=0;i<this_->cur_th_num;i++){
		if(this_->thread_info[i].is_busy)
			busy_num++;
	}

	//0.2? or other num?
	if(busy_num/(this_->cur_th_num) < BUSY_THRESHOLD)
		return 0;//idle status
	else
		return 1;//busy or normal status	
}

/**
  * internal interface. real work thread.
  * para:
  * 	pthread: thread pool struct ponter
  * return:
  */
static void *tp_work_thread(void *pthread){
	pthread_t curid;//current thread id
	int nseq;//current thread seq in the this_->thread_info array
	tp_thread_pool *this_ = (tp_thread_pool*)pthread;//main thread pool struct instance

	//get current thread id
	curid = pthread_self();
	
	//get current thread's seq in the thread info struct array.
	nseq = this_->get_thread_by_id(this_, curid);
	if(nseq < 0)
		return;
	//printf("entering working thread %d, thread id is %d\n", nseq, curid);

	//wait cond for processing real job.
	while( TRUE ){
		pthread_mutex_lock(&this_->thread_info[nseq].thread_lock);
		pthread_cond_wait(&this_->thread_info[nseq].thread_cond, &this_->thread_info[nseq].thread_lock);
		pthread_mutex_unlock(&this_->thread_info[nseq].thread_lock);		
		
		//printf("%d thread do work!\n", pthread_self());

		tp_work *work = this_->thread_info[nseq].th_work;
		tp_work_desc *job = this_->thread_info[nseq].th_job;

		//process
		work->process_job(work, job);

		//thread state be set idle after work
		pthread_mutex_lock(&this_->thread_info[nseq].thread_lock);		
		this_->thread_info[nseq].is_busy = FALSE;
		pthread_mutex_unlock(&this_->thread_info[nseq].thread_lock);
		
		//printf("%d do work over\n", pthread_self());
	}	
}

/**
  * internal interface. manage thread pool to delete idle thread.
  * para:
  * 	pthread: thread pool struct ponter
  * return:
  */
static void *tp_manage_thread(void *pthread){
	tp_thread_pool *this_ = (tp_thread_pool*)pthread;//main thread pool struct instance

	//1?
	sleep(MANAGE_INTERVAL);

	do{
		if( this_->get_tp_status(this_) == 0 ){
			do{
				if( !this_->delete_thread(this_) )
					break;
			}while(TRUE);
		}//end for if

		//1?
		sleep(MANAGE_INTERVAL);
	}while(TRUE);
}

