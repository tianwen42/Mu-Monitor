#pragma once

#include <QByteArray>
#include <QtGlobal>

class CRC16 final
{
public:
    // CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF, refin=false, refout=false.
    static quint16 compute(const QByteArray &data);
    static quint16 compute(const char *data, qsizetype size);
};
