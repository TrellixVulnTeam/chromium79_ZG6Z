// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/impl/text_trace_logging_platform.h"

#include <sstream>

#include "platform/api/logging.h"

namespace openscreen {
namespace platform {

bool IsTraceLoggingEnabled(TraceCategory::Value category) {
#ifndef NDEBUG
  constexpr uint64_t kAllLogCategoriesMask =
      std::numeric_limits<uint64_t>::max();
  return (kAllLogCategoriesMask & category) != 0;
#else
  return false;
#endif
}

TextTraceLoggingPlatform::TextTraceLoggingPlatform() = default;
TextTraceLoggingPlatform::~TextTraceLoggingPlatform() = default;

void TextTraceLoggingPlatform::LogTrace(const char* name,
                                        const uint32_t line,
                                        const char* file,
                                        Clock::time_point start_time,
                                        Clock::time_point end_time,
                                        TraceIdHierarchy ids,
                                        Error::Code error) {
  auto total_runtime = std::chrono::duration_cast<std::chrono::microseconds>(
                           end_time - start_time)
                           .count();
  constexpr auto microseconds_symbol = "\u03BCs";  // Greek Mu + 's'
  std::stringstream ss;
  ss << "TRACE [" << std::hex << ids.root << ":" << ids.parent << ":"
     << ids.current << "] (" << std::dec << total_runtime << microseconds_symbol
     << ") " << name << "<" << file << ":" << line << "> " << error;

  OSP_LOG << ss.str();
}

void TextTraceLoggingPlatform::LogAsyncStart(const char* name,
                                             const uint32_t line,
                                             const char* file,
                                             Clock::time_point timestamp,
                                             TraceIdHierarchy ids) {
  std::stringstream ss;
  ss << "ASYNC TRACE START [" << std::hex << ids.root << ":" << ids.parent
     << ":" << ids.current << std::dec << "] (" << timestamp << ") " << name
     << "<" << file << ":" << line << ">";

  OSP_LOG << ss.str();
}

void TextTraceLoggingPlatform::LogAsyncEnd(const uint32_t line,
                                           const char* file,
                                           Clock::time_point timestamp,
                                           TraceId trace_id,
                                           Error::Code error) {
  OSP_LOG << "ASYNC TRACE END [" << std::hex << trace_id << std::dec << "] ("
          << timestamp << ") " << error;
}

}  // namespace platform
}  // namespace openscreen
