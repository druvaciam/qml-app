#include "PdfDocument.h"
#include "Logging.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLibrary>
#include <QMutexLocker>
#include <QRecursiveMutex>

#include <limits>
#include <vector>

namespace {

using FPDF_DOCUMENT = void *;
using FPDF_PAGE = void *;
using FPDF_BITMAP = void *;
using FPDF_TEXTPAGE = void *;

constexpr int kBitmapBGRA = 4;     // FPDFBitmap_BGRA, from fpdfview.h
constexpr int kRenderAnnot = 0x01; // FPDF_ANNOT: draw the annotations too
/// FPDF_PageToDevice answers in whole units of a device this many across, so
/// this is how finely a position on the page is known.
constexpr int kDeviceUnits = 100000;

/// The few entry points this needs, resolved once from the library.
struct Pdfium
{
    void (*InitLibrary)() = nullptr;
    void (*DestroyLibrary)() = nullptr;
    FPDF_DOCUMENT (*LoadDocument)(const char *path, const char *password) = nullptr;
    void (*CloseDocument)(FPDF_DOCUMENT doc) = nullptr;
    int (*GetPageCount)(FPDF_DOCUMENT doc) = nullptr;
    FPDF_PAGE (*LoadPage)(FPDF_DOCUMENT doc, int index) = nullptr;
    void (*ClosePage)(FPDF_PAGE page) = nullptr;
    float (*GetPageWidthF)(FPDF_PAGE page) = nullptr;
    float (*GetPageHeightF)(FPDF_PAGE page) = nullptr;
    FPDF_BITMAP (*BitmapCreateEx)(int w, int h, int format, void *buffer, int stride) = nullptr;
    int (*BitmapFillRect)(FPDF_BITMAP bmp, int l, int t, int w, int h, unsigned long colour) = nullptr;
    void (*BitmapDestroy)(FPDF_BITMAP bmp) = nullptr;
    void (*RenderPageBitmap)(FPDF_BITMAP bmp, FPDF_PAGE page, int x, int y,
                             int w, int h, int rotate, int flags) = nullptr;
    unsigned long (*GetLastError)() = nullptr;
    int (*PageToDevice)(FPDF_PAGE page, int startX, int startY, int sizeX, int sizeY, int rotate,
                        double pageX, double pageY, int *deviceX, int *deviceY) = nullptr;

    FPDF_TEXTPAGE (*TextLoadPage)(FPDF_PAGE page) = nullptr;
    void (*TextClosePage)(FPDF_TEXTPAGE text) = nullptr;
    int (*TextCountChars)(FPDF_TEXTPAGE text) = nullptr;
    // Left, right, bottom, top: PDFium's order, not the usual one.
    int (*TextGetCharBox)(FPDF_TEXTPAGE text, int index,
                          double *left, double *right, double *bottom, double *top) = nullptr;
    int (*TextGetText)(FPDF_TEXTPAGE text, int start, int count, unsigned short *result) = nullptr;

