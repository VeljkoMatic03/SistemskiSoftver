#include "emulator/terminal.hpp"

#include <unistd.h>

Terminal::Terminal() {
    tcgetattr(STDIN_FILENO, &originalTermios);
    struct termios raw = originalTermios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    reader = std::thread(&Terminal::readLoop, this);
}

Terminal::~Terminal() {
    stopRequested.store(true, std::memory_order_relaxed);
    // read() below is a blocking syscall with no portable way to interrupt short of closing
    // stdin or signaling the thread - the emulator only ever exits after halt, and any
    // keystroke still sitting in that blocking read at exit time is harmless to discard, so
    // detach rather than join to avoid hanging process exit on a read() that may never return.
    reader.detach();
    tcsetattr(STDIN_FILENO, TCSANOW, &originalTermios);
}

bool Terminal::hasPending() const {
    return pending.load(std::memory_order_acquire);
}

uint8_t Terminal::takePending() {
    pending.store(false, std::memory_order_release);
    return pendingChar.load(std::memory_order_acquire);
}

void Terminal::readLoop() {
    while (!stopRequested.load(std::memory_order_relaxed)) {
        char ch;
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n <= 0) continue;
        pendingChar.store(static_cast<uint8_t>(ch), std::memory_order_release);
        pending.store(true, std::memory_order_release);
    }
}
