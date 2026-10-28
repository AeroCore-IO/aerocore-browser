#pragma once

#include <QList>
#include <QString>

struct PresetWebsite
{
    QString name;
    QString url;
    QString iconFileName;
};

QList<PresetWebsite> builtInPresetWebsites();
