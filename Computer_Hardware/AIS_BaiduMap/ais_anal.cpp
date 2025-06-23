#include "ais_anal.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QDateTime>
#include <QMap>
#include <QRandomGenerator>

bool AisAnal::checkNMEAChecksum(const QString &line) {
    int asteriskIndex = line.indexOf('*');
    if (asteriskIndex < 0) return false;

    QString data = line.mid(1, asteriskIndex - 1);
    QString checksumStr = line.mid(asteriskIndex + 1, 2);

    bool ok;
    int expected = checksumStr.toInt(&ok, 16);
    if (!ok) return false;

    int calc = 0;
    for (QChar ch : data) calc ^= ch.unicode();

    return calc == expected;
}

bool AisAnal::isPayloadValid(const QString &payload) {
    for (QChar c : payload) {
        ushort ch = c.unicode();
        if (!(ch >= 0x30 && ch <= 0x77)) return false;
    }
    return true;
}

QString AisAnal::sixBitToBinary(const QString& payload) {
    QString result;
    for (QChar ch : payload) {
        int val = ch.unicode() - 48;
        if (val > 40) val -= 8;
        result += QString("%1").arg(val, 6, 2, QLatin1Char('0'));
    }
    return result;
}

int AisAnal::binToInt(const QString& bin, int start, int length, bool signedVal) {
    QString sub = bin.mid(start, length);
    bool ok;
    int val = sub.toInt(&ok, 2);
    if (!ok) return 0;

    if (signedVal && sub[0] == '1') {
        int maxVal = 1 << length;
        val -= maxVal;
    }
    return val;
}

QString AisAnal::extractAscii6(const QString& bin, int start, int length) {
    QString result;
    for (int i = 0; i < length; i += 6) {
        QString bits = bin.mid(start + i, 6);
        bool ok;
        int val = bits.toInt(&ok, 2);
        if (!ok) continue;
        result += (val < 32) ? QChar(val + 64) : QChar(val);
    }
    return result.trimmed();
}

AisMessage AisAnal::parseLine(const QString& line, const QDateTime& timestamp) {
    // Skip checksum verification for ABVDM format
    if (line.startsWith("!ABVDM")) {
        QStringList parts = line.split(',');
        if (parts.size() < 6) {
            throw std::runtime_error("ABVDM格式错误");
        }
        QString payload = parts[5];
        int fill = parts[6].split('*')[0].toInt();
        return parsePayload(payload, fill, timestamp);
    }
    // Original AIVDM handling
    else if (!checkNMEAChecksum(line)) {
        throw std::runtime_error("NMEA校验失败");
    }

    QStringList parts = line.split(',');
    if (parts.size() < 6) {
        throw std::runtime_error("格式错误");
    }

    QString payload = parts[5];
    if (!isPayloadValid(payload)) {
        throw std::runtime_error("非法字符");
    }

    int fill = parts[6].split('*')[0].toInt();
    return parsePayload(payload, fill, timestamp);
}

AisMessage AisAnal::parsePayload(const QString& payload, int fillBits, const QDateTime& timestamp) {
    AisMessage msg;
    QString bits = sixBitToBinary(payload);
    msg.timestamp = timestamp;
    msg.rawPayload = payload;

    if (bits.size() < 38) {
        throw std::runtime_error("数据太短");
    }

    msg.type = binToInt(bits, 0, 6);
    msg.mmsi = QString::number(binToInt(bits, 8, 30));

    // 解析位置和航向信息
    if (msg.type == 1 || msg.type == 2 || msg.type == 3 || msg.type == 18) {
        msg.latitude = binToInt(bits, 89, 27, true) / 600000.0;
        msg.longitude = binToInt(bits, 61, 28, true) / 600000.0;
        msg.sog = binToInt(bits, 50, 10) / 10.0;
        msg.cog = binToInt(bits, 116, 12) / 10.0;
        msg.heading = binToInt(bits, 128, 9);
    }else if(msg.type == 4){
        // 处理基站报告消息
        msg.positionAccuracy = binToInt(bits, 78, 1);
        msg.longitude = binToInt(bits, 79, 28, true) / 600000.0;
        msg.latitude = binToInt(bits, 107, 27, true) / 600000.0;
        msg.fixType = binToInt(bits, 133, 4);

        // 设置MMSI
        msg.mmsi = QString::number(binToInt(bits, 8, 30));
    }else if(msg.type == 5){
        // 处理船舶静态和航程相关数据
        msg.imo = QString::number(binToInt(bits, 40, 30));
        msg.callsign = extractAscii6(bits, 70, 42);
        msg.name = extractAscii6(bits, 112, 120);
        msg.shipType = QString::number(binToInt(bits, 232, 8));
        msg.dimensionToBow = binToInt(bits, 240, 9);
        msg.dimensionToStern = binToInt(bits, 249, 9);
        msg.dimensionToPort = binToInt(bits, 258, 6);
        msg.dimensionToStarboard = binToInt(bits, 264, 6);
        msg.destination = extractAscii6(bits, 302, 120);

        // 设置MMSI
        msg.mmsi = QString::number(binToInt(bits, 8, 30));
    }else if(msg.type == 21){
        // 处理助航设备报告
        msg.aidType = binToInt(bits, 38, 5);
        msg.name = extractAscii6(bits, 43, 120);
        msg.longitude = binToInt(bits, 165, 28, true) / 600000.0;
        msg.latitude = binToInt(bits, 193, 27, true) / 600000.0;
        msg.isOffPosition = binToInt(bits, 220, 1) == 1;
        msg.aidName = msg.name;
        msg.isVirtual = binToInt(bits, 262, 1) == 1;

        // 设置MMSI
        msg.mmsi = QString::number(binToInt(bits, 8, 30));
    }else {
        msg.error = "未支持的类型：" + QString::number(msg.type);
    }

    return msg;
}
