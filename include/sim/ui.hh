#ifndef SIM_UI_HH
#define SIM_UI_HH

#include <cstdint>
#include <string>
#include <chrono>
#include <deque>
#include <mutex>
#include <map>

extern "C" {
    #include <ncurses.h>
}

namespace sim {

    class ui {
    public:
        // blocks, run your simulation in a second thread and this
        // on the main thread.
        void run();
        void set_step(int64_t step);
        void log(std::string str);

    private:
        void draw_state(bool wnd = false);
        void draw_log(bool wnd = false);
        void redraw();
    private:
        enum class element {
            WND_STATE,
            WND_LOG
        };

        std::recursive_mutex mut_;
        int64_t current_step_{0};
        std::map<element, WINDOW*> wnd_;
        std::map<element, bool> dirty_;
        std::deque<std::string> loglines_;
        std::chrono::steady_clock::time_point last_draw_ {std::chrono::steady_clock::now()};
        int max_loglines_{10};
    };

}

#endif