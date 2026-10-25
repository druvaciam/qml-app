#include "PdfDocument.h"
#include "Logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QMutexLocker>
#include <QRecursiveMutex>

namespace {

using FPDF_DOCUMENT = void *;
using FPDF_PAGE = void *;
using FPDF_BITMAP = void *;

constexpr int kBitmapBGRA = 4;     // FPDFBitmap_BGRA, from fpdfview.h
constexpr int kRenderAnnot = 0x01; // FPDF_ANNOT: draw the annotations too

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

    bool ok = false;
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
