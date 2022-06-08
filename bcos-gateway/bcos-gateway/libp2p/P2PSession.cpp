/** @file P2PSession.cpp
 *  @author monan
 *  @date 20181112
 */

#include <bcos-gateway/Gateway.h>
#include <bcos-gateway/libp2p/P2PMessage.h>
#include <bcos-gateway/libp2p/P2PSession.h>
#include <bcos-gateway/libp2p/Service.h>

#include <bcos-utilities/Common.h>
#include <boost/algorithm/string.hpp>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::boostssl;

P2PSession::~P2PSession()
{
    P2PSESSION_LOG(INFO) << "[P2PSession::~P2PSession] this=" << this;
}

void P2PSession::onMessage(bcos::boostssl::MessageFace::Ptr _message)
{
    auto p2pMessage = std::dynamic_pointer_cast<P2PMessage>(_message);
    if (!p2pMessage)
    {
        P2PSESSION_LOG(WARNING) << LOG_DESC("invalid p2pMessage")
                                << LOG_KV("type", _message->packetType())
                                << LOG_KV("seq", _message->seq())
                                << LOG_KV("len", _message->length());
        return;
    }
    if (p2pMessage->dstP2PNodeID().size() == 0 || p2pMessage->dstP2PNodeID() == m_hostNodeID)
    {
        WsSession::onMessage(_message);
        return;
    }
    // the forwarding message
    auto self = std::weak_ptr<WsSession>(shared_from_this());
    threadPool()->enqueue([_message, self]() {
        auto session = self.lock();
        if (!session)
        {
            return;
        }
        session->recvMessageHandler()(_message, session);
    });
}

void P2PSession::onReadPacket(boost::beast::flat_buffer& _buffer)
{
    auto data = boost::asio::buffer_cast<byte*>(boost::beast::buffers_front(_buffer.data()));
    auto size = boost::asio::buffer_size(m_buffer.data());

    auto message = m_messageFactory->buildMessage();
    if (message->decode(bytesConstRef(data, size)) < 0)
    {  // invalid packet, stop this session ?
        WEBSOCKET_SESSION(WARNING) << LOG_BADGE("onReadPacket") << LOG_DESC("decode packet error")
                                   << LOG_KV("endpoint", endPoint()) << LOG_KV("session", this);
        return drop(WsError::PacketError);
    }
    auto p2pMessage = std::dynamic_pointer_cast<P2PMessage>(message);
    p2pMessage->setReceiveTime(utcTime());
    _buffer.consume(_buffer.size());
    onMessage(message);
}
