#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QVariantList>
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
 * Only the C functions this needs are declared, in the .cpp. Pulling in
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
    /// This PDFium build can read the text on a page, which selecting it needs.
    Q_PROPERTY(bool textSelectable READ textSelectable CONSTANT)

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

    // ---- the text on a page, for selecting and copying it ----
    // Positions are fractions of the page as shown, 0 to 1 from its top left,
    // so they hold at any zoom. A character is its index on its own page.

    static bool textSelectable();
    /// How many characters the page has; 0 if its text cannot be read.
    Q_INVOKABLE int charCount(int page) const;
    /// The character at a point, or the nearest one when the point falls
    /// between words or past the end of a line. -1 if the page has no text.
    Q_INVOKABLE int charAt(int page, qreal x, qreal y) const;
    /// What to highlight for the characters from..to, both included: one band
    /// per line, from its first of them to its last.
    Q_INVOKABLE QVariantList textRects(int page, int from, int to) const;
    /// The text of the characters from..to, both included.
    Q_INVOKABLE QString text(int page, int from, int to) const;
    Q_INVOKABLE void copyToClipboard(const QString &text) const;

signals:
    void documentChanged();

private:
    void close();
    bool openText(int page) const;
    void closeText() const;
    QRectF toView(double left, double top, double right, double bottom) const;

    QString m_source;
    QString m_error;
    int m_pageCount = 0;
    /// FPDF_DOCUMENT, kept as void * so this header stays free of PDFium.
    void *m_doc = nullptr;

    // The one page whose text is open. A drag asks about the same page on
    // every mouse movement, and opening the text parses the whole page.
    mutable int m_textIndex = -1;
    mutable void *m_textPage = nullptr; ///< FPDF_PAGE
    mutable void *m_text = nullptr;     ///< FPDF_TEXTPAGE
    /// Each character's box, as fractions of the page; empty where a
    /// character has none, such as a line break PDFium put in.
    mutable QList<QRectF> m_boxes;
};
