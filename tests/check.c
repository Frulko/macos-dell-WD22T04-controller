#include "internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t now, last_off, last_on, event_elapsed;
    unsigned reads, stops, restores, events, fail_read;
    bool silent, fail_stop, fail_restore, hot, suspend, cancel, cancel_in_read, cancelled_read;
    bool hot_event, deadline_event, cancel_event, error_event;
    bool hot_once, critical, critical_event, spike_seen;
} simulation;
static int read_sample(void *ptr,dock_thermal *out,bool interruptible,dock_error *error) {
    simulation *s=ptr;s->now+=6000;s->reads++;
    if (interruptible && s->cancel_in_read && s->silent) {
        s->cancelled_read=true;snprintf(error->message,sizeof(error->message),"cancelled during read");return 1;
    }
    if(s->reads==s->fail_read) {snprintf(error->message,sizeof(error->message),"simulated read failure");return 1;}
    int module=64;
    if(s->silent && (s->hot || (s->hot_once && !s->spike_seen))) {module=70;s->spike_seen=true;}
    if(s->silent && s->critical) module=73;
    *out=(dock_thermal){.mode=s->silent?1:0,.speed_class=s->silent?0:1,
        .temperature_c={34,36,module},.observed_ms=s->now};
    return 0;
}
static int set_mode(void *ptr,bool silent,dock_error *error) {
    simulation *s=ptr;s->now+=6000;s->silent=silent;
    if(silent) {
        s->stops++;s->last_off=s->now;
        if(s->fail_stop) {snprintf(error->message,sizeof(error->message),"ambiguous stop");return 1;}
    } else {
        s->restores++;s->last_on=s->now;
        if(s->fail_restore) {snprintf(error->message,sizeof(error->message),"restore failure");return 1;}
    }
    return 0;
}
static uint64_t now(void *ptr) {return ((simulation *)ptr)->now;}
static void pause_io(void *ptr) {simulation *s=ptr;s->now+=100;if(s->suspend && s->silent){s->now+=30000;s->suspend=false;}}
static bool cancel(void *ptr) {simulation *s=ptr;return s->cancelled_read || (s->cancel && s->stops>0);}
static void event(void *ptr,const dock_watch_event *event) {
    simulation *s=ptr;s->events++;
    assert(event->elapsed_ms>=s->event_elapsed);s->event_elapsed=event->elapsed_ms;
    if(event->kind==DOCK_SAMPLE || event->kind==DOCK_END)
        assert(event->sample.mode==(event->state==DOCK_SILENT?1u:0u));
    if(event->kind==DOCK_TRANSITION) {
        assert(s->silent==(event->state==DOCK_SILENT)); // Emitted BEFORE the write.
        if(event->reason==DOCK_MODULE_LIMIT) {
            s->hot_event=true;
            assert(event->sample.temperature_c[2]==70 && event->target==DOCK_COOLING);
            assert(event->pending_elapsed_ms>=30000 && event->state_elapsed_ms<300000);
        }
        if(event->reason==DOCK_SILENCE_LIMIT) {
            s->deadline_event=true;assert(event->state_elapsed_ms>=300000);
        }
        if(event->reason==DOCK_CRITICAL_MODULE) {
            s->critical_event=true;assert(event->sample.temperature_c[2]==73 && event->state_elapsed_ms<30000);
        }
        if(event->reason==DOCK_READY) assert(event->state_elapsed_ms>=30000);
    }
    if(event->kind==DOCK_RESTORE && event->reason==DOCK_CANCELLED) s->cancel_event=true;
    if(event->kind==DOCK_RESTORE && event->reason==DOCK_IO_ERROR) {
        s->error_event=true;assert(event->sample.observed_ms && event->sample_age_ms>=6000);
    }
}
static int run_with_limit(simulation *s,unsigned seconds,unsigned limit) {
    dock_watch_options o;dock_watch_defaults(&o);o.session_seconds=seconds;o.silence_seconds=limit;
    dock_watch_io io={s,read_sample,set_mode,now,pause_io};dock_error e={{0}};
    return dock_watch_run(&io,&o,cancel,event,s,&e);
}
// Keep coverage for an explicitly requested five-minute cap as well as unlimited silence.
static int run(simulation *s,unsigned seconds) { return run_with_limit(s,seconds,300); }
int main(void) {
    dock_watch_options o;dock_watch_defaults(&o);assert(dock_watch_options_valid(&o));
    assert(o.ventilate_at_c[0]==45 && o.ventilate_at_c[1]==50 && o.ventilate_at_c[2]==70);
    dock_policy p={.state=DOCK_COOLING,.changed_ms=1000};dock_thermal t={.mode=0,.speed_class=1,.temperature_c={34,36,64}};
    assert(dock_policy_next(&p,&o,&t,1000)==DOCK_COOLING);
    assert(dock_policy_next(&p,&o,&t,30999)==DOCK_COOLING);
    assert(dock_policy_next(&p,&o,&t,31000)==DOCK_SILENT);
    t.temperature_c[2]=67;assert(dock_policy_next(&p,&o,&t,31000)==DOCK_COOLING);
    t.temperature_c[2]=64;t.speed_class=2;assert(dock_policy_next(&p,&o,&t,31000)==DOCK_COOLING);
    t.speed_class=1;t.mode=1;p=(dock_policy){.state=DOCK_SILENT,.changed_ms=1000};
    assert(o.silence_seconds==0);
    assert(dock_policy_next(&p,&o,&t,3601000)==DOCK_SILENT);
    o.silence_seconds=300;
    assert(dock_policy_next(&p,&o,&t,300999)==DOCK_SILENT);
    assert(dock_policy_next(&p,&o,&t,301000)==DOCK_COOLING);
    dock_watch_defaults(&o);
    for(unsigned i=0;i<3;i++) {
        p=(dock_policy){.state=DOCK_SILENT,.changed_ms=1000};
        int original=t.temperature_c[i];t.temperature_c[i]=o.ventilate_at_c[i];
        assert(dock_policy_next(&p,&o,&t,2000)==DOCK_SILENT);
        assert(dock_policy_next(&p,&o,&t,31999)==DOCK_SILENT);
        assert(dock_policy_next(&p,&o,&t,32000)==DOCK_COOLING);
        t.temperature_c[i]=-1;assert(dock_policy_next(&p,&o,&t,2000)==DOCK_COOLING);
        p=(dock_policy){.state=DOCK_SILENT,.changed_ms=1000};
        const int critical[]={55,60,73};t.temperature_c[i]=critical[i];
        assert(dock_policy_next(&p,&o,&t,2000)==DOCK_COOLING);
        t.temperature_c[i]=original;
    }
    p=(dock_policy){.state=DOCK_SILENT,.changed_ms=1000};
    // Sustained 68 C no longer qualifies for ventilation under the raised defaults.
    t.temperature_c[2]=68;assert(dock_policy_next(&p,&o,&t,2000)==DOCK_SILENT);
    assert(dock_policy_next(&p,&o,&t,32000)==DOCK_SILENT && p.pending_reason==DOCK_REASON_NONE);
    t.temperature_c[2]=70;assert(dock_policy_next(&p,&o,&t,2000)==DOCK_SILENT);
    t.temperature_c[2]=69;assert(dock_policy_next(&p,&o,&t,5000)==DOCK_SILENT);
    t.temperature_c[2]=70;assert(dock_policy_next(&p,&o,&t,6000)==DOCK_SILENT);
    assert(dock_policy_next(&p,&o,&t,35999)==DOCK_SILENT);
    assert(dock_policy_next(&p,&o,&t,36000)==DOCK_COOLING);t.temperature_c[2]=64;
    p=(dock_policy){.state=DOCK_SILENT,.changed_ms=1000};
    t.temperature_c[0]=45;assert(dock_policy_next(&p,&o,&t,2000)==DOCK_SILENT);
    t.temperature_c[0]=34;t.temperature_c[1]=50;
    assert(dock_policy_next(&p,&o,&t,10000)==DOCK_SILENT);
    assert(dock_policy_next(&p,&o,&t,32000)==DOCK_COOLING);t.temperature_c[1]=36;
    t.mode=0;assert(dock_policy_next(&p,&o,&t,2000)==DOCK_COOLING);
    o.silence_seconds=301;assert(!dock_watch_options_valid(&o));dock_watch_defaults(&o);
    o.ventilate_at_c[2]=73;assert(!dock_watch_options_valid(&o));dock_watch_defaults(&o);
    o.resume_below_c[2]=70;assert(!dock_watch_options_valid(&o));dock_watch_defaults(&o);
    o.stable_seconds=0;assert(!dock_watch_options_valid(&o));
    simulation unlimited={.now=60000};assert(!run_with_limit(&unlimited,900,0));
    assert(unlimited.stops==1 && !unlimited.silent && unlimited.restores && !unlimited.deadline_event);
    unlimited=(simulation){.now=60000,.hot=true};assert(!run_with_limit(&unlimited,120,0));
    assert(unlimited.hot_event && unlimited.restores && !unlimited.silent);
    simulation s={.now=60000};assert(!run(&s,900));assert(s.stops>=2 && !s.silent && s.restores>=s.stops && s.deadline_event);
    s=(simulation){.now=60000,.hot=true};assert(!run(&s,120));assert(s.stops && s.restores && !s.silent && s.hot_event);
    s=(simulation){.now=60000,.hot_once=true};assert(!run(&s,120));assert(s.stops==1 && s.spike_seen && !s.hot_event);
    s=(simulation){.now=60000,.critical=true};assert(!run(&s,120));assert(s.critical_event && s.restores && !s.silent);
    s=(simulation){.now=60000,.fail_stop=true};assert(run(&s,120)==1);assert(s.stops==1 && s.restores==1 && !s.silent);
    s=(simulation){.now=60000,.fail_read=9};assert(run(&s,120)==1);assert(s.stops && s.restores && !s.silent && s.error_event);
    s=(simulation){.now=60000,.cancel=true};assert(!run(&s,120));assert(s.stops==1 && s.restores==1 && !s.silent && s.cancel_event);
    s=(simulation){.now=60000,.cancel_in_read=true};assert(!run(&s,120));assert(s.cancelled_read && s.stops==1 && s.restores==1 && !s.silent);
    s=(simulation){.now=60000,.cancel=true,.fail_restore=true};assert(run(&s,120)==2);assert(s.restores==1);
    s=(simulation){.now=60000,.suspend=true};assert(run(&s,120)==1);assert(s.restores && !s.silent);
    s=(simulation){.now=60000};assert(!run(&s,10));assert(!s.stops && !s.restores);
    uint8_t data[103]={0,4,130,0,8,0,7,0,0x68}, info[183]={1};dock_info identity;
    memcpy(data+39,"WD22TB4",7);memcpy(data+32,"ABC1234",7);
    data[16]=0xb9;data[17]=0x46;data[23]=0xff;
    const uint8_t ec[9]={0,0,0,1,0,1,1,0,3};memcpy(info+3,ec,9);
    assert(!dock_decode_info(data,info,&identity));assert(identity.supply_watts==130 && identity.thermal_firmware_known);
    assert(identity.module_serial==UINT64_C(0xff000000000046b9));
    assert(!strcmp(identity.service_tag,"ABC1234"));assert(identity.port_status[0]==0x68);
    info[0]=21;assert(dock_decode_info(data,info,&identity));info[0]=1;info[11]=0x17;
    assert(!dock_decode_info(data,info,&identity) && !identity.thermal_firmware_known);
    data[4]=9;assert(dock_decode_info(data,info,&identity));
    const uint8_t pd_identity[25]={0x45,0x3c,0x41,0x60,0x4c,0,0,0,0,0x12,7,0x70,0xb0,0x1b,0,0xc0,0x4f};
    assert(dock_hpm_identity(pd_identity,25));assert(!dock_hpm_identity(pd_identity,24));assert(!dock_hpm_identity(NULL,25));
    uint8_t packet[28]={6,0xa1,0x3c,0x41,1,0x81,0x0c,0x22,0x24,0x40};
    assert(dock_hpm_thermal_packet(packet,28,0));
    assert(!dock_hpm_thermal_packet(packet,28,1));assert(!dock_hpm_thermal_packet(packet,4,0));
    packet[2]=0xff;assert(!dock_hpm_thermal_packet(packet,28,0));packet[2]=0x3c;
    packet[6]=0x0b;assert(dock_hpm_thermal_packet(packet,28,0));
    packet[6]=0x0d;assert(!dock_hpm_thermal_packet(packet,28,0));
    packet[5]=0x80;packet[6]=1;assert(dock_hpm_thermal_packet(packet,28,0));
    const char xml[]="<plist version=\"1.0\"><dict><key>ExternalConnected</key><true/>"
        "<key>AdapterDetails</key><dict><key>Watts</key><integer>88</integer>"
        "<key>AdapterVoltage</key><integer>19500</integer><key>Current</key><integer>4500</integer>"
        "<key>UsbHvcHvcIndex</key><integer>1</integer><key>UsbHvcMenu</key><array>"
        "<dict><key>Index</key><integer>1</integer><key>MaxVoltage</key><integer>19500</integer>"
        "<key>MaxCurrent</key><integer>4500</integer></dict><string>invalid</string></array></dict>"
        "<key>PowerTelemetryData</key><dict><key>SystemVoltageIn</key><integer>19300</integer>"
        "<key>SystemCurrentIn</key><integer>1501</integer><key>SystemPowerIn</key><integer>28976</integer>"
        "</dict></dict></plist>";
    CFDataRef bytes=CFDataCreate(NULL,(const UInt8 *)xml,strlen(xml));
    CFMutableDictionaryRef properties=(CFMutableDictionaryRef)CFPropertyListCreateWithData(NULL,bytes,kCFPropertyListMutableContainers,NULL,NULL);
    assert(properties);CFRelease(bytes);dock_host_power power;
    assert(!dock_decode_host_power(properties,&power));
    assert(power.external_known && power.external_connected && power.adapter_mv.known && power.adapter_mv.value==19500);
    assert(power.input_mw.known && power.input_mw.value==28976 && power.profile_count==2);
    assert(power.profiles[0].milliamps.value==4500 && !power.profiles[1].millivolts.known);
    CFMutableDictionaryRef telemetry=(CFMutableDictionaryRef)CFDictionaryGetValue(properties,CFSTR("PowerTelemetryData"));
    CFDictionarySetValue(telemetry,CFSTR("SystemPowerIn"),kCFBooleanTrue);
    int64_t negative=-1;CFNumberRef neg=CFNumberCreate(NULL,kCFNumberSInt64Type,&negative);
    CFDictionarySetValue(telemetry,CFSTR("SystemCurrentIn"),neg);CFRelease(neg);
    CFDictionaryRemoveValue(telemetry,CFSTR("SystemVoltageIn"));
    assert(!dock_decode_host_power(properties,&power));
    assert(!power.input_mw.known && !power.input_ma.known && !power.input_mv.known);
    assert(dock_decode_host_power(NULL,&power));CFRelease(properties);
    dock_register regs[]={
        {.address=0x30,.bytes={2,0x2c,0x91,0x01,0x3f,0xc2,0x19,0x06,0},.length=28},
        {.address=0x35,.bytes={0xd6,0x09,0x87,0x27,0xc2,0x19,0x06,0,0xfc,0x2e},.length=10},
        {.address=0x3f,.bytes={0x3f,0x0f},.length=10}
    };
    dock_pd_contract pd;assert(!dock_decode_pd(regs,3,&pd));
    assert(pd.offer_count==2 && pd.offers[0].millivolts==5000 && pd.offers[0].milliamps==3000);
    assert(pd.contract_inferred && pd.selected_object==2 && pd.millivolts==19500 && pd.operating_ma==4500);
    assert(pd.requested_max_ma==4700 && pd.capability_mismatch && pd.apple_trailer_raw==0x2efc);
    regs[1].bytes[3]=0x23; // 4.7 A > 4.5 A without Capability Mismatch is inconsistent.
    assert(!dock_decode_pd(regs,3,&pd) && !pd.contract_inferred);regs[1].bytes[3]=0x27;
    regs[1].bytes[4]=0;assert(!dock_decode_pd(regs,3,&pd) && !pd.contract_inferred);regs[1].bytes[4]=0xc2;
    regs[2].bytes[0]=0;assert(!dock_decode_pd(regs,3,&pd) && !pd.contract_inferred);regs[2].bytes[0]=0x3f;
    regs[1].length=4;assert(!dock_decode_pd(regs,3,&pd) && !pd.contract_inferred);regs[1].length=10;
    regs[0].bytes[0]=7;assert(dock_decode_pd(regs,3,&pd));regs[0].bytes[0]=2;
    regs[0].status=1;assert(dock_decode_pd(regs,3,&pd));
    puts("OK: watcher/cancellation/recovery, HID/HPM, host telemetry and captured PDO/RDO decoding.");
}
