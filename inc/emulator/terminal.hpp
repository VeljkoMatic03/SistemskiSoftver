#ifndef EMULATOR_TERMINAL_HPP
#define EMULATOR_TERMINAL_HPP

#include <atomic>
#include <cstdint>
#include <termios.h>
#include <thread>

// reads one raw keystroke at a time from stdin (non-canonical, no echo) on a background
// thread and stashes it
// the main emulation loop polls hasPending()/takePending()
// between instructions (never mid-instruction, per spec); this
// class only ever talks to the rest of the emulator through the two atomics below, so no
// locking is needed anywhere
class Terminal {
public:
    Terminal();
    ~Terminal();
    Terminal(const Terminal&) = delete;

    bool hasPending() const;
    uint8_t takePending(); // clears the pending flag

private:
    void readLoop();

    std::atomic<bool> pending{false};
    std::atomic<uint8_t> pendingChar{0};
    std::atomic<bool> stopRequested{false};
    struct termios originalTermios;
    std::thread reader;
};

#endif // EMULATOR_TERMINAL_HPP
