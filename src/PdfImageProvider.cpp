#include "PdfImageProvider.h"
#include "PdfDocument.h"

#include <QMutexLocker>
#include <QUrl>

PdfImageProvider::PdfImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

PdfImageProvider::~PdfImageProvider()
{
    delete m_open;
}

QImage PdfImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // "<page index>/<file path>". The page comes first and the separator is a
    // slash on purpose: the id arrives exactly as it appeared in the URL,
    // percent-encoding included, so a separator that survives encoding has to
    // be one a URL already allows. The path is decoded here.
    const int slash = id.indexOf(QLatin1Char('/'));
    if (slash <= 0) {
        return QImage();
    }
    const int page = QStringView(id).left(slash).toInt();
    const QString path = QUrl::fromPercentEncoding(id.mid(slash + 1).toUtf8());

    QMutexLocker locked(&m_lock);
    if (!m_open || m_openPath != path) {
        delete m_open;
        m_open = new PdfDocument;
        m_open->setSource(path);
        m_openPath = path;
    }

    // Enough width to be readable when QML has not asked for a size yet.
    const int width = requestedSize.width() > 0 ? requestedSize.width() : 900;
    QImage image = m_open->renderPage(page, width);
    if (size) {
        *size = image.size();
    }
    return image;
}
