// SPDX-License-Identifier: MIT
#include "internal.h"
#include <IOKit/IOKitLib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static CFTypeRef property(CFDictionaryRef d, CFStringRef key, CFTypeID type) {
    if (!d || CFGetTypeID(d) != CFDictionaryGetTypeID()) return NULL;
    CFTypeRef v = CFDictionaryGetValue(d, key);
    return v && CFGetTypeID(v) == type ? v : NULL;
}
// Unknown, negative, floating-point or out-of-range properties stay explicitly unknown.
static dock_power_value number(CFDictionaryRef d, CFStringRef key) {
    CFNumberRef n = property(d, key, CFNumberGetTypeID()); int64_t value;
    if (!n || CFNumberIsFloatType(n) || !CFNumberGetValue(n, kCFNumberSInt64Type, &value) ||
        value < 0 || value > INT_MAX) return (dock_power_value){0};
    return (dock_power_value){true, (int)value};
}
int dock_decode_host_power(CFDictionaryRef properties, dock_host_power *out) {
    if (!out) return 1;
    memset(out, 0, sizeof(*out));
    if (!properties || CFGetTypeID(properties) != CFDictionaryGetTypeID()) return 1;
    CFBooleanRef external = property(properties, CFSTR("ExternalConnected"), CFBooleanGetTypeID());
    out->external_known = external != NULL;
    out->external_connected = external && CFBooleanGetValue(external);
    CFDictionaryRef adapter = property(properties, CFSTR("AdapterDetails"), CFDictionaryGetTypeID());
    out->adapter_watts = number(adapter, CFSTR("Watts"));
    out->adapter_mv = number(adapter, CFSTR("AdapterVoltage"));
    out->adapter_ma = number(adapter, CFSTR("Current"));
    out->selected_profile = number(adapter, CFSTR("UsbHvcHvcIndex"));
    CFArrayRef menu = property(adapter, CFSTR("UsbHvcMenu"), CFArrayGetTypeID());
    if (menu) {
        CFIndex count = CFArrayGetCount(menu);
        out->profiles_truncated = count > 16;
        out->profile_count = count > 16 ? 16 : (size_t)count;
        for (size_t i = 0; i < out->profile_count; i++) {
            CFDictionaryRef p = CFArrayGetValueAtIndex(menu, i);
            out->profiles[i] = (dock_power_profile){number(p, CFSTR("Index")),
                number(p, CFSTR("MaxVoltage")), number(p, CFSTR("MaxCurrent"))};
        }
    }
    CFDictionaryRef telemetry = property(properties, CFSTR("PowerTelemetryData"), CFDictionaryGetTypeID());
    out->input_mv = number(telemetry, CFSTR("SystemVoltageIn"));
    out->input_ma = number(telemetry, CFSTR("SystemCurrentIn"));
    out->input_mw = number(telemetry, CFSTR("SystemPowerIn"));
    return 0;
}
int dock_read_host_power(dock_host_power *out, dock_error *error) {
    if (!out) return 1;
    memset(out, 0, sizeof(*out));
    io_service_t battery = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSmartBattery"));
    CFMutableDictionaryRef properties = NULL;
    kern_return_t status = battery ? IORegistryEntryCreateCFProperties(battery, &properties, NULL, 0) : kIOReturnNotFound;
    if (battery) IOObjectRelease(battery);
    int result = status ? 1 : dock_decode_host_power(properties, out);
    if (properties) CFRelease(properties);
    if (result && error) snprintf(error->message, sizeof(error->message), "Télémétrie AppleSmartBattery indisponible (0x%08x).", status);
    if (!result) out->observed_ms = dock_monotonic_ms();
    return result;
}

static const dock_register *find_register(const dock_register *r,size_t n,uint8_t address) {
    for(size_t i=0;i<n;i++) if(r[i].address==address && !r[i].status && r[i].length<=64) return r+i;
    return NULL;
}
static uint32_t little32(const uint8_t *b) {
    return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
}
// Decode standard fixed PDO fields, but infer the Apple wrapper only when the
// selected PDO exactly matches the advertised source offer and the port is connected.
int dock_decode_pd(const dock_register *registers,size_t count,dock_pd_contract *out) {
    if(!out) return 1;memset(out,0,sizeof(*out));if(!registers) return 1;
    const dock_register *source=find_register(registers,count,0x30);
    if(!source || !source->length || !source->bytes[0] || source->bytes[0]>7 ||
       source->length<1u+4u*source->bytes[0]) return 1;
    out->offer_count=source->bytes[0];
    for(size_t i=0;i<out->offer_count;i++) {
        dock_pd_offer *p=out->offers+i;p->raw=little32(source->bytes+1+4*i);
        if(!(p->raw>>30)) {
            p->millivolts=((p->raw>>10)&1023)*50;p->milliamps=(p->raw&1023)*10;
            p->fixed=p->millivolts>=5000 && p->millivolts<=20000 && p->milliamps>0 && p->milliamps<=5000;
        }
    }
    const dock_register *status=find_register(registers,count,0x3f);
    const dock_register *active=find_register(registers,count,0x35);
    if(!status || status->length!=10 || !(status->bytes[0]&1) || !active || active->length!=10) return 0;
    uint32_t rdo=little32(active->bytes),pdo=little32(active->bytes+4);
    unsigned position=(rdo>>28)&7,op=((rdo>>10)&1023)*10,max=(rdo&1023)*10;
    bool mismatch=(rdo&(1u<<26))!=0;
    // GiveBack has a minimum-current field instead of max; do not mislabel it.
    if((rdo&((1u<<31)|(1u<<27))) || !position || position>out->offer_count) return 0;
    const dock_pd_offer *offer=out->offers+position-1;
    if(!offer->fixed || offer->raw!=pdo || !op || op>offer->milliamps || max<op ||
       (!mismatch && max>offer->milliamps)) return 0;
    out->contract_inferred=true;out->capability_mismatch=mismatch;
    out->selected_object=position;out->millivolts=offer->millivolts;
    out->operating_ma=op;out->requested_max_ma=max;out->rdo_raw=rdo;out->selected_pdo_raw=pdo;
    out->apple_trailer_raw=active->bytes[8]|(unsigned)active->bytes[9]<<8;
    return 0;
}
