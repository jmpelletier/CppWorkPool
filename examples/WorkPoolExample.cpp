#include <iostream>
#include <sstream>
#include <chrono>

#include "WorkPool.hpp"

// This is an example of a custom job that prints a message.
class TestJob : public JMP::Concurrent::Job {
private:
    std::string _mess;
public:
    TestJob(std::string const & message) : _mess(message) {}
    void run() {
        // When writing to cout concurrently, construct the string first.
        // If we don't do this, the output will be garbled when several threads
        // try to output to cout at the same time.
        std::stringstream ss;d
        ss << _mess << std::endl;
        std::cout << ss.str();
    }
};

// This is a job that waits for a while before outputing its message.
class WaitJob : public JMP::Concurrent::Job {
private:
    std::string _mess;
    int _milliseconds;
public:
    WaitJob(std::string const & message, int milliseconds) : _mess(message), _milliseconds(milliseconds) {}
    void run() {
        std::this_thread::sleep_for(std::chrono::milliseconds(_milliseconds));
        std::stringstream ss;
        ss << "(" << std::this_thread::get_id() << ") ";
        ss << _mess << std::endl;
        std::cout << ss.str();
    }
};

// This is a job that does not output anything. Instead, it waits for a while
// before updating a value that can be read later.
class FutureJob : public JMP::Concurrent::Job {
private:
    std::string _mess;
    std::string _out;
    int _milliseconds;
public:
    FutureJob(std::string const & message, int milliseconds) : _mess(message), _milliseconds(milliseconds) {}
    void run() {
        std::this_thread::sleep_for(std::chrono::milliseconds(_milliseconds));
        std::stringstream ss;
        ss << "(" << std::this_thread::get_id() << ") ";
        ss << _mess << std::endl;
        _out = ss.str();
    }

    std::string get_message() {
        return _out;
    }
};

int main(int argc, char const *argv[])
{
    // An example of how the WorkQueue works. We probably don't need to use this directly.
    // This example is just for reference.
    JMP::Concurrent::WorkQueue queue;
    queue.push<TestJob>("Hello!");
    queue.push<TestJob>("How are you?");
    // Here is how to run the jobs.
    std::unique_ptr<JMP::Concurrent::Job> job = queue.pop();
    while(job) {
        job->run();
        job = queue.pop();
    }
    
    // Usually, we use a thread pool to process our jobs.
    // First we start a thread pool with at least four threads.
    unsigned int thread_count = std::max(4U, std::thread::hardware_concurrency());
    std::cout << "Thread count: " << thread_count << std::endl;
    JMP::Concurrent::WorkPool pool(thread_count);

    // Let's add some jobs to the pool. This returns immediately. The work will actually
    // be done by the worker threads.
    pool.add_job<WaitJob>("1", 100);
    pool.add_job<WaitJob>("2", 200);
    pool.add_job<WaitJob>("3", 300);
    pool.add_job<WaitJob>("4", 400);
    pool.add_job<WaitJob>("5", 500);
    pool.add_job<WaitJob>("6", 600);

    // Now we add a job so that we can use it after it is done running.
    JMP::Concurrent::Id id = pool.add_future_job<FutureJob>("Hello from the future!", 1000);

    // Let's sleep for a while.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Let's check if the job is finished.
    bool complete = pool.job_complete(id);
    std::cout << "Future ready? " << complete << std::endl;

    // Let's sleep some more...
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // By now, the job should be finished. Let's check that this is the case.
    complete = pool.job_complete(id);
    std::cout << "Future ready? " << complete << std::endl;
    
    if (complete) {
        // We are ready to process the job. Let's pop it from the pool.
        std::unique_ptr<JMP::Concurrent::Job> job = pool.pop_completed_job(id);

        // Now we can read and output the message.
        std::string message = dynamic_cast<FutureJob&>(*job).get_message();
        std::cout << message << std::endl;

        // Here, we knew that the job was of type FutureJob, but really we should 
        // be more careful when we do our cast:
        try {
            WaitJob & bad_cast = dynamic_cast<WaitJob&>(*job);
        }
        catch(std::bad_cast & e) {}
    }
    
    return 0;
}
