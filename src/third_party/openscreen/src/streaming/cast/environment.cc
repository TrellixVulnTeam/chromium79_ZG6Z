// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "streaming/cast/environment.h"

#include "platform/api/logging.h"
#include "platform/api/task_runner.h"
#include "streaming/cast/rtp_defines.h"

namespace openscreen {

using platform::UdpPacket;
using platform::UdpSocket;

namespace cast_streaming {

Environment::Environment(platform::ClockNowFunctionPtr now_function,
                         platform::TaskRunner* task_runner)
    : now_function_(now_function), task_runner_(task_runner) {
  OSP_DCHECK(now_function_);
  OSP_DCHECK(task_runner_);
}

Environment::Environment(platform::ClockNowFunctionPtr now_function,
                         platform::TaskRunner* task_runner,
                         const IPEndpoint& local_endpoint)
    : Environment(now_function, task_runner) {
  ErrorOr<std::unique_ptr<UdpSocket>> result =
      UdpSocket::Create(task_runner_, this, local_endpoint);
  const_cast<std::unique_ptr<UdpSocket>&>(socket_) = std::move(result.value());
  if (socket_) {
    socket_->Bind();
  } else {
    OSP_LOG_ERROR << "Unable to create a UDP socket bound to " << local_endpoint
                  << ": " << result.error();
  }
}

Environment::~Environment() = default;

IPEndpoint Environment::GetBoundLocalEndpoint() const {
  if (socket_) {
    return socket_->GetLocalEndpoint();
  }
  return IPEndpoint{};
}

void Environment::ConsumeIncomingPackets(PacketConsumer* packet_consumer) {
  OSP_DCHECK(packet_consumer);
  OSP_DCHECK(!packet_consumer_);
  packet_consumer_ = packet_consumer;
}

void Environment::DropIncomingPackets() {
  packet_consumer_ = nullptr;
}

int Environment::GetMaxPacketSize() const {
  // Return hard-coded values for UDP over wired Ethernet (which is a smaller
  // MTU than typical defaults for UDP over 802.11 wireless). Performance would
  // be more-optimized if the network were probed for the actual value. See
  // discussion in rtp_defines.h.
  switch (remote_endpoint_.address.version()) {
    case IPAddress::Version::kV4:
      return kMaxRtpPacketSizeForIpv4UdpOnEthernet;
    case IPAddress::Version::kV6:
      return kMaxRtpPacketSizeForIpv6UdpOnEthernet;
    default:
      OSP_NOTREACHED();
      return 0;
  }
}

void Environment::SendPacket(absl::Span<const uint8_t> packet) {
  OSP_DCHECK(remote_endpoint_.address);
  OSP_DCHECK_NE(remote_endpoint_.port, 0);
  if (socket_) {
    socket_->SendMessage(packet.data(), packet.size(), remote_endpoint_);
  }
}

Environment::PacketConsumer::~PacketConsumer() = default;

void Environment::OnError(UdpSocket* socket, Error error) {
  // Usually OnError() is only called for non-recoverable Errors. However,
  // OnSendError() and OnRead() delegate to this method, to handle their hard
  // error cases as well. So, return early here if |error| is recoverable.
  if (error.ok() || error.code() == Error::Code::kAgain) {
    return;
  }

  if (socket_error_handler_) {
    socket_error_handler_(error);
    return;
  }

  // Default behavior when no error handler is set.
  OSP_LOG_ERROR << "For UDP socket bound to " << socket_->GetLocalEndpoint()
                << ": " << error;
}

void Environment::OnSendError(UdpSocket* socket, Error error) {
  OnError(socket, error);
}

void Environment::OnRead(UdpSocket* socket,
                         ErrorOr<UdpPacket> packet_or_error) {
  if (!packet_consumer_) {
    return;
  }

  if (packet_or_error.is_error()) {
    OnError(socket, packet_or_error.error());
    return;
  }

  // Ideally, the arrival time would come from the operating system's network
  // stack (e.g., by using the SO_TIMESTAMP sockopt on POSIX systems). However,
  // there would still be the problem of mapping the timestamp to a value in
  // terms of platform::Clock. So, just sample the Clock here and call that the
  // "arrival time." While this can add variance within the system, it should be
  // minimal, assuming not too much time has elapsed between the actual packet
  // receive event and the when this code here is executing.
  const platform::Clock::time_point arrival_time = now_function_();

  UdpPacket packet = std::move(packet_or_error.value());
  packet_consumer_->OnReceivedPacket(
      packet.source(), arrival_time,
      std::move(static_cast<std::vector<uint8_t>&>(packet)));
}

}  // namespace cast_streaming
}  // namespace openscreen
