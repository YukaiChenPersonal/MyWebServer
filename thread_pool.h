#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include "locker.h"
#include <list>
#include <cstdio>
#include <exception>

//thread pool type, use template to allow using codes for different types.
template<typename T>
class threadpool {
public:
    threadpool(int thread_number = 8, int max_requests_num = 5000);

    ~threadpool();

    bool append(T* request);

private:
    static void* worker(void* arg);
    void run();

private:
     //number of threads.
     int thread_num;
    
    //thread pool array
     pthread_t * threads;

    //waiting queue max capacity
    int max_requests;

    //waiting queue
    std::list <T*> waitqueue;

    //mutex lock
    locker queuelocker;

    //signal for requests in waitqueue
    sem queuestat;

    //shut down thread flag
    bool stopf;
};

template< typename T >
threadpool< T >::threadpool(int thread_number, int max_requests) : 
        thread_num(thread_number), max_requests(max_requests), 
        stopf(false), threads(NULL) {

    if((thread_number <= 0) || (max_requests <= 0) ) {
        throw std::exception();
    }

    threads = new pthread_t[thread_number];
    if(!threads) {
        throw std::exception();
    }

    // 创建thread_number 个线程，并将他们设置为脱离线程。
    for ( int i = 0; i < thread_number; ++i ) {
        printf( "create the %dth thread\n", i);
        if(pthread_create(threads + i, NULL, worker, this ) != 0) {
            delete [] threads;
            throw std::exception();
        }
        
        if( pthread_detach( threads[i] ) ) {
            delete [] threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool() {
    delete[] threads;
    stopf = true;
}

template<typename T>
bool threadpool<T>::append(T * request){
    queuelocker.lock();
    if(waitqueue.size() > max_requests){
        queuelocker.unlock();
        return false;
    }

    waitqueue.push_back(request);
    queuelocker.unlock();
    queuestat.post();
    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg){
    threadpool* pool = (threadpool* )arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run(){
    while(!stopf){
        queuestat.wait();
        queuelocker.lock();
        if(waitqueue.empty())
        {
            queuelocker.unlock();
            continue;
        }
        
        T* request = waitqueue.front();
        waitqueue.pop_front();
        queuelocker.unlock();
        
        if(!request){
            continue;
        }
        
        request->process();
    }
}
#endif