#pragma once

#include <QMutex>
#include <QQuickImageProvider>
#include <QString>

class PdfDocument;

/**
 * Serves rendered PDF pages to QML as images.
 *
 * An Image whose source is "image://pdfpage/<file path>|<page>" asks for one
 * page, and the width it is given is the width QML wants it at - so a page is
 * drawn at the size it is shown, not at some fixed resolution.
 *
 * One document is kept open between requests. Pages of the same file are
 * nearly always asked for one after another, and re-opening the file for each
 * one would re-read its cross-reference table every time.
 */
class PdfImageProvider : public QQuickImageProvider
{
public:
    PdfImageProvider();
    ~PdfImageProvider() override;

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    /// Requests arrive on Qt Quick's own thread, and a second window could ask
    /// for a page while the first is still drawing one.
    QMutex m_lock;
    PdfDocument *m_open = nullptr;
    QString m_openPath;
};
