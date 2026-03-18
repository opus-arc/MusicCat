//
// Created by opus arc on 2026/3/10.
//

#ifndef MUSICCAT_LISTENER_H
#define MUSICCAT_LISTENER_H
#include <functional>
#include <thread>



class Listener {
    bool isRunning_;
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

    bool status() const;

    void setLastTrigger_false();

    void setLastTrigger_true();

    void sendTrueSignal();

    void sendFalseSignal();

private:
    void run();

    std::function<bool()> trigger_;
    std::function<void()> callback_true;
    std::function<void()> callback_false;
    std::chrono::milliseconds interval_;

    std::atomic<bool> enabled_{false};
    std::atomic<bool> stop_requested_{false};
    std::thread worker_;

    bool last_trigger_state = false;
    bool trueSignal_ = false;
    bool falseSignal_ = false;

};


#endif //MUSICCAT_LISTENER_H
