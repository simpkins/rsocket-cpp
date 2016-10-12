// Copyright 2004-present Facebook. All Rights Reserved.

#include "TcpDuplexConnection.h"
#include <folly/ExceptionWrapper.h>
#include <glog/logging.h>
#include "src/mixins/MemoryMixin.h"

namespace reactivesocket {
using namespace ::folly;

void TcpSubscriptionBase::request(size_t n) {
  // ignored for now, currently flow control is only at higher layers
}

void TcpSubscriptionBase::cancel() {
  connection_.closeFromReader();
}

std::shared_ptr<Subscriber<std::unique_ptr<folly::IOBuf>>> TcpDuplexConnection::getOutput() {
  if (!outputSubscriber_) {
    outputSubscriber_ = std::make_shared<TcpOutputSubscriber>(*this);
  }
  return outputSubscriber_;
};

void TcpDuplexConnection::setInput(
    std::shared_ptr<Subscriber<std::unique_ptr<folly::IOBuf>>> inputSubscriber) {
  inputSubscriber_.reset(std::move(inputSubscriber));
  inputSubscriber_.get()->onSubscribe(createManagedInstance<TcpSubscriptionBase>(*this));

  socket_->setReadCB(this);
};

void TcpDuplexConnection::send(std::unique_ptr<folly::IOBuf> element) {
  stats_.bytesWritten(element->computeChainDataLength());
  socket_->writeChain(this, std::move(element));
}

void TcpDuplexConnection::writeSuccess() noexcept {}

void TcpDuplexConnection::writeErr(
    size_t bytesWritten,
    const AsyncSocketException& ex) noexcept {
  if (inputSubscriber_) {
    inputSubscriber_.onError(ex);
  }
}

void TcpDuplexConnection::getReadBuffer(
    void** bufReturn,
    size_t* lenReturn) noexcept {
  std::tie(*bufReturn, *lenReturn) = readBuffer_.preallocate(4096, 4096);
}

void TcpDuplexConnection::readDataAvailable(size_t len) noexcept {
  readBuffer_.postallocate(len);

  stats_.bytesRead(len);

  if (inputSubscriber_) {
    readBufferAvailable(readBuffer_.split(len));
  }
}

void TcpDuplexConnection::readEOF() noexcept {
  if (inputSubscriber_) {
    inputSubscriber_.onError(std::runtime_error("connection closed"));
  }
}

void TcpDuplexConnection::readErr(
    const folly::AsyncSocketException& ex) noexcept {
  inputSubscriber_.onError(ex);
}

bool TcpDuplexConnection::isBufferMovable() noexcept {
  return true;
}

void TcpDuplexConnection::readBufferAvailable(
    std::unique_ptr<IOBuf> readBuf) noexcept {
  inputSubscriber_.onNext(std::move(readBuf));
}

void TcpDuplexConnection::closeFromWriter() {
  socket_->close();
}

void TcpDuplexConnection::closeFromReader() {
  socket_->close();
}

void TcpOutputSubscriber::onSubscribe(std::shared_ptr<Subscription> subscription) {
  // no flow control at tcp level, since we can't know the size of messages
  subscription->request(std::numeric_limits<size_t>::max());
};

void TcpOutputSubscriber::onNext(std::unique_ptr<folly::IOBuf> element) {
  connection_.send(std::move(element));
};

void TcpOutputSubscriber::onComplete() {
  connection_.closeFromWriter();
};

void TcpOutputSubscriber::onError(folly::exception_wrapper ex) {
  connection_.closeFromWriter();
};
}