    bool ok = false;
    /// Pages can still be shown without these; their text just cannot be
    /// selected.
    bool textOk = false;
};

/// Looked up once. The library sits next to the executable in a packaged
/// build; in a development build it is also looked for in the source tree,
/// so running straight out of the build folder works.
const Pdfium &pdfium()
{
    static Pdfium api = [] {
        Pdfium p;
        static QLibrary lib;

        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList candidates = {
            appDir + QStringLiteral("/pdfium"),
            appDir + QStringLiteral("/../third_party/pdfium/pdfium"),
            appDir + QStringLiteral("/../../third_party/pdfium/pdfium"),
            QStringLiteral("pdfium"),
        };
        for (const QString &name : candidates) {
            lib.setFileName(name);
            if (lib.load()) {
                break;
            }
        }
        if (!lib.isLoaded()) {
            qCInfo(lcApp) << "PDFium not found - PDF files will not be previewed";
            return p;
        }

        // The library is passed in rather than captured: it has static
        // storage, and capturing such a variable is not allowed (g++ accepts
        // it, MSVC refuses with C3495).
        auto bind = [](QLibrary &from, auto &fn, const char *name) {
            fn = reinterpret_cast<std::decay_t<decltype(fn)>>(from.resolve(name));
            return fn != nullptr;
        };

        p.ok = bind(lib, p.InitLibrary, "FPDF_InitLibrary")
            && bind(lib, p.DestroyLibrary, "FPDF_DestroyLibrary")
            && bind(lib, p.LoadDocument, "FPDF_LoadDocument")
            && bind(lib, p.CloseDocument, "FPDF_CloseDocument")
            && bind(lib, p.GetPageCount, "FPDF_GetPageCount")
            && bind(lib, p.LoadPage, "FPDF_LoadPage")
            && bind(lib, p.ClosePage, "FPDF_ClosePage")
            && bind(lib, p.GetPageWidthF, "FPDF_GetPageWidthF")
            && bind(lib, p.GetPageHeightF, "FPDF_GetPageHeightF")
            && bind(lib, p.BitmapCreateEx, "FPDFBitmap_CreateEx")
            && bind(lib, p.BitmapFillRect, "FPDFBitmap_FillRect")
            && bind(lib, p.BitmapDestroy, "FPDFBitmap_Destroy")
            && bind(lib, p.RenderPageBitmap, "FPDF_RenderPageBitmap")
            && bind(lib, p.GetLastError, "FPDF_GetLastError");

        if (!p.ok) {
            qCWarning(lcApp) << "PDFium loaded from" << lib.fileName()
                             << "but does not have the expected functions";
            return p;
        }

        p.textOk = bind(lib, p.PageToDevice, "FPDF_PageToDevice")
                && bind(lib, p.TextLoadPage, "FPDFText_LoadPage")
                && bind(lib, p.TextClosePage, "FPDFText_ClosePage")
                && bind(lib, p.TextCountChars, "FPDFText_CountChars")
                && bind(lib, p.TextGetCharBox, "FPDFText_GetCharBox")
                && bind(lib, p.TextGetText, "FPDFText_GetText");
        if (!p.textOk) {
            qCInfo(lcApp) << "PDFium has no text functions - PDF text will not be selectable";
        }

        p.InitLibrary();
        qCInfo(lcApp).noquote() << "PDFium ready:" << QFileInfo(lib.fileName()).fileName();
        return p;
    }();
    return api;
}

/// PDFium holds global state and is not thread safe, while two threads reach
/// it: pages are rendered on Qt Quick's image thread, and the view asks for
/// page proportions on the GUI thread as it lays the list out. Every call in
/// this file is serialised through here. Recursive because close() is called
/// from setSource(), which already holds it.
QRecursiveMutex &pdfiumLock()
{
    static QRecursiveMutex lock;
    return lock;
}

QString errorText(unsigned long code)
{
    switch (code) {
    case 1: return PdfDocument::tr("This file could not be read.");
    case 2: return PdfDocument::tr("This file is not a PDF, or it is damaged.");
    case 3: return PdfDocument::tr("This PDF is damaged.");
    case 4: return PdfDocument::tr("This PDF needs a password.");
    case 5: return PdfDocument::tr("This PDF uses an unsupported security scheme.");
    default: return PdfDocument::tr("This PDF could not be opened.");
    }
}

} // namespace

PdfDocument::PdfDocument(QObject *parent)
    : QObject(parent)
{
}

PdfDocument::~PdfDocument()
{
    close();
}

bool PdfDocument::isAvailable()
{
    return pdfium().ok;
}

void PdfDocument::close()
{
    QMutexLocker locked(&pdfiumLock());
    closeText();
    if (m_doc) {
        pdfium().CloseDocument(m_doc);
        m_doc = nullptr;
    }
    m_pageCount = 0;
}

