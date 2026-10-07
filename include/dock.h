// SPDX-License-Identifier: MIT
#ifndef DOCK_H
#define DOCK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOCK_API_VERSION 2
typedef struct dock dock_t;
typedef struct { char message[256]; } dock_error;
typedef struct {
    uint8_t location, type, subtype, argument, instance, version[4];
} dock_component;
typedef struct {
    char model[65], service_tag[8];
    uint8_t configuration, base_type, package[4], raw_identity[103];
    uint16_t supply_watts, module_type, board_id, port_status[2];
    uint64_t module_serial, original_module_serial;
    size_t component_count;
    dock_component components[20];
    bool thermal_firmware_known;
    // Host charger details, NOT measured dock consumption or proof of power source.
    bool host_adapter_present, host_watts_known, host_mv_known, host_ma_known;
    int host_adapter_watts, host_adapter_mv, host_adapter_ma;
} dock_info;
typedef struct {
    unsigned mode, speed_class; // 0: stopped OR unavailable; 1: 0<RPM<2750; 2: RPM>=2750
    int temperature_c[3];       // local, remote, module
    uint64_t observed_ms;       // monotonic, completed snapshot; samples are sequential
} dock_thermal;
typedef struct {
    uint8_t address, bytes[64];
    size_t length;
    uint32_t status;
} dock_register;

typedef struct { bool known; int value; } dock_power_value;
typedef struct { dock_power_value index, millivolts, milliamps; } dock_power_profile;
typedef struct {
    bool external_known, external_connected, profiles_truncated;
    dock_power_value adapter_watts, adapter_mv, adapter_ma, selected_profile;
    dock_power_value input_mv, input_ma, input_mw;
    size_t profile_count;
    dock_power_profile profiles[16];
    uint64_t observed_ms;
} dock_host_power;
// Read-only AppleSmartBattery snapshot, no root required. Private macOS keys:
// units inferred from observed values; may be cached/unavailable. Not dock-wide power.
int dock_read_host_power(dock_host_power *out, dock_error *error);
typedef struct { uint32_t raw; bool fixed; unsigned millivolts, milliamps; } dock_pd_offer;
typedef struct {
    size_t offer_count;
    dock_pd_offer offers[7];
    bool contract_inferred, capability_mismatch;
    unsigned selected_object, millivolts, operating_ma, requested_max_ma;
    uint32_t rdo_raw, selected_pdo_raw;
    uint16_t apple_trailer_raw;
} dock_pd_contract;
// Pure decoding of captured registers. Apple 0x35 layout inferred on this Mac;
// requires connected status and matching fixed PDO in received source capabilities.
int dock_decode_pd(const dock_register *registers, size_t count, dock_pd_contract *out);

// HID info does not require root on the tested Mac. No command writes settings.
int dock_read_info(dock_info *out, dock_error *error);
// HPM needs root, discovers the RID by Dell identity and holds an exclusive process lock.
// Contexts are synchronous and must not be used concurrently by multiple threads.
int dock_open(dock_t **out, dock_error *error);
void dock_close(dock_t *dock); // Attempts automatic restoration if this context changed mode.
int dock_enter(dock_t *dock, dock_error *error); // Explicit Discover + Enter; NEVER Exit.
int dock_read_thermal(dock_t *dock, dock_thermal *out, dock_error *error);
int dock_automatic(dock_t *dock, dock_error *error);
// Read-only diagnostic registers; no arbitrary register/VDM writes in the public API.
size_t dock_read_registers(dock_t *dock, dock_register *out, size_t capacity);
int dock_rid(const dock_t *dock);
uint64_t dock_monotonic_ms(void);

typedef enum { DOCK_COOLING, DOCK_SILENT, DOCK_FINISHED } dock_watch_state;
typedef struct {
    // silence_seconds == 0 disables the per-period deadline; session expiry still restores.
    unsigned session_seconds, silence_seconds, cooling_seconds, stable_seconds;
    int resume_below_c[3], ventilate_at_c[3];
} dock_watch_options;
typedef enum {
    DOCK_REASON_NONE, DOCK_READY, DOCK_SILENCE_LIMIT,
    DOCK_LOCAL_LIMIT, DOCK_REMOTE_LIMIT, DOCK_MODULE_LIMIT,
    DOCK_CANCELLED, DOCK_SESSION_LIMIT, DOCK_IO_ERROR, DOCK_INVALID_SAMPLE,
    DOCK_CRITICAL_LOCAL, DOCK_CRITICAL_REMOTE, DOCK_CRITICAL_MODULE
} dock_watch_reason;
typedef struct {
    dock_watch_state state;
    uint64_t changed_ms;
    dock_watch_reason pending_reason;
    uint64_t pending_since_ms;
} dock_policy;
void dock_watch_defaults(dock_watch_options *options);
bool dock_watch_options_valid(const dock_watch_options *options);
// No I/O; updates the stability timer in policy, but caller commits state/changed_ms.
// Invalid/unknown telemetry -> cooling.
dock_watch_state dock_policy_next(dock_policy *policy, const dock_watch_options *options,
                                 const dock_thermal *sample, uint64_t now_ms);
typedef bool (*dock_cancel_fn)(void *user);
typedef enum { DOCK_SAMPLE, DOCK_TRANSITION, DOCK_RESTORE, DOCK_END } dock_watch_event_kind;
typedef struct {
    dock_watch_event_kind kind;
    dock_watch_state state, target;
    dock_watch_reason reason;
    uint64_t elapsed_ms, state_elapsed_ms, sample_age_ms;
    dock_watch_reason pending_reason;
    uint64_t pending_elapsed_ms;
    dock_thermal sample;
} dock_watch_event;
// TRANSITION/RESTORE are emitted BEFORE the write and retain the triggering sample.
// state is the last confirmed policy state; target is requested, not yet confirmed.
typedef void (*dock_watch_fn)(void *user, const dock_watch_event *event);
// Callback is synchronous; it must return promptly. On error/cancel/expiry, tries automatic.
// No independent hardware watchdog: SIGKILL, I/O hangs and disconnect can prevent recovery.
int dock_watch(dock_t *dock, const dock_watch_options *options, dock_cancel_fn cancel,
               dock_watch_fn update, void *user, dock_error *error);
const char *dock_component_name(const dock_component *component);

#ifdef __cplusplus
}
#endif
#endif
