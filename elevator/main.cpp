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

    while (true) {
        printf("CMD>");
        fflush(stdout);
        std::string command;
        getline(std::cin, command);
        if (term_sig_received) {
            break;
        }
    }

    return 0;
}
