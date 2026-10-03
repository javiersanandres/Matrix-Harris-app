#include <gtest/gtest.h>

#include <QApplication>
#include <QIcon>
#include <QImageReader>
#include <QSize>

// ============================================================================
// The application's icon
//
// The windows take theirs from the .ico the executable is built with
// (":/app/app.ico"), so it must be embedded and readable (Qt's ico plugin),
// with every size from the title bar's to the installer's.
// ============================================================================

namespace {

    void ensureApplication() {
        if (QCoreApplication::instance()) return;
        static int argc = 1;
        static char name[] = "app_logic_tests";
        static char* argv[] = { name, nullptr };
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static QApplication app(argc, argv);
    }

} // namespace

TEST(AppIcon, IsEmbeddedWithEverySize) {
    ensureApplication();
    QImageReader reader(QStringLiteral(":/app/app.ico"));
    ASSERT_TRUE(reader.canRead()) << "the ico plugin must be there to read it";
    EXPECT_GE(reader.imageCount(), 9);

    const QList<QSize> sizes = QIcon(QStringLiteral(":/app/app.ico")).availableSizes();
    for (int px : { 16, 24, 32, 48, 256 })
        EXPECT_TRUE(sizes.contains(QSize(px, px))) << px << " px";
}
