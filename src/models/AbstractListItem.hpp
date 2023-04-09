
#pragma once

#include "types.hpp"

#include <memory>

class AbstractListItem
{
public:
    AbstractListItem(ListItemType type);
    virtual ~AbstractListItem();
    
    virtual QString getName() const = 0;
    ListItemType getType() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
