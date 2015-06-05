/* -*- mode: C++; indent-tabs-mode: nil; -*- */
/** \file
 * \brief Thread-safe shared queue of pointers to objects.
 *
 * Header file to implement a thread-safe shared queue of pointers to objects.
 *
 * Adapted for using with YARP.
 *
 * \author Truong X. Nghiem (xuan.nghiem@epfl.ch)
 * \see https://juanchopanzacpp.wordpress.com/2013/02/26/concurrent-queue-c11/
 * \see http://codingways.blogspot.ch/2012/08/implementing-thread-safe-queue-in-c11.html
 * \see https://www.justsoftwaresolutions.co.uk/threading/implementing-a-thread-safe-queue-using-condition-variables.html
 */


#ifndef MYARP_SHAREDQUEUE_H_
#define MYARP_SHAREDQUEUE_H_

#include <queue>
#include <memory>
#include <yarp/os/all.h>


/** \brief Template of thread-safe shared queue of shared_ptr pointers to objects of given type.
 */
template <typename T>
class shared_queue
{
public:
    typedef typename std::shared_ptr<T> item_type; ///< Smart pointer type to the objects.

private :
    std::queue<item_type> mData;
    mutable yarp::os::Mutex mMut;
    
    /** The semaphore to signal event, shared. */
    mutable yarp::os::Semaphore mCount;
public:
    shared_queue(): mCount(0) { }
    
    /** Push an object into the queue.
     */
    void push(item_type&& pValue)
    {
        // block execution here, if other thread already locked mMute!
        yarp::os::LockGuard mlock(mMut);
        // if we are here no other thread is owned/locked mMute. so we can modify the internal data
        mData.push(pValue);
        mCount.post();
    }
    
    void push(const item_type& pValue)
    {
        // block execution here, if other thread already locked mMute!
        yarp::os::LockGuard mlock(mMut);
        // if we are here no other thread is owned/locked mMute. so we can modify the internal data
        mData.push(pValue);
        mCount.post();
    }
    
    void push(T* pValue) // The object of type T must be dynamically allocated
    {
        // block execution here, if other thread already locked mMute!
        yarp::os::LockGuard mlock(mMut);
        // if we are here no other thread is owned/locked mMute. so we can modify the internal data
        mData.emplace(pValue);
        mCount.post();
    }
    
    /** Dynamically create an object of the given type T, wrapped inside a smart pointer and added to the queue.
     The arguments to this method will be passed directly to the constructor of the object.
     */
    template <class... Args>
    void emplace( Args&&... args )
    {
        yarp::os::LockGuard lock(mMut);
        // Create a new
        mData.emplace(new T(args...));
        mCount.post();
    }
    
    /** Pop the front/top element if there is any.
     After this, any reference to this element is invalid (e.g. reference by calling front()) and should not be accessed anymore. If this is a queue of objects, the object will be destructed.
     */
    void pop()
    {
        yarp::os::LockGuard lock(mMut);
        mData.pop();
        mCount.check();
    }
    
    /** Get a reference to the smart pointer to the top/front element.
     This reference will become invalid once pop() is called, therefore it's good practice to copy the smart pointer to a new one if you want to access the element after it is popped.
     */
    item_type& front()
    {
        // block execution here, if other thread already locked mMute!
        yarp::os::LockGuard mlock(mMut);
        return mData.front();
    }
    
    bool empty() const ///< Check if the queue is empty.
    {
        yarp::os::LockGuard lock(mMut);
        return mData.empty();
    }
    
    /** Block until the queue is non-empty, then pop and return the first element. */
    item_type wait_and_pop() {
        mCount.wait();
        item_type v = mData.front();
        mData.pop();
        return v;
    }
    
    /** Block until the queue is non-empty or timeout.
     \param timeout Timeout in seconds.
     \return Nil pointer if timeout, otherwise pop and return the first element.
     */
    item_type wait_and_pop_timeout(double timeout) {
        if (mCount.waitWithTimeout(timeout)) {
            item_type v = mData.front();
            mData.pop();
            return v;
        } else {
            return item_type();
        }
        
    }
    
    /* \brief Return the mutex used to lock/unlock access to this queue. */
    //yarp::os::Mutex& getMutex() const { return mMut; }
};

#endif /* MYARP_SHAREDQUEUE_H_ */