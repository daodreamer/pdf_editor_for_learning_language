#include "ocrbookqueue.h"

void OcrBookQueue::reset(int pages) {
    states_ = QList<State>(std::max(0, pages), State::Unknown);
    done_ = native_ = failed_ = 0;
    ready_ = false;
}
void OcrBookQueue::set(int page, State state) {
    if (page < 0 || page >= pages()) return;
    auto& current = states_[page];
    const auto count = [this](State s, int delta) {
        if (s == State::Done) done_ += delta;
        else if (s == State::Native) native_ += delta;
        else if (s == State::Failed) failed_ += delta;
    };
    count(current, -1);
    current = state;
    count(current, 1);
}
int OcrBookQueue::next(int current) const {
    const int n = pages();
    if (n == 0) return -1;
    current = std::clamp(current, 0, n - 1);
    for (int distance = 0; distance < n; ++distance) {
        if (current + distance < n && states_[current + distance] == State::Unknown) return current + distance;
        if (distance > 0 && current - distance >= 0 && states_[current - distance] == State::Unknown) return current - distance;
        if (current + distance >= n && current - distance < 0) break;
    }
    return -1;
}
