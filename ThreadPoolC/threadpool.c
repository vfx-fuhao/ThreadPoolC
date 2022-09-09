#include "threadpool.h"
#include "pthread.h"


const int NUMBER = 2;

// Job Struct
typedef struct Task
{
	void (*function) (void* arg);
	void* arg;
}Task;


// Treadpool struct
struct ThreadPool
{
	// Task Queue
	Task* taskQ;
	int queueCapacity;   // Capacity
	int queueSize;       // Current Task number
	int queueFront;      // Front queue -> get data
	int queueRear;       // Rear queue -> push data

	pthread_t managerID;     // Manager thread ID
	pthread_t* threadIDs;    // Worker thread ID
	int minNum;              
	int maxNum;
	int busyNum;
	int liveNum;
	int exitNum;
	pthread_mutex_t mutexPool;  // lock all threadpool
	pthread_mutex_t mutexBusy;  // lock busyNum
	pthread_cond_t notFull;     // Task queue is full
	pthread_cond_t notEmpty;    // Task queue is empty

	int shutdown;

};

ThreadPool* threadPoolCreate(int min, int max, int queueSize)
{
	ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
	do
	{
		if (pool == NULL)
		{
			printf("malloc threadpool fail...\n");
			break;
		}
		pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t) * max);
		if (pool->threadIDs == NULL)
		{
			printf("malloc threadIDs fail...\n");
			break;
		}

		memset(pool->threadIDs, 0, sizeof(pthread_t) * max);

		pool->minNum = min;
		pool->maxNum = max;
		pool->busyNum = 0;
		pool->liveNum = min;
		pool->exitNum = 0;

		if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
			pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
			pthread_cond_init(&pool->notEmpty, NULL) != 0 ||
			pthread_cond_init(&pool->notFull, NULL) != 0)
		{
			printf("mutex or condition init fail...\n");
			break;
		}

		// Task queue
		pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
		pool->queueCapacity = queueSize;
		pool->queueSize = 0;
		pool->queueFront = 0;
		pool->queueRear = 0;

		pool->shutdown = 0;


		// Create Thread
		pthread_create(&pool->managerID, NULL, manager, pool);
		for (int i = 0; i < min; ++i)
		{
			pthread_create(&pool->threadIDs[i], NULL, worker, pool);
		}

	} while (0);

	// free resources
	if (pool && pool->threadIDs) free(pool->threadIDs);
	if (pool->taskQ) free(pool->taskQ);
	if (pool) free(pool);

	return NULL;
}

int threadPoolDestroy(ThreadPool* pool)
{
	if (pool == NULL)
	{
		return -1;
	}
	// Shut down threadpool
	pool->shutdown = 1;
	pthread_join(pool->managerID, NULL);
	// Awake Block thread
	for (int i = 0; i < pool->liveNum; ++i)
	{
		pthread_cond_signal(&pool->notEmpty);
	}

	// Free memory 
	if (pool->taskQ)
	{
		free(pool->taskQ);
	}
	if (pool->threadIDs)
	{
		free(pool->threadIDs);
	}
	free(pool);
	pool = NULL;

	pthread_mutex_destroy(&pool->mutexPool);
	pthread_mutex_destroy(&pool->mutexBusy);
	pthread_cond_destroy(&pool->notEmpty);
	pthread_cond_destroy(&pool->notFull);

	return 0;
}

void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg)
{
	pthread_mutex_lock(&pool->mutexPool);
	while (pool->queueSize == pool->queueCapacity && !pool->shutdown)
	{
		// Block worker thread
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);
	}
	if (pool->shutdown)
	{
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}

	// Add task
	pool->taskQ[pool->queueRear].function = func;
	pool->taskQ[pool->queueRear].arg = arg;
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
	pool->queueSize++;

	pthread_cond_signal(&pool->notEmpty);
	pthread_mutex_unclock(&pool->mutexPool);
}

int threadPoolBushNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexBusy);
	int busyNum = pool->busyNum;
	pthread_mutex_unlock(&pool->mutexBusy);
	return busyNum;
}

int threadPoolAliveNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexPool);
	int aliveNum = pool->liveNum;
	pthread_mutex_unlock(&pool->mutexPool);
	return aliveNum;
}

void* worker(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;

	while (1)
	{
		pthread_mutex_lock(&pool->mutexPool);
		while (pool->queueSize == 0 && !pool->shutdown)
		{
			// Block worker thread
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

			// Destroy the thread or not
			if (pool->exitNum > 0)
			{
				pool->exitNum--;
				if (pool->liveNum > pool->minNum)
				{
					pool->liveNum--;
					pthread_mutex_unlock(&pool->mutexPool);
					threadExit(pool);
				}
				
			}
		}

		if (pool->shutdown)
		{
			pthread_mutex_unlock(&pool->mutexPool);
			threadExit(pool);
		}

		// Get tast from task queue
		Task task;
		task.function = pool->taskQ[pool->queueFront].function;
		task.arg = pool->taskQ[pool->queueFront].arg;
		// Move front node
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--;


		pthread_mutex_unlock(&pool->mutexPool);

		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy);

		task.function(task.arg);
		free(task.arg);
		task.arg = NULL;

		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexBusy);

		
	}
	return NULL;
}

void* manager(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;

	while (!pool->shutdown)
	{
		sleep(3);

		//Get threadpool number and thread number
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		//Get busy thread number
		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_lock(&pool->mutexBusy);


		// Add thread
		if (queueSize > liveNum && liveNum < pool->maxNum)
		{
			pthread_mutex_lock(&pool->mutexPool);
			int counter = 0;
			for (int i = 0; i < pool->maxNum && counter < NUMBER && pool->liveNum < pool->maxNum; ++i)
			{
				if (pool->threadIDs[i] == 0)
				{
					pthread_create(&pool->threadIDs[i], NULL, worker, pool);
					counter++;
					pool->liveNum;

				}
			}
			pthread_mutex_unlock(&pool->mutexPool);
		}


		// Destroy thread
		if (busyNum * 2 < liveNum && liveNum > pool->minNum)
		{
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);
		}

	}
	return NULL;
}

void threadExit(ThreadPool* pool)
{
	pthread_t tid = pthread_self();
	for (int i = 0; i < pool->maxNum; ++i)
	{
		if (pool->threadIDs[i] == tid)
		{
			pool->threadIDs[i] == 0;
			printf("threadExit() called, %ld exiting...\n", tid);
			break;
		}
	}
	pthread_exit(NULL);
}
