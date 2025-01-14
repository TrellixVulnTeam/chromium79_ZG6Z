# heapprofd - Android Heap Profiler

**heapprofd requires Android 10.**

heapprofd is a tool that tracks native heap allocations & deallocations of an
Android process within a given time period. The resulting profile can be used
to attribute memory usage to particular function callstacks, supporting a mix
of both native and java code. The tool can be used by Android platform and app
developers to investigate memory issues.

On debug Android builds, you can profile all apps and most system services.
On "user" builds, you can only use it on apps with the debuggable or
profileable manifest flag.

## Quickstart

<!-- This uses github because gitiles does not allow to get the raw file. -->

Use the `tools/heap_profile` script to heap profile a process. If you are
having trouble make sure you are using the [latest version](
https://raw.githubusercontent.com/catapult-project/perfetto/master/tools/heap_profile).

See all the arguments using `tools/heap_profile -h`, or use the defaults
and just profile a process (e.g. `system_server`):

```
$ tools/heap_profile --name system_server
Profiling active. Press Ctrl+C to terminate.
^CWrote profiles to /tmp/heap_profile-XSKcZ3i (symlink /tmp/heap_profile-latest)
These can be viewed using pprof. Googlers: head to pprof/ and upload them.
```

This will create a pprof-compatible heap dump when Ctrl+C is pressed.

## Viewing the data

The resulting profile proto contains four views on the data

* **space**: how many bytes were allocated but not freed at this callstack the
  moment the dump was created.
* **alloc\_space**: how many bytes were allocated (including ones freed at the
  moment of the dump) at this callstack
* **idle\_space**: if [idle page tracking](#idle-page-tracking) is being used,
  the number of bytes that were allocated at this callstack and are on pages
  that have not been touched since the last dump.
* **objects**: how many allocations without matching frees were done at this
  callstack.
* **alloc\_objects**: how many allocations (including ones with matching frees)
  were done at this callstack.

**Googlers:** Head to http://pprof/ and upload the gzipped protos to get a
visualization. *Tip: you might want to put `libart.so` as a "Hide regex" when
profiling apps.*

[Speedscope](https://speedscope.app) can also be used to visualize the heap
dump, but will only show the space view. *Tip: Click Left Heavy on the top
left for a good visualisation.*

## Sampling interval
heapprofd samples heap allocations. Given a sampling interval of n bytes,
one allocation is sampled, on average, every n bytes allocated. This allows to
reduce the performance impact on the target process. The default sampling rate
is 4096 bytes.

The easiest way to reason about this is to imagine the memory allocations as a
steady stream of one byte allocations. From this stream, every n-th byte is
selected as a sample, and the corresponding allocation gets attributed the
complete n bytes. As an optimization, we sample allocations larger than the
sampling interval with their true size.

To make this statistically more meaningful, Poisson sampling is employed.
Instead of a static parameter of n bytes, the user can only choose the mean
value around which the interval is distributed. This makes sure frequent small
allocations get sampled as well as infrequent large ones.

## Startup profiling
When a profile session names processes by name and a matching process is
started, it gets profiled from the beginning. The resulting profile will
contain all allocations done between the start of the process and the end
of the profiling session.

On Android, Java apps are usually not started, but the zygote forks and then
specializes into the desired app. If the app's name matches a name specified
in the profiling session, profiling will be enabled as part of the zygote
specialization. The resulting profile contains all allocations done between
that point in zygote specialization and the end of the profiling session.
Some allocations done early in the specialization process are not accounted
for.

The Resulting `ProfileProto` will have `from_startup` set  to true in the
corresponding `ProcessHeapSamples` message. This does not get surfaced in the
converted pprof compatible proto.

## Runtime profiling
When a profile session is started, all matching processes (by name or PID)
are enumerated and profiling is enabled. The resulting profile will contain
all allocations done between the beginning and the end of the profiling
session.

The Resulting `ProfileProto` will have `from_startup` set  to false in the
corresponding `ProcessHeapSamples` message. This does not get surfaced in the
converted pprof compatible proto.

## Concurrent profiling sessions
If multiple sessions name the same target process (either by name or PID),
only the first relevant session will profile the process. The other sessions
will report that the process had already been profiled when converting to
the pprof compatible proto.

If you see this message but do not expect any other sessions, run
```
adb shell killall -KILL perfetto
```
to stop any concurrent sessions that may be running.


The Resulting `ProfileProto` will have `rejected_concurrent` set  to true in
otherwise empty corresponding `ProcessHeapSamples` message. This does not get
surfaced in the converted pprof compatible proto.

## Target processes
Depending on the build of Android that heapprofd is run on, some processes
are not be eligible to be profiled.

On user builds, only Java applications with either the profileable or the
debuggable manifest flag set can be profiled. Profiling requests for other
processes will result in an empty profile.

On userdebug builds, all processes except for a small blacklist of critical
services can be profiled (to find the blacklist, look for
`never_profile_heap` in [heapprofd.te](
https://android.googlesource.com/platform/system/sepolicy/+/refs/heads/master/private/heapprofd.te)).
This restriction can be lifted by disabling SELinux by running
`adb shell su root setenforce 0` or by passing `--disable-selinux` to the
`heap_profile` script.

|                         | userdebug setenforce 0 | userdebug | user |
|-------------------------|------------------------|-----------|------|
| critical native service |            y           |     n     |  n   |
| native service          |            y           |     y     |  n   |
| app                     |            y           |     y     |  n   |
| profileable app         |            y           |     y     |  y   |
| debuggable app          |            y           |     y     |  y   |

## DEDUPED frames
If the name of a Java method includes `[DEDUPED]`, this means that multiple
methods share the same code. ART only stores the name of a single one in its
metadata, which is displayed here. This is not necessarily the one that was
called.

## Manual dumping
You can trigger a manual dump of all currently profiled processes by running
`adb killall -USR1 heapprofd`. This can be useful for seeing the current memory
usage of the target in a specific state.

This dump will show up in addition to the dump at the end of the profile that is
always produced. You can create multiple of these dumps, and they will be
enumerated in the output directory.

## Symbolization
If the profiled binary or libraries do not have debug symbols, you can use
pprof to symbolize offline.

To do so, copy symbolized versions of your binary and/or libraries into a
directory. Then run
`PPROF_BINARY_PATH=thatdirectory pprof heap_profile.${n}.${pid}.gz`, and pprof
will read symbol information from these files.

You can save the symbolized version by issuing the `proto` command in pprof.

## Idle page tracking
This is only available in Android versions newer than 10.

Idle page tracking allows you to analyze which allocations made by your
program are being used by a workload. This can be useful for finding leaks
as well as unused cached values.

**Do not follow these instructions on devices containing valuable data.**
They require you turn off SELinux on your device, significantly lowering
your device's security level.

Use the following command to profile the next startup of your program with idle
tracking enabled.

1. `$ adb root`
2. `$ tools/heap_profile -n ${NAME} --no-running --disable-selinux
--idle-allocations`

Then run the following commands in a separate shell.

1. `$ adb shell killall ${ROOT}` to restart your program.
2. Wait for your program to finish starting.
3. `adb shell killall -USR1 heapprofd` to trigger the first dump (see
[Manual Dumping](#manual-dumping) above). This will mark all allocations as
idle.
4. Interact with your program.

Once you are done interacting, `Ctrl-C` the invokation of
`tools/heap_profile`, and upload the `heap_dump.2.*.pb.gz` file to pprof.
You can then see the memory that was idle in the `idle_space` tab.

This will show allocations that are on pages that have not been touched since
the last dump. Small allocations that are not touched might not show up, as
they might share a page with an allocation that was.

If heapprofd is operating in sampling mode (i.e. `--interval` is larger than 1),
the values in `idle_space` will not correct for the sampling, so they are not
comparable to values in `space` and `alloc_space`, which do.

## Troubleshooting

### Buffer overrun
If the rate of allocations is too high for heapprofd to keep up, the profiling
session will end early due to a buffer overrun. If the buffer overrun is
caused by a transient spike in allocations, increasing the shared memory buffer
size (passing `--shmem-size` to heap\_profile) can resolve the issue.
Otherwise the sampling interval can be increased (at the expense of lower
accuracy in the resulting profile) by passing `--interval` to heap\_profile.

### Profile is empty
Check whether your target process is eligible to be profiled by consulting
[Target processes](#target-processes) above.

Also check the [Known Issues](#known-issues).


### Impossible callstacks
If you see a callstack that seems to impossible from looking at the code, make
sure no [DEDUPED frames](#deduped-frames) are involved.

## Known Issues

### Android 10
* Does not work on x86 platforms (including the Android cuttlefish emulator).
* If heapprofd is run standalone (by running `heapprofd` in a root shell, rather
  than through init), `/dev/socket/heapprofd` get assigned an incorrect SELinux
  domain. You will not be able to profile any processes unless you disable
  SELinux enforcement.
  Run `restorecon /dev/socket/heapprofd` in a root shell to resolve.

## Ways to count memory

When using heapprofd and interpreting results, it is important to know the
precise meaning of the different memory metrics that can be obtained from the
operating system.

**heapprofd** gives you the number of bytes the target program
requested from the allocator. If you are profiling a Java app from startup,
allocations that happen early in the application's initialization will not be
visible to heapprofd. Native services that do not fork from the Zygote
are not affected by this.

**malloc\_info** is a libc function that gives you information about the
allocator. This can be triggered on userdebug builds by using
`am dumpheap -m <PID> /data/local/tmp/heap.txt`. This will in general be more
than the memory seen by heapprofd, depending on the allocator not all memory
is immediately freed. In particular, jemalloc retains some freed memory in
thread caches.

**Heap RSS** is the amount of memory requested from the operating system by the
allocator. This is larger than the previous two numbers because memory can only
be obtained in page size chunks, and fragmentation causes some of that memory to
be wasted. This can be obtained by running `adb shell dumpsys meminfo <PID>` and
looking at the "Private Dirty" column.

|                     | heapprofd         | malloc\_info | RSS |
|---------------------|-------------------|--------------|-----|
| from native startup |          x        |      x       |  x  |
| after zygote init   |          x        |      x       |  x  |
| before zygote init  |                   |      x       |  x  |
| thread caches       |                   |      x       |  x  |
| fragmentation       |                   |              |  x  |

If you observe high RSS or malloc\_info metrics but heapprofd does not match,
there might be a problem with fragmentation or the allocator.

## Manual instructions
*It is not recommended to use these instructions unless you have advanced
requirements or are developing heapprofd. Proceed with caution*

### Download trace\_to\_text
Download the latest trace\_to\_text for [Linux](
https://storage.googleapis.com/perfetto/trace_to_text-4ab1d18e69bc70e211d27064505ed547aa82f919)
or [MacOS](https://storage.googleapis.com/perfetto/trace_to_text-mac-2ba325f95c08e8cd5a78e04fa85ee7f2a97c847e).
This is needed to convert the Perfetto trace to a pprof-compatible file.

Compare the `sha1sum` of this file to the one contained in the file name.

### Start profiling
To start profiling the process `${PID}`, run the following sequence of commands.
Adjust the `INTERVAL` to trade-off runtime impact for higher accuracy of the
results. If `INTERVAL=1`, every allocation is sampled for maximum accuracy.
Otherwise, a sample is taken every `INTERVAL` bytes on average.

```bash
INTERVAL=4096

echo '
buffers {
  size_kb: 100024
}

data_sources {
  config {
    name: "android.heapprofd"
    target_buffer: 0
    heapprofd_config {
      sampling_interval_bytes: '${INTERVAL}'
      pid: '${PID}'
    }
  }
}

duration_ms: 20000
' | adb shell perfetto --txt -c - -o /data/misc/perfetto-traces/profile

adb pull /data/misc/perfetto-traces/profile /tmp/profile
```

### Convert to pprof compatible file

While we work on UI support, you can convert the trace into pprof compatible
heap dumps.

Use the trace\_to\_text file downloaded above, with XXXXXXX replaced with the
`sha1sum` of the file.

```
trace_to_text-linux-XXXXXXX profile /tmp/profile
```

This will create a directory in `/tmp/` containing the heap dumps. Run

```
gzip /tmp/heap_profile-XXXXXX/*.pb
```

to get gzipped protos, which tools handling pprof profile protos expect.

Follow the instructions in [Viewing the Data](#viewing-the-data) to visualise
the results.
