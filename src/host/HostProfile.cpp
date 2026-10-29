#include "host/HostProfile.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QtGlobal>

#include <algorithm>
#include <limits>
#include <utility>

namespace ultraview::host {
namespace {

constexpr int kMaxProfileBytes = 65536;

QString randomDeviceIdentity()
{
    QByteArray seed(16, '\0');
    for (int index = 0; index < seed.size(); ++index)
        seed[index] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    return QString::fromLatin1(
        QCryptographicHash::hash(seed, QCryptographicHash::Md5).toHex().toUpper());
}

QString randomPassword()
{
    QString password;
    password.reserve(8);
    for (int index = 0; index < 8; ++index)
        password.append(QChar(QLatin1Char('0').unicode()
                              + QRandomGenerator::system()->bounded(10)));
    return password;
}

bool validDeviceIdentity(const QString &identity)
{
    if (identity.size() != 32)
        return false;
    for (const QChar unit : identity) {
        const ushort value = unit.unicode();
        if (!((value >= '0' && value <= '9') || (value >= 'A' && value <= 'F')))
            return false;
    }
    return true;
}

bool validPassword(const QString &password)
{
    if (password.size() != 8)
        return false;
    for (const QChar unit : password) {
        if (unit < QLatin1Char('0') || unit > QLatin1Char('9'))
            return false;
    }
    return true;
}

QString readSmallFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
        return {};
    }
    if (file.size() > kMaxProfileBytes) {
        if (error)
            *error = QStringLiteral("%1 is larger than the profile limit").arg(path);
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool writePrivateFile(const QString &path, const QByteArray &data, QString *error)
{
    const QFileInfo info(path);
    QDir directory = info.absoluteDir();
    if (!directory.exists() && !QDir().mkpath(directory.absolutePath())) {
        if (error)
            *error = QStringLiteral("cannot create %1").arg(directory.absolutePath());
        return false;
    }
    // Keep the directory private even if it was created by an older build.
    QFile::setPermissions(directory.absolutePath(),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner
                              | QFileDevice::ExeOwner);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("cannot open %1 for writing: %2").arg(path, file.errorString());
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QString hostName()
{
    const QString name = QSysInfo::machineHostName().trimmed();
    return name.isEmpty() ? QStringLiteral("Ultraview Linux") : name.left(64);
}

QString defaultRouteInterface()
{
    QFile route(QStringLiteral("/proc/net/route"));
    if (!route.open(QIODevice::ReadOnly))
        return {};
    // Iface Destination Gateway Flags RefCnt Use Metric Mask ...
    // A default route has Destination 00000000; the smallest metric wins.
    QString best;
    int bestMetric = std::numeric_limits<int>::max();
    const QList<QByteArray> lines = route.readAll().split('\n');
    for (int index = 1; index < lines.size(); ++index) {
        const QList<QByteArray> fields = lines.at(index).simplified().split(' ');
        if (fields.size() < 7 || fields.at(1) != QByteArrayLiteral("00000000"))
            continue;
        bool ok = false;
        const int metric = fields.at(6).toInt(&ok);
        if (!ok)
            continue;
        if (metric < bestMetric) {
            bestMetric = metric;
            best = QString::fromLatin1(fields.at(0));
        }
    }
    return best;
}

QString interfaceAddress(const QString &name)
{
    const QNetworkInterface interface = QNetworkInterface::interfaceFromName(name);
    if (!interface.isValid())
        return {};
    for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
        const QHostAddress address = entry.ip();
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()
            && address != QHostAddress::AnyIPv4) {
            return address.toString();
        }
    }
    return {};
}

QString interfaceMac(const QString &name)
{
    QFile file(QStringLiteral("/sys/class/net/%1/address").arg(name));
    if (file.open(QIODevice::ReadOnly)) {
        const QString normalized =
            HostProfile::normalizeMac(QString::fromLatin1(file.readAll()).trimmed());
        if (!normalized.isEmpty())
            return normalized;
    }
    const QNetworkInterface interface = QNetworkInterface::interfaceFromName(name);
    if (interface.isValid())
        return HostProfile::normalizeMac(interface.hardwareAddress());
    return {};
}

QString primaryInterfaceWithAddress(QString *address)
{
    const QString preferred = defaultRouteInterface();
    if (!preferred.isEmpty()) {
        const QString value = interfaceAddress(preferred);
        if (!value.isEmpty()) {
            if (address)
                *address = value;
            return preferred;
        }
    }
    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        if (interface.flags().testFlag(QNetworkInterface::IsLoopBack))
            continue;
        const QString value = interfaceAddress(interface.name());
        if (!value.isEmpty()) {
            if (address)
                *address = value;
            return interface.name();
        }
    }
    return {};
}

