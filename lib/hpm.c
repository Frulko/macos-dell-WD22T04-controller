// SPDX-License-Identifier: Apache-2.0
// AppleHPMLib ABI derived from AsahiLinux/macvdmtool; protocol verified on WD22TB4 EC03.
#include "internal.h"
#include "../research/macvdmtool/AppleHPMLib.h"
#include <CoreFoundation/CoreFoundation.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

// Verified against this Mac's AppleHPMLib v3 symbols and vtable.
typedef struct {
    IUNKNOWN_C_GUTS;
    uint16_t version, revision;
    IOReturn (*Read)(void *, uint64_t, uint8_t, void *, uint64_t, uint32_t, uint64_t *);
    IOReturn (*Write)(void *, uint64_t, uint8_t, const void *, uint64_t, uint32_t);
    IOReturn (*Command)(void *, uint64_t, uint32_t, uint32_t);
    IOReturn (*SendVDM)(void *, uint64_t, uint32_t, const void *, uint64_t, uint32_t);
    IOReturn (*ReceiveVDM)(void *, uint64_t, void *, uint64_t, uint32_t,
                         uint32_t *, uint8_t *, uint64_t *);
    IOReturn (*ReceiveVDMAttention)(void *, uint64_t, void *, uint64_t, uint32_t,
                                  uint32_t *, uint8_t *, uint64_t *);
} HPMCapture;
_Static_assert(offsetof(HPMCapture, Read) == 0x28, "AppleHPM read ABI");
_Static_assert(offsetof(HPMCapture, SendVDM) == 0x40, "AppleHPM send ABI");
_Static_assert(offsetof(HPMCapture, ReceiveVDM) == 0x48, "AppleHPM receive ABI");
_Static_assert(offsetof(HPMCapture, ReceiveVDMAttention) == 0x50, "AppleHPM Attention ABI");

