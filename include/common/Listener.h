//
// Created by opus arc on 2026/3/10.
//

#ifndef MUSICCAT_LISTENER_H
#define MUSICCAT_LISTENER_H
#include <functional>
#include <thread>



class Listener {
public:
    Listener(std::function<bool()> trigger,
             std::function<void()> callback_true,
             std::function<void()> callback_false,
             int intervalMs = 200);

    Listener(std::function<bool()> trigger,
             std::function<void()> callback_true,
             int intervalMs);


    ~Listener();

    void start();

    void stop();

    void on();

    void off();

private:
    void run() const;

    std::function<bool()> trigger_;
    std::function<void()> callback_true;
    std::function<void()> callback_false;
    std::chrono::milliseconds interval_;

    std::atomic<bool> enabled_{false};
    std::atomic<bool> stop_requested_{false};
    std::thread worker_;
};


#endif //MUSICCAT_LISTENER_H
