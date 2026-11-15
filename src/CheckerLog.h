#pragma once

#include <string>

class CheckerLog
{
public:
    explicit CheckerLog(const std::string& executableDirectory);
    ~CheckerLog();

    CheckerLog(const CheckerLog&) = delete;
    CheckerLog& operator=(const CheckerLog&) = delete;
};
