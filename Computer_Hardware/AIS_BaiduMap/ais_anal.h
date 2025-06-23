#ifndef AIS_ANAL_H
#define AIS_ANAL_H

#include <QString>
#include <QVector>
#include <QMap>
#include <QDateTime>
#include <stdexcept>

struct AisMessage {
    int type = -1;
    QString mmsi;
    double latitude = 0;
    double longitude = 0;
    double sog = 0;
    double cog = 0;
    int heading = -1;
    QString name;
    QString error;
    QDateTime timestamp;
    QString rawPayload;

    AisMessage() = default;

    QString imo;
    QString callsign;
    QString shipType;
    int dimensionToBow = 0;
    int dimensionToStern = 0;
    int dimensionToPort = 0;
    int dimensionToStarboard = 0;
    QString destination;
    int aidType = 0;
    bool isOffPosition = false;
    bool isVirtual = false;
    QString aidName;
    int positionAccuracy = 0;
    int fixType = 0;
};

class AisAnal {
public:
    static AisMessage parseLine(const QString& line, const QDateTime& timestamp);
    static AisMessage parsePayload(const QString& payload, int fillBits, const QDateTime& timestamp);

private:
    static QString sixBitToBinary(const QString& payload);
    static QString extractAscii6(const QString& bin, int start, int length);
    static int binToInt(const QString& bin, int start, int length, bool signedVal = false);
    static bool checkNMEAChecksum(const QString &line);
    static bool isPayloadValid(const QString &payload);
};

#endif // AIS_ANAL_H
