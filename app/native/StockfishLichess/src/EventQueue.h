#pragma once
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <vector>

/*
 * A to-do list shared between threads. The Lichess streams and the keyboard each run on their
 * own thread, but they only add jobs here. The main thread runs the jobs one at a time, so the
 * game itself is only ever touched from one thread and needs no locks.
 */
class EventQueue {
 public:
  using Job = std::function<void()>;

  /** Adds a job, to run as soon as possible or after `delayMs`. Safe to call from any thread. */
  void post(Job job, int delayMs = 0) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      jobs_.push_back({std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs), std::move(job)});
    }
    wake_.notify_one();
  }

  /** Runs jobs in order until stop() is called. */
  void run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopped_) {
      size_t next = jobs_.size();
      for (size_t i = 0; i < jobs_.size(); i++) {
        if (next == jobs_.size() || jobs_[i].due < jobs_[next].due) next = i;
      }
      if (next == jobs_.size()) {
        wake_.wait(lock);
      } else if (jobs_[next].due > std::chrono::steady_clock::now()) {
        wake_.wait_until(lock, jobs_[next].due);
      } else {
        Job job = std::move(jobs_[next].job);
        jobs_.erase(jobs_.begin() + static_cast<long>(next));
        lock.unlock();
        job();
        lock.lock();
      }
    }
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopped_ = true;
    }
    wake_.notify_one();
  }

 private:
  struct Timed {
    std::chrono::steady_clock::time_point due;
    Job job;
  };
  std::mutex mutex_;
  std::condition_variable wake_;
  std::vector<Timed> jobs_;
  bool stopped_ = false;
};