// Owns the plug-in, HPM interface and process lock until dock_close().
struct dock {
    HPMCapture **hpm; IOCFPlugInInterface **plugin; int lock, rid; bool changed;
    dock_cancel_fn cancel; void *cancel_user; bool interruptible;
};
typedef struct { uint8_t data[64], count; uint32_t sop; uint64_t size; } packet;
static int fail(dock_error *e,const char *format,...) {
    if (e) { va_list ap;va_start(ap,format);vsnprintf(e->message,sizeof(e->message),format,ap);va_end(ap); } return 1;
}
uint64_t dock_monotonic_ms(void) { return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW)/1000000; }
static uint32_t u32(const uint8_t *b) { return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24; }
bool dock_hpm_identity(const uint8_t *b,uint64_t n) {
    return b && n==25 && (b[0]&0xc0)==0x40 && (b[0]&7)>=3 && (u32(b+1)&65535)==0x413c && u32(b+9)>>16==0xb070;
}
static IOReturn reg(dock_t *d,uint8_t address,uint8_t *b,uint64_t *n) {
    *n=0;memset(b,0,64);return (*d->hpm)->Read(d->hpm,0,address,b,64,0,n);
}
// Re-check connection and Dell identity before every send; RID alone is not identity.
static int partner(dock_t *d,dock_error *e) {
    uint8_t b[64];uint64_t n;
    if (reg(d,0x3f,b,&n) || n!=10 || !(b[0]&1)) return fail(e,"Liaison Power Delivery absente.");
    if (reg(d,0x48,b,&n) || !dock_hpm_identity(b,n)) return fail(e,"Identité Dell 413c:b070 non confirmée.");
    return 0;
}
static int receive(dock_t *d,bool attention,packet *p,dock_error *e) {
    memset(p,0,sizeof(*p));
    IOReturn r=attention ? (*d->hpm)->ReceiveVDMAttention(d->hpm,0,p->data,64,0,&p->sop,&p->count,&p->size)
                        : (*d->hpm)->ReceiveVDM(d->hpm,0,p->data,64,0,&p->sop,&p->count,&p->size);
    if (r || p->size>28 || p->size%4 || p->sop>2 || p->count>7) return fail(e,"Réception HPM invalide : 0x%08x",(unsigned)r);
    return 0;
}
// Receive buffers persist between calls; counter/SOP changes distinguish new traffic.
static bool same(const packet *a,const packet *b) {
    return a->size==b->size && a->count==b->count && a->sop==b->sop && !memcmp(a->data,b->data,a->size);
}
static bool reply(const packet *p,uint32_t query) {
    return p->sop==0 && p->size>=4 && (u32(p->data)&0xffff9f3f)==(query&0xffff9f3f) && (p->data[0]&0xc0);
}
static int send(dock_t *d,const uint8_t *b,size_t n,dock_error *e) {
    if (partner(d,e)) return 1;
    IOReturn r=(*d->hpm)->SendVDM(d->hpm,0,0,b,n,0);
    return r ? fail(e,"Envoi HPM : 0x%08x",(unsigned)r) : 0;
}
static int standard(dock_t *d,const uint8_t b[4],packet *out,dock_error *e) {
    packet before,p;
    if (receive(d,false,&before,e) || send(d,b,4,e)) return 1;
    uint64_t start=dock_monotonic_ms();
    do {
        if (receive(d,false,&p,e)) return 1;
        if (!same(&p,&before) && reply(&p,u32(b))) {
            if ((p.data[0]&0xc0)!=0x40) return fail(e,"Commande Dell refusée (NAK/BUSY).");
            *out=p;return 0;
        }
        usleep(10000);
    } while (dock_monotonic_ms()-start<1000);
    return fail(e,"Pas de réponse à la négociation Dell.");
}
int dock_enter(dock_t *d,dock_error *e) {
    if (!d) return fail(e,"Contexte manquant.");
    const uint8_t discover[]={3,0xa0,0x3c,0x41},enter[]={4,0xa1,0x3c,0x41};packet p;
    if (standard(d,discover,&p,e)) return 1;
    if (p.size<8 || u32(p.data+4)!=1) return fail(e,"VDO Dell 1 absent.");
    if (standard(d,enter,&p,e)) return 1;
    uint64_t start=dock_monotonic_ms();
    do { if (partner(d,e)) return 1;usleep(250000); } while (dock_monotonic_ms()-start<3000);
    return 0;
}
bool dock_hpm_thermal_packet(const uint8_t *b,uint64_t n,uint32_t sop) {
    return b && sop==0 && n==28 && u32(b)==0x413ca106 && b[4]==1 &&
        (b[5]==0x80 || (b[5]==0x81 && (b[6]==0x0b || b[6]==0x0c)));
}
// Dell returns thermal data asynchronously in Attention, not the immediate VDM ACK.
// Observe both buffers for three seconds and acknowledge each recognized new reply.
static int exchange(dock_t *d,uint8_t opcode,bool silent,packet *out,dock_error *e) {
    if (!d || (opcode!=0x0f && opcode!=0x10 && opcode!=0x11) || (opcode!=0x10 && silent)) return fail(e,"Commande refusée.");
    if (d->interruptible && d->cancel && d->cancel(d->cancel_user)) return fail(e,"Lecture annulée.");
    packet previous[2];memset(out,0,sizeof(*out));
    for (unsigned a=0;a<2;a++) if (receive(d,a,&previous[a],e)) return 1;
    uint8_t query[28]={0x12,0xa1,0x3c,0x41,2,opcode};
    if (opcode==0x10) { query[6]=silent;query[7]=silent?0:1; }
    if (send(d,query,sizeof(query),e)) return 1;
    uint64_t start=dock_monotonic_ms(), next_check=start;unsigned acknowledgements=0;
    bool found=false;
    do {
        if (d->interruptible && d->cancel && d->cancel(d->cancel_user)) return fail(e,"Lecture annulée.");
        for (unsigned a=0;a<2;a++) {
            packet p;if (receive(d,a,&p,e)) return 1;
            if (!same(&p,&previous[a])) {
                if (a && dock_hpm_thermal_packet(p.data,p.size,p.sop)) {
                    bool matches=opcode==0x10 ? p.data[5]==0x80 : p.data[5]==0x81 && p.data[6]==(opcode==0x0f?0x0b:0x0c);
                    if (matches) { *out=p;found=true; }
                    if (acknowledgements++>=16) return fail(e,"Trop de réémissions Dell.");
                    const uint8_t ack[]={0x13,0xa1,0x3c,0x41};
                    if (send(d,ack,4,e)) return 1;
                }
                previous[a]=p;
            }
        }
        if (dock_monotonic_ms()>=next_check) { if (partner(d,e)) return 1;next_check=dock_monotonic_ms()+250; }
        usleep(10000);
    } while (dock_monotonic_ms()-start<3000);
    return found ? 0 : fail(e,"Réponse EC manquante ; essayer explicitement dockctl connect si le mode Dell est inactif.");
}
int dock_read_thermal(dock_t *d,dock_thermal *out,dock_error *e) {
    if (!out) return fail(e,"Sortie manquante.");
    memset(out,0,sizeof(*out));packet p;
    if (exchange(d,0x0f,false,&p,e)) return 1;
    out->mode=p.data[7];out->speed_class=p.data[8];
    if (out->mode>1 || out->speed_class>2) return fail(e,"Profil thermique inconnu.");
    if (exchange(d,0x11,false,&p,e)) return 1;
    for (unsigned i=0;i<3;i++) out->temperature_c[i]=(int8_t)p.data[7+i];
    out->observed_ms=dock_monotonic_ms();return 0;
}
// Mark an attempted stop before I/O: a failed send may still have changed the fan.
// Clear that obligation only after automatic mode has been read back successfully.
static int set_silent(dock_t *d,bool silent,dock_error *e) {
    if (!d) return fail(e,"Contexte manquant.");
    if (silent && d->cancel && d->cancel(d->cancel_user)) return fail(e,"Arrêt du ventilateur annulé.");
    if (silent) d->changed=true;
    packet status,p;
    int written=exchange(d,0x10,silent,&status,e);
    if (written) return 1;
    if (status.data[6]!=1) return fail(e,"Réglage refusé par l'EC : %u",status.data[6]);
    if (exchange(d,0x0f,false,&p,e)) return 1;
    if (p.data[7]!=(unsigned)silent) return fail(e,"Mode relu différent de la demande.");
    if (!silent) d->changed=false;
    return 0;
}
int dock_automatic(dock_t *d,dock_error *e) { return set_silent(d,false,e); }
static int watch_read(void *context,dock_thermal *out,bool interruptible,dock_error *e) {
    dock_t *d=context;d->interruptible=interruptible;
    int result=dock_read_thermal(d,out,e);
    d->interruptible=false;return result;
}
static int watch_set(void *d,bool silent,dock_error *e) { return set_silent(d,silent,e); }
static uint64_t watch_now(void *d) { (void)d;return dock_monotonic_ms(); }
static void watch_pause(void *d) { (void)d;usleep(100000); }
int dock_watch(dock_t *d,const dock_watch_options *o,dock_cancel_fn cancel,dock_watch_fn update,void *user,dock_error *e) {
    if (!d) return fail(e,"Contexte manquant.");
    dock_watch_io io={d,watch_read,watch_set,watch_now,watch_pause};
    d->cancel=cancel;d->cancel_user=user;
    int result=dock_watch_run(&io,o,cancel,update,user,e);
    d->cancel=NULL;d->cancel_user=NULL;return result;
}
int dock_rid(const dock_t *d) { return d ? d->rid : -1; }
size_t dock_read_registers(dock_t *d,dock_register *out,size_t capacity) {
    const uint8_t addresses[]={0x03,0x3f,0x30,0x31,0x32,0x33,0x34,0x35,0x48,0x49};
    if (!d || !out) return 0;
    size_t count=sizeof(addresses);if (count>capacity) count=capacity;
    for (size_t i=0;i<count;i++) {
        memset(&out[i],0,sizeof(out[i]));out[i].address=addresses[i];uint64_t n;
        out[i].status=(uint32_t)reg(d,addresses[i],out[i].bytes,&n);
        if (!out[i].status && n<=64) out[i].length=n;
        else if (n>64) out[i].status=(uint32_t)kIOReturnOverrun;
    }
    return count;
}
// Reject unsupported/ambiguous devices before exposing any thermal write path.
int dock_open(dock_t **out,dock_error *e) {
    if (!out) return fail(e,"Sortie manquante.");*out=NULL;
    if (geteuid()!=0) return fail(e,"AppleHPM nécessite root : utiliser sudo pour les commandes thermiques.");
    dock_t *d=calloc(1,sizeof(*d));if (!d) return fail(e,"Allocation impossible.");
    d->lock=open("/var/run/dockctl-hpm.lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    if (d->lock<0 || flock(d->lock,LOCK_EX|LOCK_NB)) {
        if (d->lock>=0) close(d->lock);free(d);return fail(e,"Une autre session HPM est active, ou verrou indisponible.");
    }
    dock_info info;
    if (dock_read_info(&info,e) || !info.thermal_firmware_known) {
        close(d->lock);free(d);return fail(e,"Dock/EC thermique non reconnu ou inaccessible ; exécuter dockctl info.");
    }
    io_iterator_t iter=0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault,IOServiceMatching("AppleHPMARMI2C"),&iter)) {
        dock_close(d);return fail(e,"Contrôleurs AppleHPM introuvables.");
    }
    io_service_t service;unsigned matched=0;
    while ((service=IOIteratorNext(iter))) {
        IOCFPlugInInterface **plugin=NULL;HPMCapture **hpm=NULL;SInt32 score=0;
        if (!IOCreatePlugInInterfaceForService(service,kAppleHPMLibType,kIOCFPlugInInterfaceID,&plugin,&score) && plugin &&
            (*plugin)->QueryInterface(plugin,CFUUIDGetUUIDBytes(kAppleHPMLibInterface),(LPVOID *)&hpm)==S_OK && hpm) {
            if ((*hpm)->version==3) {
                dock_t candidate={.hpm=hpm,.plugin=plugin,.lock=-1,.rid=-1};
                if (!partner(&candidate,NULL)) {
                    matched++;
                    if (matched==1) {
                        d->hpm=hpm;d->plugin=plugin;d->rid=-1;
                        CFTypeRef rid=IORegistryEntryCreateCFProperty(service,CFSTR("RID"),NULL,0);
                        if (rid && CFGetTypeID(rid)==CFNumberGetTypeID()) CFNumberGetValue(rid,kCFNumberIntType,&d->rid);
                        if (rid) CFRelease(rid);
                        hpm=NULL;plugin=NULL;
                    }
                }
            }
            if (hpm) (*hpm)->Release(hpm);
        }
        if (plugin) IODestroyPlugInInterface(plugin);
        IOObjectRelease(service);
    }
    IOObjectRelease(iter);
    if (matched!=1) { dock_close(d);return fail(e,"Un seul port HPM Dell attendu, %u détecté(s).",matched); }
    *out=d;return 0;
}
void dock_close(dock_t *d) {
    if (!d) return;
    if (d->hpm && d->changed) set_silent(d,false,NULL);
    if (d->hpm) (*d->hpm)->Release(d->hpm);
    if (d->plugin) IODestroyPlugInInterface(d->plugin);
    if (d->lock>=0) close(d->lock);
    free(d);
}
