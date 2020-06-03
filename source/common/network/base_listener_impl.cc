#include "common/network/base_listener_impl.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/file_event_impl.h"
#include "common/network/address_impl.h"
#include "common/network/socket_impl.h"

#include "event2/listener.h"

namespace Envoy {
namespace Network {

Address::InstanceConstSharedPtr BaseListenerImpl::getLocalAddress(os_fd_t fd) {
  return SocketInterface::addressFromFd(fd);
}

BaseListenerImpl::BaseListenerImpl(Event::DispatcherImpl& dispatcher, SocketSharedPtr socket,
                                   u_int32_t listen_backlog)
    : local_address_(nullptr), dispatcher_(dispatcher), socket_(std::move(socket)) {
  const auto ip = socket_->localAddress()->ip();
  const auto listen_backlog = socket_

  // Only use the listen socket's local address for new connections if it is not the all hosts
  // address (e.g., 0.0.0.0 for IPv4).
  if (!(ip && ip->isAnyAddress())) {
    local_address_ = socket_->localAddress();
  }

  listen_backlog_ = listen_backlog;
}

} // namespace Network
} // namespace Envoy
