#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

/**
 * Renders pages of a PDF, through PDFium.
 *
 * PDFium is loaded at run time rather than linked, for two reasons. The
 * project is built with three compilers and the prebuilt library ships only an
 * MSVC import library, which a dynamic load does not need. And a missing or
 * unloadable library then costs nothing: isAvailable() turns false and the
 * preview window shows what it shows for any other file it cannot display.
 *
 * Only the dozen C functions this needs are declared, in the .cpp. Pulling in
 * PDFium's headers would mean shipping them and keeping them in step with the
 * binary; the C API below has been stable for a decade.
 */
class PdfDocument : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// The file being shown. Setting it opens the document.
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY documentChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY documentChanged)
    /// Empty while the document is fine; the reason it cannot be shown if not.
    Q_PROPERTY(QString error READ error NOTIFY documentChanged)

public:
    explicit PdfDocument(QObject *parent = nullptr);
    ~PdfDocument() override;

    /// True when the PDFium library was found and loaded. Checked by the
    /// preview window before it offers a PDF view at all.
    static bool isAvailable();

    QString source() const { return m_source; }
    void setSource(const QString &source);
    int pageCount() const { return m_pageCount; }
    QString error() const { return m_error; }

    /// The page at the width asked for, height following the page's own
    /// proportions. An empty image if the page cannot be rendered.
    QImage renderPage(int index, int width) const;
    /// Width divided by height, for laying a page out before it is rendered.
    Q_INVOKABLE qreal pageAspect(int index) const;

signals:
    void documentChanged();

private:
    void close();

    QString m_source;
    QString m_error;
    int m_pageCount = 0;
    /// FPDF_DOCUMENT, kept as void * so this header stays free of PDFium.
    void *m_doc = nullptr;
};
