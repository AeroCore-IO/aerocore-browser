#ifndef MEMORYIMAGEPROVIDER_H
#define MEMORYIMAGEPROVIDER_H

#include <QQuickImageProvider>
#include <QImage>
#include "maincontroller.h"

class MemoryImageProvider : public QQuickImageProvider
{
public:
    MemoryImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

public:
    void setMainController(MainController* controller) { m_mainController = controller; }

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QImage radiusClipImage(QImage image, int radius, bool topLeft, bool topRight, bool bottomLeft, bool bottomRight, const QSize &targetSize, int fillMode);
    QImage processRoundedImage(const QString &id, const QSize &requestedSize);

private:
    MainController* m_mainController = nullptr;
};

#endif // MEMORYIMAGEPROVIDER_H
