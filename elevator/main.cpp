#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <cstdio>
#include <cstdlib>
#include <clocale>
#include <cstring>
#include <cinttypes>
#include <csignal>
#include <string>
#include <iostream>
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

    // Event queue
    enum EventType {
        Quit,
        Timer,
        PostTimer
    };
    struct Event {
        EventType event_;
        int param1_;
        int param2_;
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
        Event next_event;
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
            if (Timer == ev.event_) {
                printf("%s: Timer received!\n", __func__);
                delayed_event(1000, Event{PostTimer});
            } else if (PostTimer == ev.event_) {
                printf("%s: PostTimer received!\n", __func__);
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

    void sighandler(int)
    {
        printf("\nStop signal was catched. Stopping.\n");
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

    void display_help(const char *argv)
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

    if (!parse_command_line(argc, argv)) {
        display_help(argv[0]);
        return 1;
    }

    printf("Starting elevator with following parameters:\n"
           "  Floors count      : %d\n"
           "  Intrefloor timeout: %d ms\n"
           "  Door close timeout: %d ms\n",
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
        printf("CMD>");
        fflush(stdout);
        std::string command;
        if ((0 != (std::getline(std::cin, command).rdstate()
                   & (std::ios_base::badbit | std::ios_base::eofbit | std::ios_base::failbit)))
            || term_sig_received) {
            break;
        }
        if ("t" == command) {
            send_event(Event{Timer});
        }
    }

    send_event(Event{Quit});
    elevator_thread.join();
    return 0;
}
