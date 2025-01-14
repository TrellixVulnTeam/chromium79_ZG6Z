/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_TRACE_PROCESSOR_DB_BIT_VECTOR_ITERATORS_H_
#define SRC_TRACE_PROCESSOR_DB_BIT_VECTOR_ITERATORS_H_

#include "src/trace_processor/db/bit_vector.h"

namespace perfetto {
namespace trace_processor {
namespace internal {

// Base iterator class for all iterators on BitVector.
//
// This class implements caching of one Block at a time to reduce pointer
// chasing. It also defers updating counts on Clear calls until the end of each
// block.
class BaseIterator {
 public:
  BaseIterator(BitVector* bv);
  ~BaseIterator();

  BaseIterator(BaseIterator&&) noexcept = default;
  BaseIterator& operator=(BaseIterator&&) = default;

  // Clears the current bit the iterator points to.
  void Clear() {
    if (IsSet()) {
      block_.Clear(block_offset());

      is_block_changed_ = true;
      --set_bit_count_diff_;
    }
  }

  // Returns whether the current bit the iterator points to is set.
  bool IsSet() { return block_.IsSet(block_offset()); }

  // Returns the index of the current bit the iterator points to.
  uint32_t index() const { return index_; }

 protected:
  // Sets the index this iterator points to the given value.
  //
  // This method also performs some extra work on block boundaries
  // as it caches the block to improve performance by reducing pointer
  // chasing on every IsSet and Clear calls.
  void SetIndex(uint32_t index) {
    // We should always move the index forward.
    PERFETTO_DCHECK(index >= index_);

    uint32_t old_index = index_;
    index_ = index;

    // If we've reached the end of the iterator, just bail out.
    if (index >= size_)
      return;

    uint32_t old_block = old_index / BitVector::Block::kBits;
    uint32_t new_block = index / BitVector::Block::kBits;

    // Fast path: we're in the same block so we don't need to do
    // any other work.
    if (PERFETTO_LIKELY(old_block == new_block))
      return;

    // Slow path: we have to change block so this will involve flushing the old
    // block and counts (if necessary) and caching the new block.
    OnBlockChange(old_block, new_block);
  }

  // Handles flushing count changes and modified blocks to the bitvector
  // and caching the new block.
  void OnBlockChange(uint32_t old_block, uint32_t new_block);

  uint32_t size() const { return size_; }

 private:
  BaseIterator(const BaseIterator&) = delete;
  BaseIterator& operator=(const BaseIterator&) = delete;

  BitVector::BlockOffset block_offset() const {
    uint16_t bit_idx_inside_block = index_ % BitVector::Block::kBits;

    BitVector::BlockOffset bo;
    bo.word_idx = bit_idx_inside_block / BitVector::BitWord::kBits;
    bo.bit_idx = bit_idx_inside_block % BitVector::BitWord::kBits;
    return bo;
  }

  uint32_t index_ = 0;
  uint32_t size_ = 0;

  bool is_block_changed_ = false;
  int32_t set_bit_count_diff_ = 0;

  BitVector* bv_ = nullptr;

  BitVector::Block block_{};
};

// Iterator over all the bits in a bitvector.
class AllBitsIterator : public BaseIterator {
 public:
  AllBitsIterator(const BitVector*);

  // Increments the iterator to point to the next bit.
  void Next() { SetIndex(index() + 1); }

  // Returns whether the iterator is valid.
  operator bool() const { return index() < size(); }
};

}  // namespace internal
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_DB_BIT_VECTOR_ITERATORS_H_
