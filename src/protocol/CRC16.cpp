#include "CRC16.h"

quint16 CRC16::compute(const QByteArray &data)
{
    return compute(data.constData(), data.size());
}

quint16 CRC16::compute(const char *data, qsizetype size)
{
    quint16 crc = 0xffff;
    for (qsizetype i = 0; i < size; ++i) {
        crc ^= static_cast<quint16>(static_cast<quint8>(data[i])) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000) != 0) {
                crc = static_cast<quint16>((crc << 1) ^ 0x1021);
            } else {
                crc = static_cast<quint16>(crc << 1);
            }
        }
    }
    return crc;
}
