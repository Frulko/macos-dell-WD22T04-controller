// SPDX-License-Identifier: MIT
// Dell HID framing follows fwupd (Dell/Realtek, MIT option).
#include "internal.h"
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int fail(dock_error *e, const char *format, ...) {
    if (e) { va_list ap; va_start(ap, format); vsnprintf(e->message, sizeof(e->message), format, ap); va_end(ap); }
    return 1;
}
static unsigned u16(const uint8_t *b) { return b[0] | (unsigned)b[1] << 8; }
static uint64_t u64(const uint8_t *b) {
    uint64_t v = 0; for (unsigned i = 0; i < 8; i++) v |= (uint64_t)b[i] << (8*i); return v;
}
static void ascii(char *out, const uint8_t *b, size_t n) {
    size_t i = 0; for (; i < n && b[i]; i++) out[i] = b[i] >= 32 && b[i] <= 126 ? b[i] : '?'; out[i] = 0;
}
// Fixed-length EC structures: preserve raw bytes and reject unsupported base/module types.
int dock_decode_info(const uint8_t data[103], const uint8_t info[183], dock_info *out) {
    if (!data || !info || !out || data[1] != 4 || u16(data+4) != 8 || !info[0] || info[0] > 20) return 1;
    memset(out, 0, sizeof(*out));
    memcpy(out->raw_identity, data, 103);
    ascii(out->model, data+39, 64); ascii(out->service_tag, data+32, 7);
    out->configuration = data[0]; out->base_type = data[1];
    out->supply_watts = u16(data+2); out->module_type = u16(data+4); out->board_id = u16(data+6);
    out->port_status[0] = u16(data+8); out->port_status[1] = u16(data+10);
    memcpy(out->package, data+12, 4);
    out->module_serial = u64(data+16); out->original_module_serial = u64(data+24);
    out->component_count = info[0];
    for (size_t i = 0; i < out->component_count; i++) {
        const uint8_t *b = info + 3 + i*9;
        dock_component *c = out->components + i;
        c->location=b[0]; c->type=b[1]; c->subtype=b[2]; c->argument=b[3]; c->instance=b[4];
        memcpy(c->version,b+5,4);
        if (!c->location && !c->type && !c->instance && b[5]==1 && b[6]==1 && b[7]==0 && (b[8]==3 || b[8]==0x16))
            out->thermal_firmware_known = true;
    }
    return 0;
}
const char *dock_component_name(const dock_component *c) {
    switch (c->type) {
        case 0: return "EC"; case 1: return "Power Delivery";
        case 3: return c->subtype == 0 ? "USB Gen2 Hub" : "USB Gen1 Hub";
        case 4: return "DisplayPort MST"; case 5: return "Thunderbolt";
        default: return "Unknown";
    }
}
// The allowlist permits identity reads only; no arbitrary EC command escapes through HID.
static int read_ec(IOHIDDeviceRef dev, uint8_t cmd, size_t length, uint8_t *out, dock_error *e) {
    if (!((cmd==5 && length==1) || (cmd==3 && length==103) || (cmd==2 && length==183))) return fail(e,"Commande HID refusée.");
    uint8_t b[192] = {0x40,0xd6,cmd,0,0,0,(uint8_t)(length+1),0,0xec,1,0x80};
    IOReturn r = IOHIDDeviceSetReport(dev,kIOHIDReportTypeOutput,0,b,sizeof(b));
    if (r) return fail(e,"HID SetReport : 0x%08x",(unsigned)r);
    memset(b,0,sizeof(b)); CFIndex n=sizeof(b);
    r=IOHIDDeviceGetReport(dev,kIOHIDReportTypeInput,0,b,&n);
    if (r || n!=192 || b[0]!=length) return fail(e,"Réponse HID invalide (0x%08x, %ld octets).",(unsigned)r,n);
    memcpy(out,b+1,length); return 0;
}
static bool number(CFDictionaryRef d, CFStringRef key, int *out) {
    CFTypeRef v=CFDictionaryGetValue(d,key);
    return v && CFGetTypeID(v)==CFNumberGetTypeID() && CFNumberGetValue(v,kCFNumberIntType,out);
}
int dock_read_info(dock_info *out, dock_error *e) {
    if (!out) return fail(e,"Sortie manquante.");
    memset(out,0,sizeof(*out)); if (e) e->message[0]=0;
    int vendor=0x413c, product=0xb06e;
    CFNumberRef vid=CFNumberCreate(NULL,kCFNumberIntType,&vendor),pid=CFNumberCreate(NULL,kCFNumberIntType,&product);
    const void *keys[]={CFSTR(kIOHIDVendorIDKey),CFSTR(kIOHIDProductIDKey)}, *values[]={vid,pid};
    CFDictionaryRef match=CFDictionaryCreate(NULL,keys,values,2,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    IOHIDManagerRef manager=IOHIDManagerCreate(NULL,0); IOHIDManagerSetDeviceMatching(manager,match);
    CFRelease(match); CFRelease(vid); CFRelease(pid);
    CFSetRef devices=IOHIDManagerCopyDevices(manager);
    CFIndex count=devices ? CFSetGetCount(devices) : 0;
    int result=1;
    if (count!=1) fail(e,"Un dock HID 413c:b06e attendu, %ld détecté(s).",count);
    else {
        const void *value; CFSetGetValues(devices,&value); IOHIDDeviceRef dev=(IOHIDDeviceRef)value;
        IOReturn r=IOHIDDeviceOpen(dev,0);
        if (r) fail(e,"Ouverture HID : 0x%08x",(unsigned)r);
        else {
            uint8_t type=0,data[103],info[183];
            if (!read_ec(dev,5,1,&type,e) && type==4 && !read_ec(dev,3,103,data,e) && !read_ec(dev,2,183,info,e)) {
                result=dock_decode_info(data,info,out);
                if (result) fail(e,"Structure WD22TB4 non reconnue.");
            } else if (type!=4) fail(e,"Base WD19/WD22 type 4 requise.");
            IOHIDDeviceClose(dev,0);
        }
    }
    if (devices) CFRelease(devices); CFRelease(manager);
    if (!result) {
        CFDictionaryRef adapter=IOPSCopyExternalPowerAdapterDetails();
        if (adapter) {
            out->host_adapter_present=true;
            out->host_watts_known=number(adapter,CFSTR(kIOPSPowerAdapterWattsKey),&out->host_adapter_watts);
            out->host_ma_known=number(adapter,CFSTR(kIOPSPowerAdapterCurrentKey),&out->host_adapter_ma);
            // Voltage is not a documented IOPS adapter key: leave it unknown.
            CFRelease(adapter);
        }
    }
    return result;
}
