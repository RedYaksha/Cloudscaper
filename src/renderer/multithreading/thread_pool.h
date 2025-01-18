#ifndef RENDERER_MULTITHREADING_THREAD_POOL_H_
#define RENDERER_MULTITHREADING_THREAD_POOL_H_

#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <vector>
#include <thread>
#include <winrt/base.h>
#include <iostream>

//
// TODO: for this use-case packaged task is no longer needed
// and just sending/modifying a promise in the task function itself is enough.
template <typename T>
class ThreadPool {
public:
    ThreadPool(uint16_t numThreads=0);
    ~ThreadPool();
    void Start();
    void Stop();

    void AddTask(std::packaged_task<T()>&& job);
    
private:
    void ThreadDoWork();
    
    uint16_t numThreads_;
    std::vector<std::thread> threads_;
    std::condition_variable condVar_;
    std::queue<std::packaged_task<T()>> tasks_;
    std::atomic_bool shouldTerminate_;
    std::mutex mutex_;
    bool started;
};


template <typename T>
void ThreadPool<T>::AddTask(std::packaged_task<T()>&& job) {
    tasks_.push(std::move(job));
    condVar_.notify_one();
}

template <typename T>
ThreadPool<T>::ThreadPool(uint16_t numThreads)
    : started(false) {
    if(numThreads == 0) {
        numThreads_ = std::thread::hardware_concurrency();
    }
    else {
        numThreads_ = numThreads;
    }
}

template <typename T>
ThreadPool<T>::~ThreadPool() {
    Stop();
}

template <typename T>
void ThreadPool<T>::Start() {
    WINRT_ASSERT(!started && "Trying to start the thread pool again.");
    started = true;

    for(int i = 0; i < numThreads_; i++) {
        std::thread t = std::thread(&ThreadPool::ThreadDoWork, this);
        threads_.push_back(std::move(t));
    }
}

template <typename T>
void ThreadPool<T>::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(shouldTerminate_) {
            return;
        }
        
        shouldTerminate_ = true;
    }
    condVar_.notify_all();

    for(int i = 0; i < numThreads_; i++) {
        threads_[i].join();
    }

    threads_.clear();
}

template <typename T>
void ThreadPool<T>::ThreadDoWork() {
    while(true) {
        std::packaged_task<T()> task;
        {
            std::unique_lock<std::mutex> ulock(mutex_);

            // unlocks ulock, unlocks mutex_, when condVar_.notifyOne/All is called (or spuriously),
            // mutex_ is locked, condition is checked, and will repeat if fail, continue if true
            condVar_.wait(ulock, [this]()->bool{
                return tasks_.size() > 0 || shouldTerminate_; 
            });

            if(shouldTerminate_) {
                // when return, thread is killed
                std::cout << "Worker thread terminating..." << std::endl;
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }
        
        task();
    }
}

#endif // RENDERER_MULTITHREADING_THREAD_POOL_H_
