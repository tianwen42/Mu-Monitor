#include "FrameDecoder.h"

#include <protocol/FrameCodec.h>
#include <protocol/Protocol.h>

namespace {

QByteArray magicBytes()
{
    QByteArray magic(Protocol::kMagicSize, Qt::Uninitialized);
    magic[0] = static_cast<char>((Protocol::kMagic >> 8) & 0xff);
    magic[1] = static_cast<char>(Protocol::kMagic & 0xff);
    return magic;
}

int frameSizeFromHeader(const QByteArray &data)
{
    return (static_cast<int>(static_cast<quint8>(data.at(4))) << 8)
           | static_cast<int>(static_cast<quint8>(data.at(5)));
}

} // namespace

void FrameDecoder::appendData(const QByteArray &data)
{
    if (data.isEmpty()) {
        return;
    }
    m_buffer.append(data);

    const QByteArray magic = magicBytes();
    while (!m_buffer.isEmpty()) {
        const int magicIndex = m_buffer.indexOf(magic);
        if (magicIndex < 0) {
            const bool keepMagicPrefix = m_buffer.endsWith(magic.left(1));
            const int discarded = m_buffer.size() - (keepMagicPrefix ? 1 : 0);
            if (discarded > 0) {
                m_buffer.remove(0, discarded);
                enqueueError(QStringLiteral("丢弃 %1 字节无效流数据").arg(discarded));
            }
            break;
        }

        if (magicIndex > 0) {
            m_buffer.remove(0, magicIndex);
            enqueueError(QStringLiteral("丢弃 %1 字节帧前无效数据").arg(magicIndex));
            continue;
        }

        if (m_buffer.size() < Protocol::kHeaderSize) {
            break;
        }

        const int frameSize = frameSizeFromHeader(m_buffer);
        if (frameSize < Protocol::kMinFrameSize) {
            enqueueError(QStringLiteral("帧长度 %1 小于最小值 %2")
                             .arg(frameSize)
                             .arg(Protocol::kMinFrameSize));
            m_buffer.remove(0, 1);
            continue;
        }
        if (frameSize > Protocol::kMaxFrameSize) {
            enqueueError(QStringLiteral("帧长度 %1 超过上限 %2")
                             .arg(frameSize)
                             .arg(Protocol::kMaxFrameSize));
            m_buffer.remove(0, 1);
            continue;
        }

        if (m_buffer.size() < frameSize) {
            break;
        }

        const DecodeResult result = FrameCodec::decode(m_buffer.left(frameSize));
        if (result.isOk()) {
            Event event;
            event.type = EventType::DecodedFrame;
            event.frame = result.frame;
            m_events.enqueue(event);
        } else {
            enqueueError(QStringLiteral("%1：%2")
                             .arg(FrameCodec::statusText(result.status),
                                  protocolVersionName(static_cast<quint8>(m_buffer.at(2)))));
        }
        m_buffer.remove(0, frameSize);
    }
}

void FrameDecoder::clear()
{
    m_buffer.clear();
    m_events.clear();
}

bool FrameDecoder::hasEvents() const
{
    return !m_events.isEmpty();
}

int FrameDecoder::eventCount() const
{
    return m_events.size();
}

FrameDecoder::Event FrameDecoder::takeNextEvent()
{
    if (m_events.isEmpty()) {
        return {};
    }
    return m_events.dequeue();
}

int FrameDecoder::bufferedBytes() const
{
    return m_buffer.size();
}

void FrameDecoder::enqueueError(const QString &message)
{
    Event event;
    event.type = EventType::ProtocolError;
    event.message = message;
    m_events.enqueue(event);
}
