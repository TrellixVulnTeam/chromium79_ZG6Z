/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/trace_processor/proto_trace_parser.h"

#include <inttypes.h>
#include <string.h>

#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/metatrace_events.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/string_writer.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/traced/sys_stats_counters.h"
#include "perfetto/protozero/proto_decoder.h"
#include "src/trace_processor/args_tracker.h"
#include "src/trace_processor/clock_tracker.h"
#include "src/trace_processor/event_tracker.h"
#include "src/trace_processor/ftrace_descriptors.h"
#include "src/trace_processor/heap_graph_tracker.h"
#include "src/trace_processor/heap_profile_tracker.h"
#include "src/trace_processor/metadata.h"
#include "src/trace_processor/process_tracker.h"
#include "src/trace_processor/proto_incremental_state.h"
#include "src/trace_processor/stack_profile_tracker.h"
#include "src/trace_processor/syscall_tracker.h"
#include "src/trace_processor/systrace_parser.h"
#include "src/trace_processor/timestamped_trace_piece.h"
#include "src/trace_processor/trace_processor_context.h"
#include "src/trace_processor/track_tracker.h"
#include "src/trace_processor/variadic.h"

#include "protos/perfetto/common/android_log_constants.pbzero.h"
#include "protos/perfetto/common/trace_stats.pbzero.h"
#include "protos/perfetto/config/trace_config.pbzero.h"
#include "protos/perfetto/trace/android/android_log.pbzero.h"
#include "protos/perfetto/trace/android/packages_list.pbzero.h"
#include "protos/perfetto/trace/chrome/chrome_benchmark_metadata.pbzero.h"
#include "protos/perfetto/trace/chrome/chrome_trace_event.pbzero.h"
#include "protos/perfetto/trace/clock_snapshot.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_stats.pbzero.h"
#include "protos/perfetto/trace/ftrace/generic.pbzero.h"
#include "protos/perfetto/trace/ftrace/kmem.pbzero.h"
#include "protos/perfetto/trace/ftrace/lowmemorykiller.pbzero.h"
#include "protos/perfetto/trace/ftrace/mm_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/oom.pbzero.h"
#include "protos/perfetto/trace/ftrace/power.pbzero.h"
#include "protos/perfetto/trace/ftrace/raw_syscalls.pbzero.h"
#include "protos/perfetto/trace/ftrace/sched.pbzero.h"
#include "protos/perfetto/trace/ftrace/signal.pbzero.h"
#include "protos/perfetto/trace/ftrace/systrace.pbzero.h"
#include "protos/perfetto/trace/ftrace/task.pbzero.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/perfetto/perfetto_metatrace.pbzero.h"
#include "protos/perfetto/trace/power/battery_counters.pbzero.h"
#include "protos/perfetto/trace/power/power_rails.pbzero.h"
#include "protos/perfetto/trace/profiling/heap_graph.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_common.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_packet.pbzero.h"
#include "protos/perfetto/trace/ps/process_stats.pbzero.h"
#include "protos/perfetto/trace/ps/process_tree.pbzero.h"
#include "protos/perfetto/trace/sys_stats/sys_stats.pbzero.h"
#include "protos/perfetto/trace/system_info.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/perfetto/trace/track_event/debug_annotation.pbzero.h"
#include "protos/perfetto/trace/track_event/log_message.pbzero.h"
#include "protos/perfetto/trace/track_event/source_location.pbzero.h"
#include "protos/perfetto/trace/track_event/task_execution.pbzero.h"

namespace perfetto {
namespace trace_processor {

namespace {

// kthreadd is the parent process for all kernel threads and always has
// pid == 2 on Linux and Android.
const uint32_t kKthreaddPid = 2;
const char kKthreaddName[] = "kthreadd";

using protozero::ProtoDecoder;

StackProfileTracker::SourceMapping MakeSourceMapping(
    const protos::pbzero::Mapping::Decoder& entry) {
  StackProfileTracker::SourceMapping src_mapping{};
  src_mapping.build_id = entry.build_id();
  src_mapping.exact_offset = entry.exact_offset();
  src_mapping.start_offset = entry.start_offset();
  src_mapping.start = entry.start();
  src_mapping.end = entry.end();
  src_mapping.load_bias = entry.load_bias();
  for (auto path_string_id_it = entry.path_string_ids(); path_string_id_it;
       ++path_string_id_it)
    src_mapping.name_ids.emplace_back(path_string_id_it->as_uint32());
  return src_mapping;
}

StackProfileTracker::SourceFrame MakeSourceFrame(
    const protos::pbzero::Frame::Decoder& entry) {
  StackProfileTracker::SourceFrame src_frame;
  src_frame.name_id = entry.function_name_id();
  src_frame.mapping_id = entry.mapping_id();
  src_frame.rel_pc = entry.rel_pc();
  return src_frame;
}

StackProfileTracker::SourceCallstack MakeSourceCallstack(
    const protos::pbzero::Callstack::Decoder& entry) {
  StackProfileTracker::SourceCallstack src_callstack;
  for (auto frame_it = entry.frame_ids(); frame_it; ++frame_it)
    src_callstack.emplace_back(frame_it->as_uint64());
  return src_callstack;
}

class ProfilePacketInternLookup : public StackProfileTracker::InternLookup {
 public:
  ProfilePacketInternLookup(
      ProtoIncrementalState::PacketSequenceState* seq_state,
      size_t seq_state_generation)
      : seq_state_(seq_state), seq_state_generation_(seq_state_generation) {}

  base::Optional<base::StringView> GetString(
      StackProfileTracker::SourceStringId iid,
      StackProfileTracker::InternedStringType type) const override {
    protos::pbzero::InternedString::Decoder* decoder = nullptr;
    switch (type) {
      case StackProfileTracker::InternedStringType::kBuildId:
        decoder = seq_state_->LookupInternedMessage<
            protos::pbzero::InternedData::kBuildIdsFieldNumber,
            protos::pbzero::InternedString>(seq_state_generation_, iid);
        break;
      case StackProfileTracker::InternedStringType::kFunctionName:
        decoder = seq_state_->LookupInternedMessage<
            protos::pbzero::InternedData::kFunctionNamesFieldNumber,
            protos::pbzero::InternedString>(seq_state_generation_, iid);
        break;
      case StackProfileTracker::InternedStringType::kMappingPath:
        decoder = seq_state_->LookupInternedMessage<
            protos::pbzero::InternedData::kMappingPathsFieldNumber,
            protos::pbzero::InternedString>(seq_state_generation_, iid);
        break;
    }
    if (!decoder)
      return base::nullopt;
    return base::StringView(reinterpret_cast<const char*>(decoder->str().data),
                            decoder->str().size);
  }

  base::Optional<StackProfileTracker::SourceMapping> GetMapping(
      StackProfileTracker::SourceMappingId iid) const override {
    auto* decoder = seq_state_->LookupInternedMessage<
        protos::pbzero::InternedData::kMappingsFieldNumber,
        protos::pbzero::Mapping>(seq_state_generation_, iid);
    if (!decoder)
      return base::nullopt;
    return MakeSourceMapping(*decoder);
  }

  base::Optional<StackProfileTracker::SourceFrame> GetFrame(
      StackProfileTracker::SourceFrameId iid) const override {
    auto* decoder = seq_state_->LookupInternedMessage<
        protos::pbzero::InternedData::kFramesFieldNumber,
        protos::pbzero::Frame>(seq_state_generation_, iid);
    if (!decoder)
      return base::nullopt;
    return MakeSourceFrame(*decoder);
  }

  base::Optional<StackProfileTracker::SourceCallstack> GetCallstack(
      StackProfileTracker::SourceCallstackId iid) const override {
    auto* decoder = seq_state_->LookupInternedMessage<
        protos::pbzero::InternedData::kCallstacksFieldNumber,
        protos::pbzero::Callstack>(seq_state_generation_, iid);
    if (!decoder)
      return base::nullopt;
    return MakeSourceCallstack(*decoder);
  }

