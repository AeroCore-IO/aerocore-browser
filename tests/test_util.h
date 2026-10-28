#pragma once

#include <QCoreApplication>
#include <QTest>

#define QTEST_CORE_MAIN(TestObject) \
    int main(int argc, char *argv[]) \
    { \
        QCoreApplication app(argc, argv); \
        TestObject testObject; \
        return QTest::qExec(&testObject, argc, argv); \
    }
