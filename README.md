# CppWorkPool

This is a simple concurrent work pool for assigning jobs to different worker threads in C++.

## Usage

First we need to create a custom job.

```cpp
#include <chrono>
#include "WorkPool.hpp"

// This is a job that waits for a while before outputing its message.
class WaitJob : public JMP::Concurrent::Job {
private:
    std::string _message;
    int _milliseconds;
public:
    WaitJob(std::string const & message, int milliseconds) : _message(message), _milliseconds(milliseconds) {}

    // We must implement this function, which will be executed by the worker thread.
    void run() override {
        // When writing to cout concurrently, construct the string first.
        // If we don't do this, the output will be garbled when several threads
        // try to output to cout at the same time.
        std::this_thread::sleep_for(std::chrono::milliseconds(_milliseconds));
        std::stringstream ss;
        ss << "(" << std::this_thread::get_id() << ") ";
        ss << _message << std::endl;
        std::cout << ss.str();
    }
};

```

Then, we can create a WorkPool and add jobs to it. As long as they inherit from JMP::Concurrent::Job, we can add different kinds of jobs to the pool.

```cpp
unsigned int thread_count = std::max(4U, std::thread::hardware_concurrency());
JMP::Concurrent::WorkPool pool(thread_count);

pool.add_job<WaitJob>("1", 100);
pool.add_job<WaitJob>("2", 200);
pool.add_job<WaitJob>("3", 300);
pool.add_job<WaitJob>("4", 400);
pool.add_job<WaitJob>("5", 500);
pool.add_job<WaitJob>("6", 600);
```

Here the jobs wait for an increasing length of time, so messages are output in order. However, since this is asynchronous, if the wait time were the same, the output could be out of order.

Jobs that are completed are automatically deleted, but we can also ask to pool to keep the job after it's done so that we can use it later. For instance, here we have a job that doesn't output to cout, but instead updates a string that we can query later.

```cpp
class FutureJob : public JMP::Concurrent::Job {
private:
    std::string _message;
    std::string _out;
    int _milliseconds;
public:
    FutureJob(std::string const & message, int milliseconds) : _message(message), _milliseconds(milliseconds) {}
    void run() {
        std::this_thread::sleep_for(std::chrono::milliseconds(_milliseconds));
        std::stringstream ss;
        ss << "(" << std::this_thread::get_id() << ") ";
        ss << _message << std::endl;
        _out = ss.str();
    }

    std::string get_message() {
        return _out;
    }
};
```

This is how we add this kind of job to the pool.

```cpp
JMP::Concurrent::Id id = pool.add_future_job<FutureJob>("Hello from the future!", 1000);
```

We get a value that can be used later to see if the job has completed.

```cpp
bool complete = pool.job_complete(id);
```

When the job is complete, we can get a reference to it for processing. Note that this also completely removes the job from the pool.

```cpp
complete = pool.job_complete(id);
if (complete) {
    // We are ready to process the job. Let's pop it from the pool.
    std::unique_ptr<JMP::Concurrent::Job> job = pool.pop_completed_job(id);

    // Now we can read and output the message.
    try {
      std::string message = dynamic_cast<FutureJob&>(*job).get_message();
      std::cout << message << std::endl;
    }
    catch(std::bad_cast & e) {}
}
```
