#pragma once
#include <string>
#include <cstdint>

enum class Command : uint8_t { PUT = 0, DELETE = 1 };

struct LSMEntry {
    std::string key;
    std::string value;
    Command     command;
    int64_t     timestamp;
};
