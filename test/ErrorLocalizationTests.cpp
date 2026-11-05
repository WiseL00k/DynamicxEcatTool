#include <QtTest>
#include <QBuffer>
#include <QFile>
#include <QTemporaryDir>
#include <memory>

#include "Backend/Config/EcatSlaveConfigurationParser.h"
#include "Backend/Ethercat/EthercatSlaveLoader.h"
#include "Backend/Explorer/EsiParser.h"
#include "Backend/Explorer/PdoValueCodec.h"
#include "SOEM_interface/EcatMasterBus.h"
#include "SOEM_interface/ErrorText.h"

namespace {
bool hasChinese(const QString& text)
{
    for (const auto character : text)
        if (character.unicode() >= 0x4e00 && character.unicode() <= 0x9fff)
            return true;
    return false;
}
}

class ErrorLocalizationTests final : public QObject {
    Q_OBJECT
private slots:
    void protocolErrorsDescribeTheReportedCode()
    {
        using namespace soem_interface;
        QVERIFY(QString::fromStdString(alStatusText(0x001b)).contains(QStringLiteral("看门狗")));
        QVERIFY(QString::fromStdString(sdoAbortText(0x06020000)).contains(QStringLiteral("对象")));
        QVERIFY(QString::fromStdString(sdoAbortText(0x06090011)).contains(QStringLiteral("子索引")));
        QVERIFY(hasChinese(QString::fromStdString(mailboxErrorText(0x0002))));
        QVERIFY(hasChinese(QString::fromStdString(soeErrorText(0x1001))));
        const auto unknown = QString::fromStdString(sdoAbortText(0xdeadbeef));
        QVERIFY(hasChinese(unknown));
        QVERIFY(unknown.contains(QStringLiteral("DEADBEEF"), Qt::CaseInsensitive));
        QVERIFY(QString::fromStdString(alStatusText(0x1234)).contains(QStringLiteral("1234")));
    }

    void scanFailureKeepsItsErrorCodeAndChineseText()
    {
        auto bus = std::make_unique<soem_interface::EcatMasterBus>("");
        const auto scan = bus->scanForSlaves(); // Fails before opening any adapter.
        QVERIFY(!scan.success);
        QCOMPARE(scan.errorCode, soem_interface::error::InvalidNicName);
        QVERIFY(hasChinese(QString::fromStdString(scan.error)));
    }

    void pdoValidationPreservesOutputAndReportsChinese()
    {
        QByteArray image(1, char(0x5a));
        const auto result = explorer::PdoValueCodec::encode(
            QStringLiteral("256"), QStringLiteral("USINT"), 8, &image, 0);
        QVERIFY(!result.ok);
        QVERIFY(hasChinese(result.error));
        QVERIFY(result.error.contains(QStringLiteral("8")));
        QCOMPARE(image, QByteArray(1, char(0x5a)));
    }

    void missingConfigurationRetainsThePath()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("missing.yaml"));
        try {
            Backend::EcatSlaveConfigurationParser parser(path.toStdString());
            QFAIL("Missing configuration must fail");
        } catch (const std::exception& exception) {
            const auto message = QString::fromUtf8(exception.what());
            QVERIFY(hasChinese(message));
            QVERIFY(message.contains(path));
        }
    }

    void malformedEsiKeepsSourceAndLocation()
    {
        QBuffer xml;
        xml.setData("<EtherCATInfo>\n<Descriptions></EtherCATInfo>");
        QVERIFY(xml.open(QIODevice::ReadOnly));
        const auto parsed = explorer::EsiParser().parse(&xml, QStringLiteral("broken.xml"));
        QVERIFY(!parsed.ok());
        QVERIFY(!parsed.diagnostics.empty());
        const auto diagnostic = parsed.diagnostics.constLast();
        QVERIFY(hasChinese(diagnostic.message));
        QCOMPARE(diagnostic.source, QStringLiteral("broken.xml"));
        QVERIFY(diagnostic.line > 0);
        QVERIFY(diagnostic.column > 0);
    }

    void malformedYamlReportsItsLocationAtTheUiBoundary()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("broken.yaml"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("ethercat_master: [\n") > 0);
        file.close();
        auto bus = std::make_unique<soem_interface::EcatMasterBus>("");
        DeviceStatusModel devices;
        const auto result = Backend::EthercatSlaveLoader::loadFromFile(path.toStdString(), *bus, devices);
        QVERIFY(!result.ok);
        QVERIFY(hasChinese(result.errorMessage));
        QVERIFY(result.errorMessage.contains(path));
        QVERIFY(result.errorMessage.contains(QStringLiteral("行")));
        QVERIFY(result.errorMessage.contains(QStringLiteral("列")));
    }
};

QTEST_GUILESS_MAIN(ErrorLocalizationTests)
#include "ErrorLocalizationTests.moc"
