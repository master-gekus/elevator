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
    struct one_floor_buttons
    {
        unsigned int up : 1;
        unsigned int down : 1;
        unsigned int internal : 1;
    };
    one_floor_buttons buttons_state[MAX_FLOORS_COUNT];

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
        Quit,
        UpCall,
        DownCall,
        InternalButton
    };
    struct Event {
        EventType event_;
        int param_;
    };
    std::deque<Event> equeue;
    std::mutex eqmutex;
    std::condition_variable eqcond;

    void elevator_thread_proc()
    {
#define delayed_event(to,e) \
    { \
        next_time = system_clock::now() + milliseconds(to); \
        next_event = e; \
    }
        using namespace std::chrono;
        system_clock::time_point next_time = system_clock::time_point::max();
        Event next_event{Quit};
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

            if (Quit == ev.event_) {
                break;
            }
            switch (ev.event_) {
            case UpCall:
                elog("Upward call from floor %u accepted.", ev.param_);
                buttons_state[ev.param_ - 1].up = 1;
                break;
            case DownCall:
                elog("Downward call from floor %u accepted.", ev.param_);
                buttons_state[ev.param_ - 1].down = 1;
                break;
            case InternalButton:
                elog("Internal button for floor %u accepted.", ev.param_);
                buttons_state[ev.param_ - 1].internal = 1;
                break;
            default:
                break;
            }
        }
#undef delayed_event
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
            "Interanl buttons: %s\n",
            st_header, st_up, st_down, st_int
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
        case 'U':
            number = get_floor_number(cmd + 1);
            if (0 != number) {
                send_event(Event{UpCall, number});
            }
            return true;
        case 'D':
            number = get_floor_number(cmd + 1);
            if (0 != number) {
                send_event(Event{DownCall, number});
            }
            return true;
        default:
            break;
        }

        if (isdigit(*cmd)) {
            number = get_floor_number(cmd);
            if (0 != number) {
                send_event(Event{InternalButton, number});
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

    send_event(Event{Quit});
    elevator_thread.join();
    printf("Elevator stopped, have a nice day!\n");
    return 0;
}
