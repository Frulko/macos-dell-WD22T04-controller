#include "dock.h"
#include <signal.h>
#include <stdio.h>

// This example changes fan mode. Cancellation requests verified automatic restoration.
static volatile sig_atomic_t stop_requested;
static void stop(int signal_number) { (void)signal_number; stop_requested = 1; }
static bool cancelled(void *user) { (void)user; return stop_requested != 0; }
static void updated(void *user, const dock_watch_event *event) {
    (void)user;
    const dock_thermal *sample=&event->sample;
    printf("+%.1f s, événement=%d, état=%d, cible=%d, raison=%d, classe=%u, températures=%d/%d/%d °C\n",
           event->elapsed_ms/1000.0,event->kind,event->state,event->target,event->reason,sample->speed_class,
           sample->temperature_c[0], sample->temperature_c[1], sample->temperature_c[2]);
}
int main(void) {
    if (signal(SIGINT, stop) == SIG_ERR || signal(SIGTERM, stop) == SIG_ERR ||
        signal(SIGHUP, stop) == SIG_ERR || signal(SIGPIPE, SIG_IGN) == SIG_ERR) return 1;
    dock_t *dock = NULL; dock_error error = {{0}};
    if (dock_open(&dock, &error)) { fprintf(stderr, "%s\n", error.message); return 1; }
    dock_watch_options options; dock_watch_defaults(&options);
    int result = dock_watch(dock, &options, cancelled, updated, NULL, &error);
    if (result) fprintf(stderr, "%s\n", error.message);
    dock_close(dock);
    return result;
}
