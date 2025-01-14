// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_PAINT_EVENT_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_PAINT_EVENT_H_

namespace blink {

// Paint events that either PaintTiming or FirstMeaningfulPaintDetector receive
// SwapPromise swap times for.
enum class PaintEvent {
  kFirstPaint,
  kFirstContentfulPaint,
  kProvisionalFirstMeaningfulPaint,
  kFirstImagePaint,
#if defined(OS_WEBOS)
  kFirstContainerResetPaint,
#endif
};

}  // namespace blink

#endif
