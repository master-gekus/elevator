#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <clocale>
#include <cstring>
#include <cinttypes>
#include <csignal>
#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <deque>
#include <chrono>
#include <cassert>

#define MIN_FLOORS_COUNT    5
#define MAX_FLOORS_COUNT    20
#define MIN_FLOOR_HEIGHT    2.0
#define MAX_FLOOR_HEIGHT    10.0
#define MIN_ELEVATOR_SPEED  0.1
#define MAX_ELEVATOR_SPEED  10.0
#define MIN_DOOR_OPEN_TIME  0.5
#define MAX_DOOR_OPEN_TIME  120.0

namespace {
    bool term_sig_received = false;
    int floor_count;
    int floor_timeout;
    int door_timeout;
    enum {MovingUp, MovingDown, StandBy} elevator_state = StandBy;
    enum {DoorOpen, DoorsClosed} doors_state = DoorsClosed;
    int current_floor = 1;
    struct
    {
        uint8_t up;
        uint8_t down;
        uint8_t internal;
        inline bool is_any() const
        {
            return (0 != up) || (0 != down) || (0 != internal);
        }
        inline bool need_stop(bool has_up_calls, bool has_down_calls) const
        {
            if ((!has_up_calls) && (!has_down_calls)) {
                return true;
            }
            if (0 != internal) {
                return true;
            }
            if (MovingUp == elevator_state) {
                if ((0 != up) || ((!has_up_calls) && (0 != down))) {
                    return true;
                }
            }
            if (MovingDown == elevator_state) {
                if ((0 != down) || ((!has_down_calls) && (0 != up))) {
                    return true;
                }
            }
            return false;
        }
        inline void drop_all()
        {
            up = down = internal = 0;
        }
        inline void drop_by_direction()
        {
            internal = 0;
            if (MovingUp == elevator_state) {
                up = 0;
            }
            if (MovingDown == elevator_state) {
                down = 0;
            }
        }
    } buttons_state[MAX_FLOORS_COUNT];

    void
#ifdef __GNUC__
    __attribute__ ((format (printf, 1, 2)))
#endif
    lprintf(const char* format, ...)
    {
        static std::mutex out_mutex;
        va_list list;
        va_start(list, format);
        out_mutex.lock();
        vprintf(format, list);
        out_mutex.unlock();
        va_end(list);
    }

    std::string time_for_log()
    {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto time = system_clock::to_time_t(now);
        auto ms = duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) -
                  duration_cast<std::chrono::seconds>(now.time_since_epoch());

        std::stringstream stream;
        stream << std::put_time(std::localtime(&time), "%Y/%m/%d %H:%M:%S.");
        stream << std::setfill('0') << std::setw(3) << ms.count();
        return stream.str();
    }
