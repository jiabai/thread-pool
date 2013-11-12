#include "thread-pool.h"
#include <iostream>

using namespace std;

void test(tp_work*, tp_work_desc*) {
	cout << "########################### Hello" << endl;
	fflush(stdout);
	pause();
}

int main()
{
	tp_thread_pool* tp = creat_thread_pool(10, 100);
	tp->init(tp);
	
	tp_work_desc job;
	tp_work      work;
	work.process_job = test;
	
	for (int i = 0; i < 10; i++) {
		tp->process_job(tp, &work, &job);
	}
	sleep(30);
	for (int i=0; i<10; i++) {
		tp->process_job(tp, &work, &job);
	}
	pause();
	return 0;
}

