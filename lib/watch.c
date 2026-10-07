// SPDX-License-Identifier: MIT
#include "internal.h"
#include <stdio.h>
#include <string.h>

void dock_watch_defaults(dock_watch_options *o) {
    if (o) *o = (dock_watch_options){300,300,30,30,{40,45,66},{45,50,70}};
}

// Experimental backstop, below/at the EC03 next-speed thresholds (62/70/73).
// Not Dell-certified operating limits; never delayed by the stability timer.
static const int critical_c[]={55,60,73};

bool dock_watch_options_valid(const dock_watch_options *o) {
    if (!o || !o->session_seconds || o->session_seconds > 3600 ||
        o->silence_seconds < 9 || o->silence_seconds > 300 ||
        o->cooling_seconds < 30 || o->cooling_seconds > 600 ||
        !o->stable_seconds || o->stable_seconds > 120) return false;
    for (unsigned i = 0; i < 3; i++)
        if (o->resume_below_c[i] < 0 || o->resume_below_c[i] >= o->ventilate_at_c[i] ||
            o->ventilate_at_c[i] >= critical_c[i]) return false;
    return true;
}

static bool valid_sample(const dock_thermal *s) {
    if (!s || s->mode > 1 || s->speed_class > 2) return false;
    for (unsigned i = 0; i < 3; i++)
        if (s->temperature_c[i] < 0 || s->temperature_c[i] > 125) return false;
    return true;
}

typedef struct { dock_watch_state state; dock_watch_reason reason; } decision;
// One stability timer tracks continuous eligibility, not individual sensor spikes.
// Moving from one hot sensor to another does not restart a continuous over-limit period.
static decision decide(dock_policy *p, const dock_watch_options *o,
                       const dock_thermal *s, uint64_t now) {
    if (!p || !dock_watch_options_valid(o) || !valid_sample(s) || now < p->changed_ms ||
        (p->pending_reason && now < p->pending_since_ms))
        return (decision){DOCK_COOLING, DOCK_INVALID_SAMPLE};
    if (p->state == DOCK_FINISHED) return (decision){DOCK_FINISHED, DOCK_REASON_NONE};
    dock_watch_reason candidate=DOCK_REASON_NONE;
    if (p->state == DOCK_SILENT) {
        if (s->mode != 1) return (decision){DOCK_COOLING, DOCK_INVALID_SAMPLE};
        for (unsigned i = 0; i < 3; i++)
            if (s->temperature_c[i] >= critical_c[i])
                return (decision){DOCK_COOLING, (dock_watch_reason)(DOCK_CRITICAL_LOCAL+i)};
        if (now - p->changed_ms >= (uint64_t)o->silence_seconds * 1000)
            return (decision){DOCK_COOLING, DOCK_SILENCE_LIMIT};
        for (unsigned i = 0; i < 3; i++)
            if (s->temperature_c[i] >= o->ventilate_at_c[i]) {
                candidate=(dock_watch_reason)(DOCK_LOCAL_LIMIT+i);break;
            }
    } else if (s->mode==0 && s->speed_class!=2) {
        candidate=DOCK_READY;
        for (unsigned i=0;i<3;i++)
            if(s->temperature_c[i]>o->resume_below_c[i]) candidate=DOCK_REASON_NONE;
    }
    if(candidate==DOCK_REASON_NONE) { p->pending_reason=DOCK_REASON_NONE;return (decision){p->state,DOCK_REASON_NONE}; }
    if(p->pending_reason==DOCK_REASON_NONE || (p->pending_reason==DOCK_READY)!=(candidate==DOCK_READY))
        p->pending_since_ms=now;
    p->pending_reason=candidate;
    if(now-p->pending_since_ms < (uint64_t)o->stable_seconds*1000 ||
       (p->state==DOCK_COOLING && now-p->changed_ms < (uint64_t)o->cooling_seconds*1000))
        return (decision){p->state,DOCK_REASON_NONE};
    return (decision){p->state==DOCK_SILENT?DOCK_COOLING:DOCK_SILENT,candidate};
}
dock_watch_state dock_policy_next(dock_policy *p, const dock_watch_options *o,
                                 const dock_thermal *s, uint64_t now) {
    return decide(p,o,s,now).state;
}
// Preserve the triggering sample separately from subsequent post-write verification.
static void emit(dock_watch_fn update,void *user,dock_watch_event_kind kind,
                 const dock_policy *p,decision next,const dock_thermal *sample,uint64_t start,uint64_t now) {
    if (!update) return;
    dock_watch_event event={kind,p->state,next.state,next.reason,
        now>=start?now-start:0,now>=p->changed_ms?now-p->changed_ms:0,
        now>=sample->observed_ms?now-sample->observed_ms:0,p->pending_reason,
        p->pending_reason && now>=p->pending_since_ms?now-p->pending_since_ms:0,*sample};
    update(user,&event);
}
static int read_latest(dock_watch_io *io,dock_thermal *sample,bool interruptible,dock_error *error) {
    dock_thermal incoming={0};
    int result=io->read(io->user,&incoming,interruptible,error);
    if (!result) *sample=incoming; // Keep the last complete reading if I/O fails partway.
    return result;
}

