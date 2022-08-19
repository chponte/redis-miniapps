#include <hiredis/hiredis.h>
#include <iostream>

int main(void)
{
    redisContext *c = redisConnect("127.0.0.1", 6379);
    if (c == NULL || c->err) {
        if (c) {
            std::printf("Error: %s\n", c->errstr);
            // handle error
        } else {
            std::printf("Can't allocate redis context\n");
        }
        return 1;
    }

    redisFree(c);
    std::printf("Program completed\n");

    return 0;
}
