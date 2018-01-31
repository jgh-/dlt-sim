#include <cinttypes>

#include "sim/ui.hh"
#include "sim/log.hh"

extern "C" {
    WINDOW *create_newwin(int height, int width, int starty, int startx);
    void destroy_win(WINDOW *local_win);
}
namespace sim {
    
    //ui::ui() noexcept {};

    void
    ui::run() {
        initscr();          /* Start curses mode          */
        attron(A_BOLD);
        printw(" F10 to exit");  /* Print Hello World          */
        attroff(A_BOLD);
        refresh();          /* Print it on to the real screen */
        int ch{};
        keypad(stdscr, TRUE);
        draw_state(true);
        draw_log(true);
        bool running {true};
        std::thread updater([&]() {
            while(running) {
                redraw();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });

        while((ch = getch()) != KEY_F(10)) {}
        running = false;
        updater.join();
        endwin();           /* End curses mode        */

    }

    void
    ui::set_step(int64_t step) {
        std::unique_lock<std::recursive_mutex> mut_;
        current_step_ = step;
        dirty_[element::WND_STATE] = true;
    }

    void
    ui::log(std::string str) {
        std::unique_lock<std::recursive_mutex> mut_;
        loglines_.push_back(str);
        if(loglines_.size() > max_loglines_) {
            loglines_.pop_front();
        }
        dirty_[element::WND_LOG] = true;
    }

    void
    ui::draw_state(bool wnd) {
        std::unique_lock<std::recursive_mutex> mut_;

        const int h = 6;
        const int w = 25;
        const int x = 1;
        const int y = 1;

        auto it = wnd_.find(element::WND_STATE);
        auto iit = dirty_.find(element::WND_STATE);
        bool dirty = (iit == dirty_.end()) ? false : iit->second;
        wnd |= it == wnd_.end();

        // (re)Draw window if needed
        if(wnd) {
            if(it != wnd_.end()) {
                destroy_win(it->second);
                wnd_.erase(it);
            }

            wnd_.emplace(element::WND_STATE, create_newwin(h,w,y,x));
            it = wnd_.find(element::WND_STATE);
            dirty = true;
        } else {
            if(dirty) {
                wclear(it->second);
            }
        }

        // (re)Draw text if needed
        if(dirty) {
            mvprintw(y+2, x+1, "step: %" PRId64, current_step_);
            dirty_[element::WND_STATE] = false;
        }
    }

    void
    ui::draw_log(bool wnd) {
        std::unique_lock<std::recursive_mutex> mut_;
        
        int maxx, maxy;
        getmaxyx(stdscr,maxy,maxx);

        const int h = maxy / 2 - 1;
        const int w = maxx - 2;
        const int x = 1;
        int y = maxy / 2;

        auto iit = dirty_.find(element::WND_STATE);
        bool dirty = (iit == dirty_.end()) ? false : iit->second;
        auto it = wnd_.find(element::WND_LOG);
        wnd |= it == wnd_.end();

        // (re)Draw window if needed
        if(wnd) {
            if(it != wnd_.end()) {
                destroy_win(it->second);
                wnd_.erase(it);
            }

            max_loglines_ = maxy / 2 - 4;
            wnd_.emplace(element::WND_LOG, create_newwin(h,w,y,x));
        } else {
            if(dirty) {
                wclear(it->second);
            }
        }
        
        // (re)Draw text if needed
        if(dirty) {
            for(auto& it : loglines_) {
                mvprintw(++y, x+1, "%s", it.c_str());
            }
            dirty_[element::WND_LOG] = false;
        }
    }

    void
    ui::redraw() 
    {
        std::unique_lock<std::recursive_mutex> mut_;
        auto now = std::chrono::steady_clock::now();
        if(now - last_draw_ >= std::chrono::milliseconds(33)) {
            draw_log();
            draw_state();
            refresh();
            last_draw_ = now;
        }
    }
}


//
// utility, from http://tldp.org/HOWTO/NCURSES-Programming-HOWTO/windows.html#LETBEWINDOW
//
extern "C" {
    WINDOW *create_newwin(int height, int width, int starty, int startx)
    {   WINDOW *local_win;

        local_win = newwin(height, width, starty, startx);
        box(local_win, 0 , 0);      /* 0, 0 gives default characters 
                         * for the vertical and horizontal
                         * lines            */
        wrefresh(local_win);        /* Show that box        */

        return local_win;
    }

    void destroy_win(WINDOW *local_win)
    {   
        /* box(local_win, ' ', ' '); : This won't produce the desired
         * result of erasing the window. It will leave it's four corners 
         * and so an ugly remnant of window. 
         */
        wborder(local_win, ' ', ' ', ' ',' ',' ',' ',' ',' ');
        /* The parameters taken are 
         * 1. win: the window on which to operate
         * 2. ls: character to be used for the left side of the window 
         * 3. rs: character to be used for the right side of the window 
         * 4. ts: character to be used for the top side of the window 
         * 5. bs: character to be used for the bottom side of the window 
         * 6. tl: character to be used for the top left corner of the window 
         * 7. tr: character to be used for the top right corner of the window 
         * 8. bl: character to be used for the bottom left corner of the window 
         * 9. br: character to be used for the bottom right corner of the window
         */
        wrefresh(local_win);
        delwin(local_win);
    }
}