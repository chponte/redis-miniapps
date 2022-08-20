#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>

#include <hiredis/hiredis.h>

std::atomic<bool> terminate = false;

void sigint_handler(int signal) { terminate = true; }

redisContext *setupContext(char *address, char *port)
{
    redisContext *c = redisConnect(address, std::atoi(port));
    if (c == nullptr || c->err) {
        if (c) {
            std::printf("Error: %s\n", c->errstr);
            // handle error
        } else {
            std::printf("Can't allocate redis context\n");
        }
        exit(1);
    }
    return c;
}

void getMessages() {}

void saveMessage(const std::string &msg) {}

/*
Arguments: ip, port, username

*/
int main(int argc, char **argv)
{
    if (argc < 4) {
        std::printf("Arguments: IP PORT USERNAME\n");
        exit(1);
    }

    std::signal(SIGINT, sigint_handler);

    redisContext *c = setupContext(argv[1], argv[2]);

    redisReply *reply;

    getMessages();

    while (!terminate) {
        std::string msg;
        std::getline(std::cin, msg);
        saveMessage(msg);
        std::cout << msg << "\n";
    }

    redisFree(c);

    return 0;
}