void PdfDocument::setSource(const QString &source)
{
    if (source == m_source) {
        return;
    }
    // The lock is released before the signal: that brings the view back to ask
    // for page sizes and images, and there is no reason to hold the library
    // while it does.
    {
        QMutexLocker locked(&pdfiumLock());
        m_source = source;
        m_error.clear();
        close();

        if (!m_source.isEmpty()) {
            if (!pdfium().ok) {
                m_error = tr("PDF viewing is not available in this build.");
            } else {
                // Native separators and the local 8-bit encoding: PDFium takes
                // a plain char path, not UTF-16.
                const QByteArray path = QDir::toNativeSeparators(m_source).toLocal8Bit();
                m_doc = pdfium().LoadDocument(path.constData(), nullptr);
                if (!m_doc) {
                    m_error = errorText(pdfium().GetLastError());
                } else {
                    m_pageCount = pdfium().GetPageCount(m_doc);
                    qCInfo(lcApp).noquote() << "pdf:" << QFileInfo(m_source).fileName()
                                            << "|" << m_pageCount << "page(s)";
                }
            }
        }
    }
    emit documentChanged();
}

qreal PdfDocument::pageAspect(int index) const
{
    QMutexLocker locked(&pdfiumLock());
    if (!m_doc || index < 0 || index >= m_pageCount) {
        return 0.0;
    }
    FPDF_PAGE page = pdfium().LoadPage(m_doc, index);
    if (!page) {
        return 0.0;
    }
    const float w = pdfium().GetPageWidthF(page);
    const float h = pdfium().GetPageHeightF(page);
    pdfium().ClosePage(page);
    return h > 0 ? qreal(w) / qreal(h) : 0.0;
}

QImage PdfDocument::renderPage(int index, int width) const
{
    QMutexLocker locked(&pdfiumLock());
    if (!m_doc || index < 0 || index >= m_pageCount || width <= 0) {
        return QImage();
    }

    FPDF_PAGE page = pdfium().LoadPage(m_doc, index);
    if (!page) {
        return QImage();
    }

    const float pw = pdfium().GetPageWidthF(page);
    const float ph = pdfium().GetPageHeightF(page);
    const int height = (pw > 0) ? qRound(width * ph / pw) : 0;
    if (height <= 0) {
        pdfium().ClosePage(page);
        return QImage();
    }

    // PDFium writes BGRA straight into the image's own memory, so the pixels
    // are never copied: the bitmap it draws into is the QImage.
    QImage image(width, height, QImage::Format_ARGB32);
    FPDF_BITMAP bitmap = pdfium().BitmapCreateEx(width, height, kBitmapBGRA,
                                                 image.bits(), int(image.bytesPerLine()));
    if (!bitmap) {
        pdfium().ClosePage(page);
        return QImage();
    }

    // White first: a PDF page is paper, and anything it does not paint should
    // be white rather than whatever was in the buffer.
    pdfium().BitmapFillRect(bitmap, 0, 0, width, height, 0xFFFFFFFF);
    pdfium().RenderPageBitmap(bitmap, page, 0, 0, width, height, 0, kRenderAnnot);
    pdfium().BitmapDestroy(bitmap);
    pdfium().ClosePage(page);
    return image;
}

bool PdfDocument::textSelectable()
{
    return pdfium().ok && pdfium().textOk;
}

/// From PDF space - points, y going up from the bottom, before the page's own
/// rotation - to fractions of the page as it is drawn. PageToDevice applies
/// the same rotation the renderer does, so the two always agree.
QRectF PdfDocument::toView(double left, double top, double right, double bottom) const
{
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    pdfium().PageToDevice(m_textPage, 0, 0, kDeviceUnits, kDeviceUnits, 0, left, top, &x1, &y1);
    pdfium().PageToDevice(m_textPage, 0, 0, kDeviceUnits, kDeviceUnits, 0, right, bottom, &x2, &y2);
    // On a rotated page either corner can come out on either side.
    const qreal units = kDeviceUnits;
    return QRectF(QPointF(qMin(x1, x2) / units, qMin(y1, y2) / units),
                  QPointF(qMax(x1, x2) / units, qMax(y1, y2) / units));
}

/// Callers hold the lock.
bool PdfDocument::openText(int page) const
{
    if (page == m_textIndex) {
        return m_text != nullptr;
    }
    closeText();
    if (!m_doc || !pdfium().textOk || page < 0 || page >= m_pageCount) {
        return false;
    }
    m_textPage = pdfium().LoadPage(m_doc, page);
    m_text = m_textPage ? pdfium().TextLoadPage(m_textPage) : nullptr;
    // Remembered even when it failed, so a page without text is not parsed
    // again on every mouse movement.
    m_textIndex = page;
    if (!m_text) {
        return false;
    }

    const int count = pdfium().TextCountChars(m_text);
    m_boxes.reserve(qMax(0, count));
    for (int i = 0; i < count; ++i) {
        double left = 0, right = 0, bottom = 0, top = 0;
        m_boxes.append(pdfium().TextGetCharBox(m_text, i, &left, &right, &bottom, &top)
                           ? toView(left, top, right, bottom)
                           : QRectF());
    }
    return true;
}

