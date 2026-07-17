/**
 * Copyright 2013-2023 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * LIFO (stack) queue of unique_byte_buffer_t for RLC priority TX path.
 * try_write()/write() insert at front; read_active() pops newest matching current phase.
 * Non-matching (held) SDUs stay in queue and are excluded from BSR/active size.
 */

#ifndef SRSRAN_BYTE_BUFFER_LIFO_QUEUE_H
#define SRSRAN_BYTE_BUFFER_LIFO_QUEUE_H

#include "srsran/common/byte_buffer.h"

#include <cstdint>
#include <deque>
#include <iterator>
#include <mutex>
#include <utility>

namespace srsran {

class byte_buffer_lifo_queue
{
public:
  static constexpr uint8_t PHASE_UNSET = 0xFF;

  explicit byte_buffer_lifo_queue(int capacity = 128) : capacity_(capacity > 0 ? (uint32_t)capacity : 1) {}

  void write(unique_byte_buffer_t msg) { (void)try_write(std::move(msg)); }

  // Returns true on enqueue success. On failure msg remains owned by caller.
  // If full, drop held (non-current-phase) SDUs from the back to make room for active traffic.
  bool try_write(unique_byte_buffer_t&& msg)
  {
    if (msg == nullptr) {
      return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    while (q_.size() >= capacity_ && drop_one_held_from_back_unlocked()) {
    }
    if (q_.size() >= capacity_) {
      return false;
    }
    const uint32_t nbytes = msg->N_bytes;
    const uint8_t  phase  = msg->md.dscp;
    unread_bytes_ += nbytes;
    if (is_active_phase_unlocked(phase)) {
      active_bytes_ += nbytes;
      active_sdus_++;
    }
    q_.push_front(std::move(msg));
    return true;
  }

  // Pop newest SDU matching current_phase (scan from front). Held SDUs stay.
  unique_byte_buffer_t read_active()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = q_.begin(); it != q_.end(); ++it) {
      if (*it == nullptr) {
        continue;
      }
      if (not is_active_phase_unlocked((*it)->md.dscp)) {
        continue;
      }
      unique_byte_buffer_t msg = std::move(*it);
      q_.erase(it);
      account_remove_unlocked(msg);
      return msg;
    }
    return nullptr;
  }

  // Unconditional pop-front (demote/flush paths).
  unique_byte_buffer_t read()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (q_.empty()) {
      return nullptr;
    }
    unique_byte_buffer_t msg = std::move(q_.front());
    q_.pop_front();
    account_remove_unlocked(msg);
    return msg;
  }

  void set_current_phase(uint8_t phase)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_phase_ == phase) {
      return;
    }
    current_phase_ = phase;
    recount_active_unlocked();
  }

  uint8_t current_phase()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_phase_;
  }

  void resize(uint32_t capacity) { capacity_ = capacity > 0 ? capacity : 1; }

  uint32_t size()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return (uint32_t)q_.size();
  }
  uint32_t get_n_sdus() { return size(); }
  uint32_t get_n_sdus_active()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_sdus_;
  }

  uint32_t size_bytes()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return unread_bytes_;
  }
  uint32_t size_bytes_active()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_bytes_;
  }

  bool is_empty()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return q_.empty();
  }
  bool has_active()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_sdus_ > 0;
  }
  bool is_full()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return q_.size() >= capacity_;
  }

private:
  bool is_active_phase_unlocked(uint8_t phase) const
  {
    // No phase set yet: treat everything as active (legacy behaviour).
    if (current_phase_ == PHASE_UNSET) {
      return true;
    }
    return phase == current_phase_;
  }

  void account_remove_unlocked(const unique_byte_buffer_t& msg)
  {
    if (msg == nullptr) {
      return;
    }
    const uint32_t n = msg->N_bytes;
    unread_bytes_    = (unread_bytes_ >= n) ? (unread_bytes_ - n) : 0;
    if (is_active_phase_unlocked(msg->md.dscp)) {
      active_bytes_ = (active_bytes_ >= n) ? (active_bytes_ - n) : 0;
      active_sdus_  = (active_sdus_ > 0) ? (active_sdus_ - 1) : 0;
    }
  }

  void recount_active_unlocked()
  {
    active_bytes_ = 0;
    active_sdus_  = 0;
    for (const auto& sdu : q_) {
      if (sdu != nullptr && is_active_phase_unlocked(sdu->md.dscp)) {
        active_bytes_ += sdu->N_bytes;
        active_sdus_++;
      }
    }
  }

  // Drop one held SDU from the back (oldest). Returns true if dropped.
  bool drop_one_held_from_back_unlocked()
  {
    for (auto it = q_.rbegin(); it != q_.rend(); ++it) {
      if (*it == nullptr) {
        continue;
      }
      if (is_active_phase_unlocked((*it)->md.dscp)) {
        continue;
      }
      // Convert reverse_iterator to iterator for erase.
      auto fwd = std::next(it).base();
      unread_bytes_ = (unread_bytes_ >= (*fwd)->N_bytes) ? (unread_bytes_ - (*fwd)->N_bytes) : 0;
      q_.erase(fwd);
      return true;
    }
    return false;
  }

  std::mutex                       mutex_;
  std::deque<unique_byte_buffer_t> q_;
  uint32_t                         capacity_      = 128;
  uint32_t                         unread_bytes_  = 0;
  uint32_t                         active_bytes_  = 0;
  uint32_t                         active_sdus_   = 0;
  uint8_t                          current_phase_ = PHASE_UNSET;
};

} // namespace srsran

#endif // SRSRAN_BYTE_BUFFER_LIFO_QUEUE_H

