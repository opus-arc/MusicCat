//
// Created by opus arc on 2026/3/10.
//

#include <Listener.h>

#include <utility>
#include <thread>

void function_placeholder() {
}

Listener::Listener(std::function<bool()> trigger,
                   std::function<void()> callback_true,
                   std::function<void()> callback_false,
                   const int intervalMs)
    : trigger_(std::move(trigger)),
      callback_true(std::move(callback_true)),
      callback_false(std::move(callback_false)),
      interval_(intervalMs > 0 ? intervalMs : 1) {
}

Listener::Listener(std::function<bool()> trigger,
                   std::function<void()> callback_true,
                   const int intervalMs)
    : trigger_(std::move(trigger)),
      callback_true(std::move(callback_true)),
      callback_false(std::move(function_placeholder)),
      interval_(intervalMs > 0 ? intervalMs : 1) {
}

Listener::~Listener() {
    stop();
}

void Listener::start() {
    if (worker_.joinable()) {
        return;
    }

    stop_requested_.store(false, std::memory_order_relaxed);
    worker_ = std::thread(&Listener::run, this);
}

void Listener::stop() {
    stop_requested_.store(true, std::memory_order_relaxed);

    if (worker_.joinable()) {
        worker_.join();
    }
}

void Listener::on() {
    enabled_.store(true, std::memory_order_relaxed);
    isRunning_ = true;
}

void Listener::off() {
    enabled_.store(false, std::memory_order_relaxed);
    isRunning_ = false;
}

bool Listener::status() const {
    return isRunning_;
}

void Listener::run() {

    bool firstRun = true;

    while (!stop_requested_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(interval_);

        if (!enabled_.load(std::memory_order_relaxed)) {
            last_trigger_state = false;
            continue;
        }

        bool current_trigger_state = trigger_();
        if (trueSignal_) {
            trueSignal_ = false;
            current_trigger_state = true;
        }
        if (falseSignal_) {
            falseSignal_ = false;
            current_trigger_state = false;
        }


        if (current_trigger_state && !last_trigger_state) {
            callback_true();
        } else if (!current_trigger_state && last_trigger_state) {
            callback_false();
        } else if (!current_trigger_state && firstRun) {
            firstRun = false;
            callback_false();
        }


        last_trigger_state = current_trigger_state;
    }
}


void Listener::setLastTrigger_true() {
    last_trigger_state = true;
}

void Listener::setLastTrigger_false() {
    last_trigger_state = false;
}

void Listener::sendTrueSignal() {
    trueSignal_ = true;
}

void Listener::sendFalseSignal() {
    falseSignal_ = true;
}



