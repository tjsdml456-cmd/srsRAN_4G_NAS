/**
 * Copyright 2013-2023 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * LIFO (stack) queue of unique_byte_buffer_t for RLC priority TX path.
 * write()/try_write() insert at front; read() pops from front => newest first.
 */

#ifndef SRSRAN_BYTE_BUFFER_LIFO_QUEUE_H
#define SRSRAN_BYTE_BUFFER_LIFO_QUEUE_H

#include "srsran/adt/expected.h"
#include "srsran/common/byte_buffer.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>

namespace srsran {

class byte_buffer_lifo_queue
{
public:
  explicit byte_buffer_lifo_queue(int capacity = 128) : capacity_(capacity > 0 ? (uint32_t)capacity : 1) {}

  void write(unique_byte_buffer_t msg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // Blocking not used on this path; drop if full (same as try_write miss).
    if (q_.size() >= capacity_ || msg == nullptr) {
      return;
    }
    unread_bytes_ += msg->N_bytes;
    q_.push_front(std::move(msg));
  }

  srsran::error_type<unique_byte_buffer_t> try_write(unique_byte_buffer_t&& msg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (msg == nullptr) {
      return std::move(msg);
    }
    if (q_.size() >= capacity_) {
      return std::move(msg);
    }
    unread_bytes_ += msg->N_bytes;
    q_.push_front(std::move(msg));
    return {};
  }

  unique_byte_buffer_t read()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (q_.empty()) {
      return nullptr;
    }
    unique_byte_buffer_t msg = std::move(q_.front());
    q_.pop_front();
    if (msg != nullptr) {
      uint32_t n = msg->N_bytes;
      unread_bytes_ = (unread_bytes_ >= n) ? (unread_bytes_ - n) : 0;
    }
    return msg;
  }

  void     resize(uint32_t capacity) { capacity_ = capacity > 0 ? capacity : 1; }
  uint32_t size()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return (uint32_t)q_.size();
  }
  uint32_t get_n_sdus() { return size(); }
  uint32_t size_bytes()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return unread_bytes_;
  }
  bool is_empty()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return q_.empty();
  }
  bool is_full()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return q_.size() >= capacity_;
  }

private:
  std::mutex                        mutex_;
  std::deque<unique_byte_buffer_t>  q_;
  uint32_t                          capacity_    = 128;
  uint32_t                          unread_bytes_ = 0;
};

} // namespace srsran

#endif // SRSRAN_BYTE_BUFFER_LIFO_QUEUE_H

