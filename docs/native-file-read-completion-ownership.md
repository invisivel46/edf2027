# File-read completion ownership audit

Scope: the inspected ReXGlue SDK at
`D:/roms2/edf3-translation-project/rexglue/rexglue-sdk`, plus EDF's current
generated import callers. This records output provenance, not permission to
remove the conservative file-read scope.

## Allocations that are CPU-only on the inspected creation paths

`Memory::SystemHeapAlloc` in `src/system/xmemory.cpp:693` defaults to
`kSystemHeapDefault = kSystemHeapVirtual` (xmemory.h). It selects
`LookupHeapByType(false,4096)`, which returns `heaps_.v00000000`, not any of
the A/C/E physical heaps. It zeros the allocated CPU virtual range.

`XThread::EnqueueApc` allocates its new XAPC using that default. Its field
initialization and new-node links therefore target CPU-only storage on this
path. `XThread::Create` uses `CreateNative<X_KTHREAD>`; `XObject::CreateNative`
also uses default SystemHeapAlloc for the object/header and object type.
Thread scratch, TLS and PCR allocations in xthread.cpp use the same default.
This establishes their allocation provenance, not immutability of guest links
or arbitrary pointers later supplied by a guest.

## Queue insertion has an additional indirect destination

`xeKeInsertQueueApc` obtains the thread pointer from the new APC, acquires the
thread's APC spinlock and inserts the user APC at the list tail. It updates
the new node's arguments/enqueued flag and the thread's list head. However,
`XeInsertTailList` also loads the old tail from `list_head->blink_ptr` and writes
`old_tail->flink_ptr`. The new node being CPU-only does not by itself prove
that this existing node is CPU-only.

The SDK's public `KeInitializeApc` and `KeInsertQueueApc` entries accept guest-
supplied APC pointers. A queue containing such nodes is a separate provenance
case. Searches of EDF's current generated .cpp/.h files and the manifest/import
inventory found no named KeInitializeApc, KeInsertQueueApc, KeRemoveQueueApc or
NtQueueApcThread references; NtReadFile references are present in the same
generated search scope. This excludes named direct import use there, not all
indirect entry, guest modifications of list links, or every SDK queue producer.
Do not replace that distinction with a blanket claim that all APC queues are
CPU-only.

Spinlock acquisition/release writes the lock owner and PCR IRQL. The inspected
`xeKfLowerIrql` only stores the IRQL byte; it does not execute queued APC routines.
`EnqueueApc` schedules an empty host callback as a wakeup hint. `DeliverAPCs` later
invokes guest routines via the dispatcher. Later callback writes require their
own writer coverage; the file-read return scope cannot cover their lifetime.

## Physical read invalidation is another callback boundary

`XFile::ReadInternal` calls PhysicalHeap::TriggerCallbacks after a successful
physical ReadSync. TriggerCallbacks walks the registered physical invalidation
callbacks while managing page-watch/protection metadata. Registration sites found
in this workspace and SDK are GuestPhysicalVersions, graphics SharedMemory and
PrimitiveProcessor. GuestPhysicalVersions::Invalidate changes host atomic page
versions/foreign-write flags, not guest geometry bytes. The complete SharedMemory
watch callback chain and PrimitiveProcessor invalidation body still require
review before certifying this entire boundary. Their registration being in a
graphics subsystem is not proof of payload-write absence or native-mode inactivity.

## Current implementation and next decision

The data destination and optional eight-byte status output are now independently
guarded and notified. Both retain conservative unknown-range scopes. Event.Set
and completion-port QueueNotification inspected previously operate on host event/
queue state. The APC allocation audit removes uncertainty about the new node,
thread object and normal PCR allocation, but not the old-tail link or all physical
invalidation callbacks.

Next: finish callback-body/registration review and classify the actual file-read
APC request path. A no-APC path may have a smaller provable output set; do not
narrow APC-enabled calls solely from the new-node allocation result. No executable
or comparison policy changed for this audit, and no build or game run was needed.

## Follow-up: callback chain and no-APC exact ranges

Completed the identified callback bodies. PrimitiveProcessor invalidation edits
its host cache map, entry pool, bucket links and bitsets under cache_mutex_.
SharedMemory invalidation changes host page-valid/GPU-written flags, then calls
FireWatches. Searches of current SDK/workspace registration sites found texture
base/mip watches and the scaled-resolve global watch. Texture watches only mark
host texture state outdated and clear watch handles; the global watch clears
host scaled-resolve page bits. SharedMemory::UnlinkWatchRange recycles host watch
nodes/ranges. These reviewed paths do not write additional guest payload bytes
or invoke guest code. This conclusion depends on the inspected callback set;
new registrations or changed callback bodies require a fresh audit.

The native NtReadFile wrapper now selects exact physical data/status ranges only
when `!(routine & ~1u) || !apc_context`, matching the inspected SDK's conditions
for not entering EnqueueApc. Low-bit-only routine values do not request a callback.
APC-capable calls remain unknown-range, irrespective of their eventual return
status. Invalid/malformed mapped ranges also retain the existing unknown fallback.
Both outputs are guarded before the original provider and notified afterward;
the status-only and failure cases remain covered. No APC delivery semantics,
events or completion notifications are removed.

The extracted actual-hook fixture checks routine values 0, 1, aligned nonzero and
low-bit-tagged nonzero, each with null/non-null context. While the provider is
active, unrelated geometry can be copied for no-APC calls but remains excluded
by two FileRead unknown scopes for APC-capable calls with both outputs mapped.
Existing data/status output, failure, ABI and overlap tests remain in the suite.
Full build and all 21 tests passed in 12.48s. No live run yet validates this
conditional narrowing. APC-enabled queue ownership and overlapping-write fallback
remain open; this is not complete file-provider or geometry-writer replacement.
