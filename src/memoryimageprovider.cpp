#include "memoryimageprovider.h"
#include <QPainter>
#include <QPainterPath>

QImage MemoryImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    (void)requestedSize;

    QImage retImage;

    if (m_mainController == nullptr)
    {
        return retImage;
    }

    if (id.indexOf("cursorShape") == 0)
    {
       retImage = m_mainController->getCursorShapeImage();
    }
    else if (id.indexOf("roundedImage:") == 0)
    {
        retImage = processRoundedImage(id, requestedSize);
    }

    if (!retImage.isNull() && size)
    {
        *size = retImage.size();
    }

    return retImage;
}

QImage MemoryImageProvider::radiusClipImage(QImage image, int radius, bool topLeft, bool topRight, bool bottomLeft, bool bottomRight, const QSize &targetSize, int fillMode)
{
    if (image.isNull()) return image;

    if (targetSize.isValid() && !targetSize.isEmpty()) {
        Qt::AspectRatioMode aspectMode;
        switch (fillMode) {
        case 1:  // Image.PreserveAspectFit
            aspectMode = Qt::KeepAspectRatio;
            break;
        case 2:  // Image.PreserveAspectCrop
            aspectMode = Qt::KeepAspectRatioByExpanding;
            break;
        default: // Image.Stretch
            aspectMode = Qt::IgnoreAspectRatio;
            break;
        }
        image = image.scaled(targetSize, aspectMode, Qt::SmoothTransformation);

        if (fillMode == 2) {
            int x = (image.width() - targetSize.width()) / 2;
            int y = (image.height() - targetSize.height()) / 2;
            image = image.copy(x, y, targetSize.width(), targetSize.height());
        }
    }

    QImage result(image.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QPainterPath path;
    QRectF rect = image.rect();
    qreal r = qMin(static_cast<qreal>(radius), qMin(rect.width(), rect.height()) / 2.0);

    path.moveTo(rect.left() + r, rect.top());

    path.lineTo(rect.right() - r, rect.top());

    if (topRight) {
        path.arcTo(rect.right() - 2 * r, rect.top(), 2 * r, 2 * r, 90, -90);
    } else {
        path.lineTo(rect.right(), rect.top());
        path.lineTo(rect.right(), rect.top() + r);
    }

    path.lineTo(rect.right(), rect.bottom() - r);

    if (bottomRight) {
        path.arcTo(rect.right() - 2 * r, rect.bottom() - 2 * r, 2 * r, 2 * r, 0, -90);
    } else {
        path.lineTo(rect.right(), rect.bottom());
        path.lineTo(rect.right() - r, rect.bottom());
    }

    path.lineTo(rect.left() + r, rect.bottom());

    if (bottomLeft) {
        path.arcTo(rect.left(), rect.bottom() - 2 * r, 2 * r, 2 * r, 270, -90);
    } else {
        path.lineTo(rect.left(), rect.bottom());
        path.lineTo(rect.left(), rect.bottom() - r);
    }

    path.lineTo(rect.left(), rect.top() + r);

    if (topLeft) {
        path.arcTo(rect.left(), rect.top(), 2 * r, 2 * r, 180, -90);
    } else {
        path.lineTo(rect.left(), rect.top());
        path.lineTo(rect.left() + r, rect.top());
    }

    path.closeSubpath();
    painter.setClipPath(path);
    painter.drawImage(0, 0, image);

    return result;
}

QImage MemoryImageProvider::processRoundedImage(const QString &id, const QSize &requestedSize)
{
    QString params = id.mid(QString("roundedImage:").length());

    QStringList parts = params.split('?');
    QString filePath = parts[0];
    filePath.remove("file://");

    int radius = 10;
    bool tl = true, tr = true, bl = true, br = true;
    int fillMode = 0; // 0: Stretch, 1: PreserveAspectFit, 2: PreserveAspectCrop
    QSize targetSize = requestedSize;

    if (parts.size() > 1) {
        QStringList queryParts = parts[1].split('&');
        for (const QString &part : queryParts) {
            QStringList kv = part.split('=');
            if (kv.size() == 2) {
                if (kv[0] == "radius") radius = kv[1].toInt();
                else if (kv[0] == "tl") tl = kv[1].toInt() == 1;
                else if (kv[0] == "tr") tr = kv[1].toInt() == 1;
                else if (kv[0] == "bl") bl = kv[1].toInt() == 1;
                else if (kv[0] == "br") br = kv[1].toInt() == 1;
                else if (kv[0] == "fillMode") fillMode = kv[1].toInt();
                else if (kv[0] == "w") targetSize.setWidth(kv[1].toInt());
                else if (kv[0] == "h") targetSize.setHeight(kv[1].toInt());
            }
        }
    }

    QImage sourceImage(filePath);
    if (sourceImage.isNull()) return QImage();

    return radiusClipImage(sourceImage, radius, tl, tr, bl, br, targetSize, fillMode);
}
