#pragma once

typedef struct ThreadPool ThreadPool;
// Create threadpool and initialize
ThreadPool* threadPoolCreate(int min, int max, int queueSize);

// Destroy threadpool 
int threadPoolDestroy(ThreadPool* pool);

// Add job to threadpool
void threadPoolAdd(ThreadPool* pool, void(*func) (void*), void* arg);

// Get work thread count from threadpool
int threadPoolBushNum(ThreadPool* pool);

// Get live thread count from threadpool
int threadPoolAliveNum(ThreadPool* pool);

//////////////////////////////////////////
void* worker(void* arg);
void* manager(void* arg);
void threadExit(ThreadPool* pool);