/// Callers hold the lock.
void PdfDocument::closeText() const
{
    if (m_text) {
        pdfium().TextClosePage(m_text);
        m_text = nullptr;
    }
    if (m_textPage) {
        pdfium().ClosePage(m_textPage);
        m_textPage = nullptr;
    }
    m_textIndex = -1;
    m_boxes.clear();
}

int PdfDocument::charCount(int page) const
{
    QMutexLocker locked(&pdfiumLock());
    return openText(page) ? int(m_boxes.size()) : 0;
}

int PdfDocument::charAt(int page, qreal x, qreal y) const
{
    QMutexLocker locked(&pdfiumLock());
    if (!openText(page)) {
        return -1;
    }
    int nearest = -1;
    qreal best = std::numeric_limits<qreal>::max();
    for (int i = 0; i < m_boxes.size(); ++i) {
        const QRectF &box = m_boxes.at(i);
        // Spaces and the line breaks PDFium adds have no size; a point near
        // one lands on the character beside it.
        if (box.isEmpty()) {
            continue;
        }
        if (box.contains(x, y)) {
            return i;
        }
        const qreal dx = qMax(qMax(box.left() - x, 0.0), x - box.right());
        const qreal dy = qMax(qMax(box.top() - y, 0.0), y - box.bottom());
        // Being on the line matters far more than being near the character:
        // past the end of a line means the end of that line, not the start
        // of the one below it.
        const qreal distance = dy * 8 + dx;
        if (distance < best) {
            best = distance;
            nearest = i;
        }
    }
    return nearest;
}

QVariantList PdfDocument::textRects(int page, int from, int to) const
{
    QMutexLocker locked(&pdfiumLock());
    QVariantList rects;
    if (!openText(page)) {
        return rects;
    }
    from = qMax(0, from);
    to = qMin(to, int(m_boxes.size()) - 1);
    if (from > to) {
        return rects;
    }
    // One band per line, from its first selected character to its last. A box
    // per character leaves gaps between the letters and changes height with
    // each letter's shape; PDFium's own merging gives exactly that for files
    // that place every glyph separately, as many do.
    QRectF line;
    auto finishLine = [&] {
        if (!line.isNull()) {
            const qreal pad = line.height() * 0.15;
            rects.append(line.adjusted(0, -pad, 0, pad));
        }
    };
    for (int i = from; i <= to; ++i) {
        const QRectF &box = m_boxes.at(i);
        if (box.isEmpty()) {
            continue;
        }
        // Still the same line while the heights overlap at all: a full stop
        // or a comma is level with the capitals beside it, the line below is
        // not.
        if (!line.isNull() && qMin(line.bottom(), box.bottom()) > qMax(line.top(), box.top())) {
            line = line.united(box);
        } else {
            finishLine();
            line = box;
        }
    }
    finishLine();
    return rects;
}

QString PdfDocument::text(int page, int from, int to) const
{
    QMutexLocker locked(&pdfiumLock());
    if (!openText(page)) {
        return QString();
    }
    from = qMax(0, from);
    to = qMin(to, int(m_boxes.size()) - 1);
    if (from > to) {
        return QString();
    }
    const int count = to - from + 1;
    std::vector<unsigned short> buffer(count + 1);
    // The count written includes the terminating zero.
    const int written = pdfium().TextGetText(m_text, from, count, buffer.data());
    QString result = QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.data()),
                                        qMax(0, written - 1));
    // PDFium ends its lines with \r\n. The clipboard adds its own \r on
    // Windows, so a \r left here would come out doubled.
    result.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return result;
}

void PdfDocument::copyToClipboard(const QString &text) const
{
    QGuiApplication::clipboard()->setText(text);
}