 private:
  ProtoIncrementalState::PacketSequenceState* seq_state_;
  size_t seq_state_generation_;
};

namespace {
// Slices which have been opened but haven't been closed yet will be marked
// with these placeholder values.
constexpr int64_t kPendingThreadDuration = -1;
constexpr int64_t kPendingThreadInstructionDelta = -1;
}  // namespace

const char* HeapGraphRootTypeToString(int32_t type) {
  switch (type) {
    case protos::pbzero::HeapGraphRoot::ROOT_UNKNOWN:
      return "ROOT_UNKNOWN";
    case protos::pbzero::HeapGraphRoot::ROOT_JNI_GLOBAL:
      return "ROOT_JNI_GLOBAL";
    case protos::pbzero::HeapGraphRoot::ROOT_JNI_LOCAL:
      return "ROOT_JNI_LOCAL";
    case protos::pbzero::HeapGraphRoot::ROOT_JAVA_FRAME:
      return "ROOT_JAVA_FRAME";
    case protos::pbzero::HeapGraphRoot::ROOT_NATIVE_STACK:
      return "ROOT_NATIVE_STACK";
    case protos::pbzero::HeapGraphRoot::ROOT_STICKY_CLASS:
      return "ROOT_STICKY_CLASS";
    case protos::pbzero::HeapGraphRoot::ROOT_THREAD_BLOCK:
      return "ROOT_THREAD_BLOCK";
    case protos::pbzero::HeapGraphRoot::ROOT_MONITOR_USED:
      return "ROOT_MONITOR_USED";
    case protos::pbzero::HeapGraphRoot::ROOT_THREAD_OBJECT:
      return "ROOT_THREAD_OBJECT";
    case protos::pbzero::HeapGraphRoot::ROOT_INTERNED_STRING:
      return "ROOT_INTERNED_STRING";
    case protos::pbzero::HeapGraphRoot::ROOT_FINALIZING:
      return "ROOT_FINALIZING";
    case protos::pbzero::HeapGraphRoot::ROOT_DEBUGGER:
      return "ROOT_DEBUGGER";
    case protos::pbzero::HeapGraphRoot::ROOT_REFERENCE_CLEANUP:
      return "ROOT_REFERENCE_CLEANUP";
    case protos::pbzero::HeapGraphRoot::ROOT_VM_INTERNAL:
      return "ROOT_VM_INTERNAL";
    case protos::pbzero::HeapGraphRoot::ROOT_JNI_MONITOR:
      return "ROOT_JNI_MONITOR";
    default:
      return "ROOT_UNKNOWN";
  }
}

}  // namespace

ProtoTraceParser::ProtoTraceParser(TraceProcessorContext* context)
    : context_(context),
      graphics_event_parser_(new GraphicsEventParser(context_)),
      utid_name_id_(context->storage->InternString("utid")),
      sched_wakeup_name_id_(context->storage->InternString("sched_wakeup")),
      sched_waking_name_id_(context->storage->InternString("sched_waking")),
      cpu_freq_name_id_(context->storage->InternString("cpufreq")),
      cpu_idle_name_id_(context->storage->InternString("cpuidle")),
      gpu_freq_name_id_(context->storage->InternString("gpufreq")),
      comm_name_id_(context->storage->InternString("comm")),
      num_forks_name_id_(context->storage->InternString("num_forks")),
      num_irq_total_name_id_(context->storage->InternString("num_irq_total")),
      num_softirq_total_name_id_(
          context->storage->InternString("num_softirq_total")),
      num_irq_name_id_(context->storage->InternString("num_irq")),
      num_softirq_name_id_(context->storage->InternString("num_softirq")),
      cpu_times_user_ns_id_(
          context->storage->InternString("cpu.times.user_ns")),
      cpu_times_user_nice_ns_id_(
          context->storage->InternString("cpu.times.user_nice_ns")),
      cpu_times_system_mode_ns_id_(
          context->storage->InternString("cpu.times.system_mode_ns")),
      cpu_times_idle_ns_id_(
          context->storage->InternString("cpu.times.idle_ns")),
      cpu_times_io_wait_ns_id_(
          context->storage->InternString("cpu.times.io_wait_ns")),
      cpu_times_irq_ns_id_(context->storage->InternString("cpu.times.irq_ns")),
      cpu_times_softirq_ns_id_(
          context->storage->InternString("cpu.times.softirq_ns")),
      signal_deliver_id_(context->storage->InternString("signal_deliver")),
      signal_generate_id_(context->storage->InternString("signal_generate")),
      batt_charge_id_(context->storage->InternString("batt.charge_uah")),
      batt_capacity_id_(context->storage->InternString("batt.capacity_pct")),
      batt_current_id_(context->storage->InternString("batt.current_ua")),
      batt_current_avg_id_(
          context->storage->InternString("batt.current.avg_ua")),
      lmk_id_(context->storage->InternString("mem.lmk")),
      oom_score_adj_id_(context->storage->InternString("oom_score_adj")),
      ion_total_unknown_id_(context->storage->InternString("mem.ion.unknown")),
      ion_change_unknown_id_(
          context->storage->InternString("mem.ion_change.unknown")),
      metatrace_id_(context->storage->InternString("metatrace")),
      task_file_name_args_key_id_(
          context->storage->InternString("task.posted_from.file_name")),
      task_function_name_args_key_id_(
          context->storage->InternString("task.posted_from.function_name")),
      task_line_number_args_key_id_(
          context->storage->InternString("task.posted_from.line_number")),
      log_message_body_key_id_(
          context->storage->InternString("track_event.log_message")),
      data_name_id_(context->storage->InternString("data")),
      raw_chrome_metadata_event_id_(
          context->storage->InternString("chrome_event.metadata")),
      raw_chrome_legacy_system_trace_event_id_(
          context->storage->InternString("chrome_event.legacy_system_trace")),
      raw_chrome_legacy_user_trace_event_id_(
          context->storage->InternString("chrome_event.legacy_user_trace")),
      raw_legacy_event_id_(
          context->storage->InternString("track_event.legacy_event")),
      legacy_event_category_key_id_(
          context->storage->InternString("legacy_event.category")),
      legacy_event_name_key_id_(
          context->storage->InternString("legacy_event.name")),
      legacy_event_phase_key_id_(
          context->storage->InternString("legacy_event.phase")),
      legacy_event_duration_ns_key_id_(
          context->storage->InternString("legacy_event.duration_ns")),
      legacy_event_thread_timestamp_ns_key_id_(
          context->storage->InternString("legacy_event.thread_timestamp_ns")),
      legacy_event_thread_duration_ns_key_id_(
          context->storage->InternString("legacy_event.thread_duration_ns")),
      legacy_event_thread_instruction_count_key_id_(
          context->storage->InternString(
              "legacy_event.thread_instruction_count")),
      legacy_event_thread_instruction_delta_key_id_(
          context->storage->InternString(
              "legacy_event.thread_instruction_delta")),
      legacy_event_use_async_tts_key_id_(
          context->storage->InternString("legacy_event.use_async_tts")),
      legacy_event_unscoped_id_key_id_(
          context->storage->InternString("legacy_event.unscoped_id")),
      legacy_event_global_id_key_id_(
          context->storage->InternString("legacy_event.global_id")),
      legacy_event_local_id_key_id_(
          context->storage->InternString("legacy_event.local_id")),
      legacy_event_id_scope_key_id_(
          context->storage->InternString("legacy_event.id_scope")),
      legacy_event_bind_id_key_id_(
          context->storage->InternString("legacy_event.bind_id")),
      legacy_event_bind_to_enclosing_key_id_(
          context->storage->InternString("legacy_event.bind_to_enclosing")),
      legacy_event_flow_direction_key_id_(
          context->storage->InternString("legacy_event.flow_direction")),
      flow_direction_value_in_id_(context->storage->InternString("in")),
      flow_direction_value_out_id_(context->storage->InternString("out")),
      flow_direction_value_inout_id_(context->storage->InternString("inout")) {
  for (const auto& name : BuildMeminfoCounterNames()) {
    meminfo_strs_id_.emplace_back(context->storage->InternString(name));
  }
  for (const auto& name : BuildVmstatCounterNames()) {
    vmstat_strs_id_.emplace_back(context->storage->InternString(name));
  }
  rss_members_.emplace_back(context->storage->InternString("mem.rss.file"));
  rss_members_.emplace_back(context->storage->InternString("mem.rss.anon"));
  rss_members_.emplace_back(context->storage->InternString("mem.swap"));
  rss_members_.emplace_back(context->storage->InternString("mem.rss.shmem"));
  rss_members_.emplace_back(
      context->storage->InternString("mem.rss.unknown"));  // Keep this last.

  using ProcessStats = protos::pbzero::ProcessStats;
  proc_stats_process_names_[ProcessStats::Process::kVmSizeKbFieldNumber] =
      context->storage->InternString("mem.virt");
  proc_stats_process_names_[ProcessStats::Process::kVmRssKbFieldNumber] =
      context->storage->InternString("mem.rss");
  proc_stats_process_names_[ProcessStats::Process::kRssAnonKbFieldNumber] =
      context->storage->InternString("mem.rss.anon");
  proc_stats_process_names_[ProcessStats::Process::kRssFileKbFieldNumber] =
      context->storage->InternString("mem.rss.file");
  proc_stats_process_names_[ProcessStats::Process::kRssShmemKbFieldNumber] =
      context->storage->InternString("mem.rss.shmem");
  proc_stats_process_names_[ProcessStats::Process::kVmSwapKbFieldNumber] =
      context->storage->InternString("mem.swap");
  proc_stats_process_names_[ProcessStats::Process::kVmLockedKbFieldNumber] =
      context->storage->InternString("mem.locked");
  proc_stats_process_names_[ProcessStats::Process::kVmHwmKbFieldNumber] =
      context->storage->InternString("mem.rss.watermark");
  proc_stats_process_names_[ProcessStats::Process::kOomScoreAdjFieldNumber] =
      oom_score_adj_id_;

  mm_event_counter_names_ = {
      {MmEventCounterNames(
           context->storage->InternString("mem.mm.min_flt.count"),
           context->storage->InternString("mem.mm.min_flt.max_lat"),
           context->storage->InternString("mem.mm.min_flt.avg_lat")),
       MmEventCounterNames(
           context->storage->InternString("mem.mm.maj_flt.count"),
           context->storage->InternString("mem.mm.maj_flt.max_lat"),
           context->storage->InternString("mem.mm.maj_flt.avg_lat")),
       MmEventCounterNames(
           context->storage->InternString("mem.mm.read_io.count"),
           context->storage->InternString("mem.mm.read_io.max_lat"),
           context->storage->InternString("mem.mm.read_io.avg_lat")),
       MmEventCounterNames(
           context->storage->InternString("mem.mm.compaction.count"),
           context->storage->InternString("mem.mm.compaction.max_lat"),
           context->storage->InternString("mem.mm.compaction.avg_lat")),
       MmEventCounterNames(
           context->storage->InternString("mem.mm.reclaim.count"),
           context->storage->InternString("mem.mm.reclaim.max_lat"),
           context->storage->InternString("mem.mm.reclaim.avg_lat")),
       MmEventCounterNames(
           context->storage->InternString("mem.mm.swp_flt.count"),
           context->storage->InternString("mem.mm.swp_flt.max_lat"),
           context->storage->InternString("mem.mm.swp_flt.avg_lat")),
       MmEventCounterNames(
           context->storage->InternString("mem.mm.kern_alloc.count"),
           context->storage->InternString("mem.mm.kern_alloc.max_lat"),
           context->storage->InternString("mem.mm.kern_alloc.avg_lat"))}};

  // TODO(140860736): Once we support null values for
  // stack_profile_frame.symbol_set_id remove this hack
  context_->storage->mutable_symbol_table()->Insert({0, 0, 0, 0});

  // Build the lookup table for the strings inside ftrace events (e.g. the
  // name of ftrace event fields and the names of their args).
  for (size_t i = 0; i < GetDescriptorsSize(); i++) {
    auto* descriptor = GetMessageDescriptorForId(i);
    if (!descriptor->name) {
      ftrace_message_strings_.emplace_back();
      continue;
    }

    FtraceMessageStrings ftrace_strings;
    ftrace_strings.message_name_id =
        context->storage->InternString(descriptor->name);

    for (size_t fid = 0; fid <= descriptor->max_field_id; fid++) {
      const auto& field = descriptor->fields[fid];
      if (!field.name)
        continue;
      ftrace_strings.field_name_ids[fid] =
          context->storage->InternString(field.name);
    }
    ftrace_message_strings_.emplace_back(ftrace_strings);
  }
}

ProtoTraceParser::~ProtoTraceParser() = default;

void ProtoTraceParser::ParseTracePacket(int64_t ts, TimestampedTracePiece ttp) {
  PERFETTO_DCHECK(ttp.json_value == nullptr);
  const TraceBlobView& blob = ttp.blob_view;

  protos::pbzero::TracePacket::Decoder packet(blob.data(), blob.length());

  if (packet.has_process_tree())
    ParseProcessTree(packet.process_tree());

  if (packet.has_process_stats())
    ParseProcessStats(ts, packet.process_stats());

  if (packet.has_sys_stats())
    ParseSysStats(ts, packet.sys_stats());

  if (packet.has_battery())
    ParseBatteryCounters(ts, packet.battery());

  if (packet.has_power_rails())
    ParsePowerRails(packet.power_rails());

  if (packet.has_trace_stats())
    ParseTraceStats(packet.trace_stats());

  if (packet.has_ftrace_stats())
    ParseFtraceStats(packet.ftrace_stats());

  if (packet.has_android_log())
    ParseAndroidLogPacket(packet.android_log());

  if (packet.has_profile_packet()) {
    ParseProfilePacket(ts, ttp.packet_sequence_state,
                       ttp.packet_sequence_state_generation,
                       packet.profile_packet());
  }

  if (packet.has_streaming_profile_packet()) {
    ParseStreamingProfilePacket(ttp.packet_sequence_state,
                                ttp.packet_sequence_state_generation,
                                packet.streaming_profile_packet());
  }

  if (packet.has_system_info())
    ParseSystemInfo(packet.system_info());

  if (packet.has_track_event()) {
    ParseTrackEvent(ts, ttp.thread_timestamp, ttp.thread_instruction_count,
                    ttp.packet_sequence_state,
                    ttp.packet_sequence_state_generation, packet.track_event());
  }

  if (packet.has_chrome_benchmark_metadata()) {
    ParseChromeBenchmarkMetadata(packet.chrome_benchmark_metadata());
  }

  if (packet.has_chrome_events()) {
    ParseChromeEvents(ts, packet.chrome_events());
  }

  if (packet.has_perfetto_metatrace()) {
    ParseMetatraceEvent(ts, packet.perfetto_metatrace());
  }

  if (packet.has_gpu_counter_event()) {
    graphics_event_parser_->ParseGpuCounterEvent(ts,
                                                 packet.gpu_counter_event());
  }

  if (packet.has_gpu_render_stage_event()) {
    graphics_event_parser_->ParseGpuRenderStageEvent(
        ts, packet.gpu_render_stage_event());
  }

  if (packet.has_trace_config()) {
    ParseTraceConfig(packet.trace_config());
  }

  if (packet.has_gpu_log()) {
    graphics_event_parser_->ParseGpuLog(ts, packet.gpu_log());
  }

  if (packet.has_packages_list()) {
    ParseAndroidPackagesList(packet.packages_list());
  }

  if (packet.has_graphics_frame_event()) {
    graphics_event_parser_->ParseGraphicsFrameEvent(
        ts, packet.graphics_frame_event());
  }

  if (packet.has_module_symbols()) {
    ParseModuleSymbols(packet.module_symbols());
  }

  if (packet.has_heap_graph()) {
    ParseHeapGraph(ts, packet.heap_graph());
  }

  if (packet.has_vulkan_memory_event()) {
    graphics_event_parser_->ParseVulkanMemoryEvent(
        packet.vulkan_memory_event());
  }

  // TODO(lalitm): maybe move this to the flush method in the trace processor
  // once we have it. This may reduce performance in the ArgsTracker though so
  // needs to be handled carefully.
  context_->args_tracker->Flush();
  PERFETTO_DCHECK(!packet.bytes_left());
}

void ProtoTraceParser::ParseSysStats(int64_t ts, ConstBytes blob) {
  protos::pbzero::SysStats::Decoder sys_stats(blob.data, blob.size);

  for (auto it = sys_stats.meminfo(); it; ++it) {
    protos::pbzero::SysStats::MeminfoValue::Decoder mi(it->data(), it->size());
    auto key = static_cast<size_t>(mi.key());
    if (PERFETTO_UNLIKELY(key >= meminfo_strs_id_.size())) {
      PERFETTO_ELOG("MemInfo key %zu is not recognized.", key);
      context_->storage->IncrementStats(stats::meminfo_unknown_keys);
      continue;
    }
    // /proc/meminfo counters are in kB, convert to bytes
    context_->event_tracker->PushCounter(
        ts, mi.value() * 1024L, meminfo_strs_id_[key], 0, RefType::kRefNoRef);
  }

  for (auto it = sys_stats.vmstat(); it; ++it) {
    protos::pbzero::SysStats::VmstatValue::Decoder vm(it->data(), it->size());
    auto key = static_cast<size_t>(vm.key());
    if (PERFETTO_UNLIKELY(key >= vmstat_strs_id_.size())) {
      PERFETTO_ELOG("VmStat key %zu is not recognized.", key);
      context_->storage->IncrementStats(stats::vmstat_unknown_keys);
      continue;
    }
    context_->event_tracker->PushCounter(ts, vm.value(), vmstat_strs_id_[key],
                                         0, RefType::kRefNoRef);
  }

  for (auto it = sys_stats.cpu_stat(); it; ++it) {
    protos::pbzero::SysStats::CpuTimes::Decoder ct(it->data(), it->size());
    if (PERFETTO_UNLIKELY(!ct.has_cpu_id())) {
      PERFETTO_ELOG("CPU field not found in CpuTimes");
      context_->storage->IncrementStats(stats::invalid_cpu_times);
      continue;
    }
    context_->event_tracker->PushCounter(ts, ct.user_ns(),
                                         cpu_times_user_ns_id_, ct.cpu_id(),
                                         RefType::kRefCpuId);
    context_->event_tracker->PushCounter(ts, ct.user_ice_ns(),
                                         cpu_times_user_nice_ns_id_,
                                         ct.cpu_id(), RefType::kRefCpuId);
    context_->event_tracker->PushCounter(ts, ct.system_mode_ns(),
                                         cpu_times_system_mode_ns_id_,
                                         ct.cpu_id(), RefType::kRefCpuId);
    context_->event_tracker->PushCounter(ts, ct.idle_ns(),
                                         cpu_times_idle_ns_id_, ct.cpu_id(),
                                         RefType::kRefCpuId);
    context_->event_tracker->PushCounter(ts, ct.io_wait_ns(),
                                         cpu_times_io_wait_ns_id_, ct.cpu_id(),
                                         RefType::kRefCpuId);
    context_->event_tracker->PushCounter(ts, ct.irq_ns(), cpu_times_irq_ns_id_,
                                         ct.cpu_id(), RefType::kRefCpuId);
    context_->event_tracker->PushCounter(ts, ct.softirq_ns(),
                                         cpu_times_softirq_ns_id_, ct.cpu_id(),
                                         RefType::kRefCpuId);
  }

  for (auto it = sys_stats.num_irq(); it; ++it) {
    protos::pbzero::SysStats::InterruptCount::Decoder ic(it->data(),
                                                         it->size());
    context_->event_tracker->PushCounter(ts, ic.count(), num_irq_name_id_,
                                         ic.irq(), RefType::kRefIrq);
  }

  for (auto it = sys_stats.num_softirq(); it; ++it) {
    protos::pbzero::SysStats::InterruptCount::Decoder ic(it->data(),
                                                         it->size());
    context_->event_tracker->PushCounter(ts, ic.count(), num_softirq_name_id_,
                                         ic.irq(), RefType::kRefSoftIrq);
  }

  if (sys_stats.has_num_forks()) {
    context_->event_tracker->PushCounter(
        ts, sys_stats.num_forks(), num_forks_name_id_, 0, RefType::kRefNoRef);
  }

  if (sys_stats.has_num_irq_total()) {
    context_->event_tracker->PushCounter(ts, sys_stats.num_irq_total(),
                                         num_irq_total_name_id_, 0,
                                         RefType::kRefNoRef);
  }

  if (sys_stats.has_num_softirq_total()) {
    context_->event_tracker->PushCounter(ts, sys_stats.num_softirq_total(),
                                         num_softirq_total_name_id_, 0,
                                         RefType::kRefNoRef);
  }
}

void ProtoTraceParser::ParseProcessTree(ConstBytes blob) {
  protos::pbzero::ProcessTree::Decoder ps(blob.data, blob.size);

  for (auto it = ps.processes(); it; ++it) {
    protos::pbzero::ProcessTree::Process::Decoder proc(it->data(), it->size());
    if (!proc.has_cmdline())
      continue;
    auto pid = static_cast<uint32_t>(proc.pid());
    auto ppid = static_cast<uint32_t>(proc.ppid());

    // If the parent pid is kthreadd's pid, even though this pid is of a
    // "process", we want to treat it as being a child thread of kthreadd.
    if (ppid == kKthreaddPid) {
      context_->process_tracker->SetProcessMetadata(kKthreaddPid, base::nullopt,
                                                    kKthreaddName);
      context_->process_tracker->UpdateThread(pid, kKthreaddPid);
    } else {
      context_->process_tracker->SetProcessMetadata(
          pid, ppid, proc.cmdline()->as_string());
    }
  }

  for (auto it = ps.threads(); it; ++it) {
    protos::pbzero::ProcessTree::Thread::Decoder thd(it->data(), it->size());
    auto tid = static_cast<uint32_t>(thd.tid());
    auto tgid = static_cast<uint32_t>(thd.tgid());
    context_->process_tracker->UpdateThread(tid, tgid);

    if (thd.has_name()) {
      StringId threadNameId = context_->storage->InternString(thd.name());
      context_->process_tracker->UpdateThreadName(tid, threadNameId);
    }
  }
}

void ProtoTraceParser::ParseProcessStats(int64_t ts, ConstBytes blob) {
  protos::pbzero::ProcessStats::Decoder stats(blob.data, blob.size);
  const auto kOomScoreAdjFieldNumber =
      protos::pbzero::ProcessStats::Process::kOomScoreAdjFieldNumber;
  for (auto it = stats.processes(); it; ++it) {
    // Maps a process counter field it to its value.
    // E.g., 4 := 1024 -> "mem.rss.anon" := 1024.
    std::array<int64_t, kProcStatsProcessSize> counter_values{};
    std::array<bool, kProcStatsProcessSize> has_counter{};

    ProtoDecoder proc(it->data(), it->size());
    uint32_t pid = 0;
    for (auto fld = proc.ReadField(); fld.valid(); fld = proc.ReadField()) {
      if (fld.id() == protos::pbzero::ProcessStats::Process::kPidFieldNumber) {
        pid = fld.as_uint32();
        continue;
      }
      bool is_counter_field = fld.id() < proc_stats_process_names_.size() &&
                              proc_stats_process_names_[fld.id()] != 0;
      if (is_counter_field) {
        // Memory counters are in KB, keep values in bytes in the trace
        // processor.
        counter_values[fld.id()] = fld.id() == kOomScoreAdjFieldNumber
                                       ? fld.as_int64()
                                       : fld.as_int64() * 1024;
        has_counter[fld.id()] = true;
      } else {
        context_->storage->IncrementStats(stats::proc_stat_unknown_counters);
      }
    }

    // Skip field_id 0 (invalid) and 1 (pid).
    for (size_t field_id = 2; field_id < counter_values.size(); field_id++) {
      if (!has_counter[field_id])
        continue;

      // Lookup the interned string id from the field name using the
      // pre-cached |proc_stats_process_names_| map.
      StringId name = proc_stats_process_names_[field_id];
      int64_t value = counter_values[field_id];
      UniquePid upid = context_->process_tracker->GetOrCreateProcess(pid);
      context_->event_tracker->PushCounter(ts, value, name, upid,
                                           RefType::kRefUpid);
    }
  }
}

void ProtoTraceParser::ParseFtracePacket(uint32_t cpu,
                                         int64_t ts,
                                         TimestampedTracePiece ttp) {
  PERFETTO_DCHECK(ttp.json_value == nullptr);

  // Handle the (optional) alternative encoding format for sched_switch.
  if (ttp.inline_event.type == InlineEvent::Type::kSchedSwitch) {
    const auto& event = ttp.inline_event.sched_switch;
    context_->event_tracker->PushSchedSwitchCompact(
        cpu, ts, event.prev_state, static_cast<uint32_t>(event.next_pid),
        event.next_prio, event.next_comm);

    context_->args_tracker->Flush();
    return;
  }

  const TraceBlobView& ftrace = ttp.blob_view;

  ProtoDecoder decoder(ftrace.data(), ftrace.length());
  uint64_t raw_pid = 0;
  if (auto pid_field =
          decoder.FindField(protos::pbzero::FtraceEvent::kPidFieldNumber)) {
    raw_pid = pid_field.as_uint64();
  } else {
    PERFETTO_ELOG("Pid field not found in ftrace packet");
    return;
  }
  uint32_t pid = static_cast<uint32_t>(raw_pid);

  for (auto fld = decoder.ReadField(); fld.valid(); fld = decoder.ReadField()) {
    bool is_metadata_field =
        fld.id() == protos::pbzero::FtraceEvent::kPidFieldNumber ||
        fld.id() == protos::pbzero::FtraceEvent::kTimestampFieldNumber;
    if (is_metadata_field)
      continue;

    ConstBytes data = fld.as_bytes();
    if (fld.id() == protos::pbzero::FtraceEvent::kGenericFieldNumber) {
      ParseGenericFtrace(ts, cpu, pid, data);
    } else if (fld.id() !=
               protos::pbzero::FtraceEvent::kSchedSwitchFieldNumber) {
      ParseTypedFtraceToRaw(fld.id(), ts, cpu, pid, data);
    }

    switch (fld.id()) {
      case protos::pbzero::FtraceEvent::kSchedSwitchFieldNumber: {
        ParseSchedSwitch(cpu, ts, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kSchedWakeupFieldNumber: {
        ParseSchedWakeup(ts, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kSchedWakingFieldNumber: {
        ParseSchedWaking(ts, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kSchedProcessFreeFieldNumber: {
        ParseSchedProcessFree(ts, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kCpuFrequencyFieldNumber: {
        ParseCpuFreq(ts, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kGpuFrequencyFieldNumber: {
        ParseGpuFreq(ts, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kCpuIdleFieldNumber: {
        ParseCpuIdle(ts, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kPrintFieldNumber: {
        ParsePrint(cpu, ts, pid, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kZeroFieldNumber: {
        ParseZero(cpu, ts, pid, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kRssStatFieldNumber: {
        ParseRssStat(ts, pid, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kIonHeapGrowFieldNumber: {
        ParseIonHeapGrowOrShrink(ts, pid, data, true);
        break;
      }
      case protos::pbzero::FtraceEvent::kIonHeapShrinkFieldNumber: {
        ParseIonHeapGrowOrShrink(ts, pid, data, false);
        break;
      }
      case protos::pbzero::FtraceEvent::kSignalGenerateFieldNumber: {
        ParseSignalGenerate(ts, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kSignalDeliverFieldNumber: {
        ParseSignalDeliver(ts, pid, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kLowmemoryKillFieldNumber: {
        ParseLowmemoryKill(ts, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kOomScoreAdjUpdateFieldNumber: {
        ParseOOMScoreAdjUpdate(ts, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kMmEventRecordFieldNumber: {
        ParseMmEventRecord(ts, pid, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kSysEnterFieldNumber: {
        ParseSysEvent(ts, pid, true, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kSysExitFieldNumber: {
        ParseSysEvent(ts, pid, false, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kTaskNewtaskFieldNumber: {
        ParseTaskNewTask(ts, pid, data);
        break;
      }
      case protos::pbzero::FtraceEvent::kTaskRenameFieldNumber: {
        ParseTaskRename(data);
        break;
      }
      default:
        break;
    }
  }
  // TODO(lalitm): maybe move this to the flush method in the trace processor
  // once we have it. This may reduce performance in the ArgsTracker though so
  // needs to be handled carefully.
  context_->args_tracker->Flush();

  PERFETTO_DCHECK(!decoder.bytes_left());
}

void ProtoTraceParser::ParseSignalDeliver(int64_t ts,
                                          uint32_t pid,
                                          ConstBytes blob) {
  protos::pbzero::SignalDeliverFtraceEvent::Decoder sig(blob.data, blob.size);
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  context_->event_tracker->PushInstant(ts, signal_deliver_id_, sig.sig(), utid,
                                       RefType::kRefUtid);
}

// This event has both the pid of the thread that sent the signal and the
// destination of the signal. Currently storing the pid of the destination.
void ProtoTraceParser::ParseSignalGenerate(int64_t ts, ConstBytes blob) {
  protos::pbzero::SignalGenerateFtraceEvent::Decoder sig(blob.data, blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(
      static_cast<uint32_t>(sig.pid()));
  context_->event_tracker->PushInstant(ts, signal_generate_id_, sig.sig(), utid,
                                       RefType::kRefUtid);
}

void ProtoTraceParser::ParseLowmemoryKill(int64_t ts, ConstBytes blob) {
  // TODO(taylori): Store the pagecache_size, pagecache_limit and free fields
  // in an args table
  protos::pbzero::LowmemoryKillFtraceEvent::Decoder lmk(blob.data, blob.size);

  // Store the pid of the event that is lmk-ed.
  auto pid = static_cast<uint32_t>(lmk.pid());
  auto opt_utid = context_->process_tracker->GetThreadOrNull(pid);

  // Don't add LMK events for threads we've never seen before. This works around
  // the case where we get an LMK event after a thread has already been killed.
  if (!opt_utid)
    return;

  auto row_id = context_->event_tracker->PushInstant(
      ts, lmk_id_, 0, opt_utid.value(), RefType::kRefUtid, true);

  // Store the comm as an arg.
  auto comm_id = context_->storage->InternString(
      lmk.has_comm() ? lmk.comm() : base::StringView());
  context_->args_tracker->AddArg(row_id, comm_name_id_, comm_name_id_,
                                 Variadic::String(comm_id));
}

void ProtoTraceParser::ParseRssStat(int64_t ts, uint32_t pid, ConstBytes blob) {
  protos::pbzero::RssStatFtraceEvent::Decoder rss(blob.data, blob.size);
  const auto kRssStatUnknown = static_cast<uint32_t>(rss_members_.size()) - 1;
  auto member = static_cast<uint32_t>(rss.member());
  int64_t size = rss.size();
  if (member >= rss_members_.size()) {
    context_->storage->IncrementStats(stats::rss_stat_unknown_keys);
    member = kRssStatUnknown;
  }

  if (size >= 0) {
    UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
    context_->event_tracker->PushCounter(ts, size, rss_members_[member], utid,
                                         RefType::kRefUtid, true);
  } else {
    context_->storage->IncrementStats(stats::rss_stat_negative_size);
  }
}

void ProtoTraceParser::ParseIonHeapGrowOrShrink(int64_t ts,
                                                uint32_t pid,
                                                ConstBytes blob,
                                                bool grow) {
  protos::pbzero::IonHeapGrowFtraceEvent::Decoder ion(blob.data, blob.size);
  int64_t change_bytes = static_cast<int64_t>(ion.len()) * (grow ? 1 : -1);
  // The total_allocated ftrace event reports the value before the
  // atomic_long_add / sub takes place.
  int64_t total_bytes = ion.total_allocated() + change_bytes;
  StringId global_name_id = ion_total_unknown_id_;
  StringId change_name_id = ion_change_unknown_id_;

  if (ion.has_heap_name()) {
    char counter_name[255];
    base::StringView heap_name = ion.heap_name();
    snprintf(counter_name, sizeof(counter_name), "mem.ion.%.*s",
             int(heap_name.size()), heap_name.data());
    global_name_id = context_->storage->InternString(counter_name);
    snprintf(counter_name, sizeof(counter_name), "mem.ion_change.%.*s",
             int(heap_name.size()), heap_name.data());
    change_name_id = context_->storage->InternString(counter_name);
  }

  // Push the global counter.
  context_->event_tracker->PushCounter(ts, total_bytes, global_name_id, 0,
                                       RefType::kRefNoRef);

  // Push the change counter.
  // TODO(b/121331269): these should really be instant events. For now we
  // manually reset them to 0 after 1ns.
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  context_->event_tracker->PushCounter(ts, change_bytes, change_name_id, utid,
                                       RefType::kRefUtid);
  context_->event_tracker->PushCounter(ts + 1, 0, change_name_id, utid,
                                       RefType::kRefUtid);

  // We are reusing the same function for ion_heap_grow and ion_heap_shrink.
  // It is fine as the arguments are the same, but we need to be sure that the
  // protobuf field id for both are the same.
  static_assert(
      static_cast<int>(
          protos::pbzero::IonHeapGrowFtraceEvent::kTotalAllocatedFieldNumber) ==
              static_cast<int>(protos::pbzero::IonHeapShrinkFtraceEvent::
                                   kTotalAllocatedFieldNumber) &&
          static_cast<int>(
              protos::pbzero::IonHeapGrowFtraceEvent::kLenFieldNumber) ==
              static_cast<int>(
                  protos::pbzero::IonHeapShrinkFtraceEvent::kLenFieldNumber) &&
          static_cast<int>(
              protos::pbzero::IonHeapGrowFtraceEvent::kHeapNameFieldNumber) ==
              static_cast<int>(protos::pbzero::IonHeapShrinkFtraceEvent::
                                   kHeapNameFieldNumber),
      "ION field mismatch");
}

void ProtoTraceParser::ParseCpuFreq(int64_t ts, ConstBytes blob) {
  protos::pbzero::CpuFrequencyFtraceEvent::Decoder freq(blob.data, blob.size);
  uint32_t cpu = freq.cpu_id();
  uint32_t new_freq = freq.state();
  context_->event_tracker->PushCounter(ts, new_freq, cpu_freq_name_id_, cpu,
                                       RefType::kRefCpuId);
}

void ProtoTraceParser::ParseCpuIdle(int64_t ts, ConstBytes blob) {
  protos::pbzero::CpuIdleFtraceEvent::Decoder idle(blob.data, blob.size);
  uint32_t cpu = idle.cpu_id();
  uint32_t new_state = idle.state();
  context_->event_tracker->PushCounter(ts, new_state, cpu_idle_name_id_, cpu,
                                       RefType::kRefCpuId);
}

void ProtoTraceParser::ParseGpuFreq(int64_t ts, ConstBytes blob) {
  protos::pbzero::GpuFrequencyFtraceEvent::Decoder freq(blob.data, blob.size);
  uint32_t gpu = freq.gpu_id();
  uint32_t new_freq = freq.state();
  context_->event_tracker->PushCounter(ts, new_freq, gpu_freq_name_id_, gpu,
                                       RefType::kRefGpuId);
}

PERFETTO_ALWAYS_INLINE
void ProtoTraceParser::ParseSchedSwitch(uint32_t cpu,
                                        int64_t ts,
                                        ConstBytes blob) {
  protos::pbzero::SchedSwitchFtraceEvent::Decoder ss(blob.data, blob.size);
  uint32_t prev_pid = static_cast<uint32_t>(ss.prev_pid());
  uint32_t next_pid = static_cast<uint32_t>(ss.next_pid());
  context_->event_tracker->PushSchedSwitch(
      cpu, ts, prev_pid, ss.prev_comm(), ss.prev_prio(), ss.prev_state(),
      next_pid, ss.next_comm(), ss.next_prio());
}

void ProtoTraceParser::ParseSchedWakeup(int64_t ts, ConstBytes blob) {
  protos::pbzero::SchedWakeupFtraceEvent::Decoder sw(blob.data, blob.size);
  uint32_t wakee_pid = static_cast<uint32_t>(sw.pid());
  StringId name_id = context_->storage->InternString(sw.comm());
  auto utid = context_->process_tracker->UpdateThreadName(wakee_pid, name_id);
  context_->event_tracker->PushInstant(ts, sched_wakeup_name_id_, 0 /* value */,
                                       utid, RefType::kRefUtid);
}

void ProtoTraceParser::ParseSchedWaking(int64_t ts, ConstBytes blob) {
  protos::pbzero::SchedWakingFtraceEvent::Decoder sw(blob.data, blob.size);
  uint32_t wakee_pid = static_cast<uint32_t>(sw.pid());
  StringId name_id = context_->storage->InternString(sw.comm());
  auto utid = context_->process_tracker->UpdateThreadName(wakee_pid, name_id);
  context_->event_tracker->PushInstant(ts, sched_waking_name_id_, 0 /* value */,
                                       utid, RefType::kRefUtid);
}

void ProtoTraceParser::ParseSchedProcessFree(int64_t ts, ConstBytes blob) {
  protos::pbzero::SchedProcessFreeFtraceEvent::Decoder ex(blob.data, blob.size);
  uint32_t pid = static_cast<uint32_t>(ex.pid());
  context_->process_tracker->EndThread(ts, pid);
}

void ProtoTraceParser::ParseTaskNewTask(int64_t ts,
                                        uint32_t source_tid,
                                        ConstBytes blob) {
  protos::pbzero::TaskNewtaskFtraceEvent::Decoder evt(blob.data, blob.size);
  uint32_t clone_flags = static_cast<uint32_t>(evt.clone_flags());
  uint32_t new_tid = static_cast<uint32_t>(evt.pid());
  StringId new_comm = context_->storage->InternString(evt.comm());
  auto* proc_tracker = context_->process_tracker.get();

  // task_newtask is raised both in the case of a new process creation (fork()
  // family) and thread creation (clone(CLONE_THREAD, ...)).
  static const uint32_t kCloneThread = 0x00010000;  // From kernel's sched.h.

  // If the process is a fork, start a new process except if the source tid is
  // kthreadd in which case just make it a new thread associated with kthreadd.
  if ((clone_flags & kCloneThread) == 0 && source_tid != kKthreaddPid) {
    // This is a plain-old fork() or equivalent.
    proc_tracker->StartNewProcess(ts, source_tid, new_tid, new_comm);
    return;
  }

  if (source_tid == kKthreaddPid) {
    context_->process_tracker->SetProcessMetadata(kKthreaddPid, base::nullopt,
                                                  kKthreaddName);
  }

  // This is a pthread_create or similar. Bind the two threads together, so
  // they get resolved to the same process.
  auto source_utid = proc_tracker->GetOrCreateThread(source_tid);
  auto new_utid = proc_tracker->StartNewThread(ts, new_tid, new_comm);
  proc_tracker->AssociateThreads(source_utid, new_utid);
}

void ProtoTraceParser::ParseTaskRename(ConstBytes blob) {
  protos::pbzero::TaskRenameFtraceEvent::Decoder evt(blob.data, blob.size);
  uint32_t tid = static_cast<uint32_t>(evt.pid());
  StringId comm = context_->storage->InternString(evt.newcomm());
  context_->process_tracker->UpdateThreadName(tid, comm);
  context_->process_tracker->UpdateProcessNameFromThreadName(tid, comm);
}

void ProtoTraceParser::ParsePrint(uint32_t,
                                  int64_t ts,
                                  uint32_t pid,
                                  ConstBytes blob) {
  protos::pbzero::PrintFtraceEvent::Decoder evt(blob.data, blob.size);
  context_->systrace_parser->ParsePrintEvent(ts, pid, evt.buf());
}

void ProtoTraceParser::ParseZero(uint32_t,
                                 int64_t ts,
                                 uint32_t pid,
                                 ConstBytes blob) {
  protos::pbzero::ZeroFtraceEvent::Decoder evt(blob.data, blob.size);
  uint32_t tgid = static_cast<uint32_t>(evt.pid());
  context_->systrace_parser->ParseZeroEvent(ts, pid, evt.flag(), evt.name(),
                                            tgid, evt.value());
}

void ProtoTraceParser::ParseBatteryCounters(int64_t ts, ConstBytes blob) {
  protos::pbzero::BatteryCounters::Decoder evt(blob.data, blob.size);
  if (evt.has_charge_counter_uah()) {
    context_->event_tracker->PushCounter(
        ts, evt.charge_counter_uah(), batt_charge_id_, 0, RefType::kRefNoRef);
  }
  if (evt.has_capacity_percent()) {
    context_->event_tracker->PushCounter(
        ts, static_cast<double>(evt.capacity_percent()), batt_capacity_id_, 0,
        RefType::kRefNoRef);
  }
  if (evt.has_current_ua()) {
    context_->event_tracker->PushCounter(ts, evt.current_ua(), batt_current_id_,
                                         0, RefType::kRefNoRef);
  }
  if (evt.has_current_avg_ua()) {
    context_->event_tracker->PushCounter(
        ts, evt.current_avg_ua(), batt_current_avg_id_, 0, RefType::kRefNoRef);
  }
}

void ProtoTraceParser::ParsePowerRails(ConstBytes blob) {
  protos::pbzero::PowerRails::Decoder evt(blob.data, blob.size);
  if (evt.has_rail_descriptor()) {
    for (auto it = evt.rail_descriptor(); it; ++it) {
      protos::pbzero::PowerRails::RailDescriptor::Decoder desc(it->data(),
                                                               it->size());
      uint32_t idx = desc.index();
      if (PERFETTO_UNLIKELY(idx > 256)) {
        PERFETTO_DLOG("Skipping excessively large power_rail index %" PRIu32,
                      idx);
        continue;
      }
      if (power_rails_strs_id_.size() <= idx)
        power_rails_strs_id_.resize(idx + 1);
      char counter_name[255];
      snprintf(counter_name, sizeof(counter_name), "power.%.*s_uws",
               int(desc.rail_name().size), desc.rail_name().data);
      power_rails_strs_id_[idx] = context_->storage->InternString(counter_name);
    }
  }

  if (evt.has_energy_data()) {
    for (auto it = evt.energy_data(); it; ++it) {
      protos::pbzero::PowerRails::EnergyData::Decoder desc(it->data(),
                                                           it->size());
      if (desc.index() < power_rails_strs_id_.size()) {
        int64_t ts = static_cast<int64_t>(desc.timestamp_ms()) * 1000000;
        context_->event_tracker->PushCounter(ts, desc.energy(),
                                             power_rails_strs_id_[desc.index()],
                                             0, RefType::kRefNoRef);
      } else {
        context_->storage->IncrementStats(stats::power_rail_unknown_index);
      }
    }
  }
}

void ProtoTraceParser::ParseOOMScoreAdjUpdate(int64_t ts, ConstBytes blob) {
  protos::pbzero::OomScoreAdjUpdateFtraceEvent::Decoder evt(blob.data,
                                                            blob.size);
  // The int16_t static cast is because older version of the on-device tracer
  // had a bug on negative varint encoding (b/120618641).
  int16_t oom_adj = static_cast<int16_t>(evt.oom_score_adj());
  uint32_t tid = static_cast<uint32_t>(evt.pid());
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(tid);
  context_->event_tracker->PushCounter(ts, oom_adj, oom_score_adj_id_, utid,
                                       RefType::kRefUtid, true);
}

void ProtoTraceParser::ParseMmEventRecord(int64_t ts,
                                          uint32_t pid,
                                          ConstBytes blob) {
  protos::pbzero::MmEventRecordFtraceEvent::Decoder evt(blob.data, blob.size);
  uint32_t type = evt.type();
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);

  if (type >= mm_event_counter_names_.size()) {
    context_->storage->IncrementStats(stats::mm_unknown_type);
    return;
  }

  const auto& counter_names = mm_event_counter_names_[type];
  context_->event_tracker->PushCounter(ts, evt.count(), counter_names.count,
                                       utid, RefType::kRefUtid, true);
  context_->event_tracker->PushCounter(ts, evt.max_lat(), counter_names.max_lat,
                                       utid, RefType::kRefUtid, true);
  context_->event_tracker->PushCounter(ts, evt.avg_lat(), counter_names.avg_lat,
                                       utid, RefType::kRefUtid, true);
}

void ProtoTraceParser::ParseSysEvent(int64_t ts,
                                     uint32_t pid,
                                     bool is_enter,
                                     ConstBytes blob) {
  protos::pbzero::SysEnterFtraceEvent::Decoder evt(blob.data, blob.size);
  uint32_t syscall_num = static_cast<uint32_t>(evt.id());
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);

  if (is_enter) {
    context_->syscall_tracker->Enter(ts, utid, syscall_num);
  } else {
    context_->syscall_tracker->Exit(ts, utid, syscall_num);
  }

  // We are reusing the same function for sys_enter and sys_exit.
  // It is fine as the arguments are the same, but we need to be sure that the
  // protobuf field id for both are the same.
  static_assert(
      static_cast<int>(protos::pbzero::SysEnterFtraceEvent::kIdFieldNumber) ==
          static_cast<int>(protos::pbzero::SysExitFtraceEvent::kIdFieldNumber),
      "field mismatch");
}

void ProtoTraceParser::ParseGenericFtrace(int64_t ts,
                                          uint32_t cpu,
                                          uint32_t tid,
                                          ConstBytes blob) {
  protos::pbzero::GenericFtraceEvent::Decoder evt(blob.data, blob.size);
  StringId event_id = context_->storage->InternString(evt.event_name());
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(tid);
  RowId row_id = context_->storage->mutable_raw_events()->AddRawEvent(
      ts, event_id, cpu, utid);

  for (auto it = evt.field(); it; ++it) {
    protos::pbzero::GenericFtraceEvent::Field::Decoder fld(it->data(),
                                                           it->size());
    auto field_name_id = context_->storage->InternString(fld.name());
    if (fld.has_int_value()) {
      context_->args_tracker->AddArg(row_id, field_name_id, field_name_id,
                                     Variadic::Integer(fld.int_value()));
    } else if (fld.has_uint_value()) {
      context_->args_tracker->AddArg(
          row_id, field_name_id, field_name_id,
          Variadic::Integer(static_cast<int64_t>(fld.uint_value())));
    } else if (fld.has_str_value()) {
      StringId str_value = context_->storage->InternString(fld.str_value());
      context_->args_tracker->AddArg(row_id, field_name_id, field_name_id,
                                     Variadic::String(str_value));
    }
  }
}

void ProtoTraceParser::ParseTypedFtraceToRaw(uint32_t ftrace_id,
                                             int64_t ts,
                                             uint32_t cpu,
                                             uint32_t tid,
                                             ConstBytes blob) {
  ProtoDecoder decoder(blob.data, blob.size);
  if (ftrace_id >= GetDescriptorsSize()) {
    PERFETTO_DLOG("Event with id: %d does not exist and cannot be parsed.",
                  ftrace_id);
    return;
  }

  MessageDescriptor* m = GetMessageDescriptorForId(ftrace_id);
  const auto& message_strings = ftrace_message_strings_[ftrace_id];
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(tid);
  RowId raw_event_id = context_->storage->mutable_raw_events()->AddRawEvent(
      ts, message_strings.message_name_id, cpu, utid);
  for (auto fld = decoder.ReadField(); fld.valid(); fld = decoder.ReadField()) {
    if (PERFETTO_UNLIKELY(fld.id() >= kMaxFtraceEventFields)) {
      PERFETTO_DLOG(
          "Skipping ftrace arg - proto field id is too large (%" PRIu16 ")",
          fld.id());
      continue;
    }
    ProtoSchemaType type = m->fields[fld.id()].type;
    StringId name_id = message_strings.field_name_ids[fld.id()];
    switch (type) {
      case ProtoSchemaType::kInt32:
      case ProtoSchemaType::kInt64:
      case ProtoSchemaType::kSfixed32:
      case ProtoSchemaType::kSfixed64:
      case ProtoSchemaType::kSint32:
      case ProtoSchemaType::kSint64:
      case ProtoSchemaType::kBool:
      case ProtoSchemaType::kEnum: {
        context_->args_tracker->AddArg(raw_event_id, name_id, name_id,
                                       Variadic::Integer(fld.as_int64()));
        break;
      }
      case ProtoSchemaType::kUint32:
      case ProtoSchemaType::kUint64:
      case ProtoSchemaType::kFixed32:
      case ProtoSchemaType::kFixed64: {
        // Note that SQLite functions will still treat unsigned values
        // as a signed 64 bit integers (but the translation back to ftrace
        // refers to this storage directly).
        context_->args_tracker->AddArg(
            raw_event_id, name_id, name_id,
            Variadic::UnsignedInteger(fld.as_uint64()));
        break;
      }
      case ProtoSchemaType::kString:
      case ProtoSchemaType::kBytes: {
        StringId value = context_->storage->InternString(fld.as_string());
        context_->args_tracker->AddArg(raw_event_id, name_id, name_id,
                                       Variadic::String(value));
        break;
      }
      case ProtoSchemaType::kDouble: {
        context_->args_tracker->AddArg(raw_event_id, name_id, name_id,
                                       Variadic::Real(fld.as_double()));
        break;
      }
      case ProtoSchemaType::kFloat: {
        context_->args_tracker->AddArg(
            raw_event_id, name_id, name_id,
            Variadic::Real(static_cast<double>(fld.as_float())));
        break;
      }
      case ProtoSchemaType::kUnknown:
      case ProtoSchemaType::kGroup:
      case ProtoSchemaType::kMessage:
        PERFETTO_DLOG("Could not store %s as a field in args table.",
                      ProtoSchemaToString(type));
        break;
    }
  }
}

void ProtoTraceParser::ParseAndroidLogPacket(ConstBytes blob) {
  protos::pbzero::AndroidLogPacket::Decoder packet(blob.data, blob.size);
  for (auto it = packet.events(); it; ++it)
    ParseAndroidLogEvent(it->as_bytes());

  if (packet.has_stats())
    ParseAndroidLogStats(packet.stats());
}

void ProtoTraceParser::ParseAndroidLogEvent(ConstBytes blob) {
  // TODO(primiano): Add events and non-stringified fields to the "raw" table.
  protos::pbzero::AndroidLogPacket::LogEvent::Decoder evt(blob.data, blob.size);
  int64_t ts = static_cast<int64_t>(evt.timestamp());
  uint32_t pid = static_cast<uint32_t>(evt.pid());
  uint32_t tid = static_cast<uint32_t>(evt.tid());
  uint8_t prio = static_cast<uint8_t>(evt.prio());
  StringId tag_id = context_->storage->InternString(
      evt.has_tag() ? evt.tag() : base::StringView());
  StringId msg_id = context_->storage->InternString(
      evt.has_message() ? evt.message() : base::StringView());

  char arg_msg[4096];
  char* arg_str = &arg_msg[0];
  *arg_str = '\0';
  auto arg_avail = [&arg_msg, &arg_str]() {
    return sizeof(arg_msg) - static_cast<size_t>(arg_str - arg_msg);
  };
  for (auto it = evt.args(); it; ++it) {
    protos::pbzero::AndroidLogPacket::LogEvent::Arg::Decoder arg(it->data(),
                                                                 it->size());
    if (!arg.has_name())
      continue;
    arg_str +=
        snprintf(arg_str, arg_avail(),
                 " %.*s=", static_cast<int>(arg.name().size), arg.name().data);
    if (arg.has_string_value()) {
      arg_str += snprintf(arg_str, arg_avail(), "\"%.*s\"",
                          static_cast<int>(arg.string_value().size),
                          arg.string_value().data);
    } else if (arg.has_int_value()) {
      arg_str += snprintf(arg_str, arg_avail(), "%" PRId64, arg.int_value());
    } else if (arg.has_float_value()) {
      arg_str += snprintf(arg_str, arg_avail(), "%f",
                          static_cast<double>(arg.float_value()));
    }
  }

  if (prio == 0)
    prio = protos::pbzero::AndroidLogPriority::PRIO_INFO;

  if (arg_str != &arg_msg[0]) {
    PERFETTO_DCHECK(msg_id.is_null());
    // Skip the first space char (" foo=1 bar=2" -> "foo=1 bar=2").
    msg_id = context_->storage->InternString(&arg_msg[1]);
  }
  UniquePid utid = tid ? context_->process_tracker->UpdateThread(tid, pid) : 0;
  base::Optional<int64_t> opt_trace_time = context_->clock_tracker->ToTraceTime(
      protos::pbzero::ClockSnapshot::Clock::REALTIME, ts);
  if (!opt_trace_time)
    return;

  // Log events are NOT required to be sorted by trace_time. The virtual table
  // will take care of sorting on-demand.
  context_->storage->mutable_android_log()->AddLogEvent(
      opt_trace_time.value(), utid, prio, tag_id, msg_id);
}

void ProtoTraceParser::ParseAndroidLogStats(ConstBytes blob) {
  protos::pbzero::AndroidLogPacket::Stats::Decoder evt(blob.data, blob.size);
  if (evt.has_num_failed()) {
    context_->storage->SetStats(stats::android_log_num_failed,
                                static_cast<int64_t>(evt.num_failed()));
  }

  if (evt.has_num_skipped()) {
    context_->storage->SetStats(stats::android_log_num_skipped,
                                static_cast<int64_t>(evt.num_skipped()));
  }

  if (evt.has_num_total()) {
    context_->storage->SetStats(stats::android_log_num_total,
                                static_cast<int64_t>(evt.num_total()));
  }
}

void ProtoTraceParser::ParseTraceStats(ConstBytes blob) {
  protos::pbzero::TraceStats::Decoder evt(blob.data, blob.size);
  auto* storage = context_->storage.get();
  storage->SetStats(stats::traced_producers_connected,
                    static_cast<int64_t>(evt.producers_connected()));
  storage->SetStats(stats::traced_data_sources_registered,
                    static_cast<int64_t>(evt.data_sources_registered()));
  storage->SetStats(stats::traced_data_sources_seen,
                    static_cast<int64_t>(evt.data_sources_seen()));
  storage->SetStats(stats::traced_tracing_sessions,
                    static_cast<int64_t>(evt.tracing_sessions()));
  storage->SetStats(stats::traced_total_buffers,
                    static_cast<int64_t>(evt.total_buffers()));
  storage->SetStats(stats::traced_chunks_discarded,
                    static_cast<int64_t>(evt.chunks_discarded()));
  storage->SetStats(stats::traced_patches_discarded,
                    static_cast<int64_t>(evt.patches_discarded()));

  int buf_num = 0;
  for (auto it = evt.buffer_stats(); it; ++it, ++buf_num) {
    protos::pbzero::TraceStats::BufferStats::Decoder buf(it->data(),
                                                         it->size());
    storage->SetIndexedStats(stats::traced_buf_buffer_size, buf_num,
                             static_cast<int64_t>(buf.buffer_size()));
    storage->SetIndexedStats(stats::traced_buf_bytes_written, buf_num,
                             static_cast<int64_t>(buf.bytes_written()));
    storage->SetIndexedStats(stats::traced_buf_bytes_overwritten, buf_num,
                             static_cast<int64_t>(buf.bytes_overwritten()));
    storage->SetIndexedStats(stats::traced_buf_bytes_read, buf_num,
                             static_cast<int64_t>(buf.bytes_read()));
    storage->SetIndexedStats(stats::traced_buf_padding_bytes_written, buf_num,
                             static_cast<int64_t>(buf.padding_bytes_written()));
    storage->SetIndexedStats(stats::traced_buf_padding_bytes_cleared, buf_num,
                             static_cast<int64_t>(buf.padding_bytes_cleared()));
    storage->SetIndexedStats(stats::traced_buf_chunks_written, buf_num,
                             static_cast<int64_t>(buf.chunks_written()));
    storage->SetIndexedStats(stats::traced_buf_chunks_rewritten, buf_num,
                             static_cast<int64_t>(buf.chunks_rewritten()));
    storage->SetIndexedStats(stats::traced_buf_chunks_overwritten, buf_num,
                             static_cast<int64_t>(buf.chunks_overwritten()));
    storage->SetIndexedStats(stats::traced_buf_chunks_discarded, buf_num,
                             static_cast<int64_t>(buf.chunks_discarded()));
    storage->SetIndexedStats(stats::traced_buf_chunks_read, buf_num,
                             static_cast<int64_t>(buf.chunks_read()));
    storage->SetIndexedStats(
        stats::traced_buf_chunks_committed_out_of_order, buf_num,
        static_cast<int64_t>(buf.chunks_committed_out_of_order()));
    storage->SetIndexedStats(stats::traced_buf_write_wrap_count, buf_num,
                             static_cast<int64_t>(buf.write_wrap_count()));
    storage->SetIndexedStats(stats::traced_buf_patches_succeeded, buf_num,
                             static_cast<int64_t>(buf.patches_succeeded()));
    storage->SetIndexedStats(stats::traced_buf_patches_failed, buf_num,
                             static_cast<int64_t>(buf.patches_failed()));
    storage->SetIndexedStats(stats::traced_buf_readaheads_succeeded, buf_num,
                             static_cast<int64_t>(buf.readaheads_succeeded()));
    storage->SetIndexedStats(stats::traced_buf_readaheads_failed, buf_num,
                             static_cast<int64_t>(buf.readaheads_failed()));
    storage->SetIndexedStats(
        stats::traced_buf_trace_writer_packet_loss, buf_num,
        static_cast<int64_t>(buf.trace_writer_packet_loss()));
  }
}

void ProtoTraceParser::ParseFtraceStats(ConstBytes blob) {
  protos::pbzero::FtraceStats::Decoder evt(blob.data, blob.size);
  size_t phase =
      evt.phase() == protos::pbzero::FtraceStats_Phase_END_OF_TRACE ? 1 : 0;

  // This code relies on the fact that each ftrace_cpu_XXX_end event is
  // just after the corresponding ftrace_cpu_XXX_begin event.
  static_assert(
      stats::ftrace_cpu_read_events_end - stats::ftrace_cpu_read_events_begin ==
              1 &&
          stats::ftrace_cpu_entries_end - stats::ftrace_cpu_entries_begin == 1,
      "ftrace_cpu_XXX stats definition are messed up");

  auto* storage = context_->storage.get();
  for (auto it = evt.cpu_stats(); it; ++it) {
    protos::pbzero::FtraceCpuStats::Decoder cpu_stats(it->data(), it->size());
    int cpu = static_cast<int>(cpu_stats.cpu());
    storage->SetIndexedStats(stats::ftrace_cpu_entries_begin + phase, cpu,
                             static_cast<int64_t>(cpu_stats.entries()));
    storage->SetIndexedStats(stats::ftrace_cpu_overrun_begin + phase, cpu,
                             static_cast<int64_t>(cpu_stats.overrun()));
    storage->SetIndexedStats(stats::ftrace_cpu_commit_overrun_begin + phase,
                             cpu,
                             static_cast<int64_t>(cpu_stats.commit_overrun()));
    storage->SetIndexedStats(stats::ftrace_cpu_bytes_read_begin + phase, cpu,
                             static_cast<int64_t>(cpu_stats.bytes_read()));

    // oldest_event_ts can often be set to very high values, possibly because
    // of wrapping. Ensure that we are not overflowing to avoid ubsan
    // complaining.
    double oldest_event_ts = cpu_stats.oldest_event_ts() * 1e9;
    if (oldest_event_ts >= std::numeric_limits<int64_t>::max()) {
      storage->SetIndexedStats(stats::ftrace_cpu_oldest_event_ts_begin + phase,
                               cpu, std::numeric_limits<int64_t>::max());
    } else {
      storage->SetIndexedStats(stats::ftrace_cpu_oldest_event_ts_begin + phase,
                               cpu, static_cast<int64_t>(oldest_event_ts));
    }

    storage->SetIndexedStats(stats::ftrace_cpu_now_ts_begin + phase, cpu,
                             static_cast<int64_t>(cpu_stats.now_ts() * 1e9));
    storage->SetIndexedStats(stats::ftrace_cpu_dropped_events_begin + phase,
                             cpu,
                             static_cast<int64_t>(cpu_stats.dropped_events()));
    storage->SetIndexedStats(stats::ftrace_cpu_read_events_begin + phase, cpu,
                             static_cast<int64_t>(cpu_stats.read_events()));
  }
}

void ProtoTraceParser::ParseProfilePacket(
    int64_t,
    ProtoIncrementalState::PacketSequenceState* sequence_state,
    size_t sequence_state_generation,
    ConstBytes blob) {
  protos::pbzero::ProfilePacket::Decoder packet(blob.data, blob.size);
  context_->heap_profile_tracker->SetProfilePacketIndex(packet.index());

  for (auto it = packet.strings(); it; ++it) {
    protos::pbzero::InternedString::Decoder entry(it->data(), it->size());

    const char* str = reinterpret_cast<const char*>(entry.str().data);
    auto str_view = base::StringView(str, entry.str().size);
    sequence_state->stack_profile_tracker().AddString(entry.iid(), str_view);
  }

  for (auto it = packet.mappings(); it; ++it) {
    protos::pbzero::Mapping::Decoder entry(it->data(), it->size());
    StackProfileTracker::SourceMapping src_mapping = MakeSourceMapping(entry);
    sequence_state->stack_profile_tracker().AddMapping(entry.iid(),
                                                       src_mapping);
  }

  for (auto it = packet.frames(); it; ++it) {
    protos::pbzero::Frame::Decoder entry(it->data(), it->size());
    StackProfileTracker::SourceFrame src_frame = MakeSourceFrame(entry);
    sequence_state->stack_profile_tracker().AddFrame(entry.iid(), src_frame);
  }

  for (auto it = packet.callstacks(); it; ++it) {
    protos::pbzero::Callstack::Decoder entry(it->data(), it->size());
    StackProfileTracker::SourceCallstack src_callstack =
        MakeSourceCallstack(entry);
    sequence_state->stack_profile_tracker().AddCallstack(entry.iid(),
                                                         src_callstack);
  }

  for (auto it = packet.process_dumps(); it; ++it) {
    protos::pbzero::ProfilePacket::ProcessHeapSamples::Decoder entry(
        it->data(), it->size());

    int pid = static_cast<int>(entry.pid());

    if (entry.buffer_corrupted())
      context_->storage->IncrementIndexedStats(
          stats::heapprofd_buffer_corrupted, pid);
    if (entry.buffer_overran())
      context_->storage->IncrementIndexedStats(stats::heapprofd_buffer_overran,
                                               pid);
    if (entry.rejected_concurrent())
      context_->storage->IncrementIndexedStats(
          stats::heapprofd_rejected_concurrent, pid);

    for (auto sample_it = entry.samples(); sample_it; ++sample_it) {
      protos::pbzero::ProfilePacket::HeapSample::Decoder sample(
          sample_it->data(), sample_it->size());

      HeapProfileTracker::SourceAllocation src_allocation;
      src_allocation.pid = entry.pid();
      src_allocation.timestamp = static_cast<int64_t>(entry.timestamp());
      src_allocation.callstack_id = sample.callstack_id();
      src_allocation.self_allocated = sample.self_allocated();
      src_allocation.self_freed = sample.self_freed();
      src_allocation.alloc_count = sample.alloc_count();
      src_allocation.free_count = sample.free_count();

      context_->heap_profile_tracker->StoreAllocation(src_allocation);
    }
  }
  if (!packet.continued()) {
    PERFETTO_CHECK(sequence_state);
    ProfilePacketInternLookup intern_lookup(sequence_state,
                                            sequence_state_generation);
    context_->heap_profile_tracker->FinalizeProfile(
        &sequence_state->stack_profile_tracker(), &intern_lookup);
  }
}

void ProtoTraceParser::ParseStreamingProfilePacket(
    ProtoIncrementalState::PacketSequenceState* sequence_state,
    size_t sequence_state_generation,
    ConstBytes blob) {
  protos::pbzero::StreamingProfilePacket::Decoder packet(blob.data, blob.size);

  ProcessTracker* procs = context_->process_tracker.get();
  TraceStorage* storage = context_->storage.get();
  StackProfileTracker& stack_profile_tracker =
      sequence_state->stack_profile_tracker();
  ProfilePacketInternLookup intern_lookup(sequence_state,
                                          sequence_state_generation);

  uint32_t pid = static_cast<uint32_t>(sequence_state->pid());
  uint32_t tid = static_cast<uint32_t>(sequence_state->tid());
  UniqueTid utid = procs->UpdateThread(tid, pid);

  auto timestamp_it = packet.timestamp_delta_us();
  for (auto callstack_it = packet.callstack_iid(); callstack_it;
       ++callstack_it, ++timestamp_it) {
    if (!timestamp_it) {
      context_->storage->IncrementStats(stats::stackprofile_parser_error);
      PERFETTO_ELOG(
          "StreamingProfilePacket has less callstack IDs than timestamps!");
      break;
    }

    auto maybe_callstack_id = stack_profile_tracker.FindCallstack(
        callstack_it->as_uint64(), &intern_lookup);
    if (!maybe_callstack_id) {
      context_->storage->IncrementStats(stats::stackprofile_parser_error);
      PERFETTO_ELOG("StreamingProfilePacket referencing invalid callstack!");
      continue;
    }

    int64_t callstack_id = *maybe_callstack_id;

    TraceStorage::CpuProfileStackSamples::Row sample_row{
        sequence_state->IncrementAndGetTrackEventTimeNs(
            timestamp_it->as_int64()),
        callstack_id, utid};
    storage->mutable_cpu_profile_stack_samples()->Insert(sample_row);
  }
}

void ProtoTraceParser::ParseSystemInfo(ConstBytes blob) {
  protos::pbzero::SystemInfo::Decoder packet(blob.data, blob.size);
  if (packet.has_utsname()) {
    ConstBytes utsname_blob = packet.utsname();
    protos::pbzero::Utsname::Decoder utsname(utsname_blob.data,
                                             utsname_blob.size);
    base::StringView machine = utsname.machine();
    if (machine == "aarch64" || machine == "armv8l") {
      context_->syscall_tracker->SetArchitecture(kAarch64);
    } else if (machine == "x86_64") {
      context_->syscall_tracker->SetArchitecture(kX86_64);
    } else {
      PERFETTO_ELOG("Unknown architecture %s", machine.ToStdString().c_str());
    }
  }
}

void ProtoTraceParser::ParseTrackEvent(
    int64_t ts,
    int64_t tts,
    int64_t ticount,
    ProtoIncrementalState::PacketSequenceState* sequence_state,
    size_t sequence_state_generation,
    ConstBytes blob) {
  using LegacyEvent = protos::pbzero::TrackEvent::LegacyEvent;

  protos::pbzero::TrackEvent::Decoder event(blob.data, blob.size);

  const auto legacy_event_blob = event.legacy_event();
  LegacyEvent::Decoder legacy_event(legacy_event_blob.data,
                                    legacy_event_blob.size);

  // TODO(eseckler): This legacy event field will eventually be replaced by
  // fields in TrackEvent itself.
  if (PERFETTO_UNLIKELY(!event.type() && !legacy_event.has_phase())) {
    context_->storage->IncrementStats(stats::track_event_parser_errors);
    PERFETTO_DLOG("TrackEvent without type or phase");
    return;
  }

  ProcessTracker* procs = context_->process_tracker.get();
  TraceStorage* storage = context_->storage.get();
  TrackTracker* track_tracker = context_->track_tracker.get();
  SliceTracker* slice_tracker = context_->slice_tracker.get();

  std::vector<uint64_t> category_iids;
  for (auto it = event.category_iids(); it; ++it) {
    category_iids.push_back(it->as_uint64());
  }
  std::vector<protozero::ConstChars> category_strings;
  for (auto it = event.categories(); it; ++it) {
    category_strings.push_back(it->as_string());
  }

  StringId category_id = 0;

  // If there's a single category, we can avoid building a concatenated
  // string.
  if (PERFETTO_LIKELY(category_iids.size() == 1 && category_strings.empty())) {
    auto* decoder = sequence_state->LookupInternedMessage<
        protos::pbzero::InternedData::kEventCategoriesFieldNumber,
        protos::pbzero::EventCategory>(sequence_state_generation,
                                       category_iids[0]);
    if (decoder)
      category_id = storage->InternString(decoder->name());
  } else if (category_iids.empty() && category_strings.size() == 1) {
    category_id = storage->InternString(category_strings[0]);
  } else if (category_iids.size() + category_strings.size() > 1) {
    // We concatenate the category strings together since we currently only
    // support a single "cat" column.
    // TODO(eseckler): Support multi-category events in the table schema.
    std::string categories;
    for (uint64_t iid : category_iids) {
      auto* decoder = sequence_state->LookupInternedMessage<
          protos::pbzero::InternedData::kEventCategoriesFieldNumber,
          protos::pbzero::EventCategory>(sequence_state_generation, iid);
      if (!decoder)
        continue;
      base::StringView name = decoder->name();
      if (!categories.empty())
        categories.append(",");
      categories.append(name.data(), name.size());
    }
    for (const protozero::ConstChars& cat : category_strings) {
      if (!categories.empty())
        categories.append(",");
      categories.append(cat.data, cat.size);
    }
    if (!categories.empty())
      category_id = storage->InternString(base::StringView(categories));
  }

  StringId name_id = 0;

  uint64_t name_iid = event.name_iid();
  if (!name_iid)
    name_iid = legacy_event.name_iid();

  if (PERFETTO_LIKELY(name_iid)) {
    auto* decoder = sequence_state->LookupInternedMessage<
        protos::pbzero::InternedData::kEventNamesFieldNumber,
        protos::pbzero::EventName>(sequence_state_generation, name_iid);
    if (decoder)
      name_id = storage->InternString(decoder->name());
  } else if (event.has_name()) {
    name_id = storage->InternString(event.name());
  }

  // TODO(eseckler): Also consider track_uuid from TrackEventDefaults.
  // Fall back to the default descriptor track (uuid 0).
  uint64_t track_uuid = event.has_track_uuid() ? event.track_uuid() : 0u;
  TrackId track_id;
  base::Optional<UniqueTid> utid;
  base::Optional<UniqueTid> upid;

  // Determine track from track_uuid specified in either TrackEvent or
  // TrackEventDefaults. If none is set, fall back to the track specified by the
  // sequence's (or event's) pid + tid or a default track.
  if (track_uuid) {
    base::Optional<TrackId> opt_track_id =
        track_tracker->GetDescriptorTrack(track_uuid);
    if (!opt_track_id) {
      storage->IncrementStats(stats::track_event_parser_errors);
      PERFETTO_DLOG("TrackEvent with unknown track_uuid %" PRIu64, track_uuid);
      return;
    }
    track_id = *opt_track_id;

    auto thread_track_row =
        context_->storage->thread_track_table().id().IndexOf(
            SqlValue::Long(track_id));
    if (thread_track_row) {
      utid = storage->thread_track_table().utid()[*thread_track_row];
      upid = storage->GetThread(*utid).upid;
    } else {
      auto process_track_row =
          context_->storage->process_track_table().id().IndexOf(
              SqlValue::Long(track_id));
      if (process_track_row)
        upid = storage->process_track_table().upid()[*process_track_row];
    }
  } else if (sequence_state->pid_and_tid_valid() ||
             (legacy_event.has_pid_override() &&
              legacy_event.has_tid_override())) {
    uint32_t pid = static_cast<uint32_t>(sequence_state->pid());
    uint32_t tid = static_cast<uint32_t>(sequence_state->tid());
    if (legacy_event.has_pid_override())
      pid = static_cast<uint32_t>(legacy_event.pid_override());
    if (legacy_event.has_tid_override())
      tid = static_cast<uint32_t>(legacy_event.tid_override());

    utid = procs->UpdateThread(tid, pid);
    upid = storage->GetThread(*utid).upid;
    track_id = track_tracker->GetOrCreateDescriptorTrackForThread(*utid);
  } else {
    track_id = track_tracker->GetOrCreateDefaultDescriptorTrack();
  }

  // TODO(eseckler): Replace phase with type and remove handling of
  // legacy_event.phase() once it is no longer used by producers.
  int32_t phase = 0;
  if (legacy_event.has_phase()) {
    phase = legacy_event.phase();

    switch (phase) {
      case 'b':
      case 'e':
      case 'n': {
        // Intern tracks for legacy async events based on legacy event ids.
        int64_t source_id = 0;
        bool source_id_is_process_scoped = false;
        if (legacy_event.has_unscoped_id()) {
          source_id = static_cast<int64_t>(legacy_event.unscoped_id());
        } else if (legacy_event.has_global_id()) {
          source_id = static_cast<int64_t>(legacy_event.global_id());
        } else if (legacy_event.has_local_id()) {
          if (!upid) {
            storage->IncrementStats(stats::track_event_parser_errors);
            PERFETTO_DLOG(
                "TrackEvent with local_id without process association");
            return;
          }

          source_id = static_cast<int64_t>(legacy_event.local_id());
          source_id_is_process_scoped = true;
        } else {
          storage->IncrementStats(stats::track_event_parser_errors);
          PERFETTO_DLOG("Async LegacyEvent without ID");
          return;
        }

        // Catapult treats nestable async events of different categories with
        // the same ID as separate tracks. We replicate the same behavior here.
        StringId id_scope = category_id;
        if (legacy_event.has_id_scope()) {
          std::string concat = storage->GetString(category_id).ToStdString() +
                               ":" + legacy_event.id_scope().ToStdString();
          id_scope = storage->InternString(base::StringView(concat));
        }

        track_id = context_->track_tracker->InternLegacyChromeAsyncTrack(
            name_id, upid ? *upid : 0, source_id, source_id_is_process_scoped,
            id_scope);
        break;
      }
      case 'i':
      case 'I': {
        // Intern tracks for global or process-scoped legacy instant events.
        switch (legacy_event.instant_event_scope()) {
          case LegacyEvent::SCOPE_UNSPECIFIED:
          case LegacyEvent::SCOPE_THREAD:
            // Thread-scoped legacy instant events already have the right track
            // based on the tid/pid of the sequence.
            if (!utid) {
              storage->IncrementStats(stats::track_event_parser_errors);
              PERFETTO_DLOG(
                  "Thread-scoped instant event without thread association");
              return;
            }
            break;
          case LegacyEvent::SCOPE_GLOBAL:
            track_id = context_->track_tracker
                           ->GetOrCreateLegacyChromeGlobalInstantTrack();
            break;
          case LegacyEvent::SCOPE_PROCESS:
            if (!upid) {
              storage->IncrementStats(stats::track_event_parser_errors);
              PERFETTO_DLOG(
                  "Process-scoped instant event without process association");
              return;
            }

            track_id =
                context_->track_tracker->InternLegacyChromeProcessInstantTrack(
                    *upid);
            break;
        }
        break;
      }
      default:
        break;
    }
  } else {
    switch (event.type()) {
      case protos::pbzero::TrackEvent::TYPE_SLICE_BEGIN:
        phase = utid ? 'B' : 'b';
        break;
      case protos::pbzero::TrackEvent::TYPE_SLICE_END:
        phase = utid ? 'E' : 'e';
        break;
      case protos::pbzero::TrackEvent::TYPE_INSTANT:
        phase = utid ? 'i' : 'n';
        break;
      default:
        PERFETTO_FATAL("unexpected event type %d", event.type());
        return;
    }
  }

  auto args_callback = [this, &event, &legacy_event, &sequence_state,
                        sequence_state_generation, ts,
                        utid](ArgsTracker* args_tracker, RowId row_id) {
    for (auto it = event.debug_annotations(); it; ++it) {
      ParseDebugAnnotationArgs(it->as_bytes(), sequence_state,
                               sequence_state_generation, args_tracker, row_id);
    }

    if (event.has_task_execution()) {
      ParseTaskExecutionArgs(event.task_execution(), sequence_state,
                             sequence_state_generation, args_tracker, row_id);
    }

    if (event.has_log_message()) {
      ParseLogMessage(event.log_message(), sequence_state,
                      sequence_state_generation, ts, utid, args_tracker,
                      row_id);
    }

    // TODO(eseckler): Parse legacy flow events into flow events table once we
    // have a design for it.
    if (legacy_event.has_bind_id()) {
      args_tracker->AddArg(row_id, legacy_event_bind_id_key_id_,
                           legacy_event_bind_id_key_id_,
                           Variadic::UnsignedInteger(legacy_event.bind_id()));
    }

    if (legacy_event.bind_to_enclosing()) {
      args_tracker->AddArg(row_id, legacy_event_bind_to_enclosing_key_id_,
                           legacy_event_bind_to_enclosing_key_id_,
                           Variadic::Boolean(true));
    }

    if (legacy_event.flow_direction()) {
      StringId value;
      switch (legacy_event.flow_direction()) {
        case protos::pbzero::TrackEvent::LegacyEvent::FLOW_IN:
          value = flow_direction_value_in_id_;
          break;
        case protos::pbzero::TrackEvent::LegacyEvent::FLOW_OUT:
          value = flow_direction_value_out_id_;
          break;
        case protos::pbzero::TrackEvent::LegacyEvent::FLOW_INOUT:
          value = flow_direction_value_inout_id_;
          break;
        default:
          PERFETTO_FATAL("Unknown flow direction: %d",
                         legacy_event.flow_direction());
          break;
      }
      args_tracker->AddArg(row_id, legacy_event_flow_direction_key_id_,
                           legacy_event_flow_direction_key_id_,
                           Variadic::String(value));
    }
  };

  switch (static_cast<char>(phase)) {
    case 'B': {  // TRACE_EVENT_PHASE_BEGIN.
      if (!utid) {
        storage->IncrementStats(stats::track_event_parser_errors);
        PERFETTO_DLOG("TrackEvent with phase B without thread association");
        return;
      }

      auto opt_slice_id =
          slice_tracker->Begin(ts, track_id, *utid, RefType::kRefUtid,
                               category_id, name_id, args_callback);
      if (opt_slice_id.has_value()) {
        auto* thread_slices = storage->mutable_thread_slices();
        PERFETTO_DCHECK(!thread_slices->slice_count() ||
                        thread_slices->slice_ids().back() <
                            opt_slice_id.value());
        thread_slices->AddThreadSlice(opt_slice_id.value(), tts,
                                      kPendingThreadDuration, ticount,
                                      kPendingThreadInstructionDelta);
      }
      break;
    }
    case 'E': {  // TRACE_EVENT_PHASE_END.
      if (!utid) {
        storage->IncrementStats(stats::track_event_parser_errors);
        PERFETTO_DLOG("TrackEvent with phase E without thread association");
        return;
      }

      auto opt_slice_id =
          slice_tracker->End(ts, track_id, category_id, name_id, args_callback);
      if (opt_slice_id.has_value()) {
        auto* thread_slices = storage->mutable_thread_slices();
        thread_slices->UpdateThreadDeltasForSliceId(opt_slice_id.value(), tts,
                                                    ticount);
      }
      break;
    }
    case 'X': {  // TRACE_EVENT_PHASE_COMPLETE.
      if (!utid) {
        storage->IncrementStats(stats::track_event_parser_errors);
        PERFETTO_DLOG("TrackEvent with phase X without thread association");
        return;
      }

      auto duration_ns = legacy_event.duration_us() * 1000;
      if (duration_ns < 0)
        return;
      auto opt_slice_id = slice_tracker->Scoped(
          ts, track_id, *utid, RefType::kRefUtid, category_id, name_id,
          duration_ns, args_callback);
      if (opt_slice_id.has_value()) {
        auto* thread_slices = storage->mutable_thread_slices();
        PERFETTO_DCHECK(!thread_slices->slice_count() ||
                        thread_slices->slice_ids().back() <
                            opt_slice_id.value());
        auto thread_duration_ns = legacy_event.thread_duration_us() * 1000;
        thread_slices->AddThreadSlice(opt_slice_id.value(), tts,
                                      thread_duration_ns, ticount,
                                      legacy_event.thread_instruction_delta());
      }
      break;
    }
    case 'i':
    case 'I': {  // TRACE_EVENT_PHASE_INSTANT.
      // Handle instant events as slices with zero duration, so that they end
      // up nested underneath their parent slices.
      int64_t duration_ns = 0;
      int64_t tidelta = 0;

      switch (legacy_event.instant_event_scope()) {
        case LegacyEvent::SCOPE_UNSPECIFIED:
        case LegacyEvent::SCOPE_THREAD: {
          // TODO(lalitm): Associate thread slices with track instead.
          auto opt_slice_id = slice_tracker->Scoped(
              ts, track_id, *utid, RefType::kRefUtid, category_id, name_id,
              duration_ns, args_callback);
          if (opt_slice_id.has_value()) {
            auto* thread_slices = storage->mutable_thread_slices();
            PERFETTO_DCHECK(!thread_slices->slice_count() ||
                            thread_slices->slice_ids().back() <
                                opt_slice_id.value());
            thread_slices->AddThreadSlice(opt_slice_id.value(), tts,
                                          duration_ns, ticount, tidelta);
          }
          break;
        }
        case LegacyEvent::SCOPE_GLOBAL: {
          slice_tracker->Scoped(ts, track_id, /*ref=*/0, RefType::kRefNoRef,
                                category_id, name_id, duration_ns,
                                args_callback);
          break;
        }
        case LegacyEvent::SCOPE_PROCESS: {
          slice_tracker->Scoped(ts, track_id, *upid, RefType::kRefUpid,
                                category_id, name_id, duration_ns,
                                args_callback);
          break;
        }
        default: {
          PERFETTO_FATAL("Unknown instant event scope: %u",
                         legacy_event.instant_event_scope());
          break;
        }
      }
      break;
    }
    case 'b': {  // TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN
      auto opt_slice_id =
          slice_tracker->Begin(ts, track_id, track_id, RefType::kRefTrack,
                               category_id, name_id, args_callback);
      // For the time beeing, we only create vtrack slice rows if we need to
      // store thread timestamps/counters.
      if (legacy_event.use_async_tts() && opt_slice_id.has_value()) {
        auto* vtrack_slices = storage->mutable_virtual_track_slices();
        PERFETTO_DCHECK(!vtrack_slices->slice_count() ||
                        vtrack_slices->slice_ids().back() <
                            opt_slice_id.value());
        vtrack_slices->AddVirtualTrackSlice(opt_slice_id.value(), tts,
                                            kPendingThreadDuration, ticount,
                                            kPendingThreadInstructionDelta);
      }
      break;
    }
    case 'e': {  // TRACE_EVENT_PHASE_NESTABLE_ASYNC_END
      auto opt_slice_id =
          slice_tracker->End(ts, track_id, category_id, name_id, args_callback);
      if (legacy_event.use_async_tts() && opt_slice_id.has_value()) {
        auto* vtrack_slices = storage->mutable_virtual_track_slices();
        vtrack_slices->UpdateThreadDeltasForSliceId(opt_slice_id.value(), tts,
                                                    ticount);
      }
      break;
    }
    case 'n': {  // TRACE_EVENT_PHASE_NESTABLE_ASYNC_INSTANT
      // Handle instant events as slices with zero duration, so that they end up
      // nested underneath their parent slices.
      int64_t duration_ns = 0;
      int64_t tidelta = 0;
      auto opt_slice_id = slice_tracker->Scoped(
          ts, track_id, track_id, RefType::kRefTrack, category_id, name_id,
          duration_ns, args_callback);
      if (legacy_event.use_async_tts() && opt_slice_id.has_value()) {
        auto* vtrack_slices = storage->mutable_virtual_track_slices();
        PERFETTO_DCHECK(!vtrack_slices->slice_count() ||
                        vtrack_slices->slice_ids().back() <
                            opt_slice_id.value());
        vtrack_slices->AddVirtualTrackSlice(opt_slice_id.value(), tts,
                                            duration_ns, ticount, tidelta);
      }
      break;
    }
    case 'M': {  // TRACE_EVENT_PHASE_METADATA (process and thread names).
      // Parse process and thread names from correspondingly named events.
      // TODO(eseckler): Also consider names from process/thread descriptors.
      NullTermStringView event_name = storage->GetString(name_id);
      PERFETTO_DCHECK(event_name.data());
      if (strcmp(event_name.c_str(), "thread_name") == 0) {
        if (!utid) {
          storage->IncrementStats(stats::track_event_parser_errors);
          PERFETTO_DLOG(
              "thread_name metadata event without thread association");
          return;
        }

        auto it = event.debug_annotations();
        if (!it)
          break;
        protos::pbzero::DebugAnnotation::Decoder annotation(it->data(),
                                                            it->size());
        auto thread_name = annotation.string_value();
        if (!thread_name.size)
          break;
        auto thread_name_id = storage->InternString(thread_name);
        procs->UpdateThreadName(storage->GetThread(*utid).tid, thread_name_id);
        break;
      }
      if (strcmp(event_name.c_str(), "process_name") == 0) {
        if (!upid) {
          storage->IncrementStats(stats::track_event_parser_errors);
          PERFETTO_DLOG(
              "process_name metadata event without process association");
          return;
        }

        auto it = event.debug_annotations();
        if (!it)
          break;
        protos::pbzero::DebugAnnotation::Decoder annotation(it->data(),
                                                            it->size());
        auto process_name = annotation.string_value();
        if (!process_name.size)
          break;
        procs->SetProcessMetadata(storage->GetProcess(*upid).pid, base::nullopt,
                                  process_name);
        break;
      }
      // Other metadata events are proxied via the raw table for JSON export.
      ParseLegacyEventAsRawEvent(ts, tts, ticount, utid, category_id, name_id,
                                 legacy_event, args_callback);
      break;
    }
    default: {
      // Other events are proxied via the raw table for JSON export.
      ParseLegacyEventAsRawEvent(ts, tts, ticount, utid, category_id, name_id,
                                 legacy_event, args_callback);
    }
  }
}

void ProtoTraceParser::ParseLegacyEventAsRawEvent(
    int64_t ts,
    int64_t tts,
    int64_t ticount,
    base::Optional<UniqueTid> utid,
    StringId category_id,
    StringId name_id,
    const protos::pbzero::TrackEvent::LegacyEvent::Decoder& legacy_event,
    SliceTracker::SetArgsCallback args_callback) {
  if (!utid) {
    context_->storage->IncrementStats(stats::track_event_parser_errors);
    PERFETTO_DLOG("raw legacy event without thread association");
    return;
  }

  RowId row_id = context_->storage->mutable_raw_events()->AddRawEvent(
      ts, raw_legacy_event_id_, 0, *utid);
  ArgsTracker args(context_);
  args.AddArg(row_id, legacy_event_category_key_id_,
              legacy_event_category_key_id_, Variadic::String(category_id));
  args.AddArg(row_id, legacy_event_name_key_id_, legacy_event_name_key_id_,
              Variadic::String(name_id));

  std::string phase_string(1, static_cast<char>(legacy_event.phase()));
  StringId phase_id = context_->storage->InternString(phase_string.c_str());
  args.AddArg(row_id, legacy_event_phase_key_id_, legacy_event_phase_key_id_,
              Variadic::String(phase_id));

  if (legacy_event.has_duration_us()) {
    args.AddArg(row_id, legacy_event_duration_ns_key_id_,
                legacy_event_duration_ns_key_id_,
                Variadic::Integer(legacy_event.duration_us() * 1000));
  }

  if (tts) {
    args.AddArg(row_id, legacy_event_thread_timestamp_ns_key_id_,
                legacy_event_thread_timestamp_ns_key_id_,
                Variadic::Integer(tts));
    if (legacy_event.has_thread_duration_us()) {
      args.AddArg(row_id, legacy_event_thread_duration_ns_key_id_,
                  legacy_event_thread_duration_ns_key_id_,
                  Variadic::Integer(legacy_event.thread_duration_us() * 1000));
    }
  }

  if (ticount) {
    args.AddArg(row_id, legacy_event_thread_instruction_count_key_id_,
                legacy_event_thread_instruction_count_key_id_,
                Variadic::Integer(tts));
    if (legacy_event.has_thread_instruction_delta()) {
      args.AddArg(row_id, legacy_event_thread_instruction_delta_key_id_,
                  legacy_event_thread_instruction_delta_key_id_,
                  Variadic::Integer(legacy_event.thread_instruction_delta()));
    }
  }

  if (legacy_event.use_async_tts()) {
    args.AddArg(row_id, legacy_event_use_async_tts_key_id_,
                legacy_event_use_async_tts_key_id_, Variadic::Boolean(true));
  }

  bool has_id = false;
  if (legacy_event.has_unscoped_id()) {
    // Unscoped ids are either global or local depending on the phase. Pass them
    // through as unscoped IDs to JSON export to preserve this behavior.
    args.AddArg(row_id, legacy_event_unscoped_id_key_id_,
                legacy_event_unscoped_id_key_id_,
                Variadic::UnsignedInteger(legacy_event.unscoped_id()));
    has_id = true;
  } else if (legacy_event.has_global_id()) {
    args.AddArg(row_id, legacy_event_global_id_key_id_,
                legacy_event_global_id_key_id_,
                Variadic::UnsignedInteger(legacy_event.global_id()));
    has_id = true;
  } else if (legacy_event.has_local_id()) {
    args.AddArg(row_id, legacy_event_local_id_key_id_,
                legacy_event_local_id_key_id_,
                Variadic::UnsignedInteger(legacy_event.local_id()));
    has_id = true;
  }

  if (has_id && legacy_event.has_id_scope() && legacy_event.id_scope().size) {
    args.AddArg(row_id, legacy_event_id_scope_key_id_,
                legacy_event_id_scope_key_id_,
                Variadic::String(
                    context_->storage->InternString(legacy_event.id_scope())));
  }

  // No need to parse legacy_event.instant_event_scope() because we import
  // instant events into the slice table.

  args_callback(&args, row_id);
}

void ProtoTraceParser::ParseDebugAnnotationArgs(
    ConstBytes debug_annotation,
    ProtoIncrementalState::PacketSequenceState* sequence_state,
    size_t sequence_state_generation,
    ArgsTracker* args_tracker,
    RowId row_id) {
  TraceStorage* storage = context_->storage.get();

  protos::pbzero::DebugAnnotation::Decoder annotation(debug_annotation.data,
                                                      debug_annotation.size);


  StringId name_id = 0;

  uint64_t name_iid = annotation.name_iid();
  if (PERFETTO_LIKELY(name_iid)) {
    auto* decoder = sequence_state->LookupInternedMessage<
        protos::pbzero::InternedData::kDebugAnnotationNamesFieldNumber,
        protos::pbzero::DebugAnnotationName>(sequence_state_generation,
                                             name_iid);
    if (!decoder)
      return;

    std::string name_prefixed = "debug." + decoder->name().ToStdString();
    name_id = storage->InternString(base::StringView(name_prefixed));
  } else if (annotation.has_name()) {
    name_id = storage->InternString(annotation.name());
  } else {
    context_->storage->IncrementStats(stats::track_event_parser_errors);
    PERFETTO_DLOG("Debug annotation without name");
    return;
  }

  if (annotation.has_bool_value()) {
    args_tracker->AddArg(row_id, name_id, name_id,
                         Variadic::Boolean(annotation.bool_value()));
  } else if (annotation.has_uint_value()) {
    args_tracker->AddArg(row_id, name_id, name_id,
                         Variadic::UnsignedInteger(annotation.uint_value()));
  } else if (annotation.has_int_value()) {
    args_tracker->AddArg(row_id, name_id, name_id,
                         Variadic::Integer(annotation.int_value()));
  } else if (annotation.has_double_value()) {
    args_tracker->AddArg(row_id, name_id, name_id,
                         Variadic::Real(annotation.double_value()));
  } else if (annotation.has_string_value()) {
    args_tracker->AddArg(
        row_id, name_id, name_id,
        Variadic::String(storage->InternString(annotation.string_value())));
  } else if (annotation.has_pointer_value()) {
    args_tracker->AddArg(row_id, name_id, name_id,
                         Variadic::Pointer(annotation.pointer_value()));
  } else if (annotation.has_legacy_json_value()) {
    args_tracker->AddArg(
        row_id, name_id, name_id,
        Variadic::Json(storage->InternString(annotation.legacy_json_value())));
  } else if (annotation.has_nested_value()) {
    auto name = storage->GetString(name_id);
    ParseNestedValueArgs(annotation.nested_value(), name, name, args_tracker,
                         row_id);
  }
}

void ProtoTraceParser::ParseNestedValueArgs(ConstBytes nested_value,
                                            base::StringView flat_key,
                                            base::StringView key,
                                            ArgsTracker* args_tracker,
                                            RowId row_id) {
  protos::pbzero::DebugAnnotation::NestedValue::Decoder value(
      nested_value.data, nested_value.size);
  switch (value.nested_type()) {
    case protos::pbzero::DebugAnnotation::NestedValue::UNSPECIFIED: {
      auto flat_key_id = context_->storage->InternString(flat_key);
      auto key_id = context_->storage->InternString(key);
      // Leaf value.
      if (value.has_bool_value()) {
        args_tracker->AddArg(row_id, flat_key_id, key_id,
                             Variadic::Boolean(value.bool_value()));
      } else if (value.has_int_value()) {
        args_tracker->AddArg(row_id, flat_key_id, key_id,
                             Variadic::Integer(value.int_value()));
      } else if (value.has_double_value()) {
        args_tracker->AddArg(row_id, flat_key_id, key_id,
                             Variadic::Real(value.double_value()));
      } else if (value.has_string_value()) {
        args_tracker->AddArg(row_id, flat_key_id, key_id,
                             Variadic::String(context_->storage->InternString(
                                 value.string_value())));
      }
      break;
    }
    case protos::pbzero::DebugAnnotation::NestedValue::DICT: {
      auto key_it = value.dict_keys();
      auto value_it = value.dict_values();
      for (; key_it && value_it; ++key_it, ++value_it) {
        std::string child_name = key_it->as_std_string();
        std::string child_flat_key = flat_key.ToStdString() + "." + child_name;
        std::string child_key = key.ToStdString() + "." + child_name;
        ParseNestedValueArgs(value_it->as_bytes(),
                             base::StringView(child_flat_key),
                             base::StringView(child_key), args_tracker, row_id);
      }
      break;
    }
    case protos::pbzero::DebugAnnotation::NestedValue::ARRAY: {
      int child_index = 0;
      std::string child_flat_key = flat_key.ToStdString();
      for (auto value_it = value.array_values(); value_it;
           ++value_it, ++child_index) {
        std::string child_key =
            key.ToStdString() + "[" + std::to_string(child_index) + "]";
        ParseNestedValueArgs(value_it->as_bytes(),
                             base::StringView(child_flat_key),
                             base::StringView(child_key), args_tracker, row_id);
      }
      break;
    }
  }
}

void ProtoTraceParser::ParseTaskExecutionArgs(
    ConstBytes task_execution,
    ProtoIncrementalState::PacketSequenceState* sequence_state,
    size_t sequence_state_generation,
    ArgsTracker* args_tracker,
    RowId row) {
  protos::pbzero::TaskExecution::Decoder task(task_execution.data,
                                              task_execution.size);
  uint64_t iid = task.posted_from_iid();
  if (!iid)
    return;

  auto* decoder = sequence_state->LookupInternedMessage<
      protos::pbzero::InternedData::kSourceLocationsFieldNumber,
      protos::pbzero::SourceLocation>(sequence_state_generation, iid);
  if (!decoder)
    return;

  StringId file_name_id = 0;
  StringId function_name_id = 0;
  uint32_t line_number = 0;

  TraceStorage* storage = context_->storage.get();
  file_name_id = storage->InternString(decoder->file_name());
  function_name_id = storage->InternString(decoder->function_name());
  line_number = decoder->line_number();

  args_tracker->AddArg(row, task_file_name_args_key_id_,
                       task_file_name_args_key_id_,
                       Variadic::String(file_name_id));
  args_tracker->AddArg(row, task_function_name_args_key_id_,
                       task_function_name_args_key_id_,
                       Variadic::String(function_name_id));

  args_tracker->AddArg(row, task_line_number_args_key_id_,
                       task_line_number_args_key_id_,
                       Variadic::UnsignedInteger(line_number));
}

void ProtoTraceParser::ParseLogMessage(
    ConstBytes blob,
    ProtoIncrementalState::PacketSequenceState* sequence_state,
    size_t sequence_state_generation,
    int64_t ts,
    base::Optional<UniqueTid> utid,
    ArgsTracker* args_tracker,
    RowId row) {
  if (!utid) {
    context_->storage->IncrementStats(stats::track_event_parser_errors);
    PERFETTO_DLOG("LogMessage without thread association");
    return;
  }

  protos::pbzero::LogMessage::Decoder message(blob.data, blob.size);

  TraceStorage* storage = context_->storage.get();

  StringId log_message_id = 0;

  auto* decoder = sequence_state->LookupInternedMessage<
      protos::pbzero::InternedData::kLogMessageBodyFieldNumber,
      protos::pbzero::LogMessageBody>(sequence_state_generation,
                                      message.body_iid());
  if (!decoder)
    return;

  log_message_id = storage->InternString(decoder->body());

  // TODO(nicomazz): LogMessage also contains the source of the message (file
  // and line number). Android logs doesn't support this so far.
  context_->storage->mutable_android_log()->AddLogEvent(
      ts, *utid,
      /*priority*/ 0,
      /*tag_id*/ 0,  // TODO(nicomazz): Abuse tag_id to display
                     // "file_name:line_number".
      log_message_id);

  args_tracker->AddArg(row, log_message_body_key_id_, log_message_body_key_id_,
                       Variadic::String(log_message_id));
  // TODO(nicomazz): Add the source location as an argument.
}

void ProtoTraceParser::ParseChromeBenchmarkMetadata(ConstBytes blob) {
  TraceStorage* storage = context_->storage.get();
  protos::pbzero::ChromeBenchmarkMetadata::Decoder packet(blob.data, blob.size);
  if (packet.has_benchmark_name()) {
    auto benchmark_name_id = storage->InternString(packet.benchmark_name());
    storage->SetMetadata(metadata::benchmark_name,
                         Variadic::String(benchmark_name_id));
  }
  if (packet.has_benchmark_description()) {
    auto benchmark_description_id =
        storage->InternString(packet.benchmark_description());
    storage->SetMetadata(metadata::benchmark_description,
                         Variadic::String(benchmark_description_id));
  }
  if (packet.has_label()) {
    auto label_id = storage->InternString(packet.label());
    storage->SetMetadata(metadata::benchmark_label, Variadic::String(label_id));
  }
  if (packet.has_story_name()) {
    auto story_name_id = storage->InternString(packet.story_name());
    storage->SetMetadata(metadata::benchmark_story_name,
                         Variadic::String(story_name_id));
  }
  for (auto it = packet.story_tags(); it; ++it) {
    auto story_tag_id = storage->InternString(it->as_string());
    storage->AppendMetadata(metadata::benchmark_story_tags,
                            Variadic::String(story_tag_id));
  }
  if (packet.has_benchmark_start_time_us()) {
    storage->SetMetadata(metadata::benchmark_start_time_us,
                         Variadic::Integer(packet.benchmark_start_time_us()));
  }
  if (packet.has_story_run_time_us()) {
    storage->SetMetadata(metadata::benchmark_story_run_time_us,
                         Variadic::Integer(packet.story_run_time_us()));
  }
  if (packet.has_story_run_index()) {
    storage->SetMetadata(metadata::benchmark_story_run_index,
                         Variadic::Integer(packet.story_run_index()));
  }
  if (packet.has_had_failures()) {
    storage->SetMetadata(metadata::benchmark_had_failures,
                         Variadic::Integer(packet.had_failures()));
  }
}

void ProtoTraceParser::ParseChromeEvents(int64_t ts, ConstBytes blob) {
  TraceStorage* storage = context_->storage.get();
  protos::pbzero::ChromeEventBundle::Decoder bundle(blob.data, blob.size);
  ArgsTracker args(context_);
  if (bundle.has_metadata()) {
    RowId row_id = storage->mutable_raw_events()->AddRawEvent(
        ts, raw_chrome_metadata_event_id_, 0, 0);

    // Metadata is proxied via a special event in the raw table to JSON export.
    for (auto it = bundle.metadata(); it; ++it) {
      protos::pbzero::ChromeMetadata::Decoder metadata(it->as_bytes().data,
                                                       it->as_bytes().size);
      StringId name_id = storage->InternString(metadata.name());
      Variadic value;
      if (metadata.has_string_value()) {
        value =
            Variadic::String(storage->InternString(metadata.string_value()));
      } else if (metadata.has_int_value()) {
        value = Variadic::Integer(metadata.int_value());
      } else if (metadata.has_bool_value()) {
        value = Variadic::Integer(metadata.bool_value());
      } else if (metadata.has_json_value()) {
        value = Variadic::Json(storage->InternString(metadata.json_value()));
      } else {
        PERFETTO_FATAL("Empty ChromeMetadata message");
      }
      args.AddArg(row_id, name_id, name_id, value);
    }
  }

  if (bundle.has_legacy_ftrace_output()) {
    RowId row_id = storage->mutable_raw_events()->AddRawEvent(
        ts, raw_chrome_legacy_system_trace_event_id_, 0, 0);

    std::string data;
    for (auto it = bundle.legacy_ftrace_output(); it; ++it) {
      data += it->as_string().ToStdString();
    }
    Variadic value =
        Variadic::String(storage->InternString(base::StringView(data)));
    args.AddArg(row_id, data_name_id_, data_name_id_, value);
  }

  if (bundle.has_legacy_json_trace()) {
    for (auto it = bundle.legacy_json_trace(); it; ++it) {
      protos::pbzero::ChromeLegacyJsonTrace::Decoder legacy_trace(
          it->as_bytes().data, it->as_bytes().size);
      if (legacy_trace.type() !=
          protos::pbzero::ChromeLegacyJsonTrace::USER_TRACE) {
        continue;
      }
      RowId row_id = storage->mutable_raw_events()->AddRawEvent(
          ts, raw_chrome_legacy_user_trace_event_id_, 0, 0);
      Variadic value =
          Variadic::String(storage->InternString(legacy_trace.data()));
      args.AddArg(row_id, data_name_id_, data_name_id_, value);
    }
  }
}

void ProtoTraceParser::ParseMetatraceEvent(int64_t ts, ConstBytes blob) {
  protos::pbzero::PerfettoMetatrace::Decoder event(blob.data, blob.size);
  auto utid = context_->process_tracker->GetOrCreateThread(event.thread_id());

  StringId cat_id = metatrace_id_;
  StringId name_id = 0;
  char fallback[64];

  if (event.has_event_id()) {
    auto eid = event.event_id();
    if (eid < metatrace::EVENTS_MAX) {
      name_id = context_->storage->InternString(metatrace::kEventNames[eid]);
    } else {
      sprintf(fallback, "Event %d", eid);
      name_id = context_->storage->InternString(fallback);
    }
    TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
    context_->slice_tracker->Scoped(ts, track_id, utid, RefType::kRefUtid,
                                    cat_id, name_id, event.event_duration_ns());
  } else if (event.has_counter_id()) {
    auto cid = event.counter_id();
    if (cid < metatrace::COUNTERS_MAX) {
      name_id = context_->storage->InternString(metatrace::kCounterNames[cid]);
    } else {
      sprintf(fallback, "Counter %d", cid);
      name_id = context_->storage->InternString(fallback);
    }
    context_->event_tracker->PushCounter(ts, event.counter_value(), name_id,
                                         utid, RefType::kRefUtid);
  }

  if (event.has_overruns())
    context_->storage->IncrementStats(stats::metatrace_overruns);
}

void ProtoTraceParser::ParseTraceConfig(ConstBytes blob) {
  protos::pbzero::TraceConfig::Decoder trace_config(blob.data, blob.size);
  if (trace_config.has_statsd_metadata()) {
    ParseStatsdMetadata(trace_config.statsd_metadata());
  }
}

void ProtoTraceParser::ParseStatsdMetadata(ConstBytes blob) {
  protos::pbzero::TraceConfig::StatsdMetadata::Decoder metadata(blob.data,
                                                                blob.size);
  if (metadata.has_triggering_subscription_id()) {
    context_->storage->SetMetadata(
        metadata::statsd_triggering_subscription_id,
        Variadic::Integer(metadata.triggering_subscription_id()));
  }
}

void ProtoTraceParser::ParseAndroidPackagesList(ConstBytes blob) {
  protos::pbzero::PackagesList::Decoder pkg_list(blob.data, blob.size);
  context_->storage->SetStats(stats::packages_list_has_read_errors,
                              pkg_list.read_error());
  context_->storage->SetStats(stats::packages_list_has_parse_errors,
                              pkg_list.parse_error());

  // Insert the package info into arg sets (one set per package), with the arg
  // set ids collected in the Metadata table, under
  // metadata::android_packages_list key type.
  for (auto it = pkg_list.packages(); it; ++it) {
    // Insert a placeholder metadata entry, which will be overwritten by the
    // arg_set_id when the arg tracker is flushed.
    RowId row_id = context_->storage->AppendMetadata(
        metadata::android_packages_list, Variadic::Integer(0));

    auto add_arg = [this, row_id](base::StringView name, Variadic value) {
      StringId key_id = context_->storage->InternString(name);
      context_->args_tracker->AddArg(row_id, key_id, key_id, value);
    };
    protos::pbzero::PackagesList_PackageInfo::Decoder pkg(it->data(),
                                                          it->size());
    add_arg("name",
            Variadic::String(context_->storage->InternString(pkg.name())));
    add_arg("uid", Variadic::UnsignedInteger(pkg.uid()));
    add_arg("debuggable", Variadic::Boolean(pkg.debuggable()));
    add_arg("profileable_from_shell",
            Variadic::Boolean(pkg.profileable_from_shell()));
    add_arg("version_code", Variadic::Integer(pkg.version_code()));
  }
}

void ProtoTraceParser::ParseModuleSymbols(ConstBytes blob) {
  protos::pbzero::ModuleSymbols::Decoder module_symbols(blob.data, blob.size);
  std::string hex_build_id = base::ToHex(module_symbols.build_id().data,
                                         module_symbols.build_id().size);
  auto mapping_rows =
      context_->storage->stack_profile_mappings().FindMappingRow(
          context_->storage->InternString(module_symbols.path()),
          context_->storage->InternString(base::StringView(hex_build_id)));
  if (mapping_rows.empty()) {
    context_->storage->IncrementStats(stats::stackprofile_invalid_mapping_id);
    return;
  }
  for (auto addr_it = module_symbols.address_symbols(); addr_it; ++addr_it) {
    protos::pbzero::AddressSymbols::Decoder address_symbols(addr_it->data(),
                                                            addr_it->size());

    ssize_t frame_row = -1;
    for (int64_t mapping_row : mapping_rows) {
      frame_row = context_->storage->stack_profile_frames().FindFrameRow(
          static_cast<size_t>(mapping_row), address_symbols.address());
      if (frame_row != -1)
        break;
    }
    if (frame_row == -1) {
      context_->storage->IncrementStats(stats::stackprofile_invalid_frame_id);
      continue;
    }
    uint32_t symbol_set_id = context_->storage->symbol_table().size();
    context_->storage->mutable_stack_profile_frames()->SetSymbolSetId(
        static_cast<size_t>(frame_row), symbol_set_id);
    for (auto line_it = address_symbols.lines(); line_it; ++line_it) {
      protos::pbzero::Line::Decoder line(line_it->data(), line_it->size());
      context_->storage->mutable_symbol_table()->Insert(
          {symbol_set_id, context_->storage->InternString(line.function_name()),
           context_->storage->InternString(line.source_file_name()),
           line.line_number()});
    }
  }
}

void ProtoTraceParser::ParseHeapGraph(int64_t ts, ConstBytes blob) {
  protos::pbzero::HeapGraph::Decoder heap_graph(blob.data, blob.size);
  UniquePid upid = context_->process_tracker->GetOrCreateProcess(
      static_cast<uint32_t>(heap_graph.pid()));
  context_->heap_graph_tracker->SetPacketIndex(heap_graph.index());
  for (auto it = heap_graph.objects(); it; ++it) {
    protos::pbzero::HeapGraphObject::Decoder object(it->data(), it->size());
    HeapGraphTracker::SourceObject obj;
    obj.object_id = object.id();
    obj.self_size = object.self_size();
    obj.type_id = object.type_id();
    auto ref_field_ids_it = object.reference_field_id();
    auto ref_object_ids_it = object.reference_object_id();
    for (; ref_field_ids_it && ref_object_ids_it;
         ++ref_field_ids_it, ++ref_object_ids_it) {
      HeapGraphTracker::SourceObject::Reference ref;
      ref.field_name_id = ref_field_ids_it->as_uint64();
      ref.owned_object_id = ref_object_ids_it->as_uint64();
      obj.references.emplace_back(std::move(ref));
    }

    if (ref_field_ids_it || ref_object_ids_it) {
      context_->storage->IncrementIndexedStats(stats::heap_graph_missing_packet,
                                               static_cast<int>(upid));
      continue;
    }
    context_->heap_graph_tracker->AddObject(upid, ts, std::move(obj));
  }
  for (auto it = heap_graph.type_names(); it; ++it) {
    protos::pbzero::InternedString::Decoder entry(it->data(), it->size());
    const char* str = reinterpret_cast<const char*>(entry.str().data);
    auto str_view = base::StringView(str, entry.str().size);

    context_->heap_graph_tracker->AddInternedTypeName(
        entry.iid(), context_->storage->InternString(str_view));
  }
  for (auto it = heap_graph.field_names(); it; ++it) {
    protos::pbzero::InternedString::Decoder entry(it->data(), it->size());
    const char* str = reinterpret_cast<const char*>(entry.str().data);
    auto str_view = base::StringView(str, entry.str().size);

    context_->heap_graph_tracker->AddInternedFieldName(
        entry.iid(), context_->storage->InternString(str_view));
  }
  for (auto it = heap_graph.roots(); it; ++it) {
    protos::pbzero::HeapGraphRoot::Decoder entry(it->data(), it->size());
    const char* str = HeapGraphRootTypeToString(entry.root_type());
    auto str_view = base::StringView(str);

    HeapGraphTracker::SourceRoot src_root;
    src_root.root_type = context_->storage->InternString(str_view);
    for (auto obj_it = entry.object_ids(); obj_it; ++obj_it)
      src_root.object_ids.emplace_back(obj_it->as_uint64());
    context_->heap_graph_tracker->AddRoot(upid, ts, std::move(src_root));
  }
  if (!heap_graph.continued()) {
    context_->heap_graph_tracker->FinalizeProfile();
  }
}

}  // namespace trace_processor
}  // namespace perfetto
