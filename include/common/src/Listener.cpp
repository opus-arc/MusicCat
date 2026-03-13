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
}

void Listener::off() {
    enabled_.store(false, std::memory_order_relaxed);
}

void Listener::run() const {
    bool last_trigger_state = false;
    bool firstRun = true;

    while (!stop_requested_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(interval_);

        if (!enabled_.load(std::memory_order_relaxed)) {
            last_trigger_state = false;
            continue;
        }

        const bool current_trigger_state = trigger_();

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