QString kernelRelease()
{
    QFile file(QStringLiteral("/proc/sys/kernel/osrelease"));
    if (file.open(QIODevice::ReadOnly))
        return QString::fromLatin1(file.readAll()).trimmed();
    return QSysInfo::kernelVersion();
}

QString userName()
{
    static const char *const variables[] = {"USER", "LOGNAME", "USERNAME"};
    for (const char *name : variables) {
        const QString value = qEnvironmentVariable(name).trimmed();
        if (!value.isEmpty())
            return value;
    }
    return QStringLiteral("user");
}

QString userNameWireValue(const QString &name)
{
    bool ascii = true;
    for (const QChar unit : name) {
        if (unit.unicode() > 0x7f) {
            ascii = false;
            break;
        }
    }
    if (ascii)
        return name;
    return QString::fromLatin1(name.toUtf8().toBase64());
}

} // namespace

QString HostProfile::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/host-profile.json");
}

bool HostProfile::isAcceptablePassword(const QString &password)
{
    if (password.size() < 8 || password.size() > 256)
        return false;
    for (const QChar unit : password) {
        const ushort value = unit.unicode();
        if (value < 32 || value > 126)
            return false;
    }
    return true;
}

QString HostProfile::operatingSystemWireValue(const QString &kernelRelease)
{
    QList<qint64> numbers;
    qint64 current = -1;
    for (const QChar unit : kernelRelease) {
        if (unit.isDigit()) {
            if (current < 0)
                current = 0;
            current = current * 10 + (unit.unicode() - '0');
            if (current > 0x7fffffff)
                current = 0x7fffffff;
        } else if (current >= 0) {
            numbers.append(current);
            current = -1;
            if (numbers.size() == 3)
                break;
        }
    }
    if (current >= 0 && numbers.size() < 3)
        numbers.append(current);
    while (numbers.size() < 3)
        numbers.append(0);
    return QStringLiteral("%1|%2|0|%3|0|0").arg(numbers.at(2)).arg(numbers.at(0)).arg(numbers.at(1));
}

QString HostProfile::normalizeMac(const QString &text)
{
    QString hex;
    hex.reserve(12);
    for (const QChar unit : text) {
        if (unit == QLatin1Char(':') || unit == QLatin1Char('-')
            || unit.isSpace())
            continue;
        const ushort value = unit.unicode();
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')
              || (value >= 'A' && value <= 'F')))
            return {};
        hex.append(unit.toUpper());
    }
    if (hex.size() != 12 || hex == QLatin1String("000000000000")
        || hex == QLatin1String("020000000000"))
        return {};
    bool ok = false;
    const int first = hex.left(2).toInt(&ok, 16);
    if (!ok || (first & 1) != 0)
        return {};
    return hex;
}

QString HostProfile::fallbackMac(const QString &deviceIdentity)
{
    QString mac = deviceIdentity.left(12).toUpper();
    if (mac.size() < 12)
        mac = mac.leftJustified(12, QLatin1Char('0'));
    bool ok = false;
    const int first = mac.left(2).toInt(&ok, 16);
    if (!ok)
        return QStringLiteral("0223456789AB");
    const int local = (first & 0xfc) | 0x02;
    mac.replace(0, 2, QStringLiteral("%1").arg(local, 2, 16, QLatin1Char('0')).toUpper());
    return mac;
}

