#ifndef CYBERDESKTOP_BACKGROUND_SERVICE_H
#define CYBERDESKTOP_BACKGROUND_SERVICE_H

#include "sdk/PluginApi.h"

#define CD_SERVICE_BACKGROUND "host.background"
#define CD_BACKGROUND_VERSION 1u
#define CD_BACKGROUND_COMMANDS 64u
#define CD_BACKGROUND_COMMAND_BYTES 1024u
#define CD_BACKGROUND_SNAPSHOT_SLOTS 4u
#define CD_BACKGROUND_SNAPSHOT_BYTES 262144u

#define CD_BACKGROUND_LATEST 1u
#define CD_BACKGROUND_INITIALIZE 1u
#define CD_BACKGROUND_RESET 2u
#define CD_BACKGROUND_TICK 4u
#define CD_BACKGROUND_SUSPEND 8u
#define CD_BACKGROUND_ACTIVE 1u

#define CD_BACKGROUND_PAUSE 1u
#define CD_BACKGROUND_RESUME 2u
#define CD_BACKGROUND_RESTART 3u
#define CD_BACKGROUND_CANCEL 4u

#define CD_BACKGROUND_STARTING 1u
#define CD_BACKGROUND_RUNNING 2u
#define CD_BACKGROUND_PAUSED 3u
#define CD_BACKGROUND_CANCELING 4u
#define CD_BACKGROUND_FINISHED 5u
#define CD_BACKGROUND_FAULTED 6u
#define CD_BACKGROUND_RETAINED 7u

#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif

/* Ordinary commands are copied before submit returns. Discrete commands retain
   FIFO order. LATEST replaces the same kind/key only after the last discrete
   command. BUSY is explicit backpressure: the caller retains the event.
   Every command and control request names the current nonzero generation. */
typedef struct CdBackgroundCommand {
    uint32_t struct_size, kind, flags, size;
    uint64_t generation, key, sequence;
    const void* data;
} CdBackgroundCommand;

/* Only owner-lane callbacks access the model. elapsed_ns excludes paused time,
   is zero for initialization/reset, and is bounded to 100 ms. The model keeps
   its existing fixed step/substep and maximum catchup policy. A paused task may
   initialize/reset and publish one static snapshot with zero elapsed time;
   ordinary input and periodic TICK work remain disabled until resume. */
typedef struct CdBackgroundWork {
    uint32_t struct_size, version, flags, command_count;
    uint64_t generation, sequence, monotonic_ns, elapsed_ns;
    const CdBackgroundCommand* commands;
    const void* reset_data;
    uint32_t reset_size, reserved;
} CdBackgroundWork;

/* Host-owned, max_align_t-aligned writable storage, valid only for this call.
   Output is pointer-free POD, including model version/step/hit data as needed.
   Images, fonts, handles and static geometry remain outside frame snapshots.
   Set ACTIVE to request another period; otherwise the lane sleeps until input.
   A successful call publishes size bytes (including a zero-size frame), except
   SUSPEND: data is NULL/capacity zero, cancel model input without advancing or
   publishing. It may accompany INITIALIZE when paused before the first call.
   slot is a host-owned index 0..3, or UINT32_MAX for SUSPEND. Preserve it.
   Plugin-local immutable geometry may use the same four slots: the host never
   reuses a leased slot or the currently published slot for a callback. */
typedef struct CdBackgroundOutput {
    uint32_t struct_size, version, capacity, size;
    void* data;
    uint32_t flags, slot;
} CdBackgroundOutput;

typedef CdResult(CD_CALL* CdBackgroundExecute)(void* model_context, const CdBackgroundWork* work,
                                               CdBackgroundOutput* output);
typedef void(CD_CALL* CdBackgroundDestroy)(void* model_context);

/* One task per instance, fixed lane from first callback through destroy.
   On successful create, ownership of model_context transfers to the host;
   destroy runs exactly once on that lane, including failed initialization.
   On failure, ownership stays with the caller. Callbacks must return and must
   not access UI/rendering/host services. No C++ object crosses this ABI. */
typedef struct CdBackgroundTaskDesc {
    uint32_t struct_size, version, snapshot_bytes, reserved;
    uint64_t period_ns;
    void* model_context;
    CdBackgroundExecute execute;
    CdBackgroundDestroy destroy;
} CdBackgroundTaskDesc;

/* Reset and pause/cancel have storage independent of the ordinary command
   queue. RESTART copies <=1024 bytes and advances generation atomically,
   invalidating queued commands and old published results. PAUSE revokes pending
   ordinary commands and acknowledges after in-flight work and an owner-lane
   SUSPEND callback that cancels model input. RESUME uses a new time anchor.
   CANCEL revokes UI notification immediately and never waits for callbacks.
   An in-flight canceled task still occupies its lane and resource budgets. */
typedef struct CdBackgroundControl {
    uint32_t struct_size, operation, size, reserved;
    uint64_t generation;
    const void* data;
} CdBackgroundControl;

/* Immutable, independent read lease; release exactly once on any thread.
   Acquire fails after a task fault, including while its destroy is in flight;
   callers may read stats to distinguish failure from an unavailable snapshot.
   release_context does not refer to the UI instance. A lease keeps storage and
   the actual plugin module loaded even after cancel/instance deletion.
   Four preallocated slots: held readers are never overwritten. Exhaustion
   delays execution/publication and reports backpressure without allocation. */
typedef struct CdBackgroundSnapshot {
    uint32_t struct_size, version, size, slot;
    uint64_t generation, sequence, monotonic_ns;
    const void* data;
    void* release_context;
    void(CD_CALL* release)(void* release_context);
} CdBackgroundSnapshot;

/* Independent of diagnostics/v1's 64 metric slots. Read does not query models.
   state is the acknowledged lane state; requested pause/cancel can precede it. */
typedef struct CdBackgroundStats {
    uint32_t struct_size, version, state, lane;
    uint64_t generation, completed_sequence, callbacks, callback_ns, maximum_callback_ns;
    uint64_t queue_entries, queue_bytes, command_backpressure, coalesced_commands;
    uint64_t snapshot_backpressure, snapshot_leases, snapshot_bytes, snapshot_age_ns;
    uint64_t pause_requested_ns, pause_acknowledged_ns, late_ns;
    int32_t fault;
    uint32_t reserved;
    uint64_t submitted_command_sequence, completed_command_sequence, acquired_command_sequence;
    uint64_t acquired_snapshot_age_ns;
    uint64_t revoked_command_sequence;
} CdBackgroundStats;

/* API calls except snapshot.release are owner-UI-thread operations. Optional
   service: unavailable hosts return UNSUPPORTED; callers explicitly choose a
   supported reference mode or report unsupported, never silently add a thread.
   context is service-owned and valid until the UI instance is destroyed.
   All output structures must supply struct_size and version before calls. */
typedef struct CdBackgroundApi {
    uint32_t struct_size, version;
    void* context;
    CdResult(CD_CALL* create)(void* context, const CdBackgroundTaskDesc* desc, uint64_t* token);
    CdResult(CD_CALL* submit)(void* context, uint64_t token, const CdBackgroundCommand* command);
    CdResult(CD_CALL* control)(void* context, uint64_t token, const CdBackgroundControl* control, uint64_t* generation);
    CdResult(CD_CALL* acquire)(void* context, uint64_t token, CdBackgroundSnapshot* output);
    CdResult(CD_CALL* stats)(void* context, uint64_t token, CdBackgroundStats* output);
} CdBackgroundApi;

#ifdef __cplusplus
}
#endif
#pragma pack(pop)
#endif