int dock_watch_run(dock_watch_io *io, const dock_watch_options *o,
                   dock_cancel_fn cancel, dock_watch_fn update, void *user, dock_error *error) {
    if (!io || !dock_watch_options_valid(o)) {
        if (error) snprintf(error->message, sizeof(error->message), "Options du watcher invalides.");
        return 1;
    }
    uint64_t start=io->now(io->user);
    dock_thermal sample = {0};
    int result = read_latest(io, &sample, true, error);
    if (cancel && cancel(user)) return 0; // No write has occurred during preflight.
    if (result || !valid_sample(&sample) || sample.mode != 0) {
        if (!result && error) snprintf(error->message, sizeof(error->message), "Mode automatique et télémétrie valides requis avant le watcher.");
        return 1;
    }
    uint64_t previous = io->now(io->user);
    dock_policy policy = {.state=DOCK_COOLING,.changed_ms=previous};
    dock_watch_reason end_reason=DOCK_SESSION_LIMIT;
    bool attempted = false;
    // Initial cooling interval also observes behaviour before the first stop.
    while (io->now(io->user) - start < (uint64_t)o->session_seconds * 1000) {
        if (cancel && cancel(user)) break;
        uint64_t now = io->now(io->user);
        if (now < previous || now - previous > 25000 || !valid_sample(&sample) ||
            sample.observed_ms > now || now - sample.observed_ms > 10000 ||
            sample.mode != (policy.state == DOCK_SILENT ? 1u : 0u)) {
            if (error) snprintf(error->message, sizeof(error->message), "Surveillance interrompue : échantillon, mode ou délai inattendu.");
            result = 1; end_reason=DOCK_INVALID_SAMPLE; break;
        }
        previous = now;
        decision next = decide(&policy, o, &sample, now);
        if (next.state != policy.state) {
            emit(update,user,DOCK_TRANSITION,&policy,next,&sample,start,now);
            // Set before I/O: even an ambiguous failed stop must be followed by restoration.
            attempted = true;
            uint64_t requested = now;
            result = io->set_silent(io->user, next.state == DOCK_SILENT, error);
            if (result) break;
            policy = (dock_policy){.state=next.state,.changed_ms=next.state == DOCK_SILENT ? requested : io->now(io->user)};
            // Never label an old sample as a new measured mode.
            result = read_latest(io, &sample, true, error);
            if (result) break;
            continue; // Evaluate the fresh sample immediately, including a hot post-stop sample.
        }
        emit(update,user,DOCK_SAMPLE,&policy,next,&sample,start,now);
        if (cancel && cancel(user)) break;
        io->pause(io->user);
        result = read_latest(io, &sample, true, error);
        if (result) break;
    }
    if (cancel && cancel(user)) { result = 0;end_reason=DOCK_CANCELLED; }
    else if (result && end_reason!=DOCK_INVALID_SAMPLE) end_reason=DOCK_IO_ERROR;
    if (attempted) {
        emit(update,user,DOCK_RESTORE,&policy,(decision){DOCK_COOLING,end_reason},&sample,start,io->now(io->user));
        dock_error recovery = {{0}};
        if (io->set_silent(io->user, false, &recovery)) {
            if (error) snprintf(error->message, sizeof(error->message), "Retour automatique NON confirmé : %.180s", recovery.message);
            return 2;
        }
    }
    // Final callback contains a fresh reading, never an inferred fan speed.
    dock_error final_error = {{0}};
    if (read_latest(io, &sample, false, &final_error) || !valid_sample(&sample) || sample.mode != 0) {
        if (error && !result) snprintf(error->message, sizeof(error->message), "Lecture finale automatique non confirmée : %.170s", final_error.message);
        return result ? result : 1;
    }
    policy=(dock_policy){.state=DOCK_FINISHED,.changed_ms=io->now(io->user)};
    emit(update,user,DOCK_END,&policy,(decision){DOCK_FINISHED,end_reason},&sample,start,io->now(io->user));
    return result;
}
