#pragma once
#include <queue>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace JMP
{
    namespace Concurrent {
        
        // This is an identifier that is assigned to concurrent jobs.
        using Id = uint64_t;

        // Forward declaration
        class WorkQueue;
        class WorkMap;

        // The base class for our custom jobs. We must override the run() function with our implementation.
        class Job {
        protected:
            friend class WorkQueue;
            friend class WorkMap;
            Id _id;
        public:
            Job() = default;
            virtual ~Job() = default;
            virtual void run() = 0;
            virtual void preprocess() {}
            virtual void postprocess() {}

            Id id() const { return _id; }
        };

        // This is a convenience wrapper for storing jobs.
        class WorkQueue {
        private:
            std::mutex _lock;
            std::queue<std::unique_ptr<Job>> _queue;
        public:

            WorkQueue() = default;

            // Disallow copying
            WorkQueue(WorkQueue const &) = delete;
            WorkQueue& operator=(WorkQueue &) = delete;

            // Add a new job to the queue. The job can be of any type that inherits from JMP::Concurrent::Job.
            // Usage: my_queue.push<MyJob>(100, "a string");
            template <typename T, typename... Args>
            void push(Args... args) {
                _queue.emplace(std::make_unique<T>(args...));
            }

            // This removes the oldest job from the queue and returns a unique pointer to this job.
            // If the queue is empty, will return a null pointer.
            std::unique_ptr<Job> pop() {
                std::unique_lock lock(_lock);
                if (!_queue.empty()) {
                    std::unique_ptr<Job> job = std::move(_queue.front());
                    _queue.pop();
                    return job;
                }
                else {
                    return std::unique_ptr<Job>(nullptr);
                }
            }

            // Is the queue empty?
            bool empty() {
                std::unique_lock lock(_lock);
                return _queue.empty();
            }
        };

        // This is like WorkQueue but jobs are stored in a map, rather than a queue.
        // Whenever a new job is added, it is assigned an Id that can be used to access the job later.
        class WorkMap {
        private:
            std::map<JMP::Concurrent::Id, std::unique_ptr<Job>> _map;
            std::atomic<JMP::Concurrent::Id> _current_id;
            std::mutex _lock;
        public:
            WorkMap() : _current_id(1) {}

            // Disallow copying
            WorkMap(WorkMap const &) = delete;
            WorkMap& operator=(WorkMap &) = delete;

            // Add a new job to the map. The job can be of any type that inherits from JMP::Concurrent::Job.
            // Returns the Id that was assigned to the job.
            // Usage: JMP::Concurrent::Id my_id = my_map.add<MyJob>(100, "a string");
            template <typename T, typename... Args>
            JMP::Concurrent::Id add(Args... args) {
                _current_id++;
                std::unique_ptr<T> job = std::make_unique<T>(args...);
                job->_id = _current_id;
                _map[_current_id] = std::move(job);
                
                return _current_id;
            }

            // Add a new job to the map, but provide the Id. The job can be of any type that inherits from JMP::Concurrent::Job.
            // Returns the Id that was assigned to the job (same as first argument).
            // Usage: JMP::Concurrent::Id my_id = my_map.add<MyJob>(100, "a string");
            JMP::Concurrent::Id add(std::pair<JMP::Concurrent::Id, std::unique_ptr<Job>> & pair) {
                pair.second->_id = pair.first;
                _map[pair.first] = std::move(pair.second);
                return pair.first;
            }

            std::map<JMP::Concurrent::Id, std::unique_ptr<Job>>::iterator find(JMP::Concurrent::Id id) {
                return _map.find(id);
            }

            // Removes and returns a unique pointer to the first job in the map. This may not necessarily be the oldest job.
            // If the map is empty, with return a std::pair with an Id of 0, and a null pointer. 
            // (Use the pointer rather than the Id for checking if the returned value is valid.)
            std::pair<JMP::Concurrent::Id, std::unique_ptr<Job>> pop() {
                std::unique_lock lock(_lock);
                auto it = _map.begin();
                if (it != _map.end()) {
                    std::pair<JMP::Concurrent::Id, std::unique_ptr<Job>> pair = std::make_pair(it->first, std::move(it->second));
                    _map.erase(it);
                    return pair;
                }
                else {
                    return std::make_pair(0, std::unique_ptr<Job>(nullptr));
                }
            }

            // Removes and returns a unique pointer to the job stored in the map at the given Id. 
            // If the map does not contain the given id, a std::pair with an Id of 0, and a null pointer. 
            // (Use the pointer rather than the Id for checking if the returned value is valid.)
            std::pair<JMP::Concurrent::Id, std::unique_ptr<Job>> pop(JMP::Concurrent::Id id) {
                std::unique_lock lock(_lock);
                auto it = _map.find(id);
                if (it != _map.end()) {
                    std::pair<JMP::Concurrent::Id, std::unique_ptr<Job>> pair = std::make_pair(it->first, std::move(it->second));
                    _map.erase(it);
                    return pair;
                }
                else {
                    return std::make_pair(0, std::unique_ptr<Job>(nullptr));
                }
            }

            // Check whether this map contains the given Id.
            bool contains(JMP::Concurrent::Id id) {
                std::unique_lock lock(_lock);
                return _map.find(id) != _map.end();
            }

            // Is the map empty?
            bool empty() {
                std::unique_lock lock(_lock);
                return _map.empty();
            }
        };

        // This is a thread pool for running jobs concurrently.
        class WorkPool {
        private:
            std::vector<std::thread> _workers;
            WorkQueue _queue;
            WorkMap _future_jobs;
            WorkMap _completed_jobs;
            
            bool _active;            

            void _work() {
                while(_active) {
                    // Run jobs with no futures first
                    std::unique_ptr<Job> job = _queue.pop();
                    if (job) {
                        job->preprocess();
                        job->run();
                        job->postprocess();
                    }

                    // Then run jobs with futures
                    std::pair<JMP::Concurrent::Id, std::unique_ptr<Job>> pair = _future_jobs.pop();
                    if (pair.second) {
                        pair.second->preprocess();
                        pair.second->run();
                        pair.second->postprocess();
                        _completed_jobs.add(pair);
                    }
                }
            }
        public:
            // There is no default constructor.
            WorkPool() = delete;

            // We must specify the number of worker threads in the pool.
            WorkPool(unsigned int worker_count) : _active(true) {
                for (unsigned int i = 0; i < worker_count; i++) {
                    _workers.emplace_back(&WorkPool::_work, this);
                }
            }
            
            // Important: when a WorkPool is destroyed, it will wait until all jobs are completed.
            // This is a blocking operation that may stall for a long time if a lengthy job is in process.
            ~WorkPool() {
                // In some cases, it's possible that the WorkPool will get destroyed before any thread has 
                // started working. We need to make sure that all threads have actually started their work
                // before joining.
                _active = false;
                wait_for_jobs();
                for(std::thread & t : _workers) {
                    t.join();
                }
            }

            // Disallow copying
            WorkPool(WorkPool const &) = delete;
            WorkPool& operator=(WorkPool &) = delete;

            // Add a new job to the queue. The job can be of any type that inherits from JMP::Concurrent::Job.
            // Usage: my_pool.add_job<MyJob>(100, "a string");
            template <typename T, typename... Args>
            void add_job(Args... args) {
                if (_active) {
                    _queue.push<T>(args...);
                }
                
            }

            // Add a new job but keep a reference so that it can be accessed after it is completed.
            // Returns the Id assigned to the job.
            template <typename T, typename... Args>
            JMP::Concurrent::Id add_future_job(Args... args) {
                if (_active) {
                    return _future_jobs.add<T>(args...);
                }
                else {
                    return 0;
                }
            }

            // Checks to see if a job with the given Id has completed and is available to access.
            bool job_complete(JMP::Concurrent::Id job_id) {
                return _completed_jobs.contains(job_id);
            }

            // Returns a pointer to a completed job and removes this job from the completed job queue. 
            // If there is no completed job with the given Id, of if the job has already been popped,
            // this will return a null pointer.
            std::unique_ptr<Job> pop_completed_job(JMP::Concurrent::Id job_id) {
                std::pair<JMP::Concurrent::Id, std::unique_ptr<Job>> pair = _completed_jobs.pop(job_id);
                return std::move(pair.second);
            }

            // Waits for all jobs to complete. This is a blocking call.
            void wait_for_jobs() {
                while (!_queue.empty() || !_future_jobs.empty());
            }
        };

    }//namespace
    
} //namespace