#include "logic_worker.h"

#include "logic.h"

#include <utility>

namespace gateway {

LogicWorker::LogicWorker(Logic* parent) : parent_(parent) {
    thread_ = std::thread(&LogicWorker::run, this);
}

LogicWorker::~LogicWorker() {
    stop_ = true;
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void LogicWorker::post(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(task));
    }
    cv_.notify_one();
}

void LogicWorker::run() {
    while (!stop_.load()) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_.load() || !queue_.empty(); });
            if (stop_.load()) {
                break;
            }
            task = std::move(queue_.front());
            queue_.pop();
        }
        parent_->dispatch(std::move(task.session), std::move(task.pkt));
    }
}

}  // namespace gateway
