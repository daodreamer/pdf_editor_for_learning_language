#pragma once
#include <QList>
#include <QtGlobal>
#include <algorithm>
#include <limits>

// Page states and order of the whole-book background OCR. Pure UI-thread bookkeeping: no I/O,
// no timers. ShellState decides *whether* a background step may run (foreground work, storage,
// export); this class answers *which* page and whether the user has been idle long enough.
class OcrBookQueue {
public:
    enum class State : quint8 { Unknown, Native, Done, Busy, Failed };
    void reset(int pages);           // every page Unknown, not ready
    void setReady(bool ready) { ready_ = ready; }
    bool ready() const { return ready_; }
    int pages() const { return int(states_.size()); }
    State state(int page) const { return page >= 0 && page < pages() ? states_[page] : State::Failed; }
    bool isDone(int page) const { return state(page) == State::Done; }
    void set(int page, State state);
    // Busy -> Unknown: a step that was cancelled or discarded goes back into the queue.
    void release(int page) { if (state(page) == State::Busy) set(page, State::Unknown); }
    // Nearest Unknown page to `current`; equal distance prefers the following page. -1 if none.
    int next(int current) const;
    int done() const { return done_; }      // OCR pages with a stored result
    int native() const { return native_; }  // pages skipped for their text layer
    int failed() const { return failed_; }
    bool complete() const { return ready_ && pages() > 0 && done_ + native_ + failed_ == pages(); }

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    void setPaused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }
    void noteInteraction(qint64 nowMs) { lastInteraction_ = nowMs; }
    // Milliseconds until the idle period is over (0 when idle now).
    qint64 idleRemaining(qint64 nowMs, qint64 idleMs) const {
        return std::max<qint64>(0, lastInteraction_ + idleMs - nowMs);
    }
    bool active() const { return enabled_ && !paused_ && ready_ && !complete(); }
private:
    QList<State> states_;
    int done_ = 0, native_ = 0, failed_ = 0;
    bool ready_ = false, enabled_ = false, paused_ = false;
    qint64 lastInteraction_ = std::numeric_limits<qint64>::min() / 2;
};
