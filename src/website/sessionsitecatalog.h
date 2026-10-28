#pragma once

#include <QString>

class SessionSiteCatalog
{
public:
    virtual ~SessionSiteCatalog() = default;

    virtual int count() const = 0;
    virtual QString urlAt(int row) const = 0;
    virtual QString titleAt(int row) const = 0;
};
