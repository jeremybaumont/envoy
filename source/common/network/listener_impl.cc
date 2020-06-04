#include "common/network/listener_impl.h"

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/file_event_impl.h"
#include "common/network/address_impl.h"
#include "common/network/io_socket_handle_impl.h"

#include "event2/listener.h"

namespace Envoy {
namespace Network {

void ListenerImpl::listenCallback(evconnlistener*, evutil_socket_t fd, sockaddr* remote_addr,
                                  int remote_addr_len, void* arg) {
  ListenerImpl* listener = static_cast<ListenerImpl*>(arg);

  // Create the IoSocketHandleImpl for the fd here.
  IoHandlePtr io_handle = std::make_unique<IoSocketHandleImpl>(fd);

  // Get the local address from the new socket if the listener is listening on IP ANY
  // (e.g., 0.0.0.0 for IPv4) (local_address_ is nullptr in this case).
  const Address::InstanceConstSharedPtr& local_address =
      listener->local_address_ ? listener->local_address_
                               : listener->getLocalAddress(io_handle->fd());

  // The accept() call that filled in remote_addr doesn't fill in more than the sa_family field
  // for Unix domain sockets; apparently there isn't a mechanism in the kernel to get the
  // `sockaddr_un` associated with the client socket when starting from the server socket.
  // We work around this by using our own name for the socket in this case.
  // Pass the 'v6only' parameter as true if the local_address is an IPv6 address. This has no effect
  // if the socket is a v4 socket, but for v6 sockets this will create an IPv4 remote address if an
  // IPv4 local_address was created from an IPv6 mapped IPv4 address.
  const Address::InstanceConstSharedPtr& remote_address =
      (remote_addr->sa_family == AF_UNIX)
          ? SocketInterface::peerAddressFromFd(io_handle->fd())
          : Address::addressFromSockAddr(*reinterpret_cast<const sockaddr_storage*>(remote_addr),
                                         remote_addr_len,
                                         local_address->ip()->version() == Address::IpVersion::v6);
  listener->cb_.onAccept(
      std::make_unique<AcceptedSocketImpl>(std::move(io_handle), local_address, remote_address));
}

void ListenerImpl::setupServerSocket(Event::DispatcherImpl& dispatcher, Socket& socket) {
#if defined(__linux__)
  // On Linux, pass in -1 to the listen backlog to use net.core.somaxconn and allow runtime tuning.
  // Default before Linux 5.4 is 128 which matches libevent hardcoded setting.
  // Default on Linux 5.4 and later is 4096.
  const int listen_backlog = -1;
#elif
  // Default non-Linux platforms to the libevent default of 128.
  const int listen_backlog = 128;
#endif

  const Api::SysCallIntResult result =
      Api::OsSysCallsSingleton::get().listen(socket.ioHandle().fd(), listen_backlog);
  RELEASE_ASSERT(result.rc_ != -1,
                 fmt::format("listen(2) failed, got error: {}", strerror(result.errno_)))

  // Using 0 as backlog is equivalent to not call listen syscall again, see:
  // https://github.com/libevent/libevent/blob/29cc8386a2f7911eaa9336692a2c5544d8b4734f/listener.c#L172
  listener_.reset(
      evconnlistener_new(&dispatcher.base(), listenCallback, this, 0, 0, socket.ioHandle().fd()));

  if (!listener_) {
    throw CreateListenerException(
        fmt::format("cannot listen on socket: {}", socket.localAddress()->asString()));
  }

  if (!Network::Socket::applyOptions(socket.options(), socket,
                                     envoy::config::core::v3::SocketOption::STATE_LISTENING)) {
    throw CreateListenerException(fmt::format("cannot set post-listen socket option on socket: {}",
                                              socket.localAddress()->asString()));
  }

  evconnlistener_set_error_cb(listener_.get(), errorCallback);
}

ListenerImpl::ListenerImpl(Event::DispatcherImpl& dispatcher, SocketSharedPtr socket,
                           ListenerCallbacks& cb, bool bind_to_port)
    : BaseListenerImpl(dispatcher, std::move(socket)), cb_(cb), listener_(nullptr) {
  if (bind_to_port) {
    setupServerSocket(dispatcher, *socket_);
  }
}

void ListenerImpl::errorCallback(evconnlistener*, void*) {
  // We should never get an error callback. This can happen if we run out of FDs or memory. In those
  // cases just crash.
  PANIC(fmt::format("listener accept failure: {}", strerror(errno)));
}

void ListenerImpl::enable() {
  if (listener_.get()) {
    evconnlistener_enable(listener_.get());
  }
}

void ListenerImpl::disable() {
  if (listener_.get()) {
    evconnlistener_disable(listener_.get());
  }
}

} // namespace Network
} // namespace Envoy
