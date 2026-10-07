// SPDX-License-Identifier: MIT
#include "dock.h"
#include <CoreFoundation/CoreFoundation.h>
int dock_decode_host_power(CFDictionaryRef properties, dock_host_power *out);
int dock_decode_info(const uint8_t data[103], const uint8_t info[183], dock_info *out);
bool dock_hpm_identity(const uint8_t *bytes, uint64_t length);
bool dock_hpm_thermal_packet(const uint8_t *bytes, uint64_t length, uint32_t sop);
// Injectable clock/I/O lets tests exercise recovery without sending hardware commands.
typedef struct {
    void *user;
    int (*read)(void *, dock_thermal *, bool interruptible, dock_error *);
    int (*set_silent)(void *, bool, dock_error *);
    uint64_t (*now)(void *);
    void (*pause)(void *);
} dock_watch_io;
int dock_watch_run(dock_watch_io *io, const dock_watch_options *options,
                   dock_cancel_fn cancel, dock_watch_fn update, void *user, dock_error *error);
