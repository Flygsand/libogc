#include <stdlib.h>
#include <errno.h>
#include "mutex.h"
#include "lwp_threadq.h"
#include "cond.h"

struct _cond {
	u32 id;
	mutex_t *mutex;
	lwp_thrqueue wait_queue;
};

static u32 sys_condvarids = 0;

extern unsigned int timespec_to_interval(const struct timespec *time);

static u32 __lwp_cond_waitsupp(cond_t cond,mutex_t *mutex,u32 timeout,u8 timedout)
{
	u32 status,mstatus;
	struct _cond *thecond = (struct _cond*)cond;

	if(!cond) return -1;

	__lwp_thread_dispatchdisable();
	if(thecond->mutex && thecond->mutex!=mutex) {
		__lwp_thread_dispatchenable();
		return EINVAL;
	}

	LWP_MutexUnlock(mutex);
	if(!timedout) {
		thecond->mutex = mutex;
		__lwp_threadqueue_csenter(&thecond->wait_queue);
		_thr_executing->wait.ret_code = 0;
		_thr_executing->wait.queue = &thecond->wait_queue;
		_thr_executing->wait.id = thecond->id;
		__lwp_threadqueue_enqueue(&thecond->wait_queue,timeout);
		__lwp_thread_dispatchenable();
		
		status = _thr_executing->wait.ret_code;
		if(status && status!=ETIMEDOUT)
			return status;
	} else {
		__lwp_thread_dispatchenable();
		status = ETIMEDOUT;
	}
	mstatus = LWP_MutexLock(mutex);
	if(mstatus)
		return EINVAL;

	return status;
}

static u32 __lwp_cond_signalsupp(cond_t cond,u8 isbroadcast)
{
	lwp_cntrl *thethread;
	struct _cond *thecond = (struct _cond*)cond;
	
	if(!thecond) return -1;
	
	__lwp_thread_dispatchdisable();
	do {
		thethread = __lwp_threadqueue_dequeue(&thecond->wait_queue);
		if(!thethread) thecond->mutex = NULL;
	} while(isbroadcast && thethread);
	__lwp_thread_dispatchenable();
	return 0;
}

u32 LWP_CondInit(cond_t *cond)
{
	struct _cond *ret;
	
	if(!cond) return -1;
	
	__lwp_thread_dispatchdisable();
	ret = (struct _cond*)malloc(sizeof(struct _cond));
	if(!ret) {
		__lwp_thread_dispatchenable();
		return ENOMEM;
	}

	ret->mutex = NULL;
	__lwp_threadqueue_init(&ret->wait_queue,LWP_THREADQ_MODEFIFO,LWP_STATES_WAITING_FOR_CONDVAR,ETIMEDOUT);

	ret->id = ++sys_condvarids;
	*cond = (void*)ret;
	__lwp_thread_dispatchenable();

	return 0;
}

u32 LWP_CondWait(cond_t cond,mutex_t *mutex)
{
	return __lwp_cond_waitsupp(cond,mutex,LWP_THREADQ_NOTIMEOUT,FALSE);
}

u32 LWP_CondSignal(cond_t cond)
{
	return __lwp_cond_signalsupp(cond,FALSE);
}

u32 LWP_CondBroadcast(cond_t cond)
{
	return __lwp_cond_signalsupp(cond,TRUE);
}

u32 LWP_CondTimedWait(cond_t cond,mutex_t *mutex,const struct timespec *abstime)
{
	u32 timeout;
	// todo: allow negative values too, and make checks.
	// if allready timedout, function should cleanup the queues anyway.
	timeout = timespec_to_interval(abstime);
	return __lwp_cond_waitsupp(cond,mutex,timeout,FALSE);
}

u32 LWP_CondDestroy(cond_t cond)
{
	struct _cond *ptr = (struct _cond*)cond;
	__lwp_thread_dispatchdisable();
	if(__lwp_threadqueue_first(&ptr->wait_queue)) {
		__lwp_thread_dispatchenable();
		return EBUSY;
	}
	free(ptr);
	__lwp_thread_dispatchenable();
	return 0;
}