HostIdentity HostProfile::loadOrCreate(QString *error)
{
    const QString path = defaultPath();
    HostIdentity identity;
    identity.profilePath = path;

    QString json;
    if (QFileInfo::exists(path)) {
        QString readError;
        json = readSmallFile(path, &readError);
        if (json.isEmpty() && !readError.isEmpty()) {
            if (error)
                *error = readError;
            return {};
        }
        if (json.size() > kMaxProfileBytes) {
            if (error)
                *error = QStringLiteral("the host profile is too large");
            return {};
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (error)
                *error = QStringLiteral("the host profile is not valid JSON; fix or delete %1")
                             .arg(path);
            return {};
        }
        const QJsonObject object = document.object();
        identity.deviceIdentity = object.value(QStringLiteral("device_identity")).toString();
        identity.password = object.value(QStringLiteral("password")).toString();
        identity.displayName = object.value(QStringLiteral("display_name")).toString().trimmed();
        if (!validDeviceIdentity(identity.deviceIdentity) || !validPassword(identity.password)) {
            if (error)
                *error = QStringLiteral("the host profile is invalid; fix or delete %1").arg(path);
            return {};
        }
    } else {
        identity.deviceIdentity = randomDeviceIdentity();
        identity.password = randomPassword();
        identity.displayName = hostName();
    }

    if (identity.displayName.isEmpty())
        identity.displayName = hostName();
    identity.displayName = identity.displayName.left(64);

    QJsonObject stored;
    stored.insert(QStringLiteral("device_identity"), identity.deviceIdentity);
    stored.insert(QStringLiteral("password"), identity.password);
    stored.insert(QStringLiteral("display_name"), identity.displayName);
    QString writeError;
    if (!writePrivateFile(path,
                          QJsonDocument(stored).toJson(QJsonDocument::Indented),
                          &writeError)) {
        if (error)
            *error = writeError;
        return {};
    }

    QString address;
    const QString interfaceName = primaryInterfaceWithAddress(&address);
    if (address.isEmpty()) {
        if (error)
            *error = QStringLiteral("no active IPv4 network connection is available; connect "
                                    "this computer to a network and try again");
        return {};
    }
    identity.localAddress = address;
    identity.macAddress = interfaceMac(interfaceName);
    if (identity.macAddress.isEmpty())
        identity.macAddress = fallbackMac(identity.deviceIdentity);
    identity.operatingSystem = operatingSystemWireValue(kernelRelease());
    identity.userName = userNameWireValue(userName());
    return identity;
}

QString HostIdentity::computerNameBase64() const
{
    return QString::fromLatin1(displayName.toUtf8().toBase64());
}

QStringList HostIdentity::registrationTerms() const
{
    // The 28-field host command 3.  Field 13 is the master-password proof;
    // this client uses the displayed 8-digit password, so it stays empty.
    return QStringList{
        QStringLiteral("3"),
        deviceIdentity,
        password,
        QStringLiteral("0"),
        localAddress,
        QStringLiteral("0"),
        computerNameBase64(),
        QStringLiteral("6.6.133"),
        macAddress,
        QString(),
        operatingSystem,
        QStringLiteral("0"),
        QString(),
        QString(),
        QStringLiteral("0"),
        QString(),
        userName,
        QStringLiteral("0"),
        QStringLiteral("0"),
        QStringLiteral("0"),
        QStringLiteral("0"),
        QStringLiteral("0"),
        QStringLiteral("0"),
        QStringLiteral("0"),
        QStringLiteral("0"),
        QString(),
        QString(),
        QString(),
    };
}

bool HostIdentity::isValid() const
{
    return validDeviceIdentity(deviceIdentity) && validPassword(password)
        && !localAddress.isEmpty() && !macAddress.isEmpty()
        && registrationTerms().size() == 28;
}

} // namespace ultraview::host
