
/** \copyright
 * Copyright (c) 2013, Balazs Racz
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are  permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \file EventHandlerTemplates.cxx
 *
 * Implementations of common event handlers.
 *
 * @author Balazs Racz
 * @date 6 November 2013
 */

#include "nmranet/EventHandlerTemplates.hxx"
#include "if/nmranet_if.h"  // for MTI values

BitRangeEventPC::BitRangeEventPC(WriteHelper::node_type node,
                                 uint64_t event_base,
                                 uint32_t* backing_store,
                                 unsigned size)
    : event_base_(event_base), node_(node), data_(backing_store), size_(size) {
  NMRAnetEventRegistry::instance()->RegisterHandler(this, 0, 0);
}

BitRangeEventPC::~BitRangeEventPC() {
  NMRAnetEventRegistry::instance()->UnregisterHandler(this, 0, 0);
}

void BitRangeEventPC::GetBitAndMask(unsigned bit,
                                    uint32_t** data,
                                    uint32_t* mask) const {
  *data = nullptr;
  if ((bit >= size_) || (bit < 0))
    return;
  *data = data_ + (bit >> 5);
  *mask = 1 << (bit & 31);
}

bool BitRangeEventPC::Get(unsigned bit) const {
  HASSERT(bit < size_);
  uint32_t* ofs;
  uint32_t mask;
  GetBitAndMask(bit, &ofs, &mask);
  if (!ofs)
    return false;
  return (*ofs) & mask;
}

void BitRangeEventPC::Set(unsigned bit,
                          bool new_value,
                          WriteHelper* writer,
                          Notifiable* done) {
  HASSERT(bit < size_);
  uint32_t* ofs;
  uint32_t mask;
  GetBitAndMask(bit, &ofs, &mask);
  bool old_value = new_value;
  HASSERT(ofs);
  if (ofs)
    old_value = (*ofs) & mask;
  if (old_value != new_value) {
    uint64_t event = event_base_ + bit * 2;
    if (!new_value)
      event++;
    writer->WriteAsync(node_,
                       MTI_EVENT_REPORT,
                       WriteHelper::Global(),
                       EventIdToBuffer(event),
                       done);
  } else {
    if (done)
      done->Notify();
  }
}

void BitRangeEventPC::HandleEventReport(EventReport* event, Notifiable* done) {
  done->Notify();
  if (event->event < event_base_)
    return;
  uint64_t d = (event->event - event_base_);
  bool new_value = !(d & 1);
  d >>= 1;
  if (d >= size_)
    return;
  uint32_t* ofs;
  uint32_t mask;
  GetBitAndMask(d, &ofs, &mask);
  if (new_value) {
    *ofs |= mask;
  } else {
    *ofs &= ~mask;
  }
}

void BitRangeEventPC::HandleIdentifyProducer(EventReport* event,
                                             Notifiable* done) {
  HandleIdentifyBase(MTI_PRODUCER_IDENTIFIED_VALID, event, done);
}

void BitRangeEventPC::HandleIdentifyConsumer(EventReport* event,
                                             Notifiable* done) {
  HandleIdentifyBase(MTI_CONSUMER_IDENTIFIED_VALID, event, done);
}
void BitRangeEventPC::HandleIdentifyBase(int mti_valid,
                                         EventReport* event,
                                         Notifiable* done) {
  if (event->event < event_base_)
    return done->Notify();
  uint64_t d = (event->event - event_base_);
  bool new_value = !(d & 1);
  d >>= 1;
  if (d >= size_)
    return done->Notify();
  uint32_t* ofs;
  uint32_t mask;
  GetBitAndMask(d, &ofs, &mask);
  int mti = mti_valid;
  bool old_value = *ofs & mask;
  if (old_value != new_value) {
    mti++; // mti INVALID
  }

  event_write_helper1.WriteAsync(
      node_, mti, WriteHelper::Global(), EventIdToBuffer(event->event), done);
}

uint64_t EncodeRange(uint64_t begin, unsigned size) {
  // We assemble a valid event range identifier that covers our block.
  uint64_t end = begin + size * 2;
  uint64_t shift = 1;
  while (begin + shift <= end) {
    begin &= ~shift;
    shift <<= 1;
  }
  if (begin & shift) {
    // last real bit is 1 => range ends with zero.
    return begin;
  } else {
    // last real bit is zero. Set all lower bits to 1.
    begin |= shift - 1;
    return begin;
  }
}

void BitRangeEventPC::HandleIdentifyGlobal(EventReport* event,
                                           Notifiable* done) {
  uint64_t range = EncodeRange(event_base_, size_);
  event_barrier.Reset(done);
  event_write_helper1.WriteAsync(node_,
                                 MTI_PRODUCER_IDENTIFIED_RANGE,
                                 WriteHelper::Global(),
                                 EventIdToBuffer(range),
                                 event_barrier.NewChild());
  event_write_helper2.WriteAsync(node_,
                                 MTI_CONSUMER_IDENTIFIED_RANGE,
                                 WriteHelper::Global(),
                                 EventIdToBuffer(range),
                                 event_barrier.NewChild());
  event_barrier.MaybeDone();
}
