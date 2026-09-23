#pragma once

#include <protocol/Frame.h>

#include <QByteArray>
#include <QQueue>
#include <QString>

class FrameDecoder final
{
public:
    enum class EventType {
        DecodedFrame,
        ProtocolError,
    };

    struct Event
    {
        EventType type = EventType::ProtocolError;
        Frame frame;
        QString message;
    };

    void appendData(const QByteArray &data);
    void clear();

    bool hasEvents() const;
    int eventCount() const;
    Event takeNextEvent();
    int bufferedBytes() const;

private:
    void enqueueError(const QString &message);

    QByteArray m_buffer;
    QQueue<Event> m_events;
};
