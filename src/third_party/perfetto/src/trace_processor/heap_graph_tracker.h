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

#ifndef SRC_TRACE_PROCESSOR_HEAP_GRAPH_TRACKER_H_
#define SRC_TRACE_PROCESSOR_HEAP_GRAPH_TRACKER_H_

#include <map>
#include <vector>

#include "perfetto/ext/base/optional.h"

#include "protos/perfetto/trace/profiling/heap_graph.pbzero.h"
#include "src/trace_processor/trace_processor_context.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

class TraceProcessorContext;

class HeapGraphTracker {
 public:
  struct SourceObject {
    // All ids in this are in the trace iid space, not in the trace processor
    // id space.
    struct Reference {
      uint64_t field_name_id = 0;
      uint64_t owned_object_id = 0;
    };
    uint64_t object_id = 0;
    uint64_t self_size = 0;
    uint64_t type_id = 0;
    std::vector<Reference> references;
  };

  struct SourceRoot {
    StringPool::Id root_type;
    std::vector<uint64_t> object_ids;
  };

  explicit HeapGraphTracker(TraceProcessorContext* context);

  void AddRoot(UniquePid upid, int64_t ts, SourceRoot root);
  void AddObject(UniquePid upid, int64_t ts, SourceObject obj);
  void AddInternedTypeName(uint64_t intern_id, StringPool::Id strid);
  void AddInternedFieldName(uint64_t intern_id, StringPool::Id strid);
  void FinalizeProfile();
  void SetPacketIndex(uint64_t index);

 private:
  bool SetPidAndTimestamp(UniquePid upid, int64_t ts);
  TraceProcessorContext* const context_;

  UniquePid current_upid_ = 0;
  int64_t current_ts_ = 0;
  std::vector<SourceObject> current_objects_;
  std::vector<SourceRoot> current_roots_;
  std::map<uint64_t, StringPool::Id> interned_type_names_;
  std::map<uint64_t, StringPool::Id> interned_field_names_;
  std::map<uint64_t, int64_t> object_id_to_row_;
  uint64_t prev_index_ = 0;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_HEAP_GRAPH_TRACKER_H_
