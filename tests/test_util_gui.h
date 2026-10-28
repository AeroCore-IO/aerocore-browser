#pragma once

#include <QApplication>
#include <QGuiApplication>
#include <QTest>
#include <QtGlobal>

#define QTEST_GUI_MAIN(TestObject) \
    int main(int argc, char *argv[]) \
    { \
        if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) { \
            qputenv("QT_QPA_PLATFORM", "offscreen"); \
        } \
        QGuiApplication app(argc, argv); \
        TestObject testObject; \
        return QTest::qExec(&testObject, argc, argv); }

#define QTEST_WIDGET_MAIN(TestObject) \
    int main(int argc, char *argv[]) \
    { \
        if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) { \
            qputenv("QT_QPA_PLATFORM", "offscreen"); \
        } \
        QApplication app(argc, argv); \
        TestObject testObject; \
        return QTest::qExec(&testObject, argc, argv); }
