#pragma once

#include "session.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace gateway {

class Logic;

// 逻辑工作线程：持有自己的任务队列，单线程串行处理投递到它的消息。
// 同一连接的消息按哈希路由到固定 worker，保证顺序性。
class LogicWorker {
public:
    struct Task {
        std::shared_ptr<Session> session;
        common::Packet pkt;
    };

    explicit LogicWorker(Logic* parent);
    ~LogicWorker();

    void post(Task task);

private:
    void run();

    Logic* parent_;
    std::queue<Task> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic_bool stop_{false};
    std::thread thread_;
};

}  // namespace gateway
