#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QUrl>
#include <QtQuickTest/quicktest.h>

class DcUiTestSetup final : public QObject
{
    Q_OBJECT

private slots:
    void applicationAvailable()
    {
#ifdef Q_OS_WIN
        const QString systemRoot = qEnvironmentVariable("SystemRoot");
        const QString fontPath = systemRoot.isEmpty()
            ? QString()
            : QDir(systemRoot).filePath(QStringLiteral("Fonts/msyh.ttc"));
        if (!fontPath.isEmpty()) {
            const int fontId = QFontDatabase::addApplicationFont(fontPath);
            if (fontId >= 0) {
                const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
                if (!families.isEmpty()) {
                    QGuiApplication::setFont(QFont(families.constFirst()));
                }
            }
        }
#endif

        captureDirectory_ = QDir::current().filePath(
            QStringLiteral("test-artifacts/dc-ui"));
        QDir().mkpath(captureDirectory_);
    }

    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->rootContext()->setContextProperty(
            QStringLiteral("DcUiCaptureEnabled"),
            qEnvironmentVariableIntValue("DC_UI_CAPTURE_SCREENSHOTS") == 1);
        engine->rootContext()->setContextProperty(
            QStringLiteral("DcUiCaptureDirectory"),
            QUrl::fromLocalFile(captureDirectory_));
    }

private:
    QString captureDirectory_;
};

QUICK_TEST_MAIN_WITH_SETUP(dc_ui, DcUiTestSetup)

#include "QuickTestMain.moc"
