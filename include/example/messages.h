#ifndef REDIS_MINIAPP_MESSAGES_H
#define REDIS_MINIAPP_MESSAGES_H

#include <cinttypes>

namespace example
{
constexpr uint32_t maxUsernameLength = 31;
constexpr uint32_t maxMessageLength = 255;

struct UsernameBlock
{
    char username[maxUsernameLength + 1];

    UsernameBlock()
    {
        username[0] = '\0';
    }

    UsernameBlock(std::string u)
    {
        std::sprintf(username, "%s", u.c_str());
    }
};

struct MessageBlock
{
    char message[maxMessageLength + 1];

    MessageBlock()
    {
        message[0] = '\0';
    }
};
}; // namespace example

#endif