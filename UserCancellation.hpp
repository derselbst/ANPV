
#pragma once

#include <stdexcept>

class UserCancellation : public std::exception
{
public:
    UserCancellation() = default;
    ~UserCancellation() override = default;
}; 