#define elog(fmt,...) lprintf("%s: " fmt "\n", time_for_log().c_str(), ##__VA_ARGS__)

    // Event queue
    enum EventType {
        QuitEvent,
        UpCallEvent,
        DownCallEvent,
        InternalButtonEvent,
        DoorsClosedEvent,
        FloorReachedEvent,
    };
    struct Event {
        EventType event_;
        int param_;
    };
    std::deque<Event> equeue;
    std::mutex eqmutex;
    std::condition_variable eqcond;
    std::chrono::system_clock::time_point next_time = std::chrono::system_clock::time_point::max();
    Event next_event{QuitEvent};

    void delayed_event(unsigned int time_out, const Event& event)
    {
        next_time = std::chrono::system_clock::now() + std::chrono::milliseconds(time_out);
        next_event = event;
    }

    void open_doors()
    {
        if (DoorOpen == doors_state) {
            return;
        }
        elog("Open doors on floor %u", current_floor);
        doors_state = DoorOpen;
        delayed_event(door_timeout, Event{DoorsClosedEvent});
    }

    void start_moving(bool log_start)
    {
        if (StandBy == elevator_state) {
            return;
        }
        if (log_start) {
            elog("Start moving from floor %u", current_floor);
        }
        delayed_event(floor_timeout, Event{FloorReachedEvent});
    }

    void start_moving_to_floor(int floor)
    {
        if (StandBy == elevator_state) {
            elevator_state = (floor < current_floor) ? MovingDown : MovingUp;
            if (DoorsClosed != doors_state) {
                return;
            }
        }
        start_moving(true);
    }

    void process_button(uint8_t& btn_state, int floor)
    {
        btn_state = 1;
        if (StandBy != elevator_state) {
            return;
        }
        if (current_floor == floor) {
            btn_state = 0;
            open_doors();
        } else {
            start_moving_to_floor(floor);
        }
    }

    void process_doors_closed()
    {
        elog("Doors was closed on floor %d", current_floor);
        doors_state = DoorsClosed;
        start_moving(true);
    }

    void process_floor_reached()
    {
        if (MovingUp == elevator_state) {
            current_floor++;
        } else {
            assert(MovingDown == elevator_state);
            current_floor--;
        }
        assert((current_floor > 0) && (current_floor <= floor_count));

        bool has_up_calls = false;
        for (int i = current_floor; i < floor_count; i++) {
            if (buttons_state[i].is_any()) {
                has_up_calls = true;
                break;
            }
        }

        bool has_down_calls = false;
        for (int i = current_floor - 2; i >= 0; i--) {
            if (buttons_state[i].is_any()) {
                has_down_calls = true;
                break;
            }
        }

        if (buttons_state[current_floor - 1].need_stop(has_up_calls, has_down_calls)) {
            elog("Elevator reached floor %u", current_floor);
            if ((!has_up_calls) && (!has_down_calls)) {
                assert(buttons_state[current_floor - 1].is_any());
                buttons_state[current_floor - 1].drop_all();
                elevator_state = StandBy;
            } else {
                if ((MovingUp == elevator_state) && (!has_up_calls)) {
                    elevator_state = MovingDown;
                    buttons_state[current_floor - 1].drop_all();
                } else if ((MovingDown == elevator_state) && (!has_down_calls)) {
                    elevator_state = MovingUp;
                    buttons_state[current_floor - 1].drop_all();
                } else {
                    buttons_state[current_floor - 1].drop_by_direction();
                }
            }
            open_doors();
        } else {
            elog("Elevator passes through floor %u", current_floor);
            start_moving(false);
        }
    }

    void elevator_thread_proc()
    {
        using namespace std::chrono;
        while (true) {
            Event ev;
            std::unique_lock<std::mutex> lock(eqmutex);
            bool timed_out = false;
            if (system_clock::time_point::max() == next_time) {
                eqcond.wait(lock, [](){ return !equeue.empty();});
            } else {
                timed_out = !eqcond.wait_until(lock, next_time, [](){ return !equeue.empty();});
            }
            if (timed_out) {
                next_time = system_clock::time_point::max();
                ev = next_event;
            } else {
                ev = equeue.back();
                equeue.pop_back();
            }
            lock.unlock();

            if (QuitEvent == ev.event_) {
                break;
            }
            switch (ev.event_) {
            case UpCallEvent:
                process_button(buttons_state[ev.param_ - 1].up, ev.param_);
                break;
            case DownCallEvent:
                process_button(buttons_state[ev.param_ - 1].down, ev.param_);
                break;
            case InternalButtonEvent:
                process_button(buttons_state[ev.param_ - 1].internal, ev.param_);
                break;
            case DoorsClosedEvent:
                process_doors_closed();
                break;
            case FloorReachedEvent:
                process_floor_reached();
                break;
            default:
                break;
            }
        }
    }

    void send_event(const Event& ev)
    {
        eqmutex.lock();
        equeue.push_front(ev);
        eqmutex.unlock();
        eqcond.notify_one();
    }

    void display_elevator_state()
    {
        auto state = buttons_state;
        char st_header[MAX_FLOORS_COUNT * 3 + 1];
        char st_up[MAX_FLOORS_COUNT * 3 + 1];
        char st_down[MAX_FLOORS_COUNT * 3 + 1];
        char st_int[MAX_FLOORS_COUNT * 3 + 1];
        for (int i = 0; i < floor_count; i++) {
            sprintf(&(st_header[i * 3]), "%2u ", i + 1);
            memcpy(&(st_up[i * 3]), state[i].up ? " ^ " : "   ", 3);
            memcpy(&(st_down[i * 3]), state[i].down ? " v " : "   ", 3);
            memcpy(&(st_int[i * 3]), state[i].internal ? " * " : "   ", 3);
        }
        st_up[floor_count * 3] = '\0';
        st_down[floor_count * 3] = '\0';
        st_int[floor_count * 3] = '\0';
        lprintf(
            "                  %s\n"
            "Calls upward:     %s\n"
            "Calls downward:   %s\n"
            "Interanl buttons: %s\n"
            "Elevator %s floor %u%s.\n",
            st_header, st_up, st_down, st_int,
            ((DoorOpen == doors_state) || (StandBy == elevator_state)) ? "stays on" :
            (MovingUp == elevator_state) ? "moves up from" : "moves down from",
            current_floor,
            (DoorOpen == doors_state) ? "; doors are open" : ""
        );
    }

    int get_floor_number(const char *str)
    {
        int number = static_cast<int>(strtol(str, nullptr, 0));
        if ((1 > number) || (floor_count < number)) {
            lprintf("Invalid floor number: %s\n", str);
            return 0;
        }
        return number;
    }

    void display_commands_help()
    {
        lprintf(
            "Avaible commands:\n"
            "  ? - display this text\n"
            "  Q - quit program\n"
            "  S - display elevator and elevator's buttons status\n"
            "  U<number> - press 'Call Up' button on floor <number>\n"
            "  D<number> - press 'Call Down' button on floor <number>\n"
            "  <number> - press button <number> inside elevator\n"
        );
    }

    bool process_command(const char* cmd)
    {
        while (isspace(*cmd)) {
            ++cmd;
        }
        if ('\0' == (*cmd)) {
            return true;
        }
        int number = 0;
        switch (toupper(*cmd)) {
        case 'Q':
            lprintf("Stopping elevator...\n");
            return false;
        case 'S':
            display_elevator_state();
            return true;
        case '?':
            display_commands_help();
            return true;
        case 'U':
            number = get_floor_number(cmd + 1);
            if (0 != number) {
                if (floor_count == number) {
                    lprintf("No \"Up\" button on last floor.\n");
                } else {
                    send_event(Event{UpCallEvent, number});
                }
            }
            return true;
        case 'D':
            number = get_floor_number(cmd + 1);
            if (0 != number) {
                if (1 == number) {
                    lprintf("No \"Down\" button on first floor.\n");
                } else {
                    send_event(Event{DownCallEvent, number});
                }
            }
            return true;
        default:
            break;
        }

        if (isdigit(*cmd)) {
            number = get_floor_number(cmd);
            if (0 != number) {
                send_event(Event{InternalButtonEvent, number});
            }
            return true;
        }

        lprintf("Unrecognized command \"%c\". Type \"?\" for list of avaible commands.\n", *cmd);
        return true;
    }

    void sighandler(int)
    {
        lprintf("\nStop signal was catched. Stopping.\n");
        term_sig_received = true;
    }

    bool parse_command_line(int argc, char *argv[])
    {
        if (5 != argc) {
            fprintf(stderr, "Command line error: Invalid argument count.\n");
            return false;
        }

        floor_count = static_cast<int>(strtol(argv[1], nullptr, 0));
        if ((MIN_FLOORS_COUNT > floor_count) || (MAX_FLOORS_COUNT < floor_count)) {
            fprintf(stderr, "Command line error: Invalid number of floors.\n");
            return false;
        }

        float floor_height = strtof(argv[2], nullptr);
        if ((MIN_FLOOR_HEIGHT > floor_height) || (MAX_FLOOR_HEIGHT < floor_height)) {
            fprintf(stderr, "Command line error: Invalid floor height.\n");
            return false;
        }

        float elevator_speed = strtof(argv[3], nullptr);
        if ((MIN_ELEVATOR_SPEED > elevator_speed) || (MAX_ELEVATOR_SPEED < elevator_speed)) {
            fprintf(stderr, "Command line error: Invalid elevator speed.\n");
            return false;
        }

        float door_open_time = strtof(argv[4], nullptr);
        if ((MIN_DOOR_OPEN_TIME > door_open_time) || (MAX_DOOR_OPEN_TIME < door_open_time)) {
            fprintf(stderr, "Command line error: Invalid door open time.\n");
            return false;
        }

        floor_timeout = static_cast<int>((floor_height * 1000.0 / elevator_speed) + 0.5);
        door_timeout = static_cast<int>((door_open_time * 1000.0) + 0.5);

        return true;
    }

    void display_command_line_help(const char *argv)
    {
        const char *name = strrchr(argv,
#ifdef _WIN32
                                   '\\'
#else
                                   '/'
#endif
                                   );
        if (nullptr == name) {
            name = argv;
        } else {
            name++;
        }
        printf(
            "Usage:\n"
            "  %s <floors_count> <floor_height> <elevator_speed> <doors_open_time>\n\n"
            "where:\n"
            "  <floors_count>    - number of floors; integer from %d to %d.\n\n"
            "  <floor_height>    - floor height in meters; decimal from %.1f to %.1f.\n\n"
            "  <elevator_speed>  - elevator_speed in meters per secons; decimal from\n"
            "                      %.1f to %.1f.\n\n"
            "  <doors_open_time> - time between opening doors and closing them in\n"
            "                      seconds; decimal from %.1f to %.1f.\n"
        , name,
        MIN_FLOORS_COUNT, MAX_FLOORS_COUNT,
        MIN_FLOOR_HEIGHT, MAX_FLOOR_HEIGHT,
        MIN_ELEVATOR_SPEED, MAX_ELEVATOR_SPEED,
        MIN_DOOR_OPEN_TIME, MAX_DOOR_OPEN_TIME);
    }
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "C");
    memset(&buttons_state, 0, sizeof(buttons_state));

    if (!parse_command_line(argc, argv)) {
        display_command_line_help(argv[0]);
        return 1;
    }

    printf("Starting elevator with following parameters:\n"
           "  Floors count      : %d\n"
           "  Intrefloor timeout: %d ms\n"
           "  Door close timeout: %d ms\n"
           "Type a command or \"?\" to list of avaible commands.\n",
           floor_count, floor_timeout, door_timeout);

#ifdef _WIN32
    signal(SIGINT, sighandler);
#else
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = sighandler;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGINT);
    sigaction(SIGINT, &action, nullptr);
#endif

    std::thread elevator_thread(elevator_thread_proc);

    while (true) {
        std::string command;
        if ((0 != (std::getline(std::cin, command).rdstate()
                   & (std::ios_base::badbit | std::ios_base::eofbit | std::ios_base::failbit)))
            || term_sig_received || (!process_command(command.c_str()))) {
            break;
        }
    }

    send_event(Event{QuitEvent});
    elevator_thread.join();
    printf("Elevator stopped, have a nice day!\n");
    return 0;
}
