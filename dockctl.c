// SPDX-License-Identifier: MIT
#include "dock.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>

static volatile sig_atomic_t interrupted;
// Signal handlers only set a flag and use async-signal-safe write(); recovery runs normally.
static void stop(int signo) {
    (void)signo;int saved_errno=errno;
    if (!interrupted) {
        static const char message[]="\nArrêt demandé ; restauration automatique si nécessaire, puis vérification. Patienter quelques secondes…\n";
        interrupted=1;
        (void)write(STDERR_FILENO,message,sizeof(message)-1);
    }
    errno=saved_errno;
}
static bool cancelled(void *user) { (void)user;return interrupted!=0; }
static void json_string(const char *s) {
    putchar('"');for (const unsigned char *p=(const unsigned char *)s;*p;p++) {
        if (*p=='"' || *p=='\\') printf("\\%c",*p);
        else if (*p<32) printf("\\u%04x",*p);else putchar(*p);
    }putchar('"');
}
static void hex(const uint8_t *b,size_t n) { for (size_t i=0;i<n;i++) printf("%02x",b[i]); }
static void version(const uint8_t *b,size_t n) { for (size_t i=0;i<n;i++) printf("%s%02x",i?".":"",b[i]); }
static void optional_number(bool known,int value) { if (known) printf("%d",value);else fputs("null",stdout); }
static void power_value(dock_power_value v) { optional_number(v.known,v.value); }
static void print_host_power(const dock_host_power *p,bool json) {
    if(json) {
        fputs("{\"source\":\"AppleSmartBattery\",\"private_keys\":true,\"units_inferred\":true,\"attribution_to_dock_verified\":false,\"external_connected\":",stdout);
        fputs(p->external_known?(p->external_connected?"true":"false"):"null",stdout);
        fputs(",\"adapter\":{\"rated_watts\":",stdout);power_value(p->adapter_watts);
        fputs(",\"voltage_mv\":",stdout);power_value(p->adapter_mv);
        fputs(",\"current_limit_ma\":",stdout);power_value(p->adapter_ma);
        fputs(",\"selected_profile_index\":",stdout);power_value(p->selected_profile);
        fputs(",\"profiles\":[",stdout);
        for(size_t i=0;i<p->profile_count;i++) {
            if(i)putchar(',');fputs("{\"index\":",stdout);power_value(p->profiles[i].index);
            fputs(",\"max_voltage_mv\":",stdout);power_value(p->profiles[i].millivolts);
            fputs(",\"max_current_ma\":",stdout);power_value(p->profiles[i].milliamps);putchar('}');
        }
        printf("],\"profiles_truncated\":%s},\"mac_input\":{\"voltage_mv\":",p->profiles_truncated?"true":"false");power_value(p->input_mv);
        fputs(",\"current_ma\":",stdout);power_value(p->input_ma);
        fputs(",\"power_mw\":",stdout);power_value(p->input_mw);
        printf("},\"measured_dock_consumption_watts\":null,\"observed_monotonic_ms\":%" PRIu64 "}",p->observed_ms);
    } else {
        printf("Chargeur annoncé : ");power_value(p->adapter_watts);printf(" W ; ");power_value(p->adapter_mv);
        printf(" mV ; limite ");power_value(p->adapter_ma);puts(" mA");
        printf("Profil sélectionné : ");power_value(p->selected_profile);putchar('\n');
        for(size_t i=0;i<p->profile_count;i++) {
            printf("  Profil ");power_value(p->profiles[i].index);printf(" : ");power_value(p->profiles[i].millivolts);
            printf(" mV / ");power_value(p->profiles[i].milliamps);puts(" mA");
        }
        printf("Entrée du Mac : ");power_value(p->input_mv);printf(" mV ; ");power_value(p->input_ma);
        printf(" mA ; ");power_value(p->input_mw);puts(" mW");
        puts("Source : AppleSmartBattery, clés privées, unités inférées ; valeurs éventuellement en cache.\nCe relevé ne mesure ni le dock entier ni ses ports aval ; source d'alimentation non attribuée au dock.");
    }
}
static void print_info(const dock_info *i,bool json) {
    if (json) {
        fputs("{\"model\":",stdout);json_string(i->model);fputs(",\"service_tag\":",stdout);json_string(i->service_tag);
        printf(",\"base_type\":%u,\"module_type\":%u,\"board_id\":%u,\"configuration_raw\":%u,\"declared_supply_watts\":%u,\"package\":\"",
               i->base_type,i->module_type,i->board_id,i->configuration,i->supply_watts);version(i->package,4);
        printf("\",\"module_serial_hex\":\"%016" PRIx64 "\",\"original_module_serial_hex\":\"%016" PRIx64 "\",\"ports_raw\":[%u,%u],\"thermal_firmware_known\":%s,\"components\":[",
               i->module_serial,i->original_module_serial,i->port_status[0],i->port_status[1],i->thermal_firmware_known?"true":"false");
        for (size_t n=0;n<i->component_count;n++) {
            const dock_component *c=i->components+n;if(n)putchar(',');
            printf("{\"name\":");json_string(dock_component_name(c));
            printf(",\"location\":%u,\"type\":%u,\"subtype\":%u,\"argument\":%u,\"instance\":%u,\"version_raw\":\"",c->location,c->type,c->subtype,c->argument,c->instance);
            hex(c->version,4);fputs("\"}",stdout);
        }
        fputs("],\"host_adapter\":{\"attribution_to_dock_verified\":false,\"present\":",stdout);fputs(i->host_adapter_present?"true":"false",stdout);
        fputs(",\"rated_watts\":",stdout);optional_number(i->host_watts_known,i->host_adapter_watts);
        fputs(",\"rated_current_ma\":",stdout);optional_number(i->host_ma_known,i->host_adapter_ma);
        fputs(",\"voltage_mv\":null},\"measured_dock_consumption_watts\":null,\"identity_raw\":\"",stdout);hex(i->raw_identity,103);fputs("\"}",stdout);
    } else {
        printf("%s — Service Tag %s\nBloc déclaré : %u W ; carte %u ; module %u ; configuration brute 0x%02x\n",
               i->model,i->service_tag,i->supply_watts,i->board_id,i->module_type,i->configuration);
        printf("Package : ");version(i->package,4);
        printf("\nSérie module : %016" PRIx64 " ; originale : %016" PRIx64 "\nPorts bruts : 0x%04x / 0x%04x\n",
               i->module_serial,i->original_module_serial,i->port_status[0],i->port_status[1]);
        for(size_t n=0;n<i->component_count;n++) {
            const dock_component *c=i->components+n;
            printf("  %s (type %u, %s, sous-type %u, instance %u, arg %u) : ",dock_component_name(c),c->type,c->location?"module":"base",c->subtype,c->instance,c->argument);
            version(c->version,4);putchar('\n');
        }
        if(i->thermal_firmware_known)printf("Accès thermique EC : port 0 %s, port 1 %s\n",i->port_status[0]&8?"prérequis présent":"inactif",i->port_status[1]&8?"prérequis présent":"inactif");
        printf("Chargeur vu par macOS : ");if(i->host_watts_known)printf("%d W",i->host_adapter_watts);else printf("puissance inconnue");
        if(i->host_ma_known)printf(", courant déclaré %d mA",i->host_adapter_ma);
        puts(" (attribution au dock non vérifiée).\nConsommation réelle du dock : non disponible.");
    }
}
static void print_thermal(const dock_thermal *s,bool json) {
    if(json)printf("{\"mode\":%u,\"speed_class\":%u,\"exact_rpm\":null,\"temperature_c\":{\"local\":%d,\"remote\":%d,\"module\":%d},\"observed_monotonic_ms\":%" PRIu64 "}",s->mode,s->speed_class,s->temperature_c[0],s->temperature_c[1],s->temperature_c[2],s->observed_ms);
    else printf("Mode %s ; classe vitesse %u (%s) ; températures %d / %d / %d °C\n",s->mode?"forcé":"automatique",s->speed_class,s->speed_class==0?"arrêt ou mesure indisponible":s->speed_class==1?"0 < RPM < 2750":"RPM >= 2750",s->temperature_c[0],s->temperature_c[1],s->temperature_c[2]);
}
static void print_power(dock_t *d,bool json) {
    dock_register registers[10];size_t n=dock_read_registers(d,registers,10);
    dock_pd_contract pd;dock_decode_pd(registers,n,&pd);
    if(json) {
        printf("{\"rid\":%d,\"decoded_contract_watts\":",dock_rid(d));
        if(pd.contract_inferred)printf("%.3f",(double)pd.millivolts*pd.operating_ma/1000000);else fputs("null",stdout);
        fputs(",\"source_offers\":[",stdout);
        for(size_t i=0;i<pd.offer_count;i++) {
            const dock_pd_offer *p=pd.offers+i;if(i)putchar(',');
            printf("{\"object_position\":%zu,\"pdo_raw\":%u,\"voltage_mv\":",i+1,p->raw);optional_number(p->fixed,p->millivolts);
            fputs(",\"max_current_ma\":",stdout);optional_number(p->fixed,p->milliamps);putchar('}');
        }
        fputs("],\"active_contract\":",stdout);
        if(pd.contract_inferred)printf("{\"apple_layout_inferred\":true,\"selected_object\":%u,\"voltage_mv\":%u,\"operating_current_ma\":%u,\"requested_max_current_ma\":%u,\"capability_mismatch\":%s,\"rdo_raw\":%u,\"selected_pdo_raw\":%u,\"apple_trailer_raw\":%u}",pd.selected_object,pd.millivolts,pd.operating_ma,pd.requested_max_ma,pd.capability_mismatch?"true":"false",pd.rdo_raw,pd.selected_pdo_raw,pd.apple_trailer_raw);
        else fputs("null",stdout);
        fputs(",\"registers\":[",stdout);
    } else {
        for(size_t i=0;i<pd.offer_count;i++)if(pd.offers[i].fixed)
            printf("Offre source %zu : %.2f V / %.2f A\n",i+1,pd.offers[i].millivolts/1000.0,pd.offers[i].milliamps/1000.0);
        if(pd.contract_inferred) {
            printf("Contrat inféré : %.2f V / %.2f A = %.2f W (capacité, pas consommation).\n",pd.millivolts/1000.0,pd.operating_ma/1000.0,(double)pd.millivolts*pd.operating_ma/1000000);
            printf("RDO : objet %u ; maximum demandé %.2f A ; capability mismatch %s.\n",pd.selected_object,pd.requested_max_ma/1000.0,pd.capability_mismatch?"oui":"non");
        }
        puts("Registres PD bruts (disposition Apple inférée sur ce Mac ; octets inconnus conservés) :");
    }
    for(size_t j=0;j<n;j++) {
        dock_register *r=registers+j;
        if(json) { if(j)putchar(',');printf("{\"address\":%u,\"status\":%u,\"hex\":\"",r->address,r->status);hex(r->bytes,r->length);fputs("\"}",stdout); }
        else {printf("  0x%02x : status=0x%08x len=%zu ",r->address,r->status,r->length);hex(r->bytes,r->length);putchar('\n');}
    }
    if(json)fputs("]}",stdout);
}
static const struct { const char *id,*status,*description; } features[]={
    {"thermal_read","device_verified","Trois températures et classe de vitesse ; pas de RPM exact."},
    {"thermal_auto","device_verified","Régulation autonome du dock ; retour confirmé après les essais."},
    {"silence_watch","experimental","Silence cible max 300 s ; seuils réglables 42/47/68 C, stabilité 30 s ; critiques immédiats 55/60/73 C, hors latence I/O."},
    {"power_supply","device_verified","Puissance déclarée du bloc, pas consommation instantanée."},
    {"power_contract","capture_correlated","Offres fixes PDO ; RDO et PDO actif corrélés sur la capture Apple 10 octets ; disposition inférée, puissance de capacité."},
    {"host_power","experimental","Profils de charge, tension/courant/puissance d'entrée du Mac via AppleSmartBattery ; clés privées, pas consommation totale du dock."},
    {"exact_fan_rpm","internal_only","Lecture tachymétrique retrouvée dans l'EC ; la réponse thermique publique la réduit à une classe."},
    {"downstream_port_control","firmware_path_found","Handlers internes retrouvés, certains avec reset PD ; ports physiques et effets non validés, aucune commande exposée."},
    {"usb_thunderbolt_displays_ethernet","documented","USB, Thunderbolt 4, DP/HDMI, réseau Gigabit ; capacité effective dépend du Mac et des branchements."},
    {"power_delivery_non_dell","documented","Dell annonce jusqu'à 90 W vers un système non Dell avec son bloc 180 W ; ce dock déclare un bloc 130 W."},
    {"mac_displays","documented","macOS ne fournit pas le bureau étendu via MST ; nombre d'écrans dépend du Mac et des ports utilisés."},
    {"mac_passthrough_wol","platform_dependent","Dell indique MAC passthrough, Wake-on-Dock et Wake-on-LAN indisponibles sur les hôtes Apple de son guide."},
    {"power_button_pxe","platform_dependent","Bouton hôte annoncé compatible Apple par le guide Dell ; PXE et réglages non vérifiés ici."},
    {"firmware_update","deferred","Versions disponibles ; aucun flash exposé dans la bibliothèque."},
    {"arbitrary_rpm_pwm_ports","not_established","Pas de consigne RPM arbitraire, ni de réglage de ports établi. Les octets inconnus restent bruts."}
};
static void print_features(bool json) {
    if(json)putchar('[');
    for(size_t i=0;i<sizeof(features)/sizeof(features[0]);i++) {
        if(json) {if(i)putchar(',');fputs("{\"id\":",stdout);json_string(features[i].id);fputs(",\"status\":",stdout);json_string(features[i].status);fputs(",\"description\":",stdout);json_string(features[i].description);putchar('}');}
        else printf("%s [%s]\n  %s\n",features[i].id,features[i].status,features[i].description);
    }
    if(json)putchar(']');
}
static const char *state_name(dock_watch_state state) {
    return state==DOCK_SILENT?"silent":state==DOCK_FINISHED?"finished":"cooling";
}
static void watch_update(void *user,const dock_watch_event *event) {
    bool json=*(bool *)user;
    const char *kinds[]={"sample","transition","restore","finished"};
    const char *reasons[]={"observation","eligible","silence_deadline","local_threshold","remote_threshold","module_threshold",
        "cancelled","session_deadline","io_error","invalid_sample_or_delay","critical_local","critical_remote","critical_module"};
    const char *explanations[]={"observation","attente terminée et températures admissibles","durée cible du silence atteinte",
        "seuil de température locale atteint","seuil de température distante atteint","seuil de température module atteint",
        "interruption demandée","durée de session atteinte","erreur de communication","mesure, mode ou délai inattendu",
        "seuil critique local atteint","seuil critique distant atteint","seuil critique module atteint"};
    if(json) {
        printf("{\"event\":\"%s\",\"state\":\"%s\",\"target\":\"%s\",\"reason\":\"%s\",\"elapsed_ms\":%" PRIu64 ",\"state_elapsed_ms\":%" PRIu64 ",\"sample_age_ms\":%" PRIu64 ",\"pending_reason\":\"%s\",\"pending_elapsed_ms\":%" PRIu64 ",\"sample\":",
            kinds[event->kind],state_name(event->state),state_name(event->target),reasons[event->reason],
            event->elapsed_ms,event->state_elapsed_ms,event->sample_age_ms,reasons[event->pending_reason],event->pending_elapsed_ms);
        print_thermal(&event->sample,true);puts("}");
    } else {
        printf("[+%.1f s] ",event->elapsed_ms/1000.0);
        if(event->kind==DOCK_TRANSITION || event->kind==DOCK_RESTORE)
            printf("%s → %s demandé : %s (état depuis %.1f s ; relevé âgé de %.1f s).\n  Mesure avant réglage : ",
                state_name(event->state),state_name(event->target),explanations[event->reason],event->state_elapsed_ms/1000.0,event->sample_age_ms/1000.0);
        else {
            printf("[%s depuis %.1f s] ",state_name(event->state),event->state_elapsed_ms/1000.0);
            if(event->pending_reason!=DOCK_REASON_NONE)
                printf("temporisation %s %.1f s ; ",reasons[event->pending_reason],event->pending_elapsed_ms/1000.0);
        }
        print_thermal(&event->sample,false);
    }
    fflush(stdout);
}
static void usage(void) {
    puts("Usage : dockctl info|list|features|host-power [--json]\n"
         "        sudo dockctl inspect|thermal|power|connect|auto [--json]\n"
         "        sudo dockctl watch [--silence] [--seconds 300] [--json]\n"
         "        Réglages avec --silence :\n"
         "          --ventilate-at 42,47,68   seuils locale/distante/module (°C)\n"
         "          --resume-at 40,45,66      seuils pour retrouver le silence\n"
         "          --stable-seconds 30       condition maintenue (1–120 s)\n"
         "          --cooling-seconds 30      minimum en automatique (30–600 s)\n"
         "          --silence-seconds 300     limite par période silencieuse (9–300 s)\n"
         "watch : lecture seule par défaut. --silence active le watcher expérimental.\n"
         "Retour sans temporisation sur seuil critique 55/60/73 °C, erreur ou Ctrl-C.\n"
         "JSON du watcher : un objet par ligne. Ctrl-C termine avec retour automatique si nécessaire.");
}
static bool temperatures(const char *text,int out[3]) {
    for(unsigned i=0;i<3;i++) {
        char *end;errno=0;long n=strtol(text,&end,10);
        if(errno || end==text || n<0 || n>125 || *end!=(i==2?'\0':',')) return false;
        out[i]=(int)n;text=end+1;
    }
    return true;
}
int main(int argc,char **argv) {
    if(argc<2 || !strcmp(argv[1],"--help") || !strcmp(argv[1],"help")) {usage();return argc<2?2:0;}
    const char *cmd=argv[1];bool json=false,silence=false,seconds_given=false,tuning_given=false;unsigned seconds=300;
    dock_watch_options options;dock_watch_defaults(&options);
    for(int i=2;i<argc;i++) {
        if(!strcmp(argv[i],"--json"))json=true;
        else if(!strcmp(argv[i],"--silence"))silence=true;
        else if((!strcmp(argv[i],"--ventilate-at") || !strcmp(argv[i],"--resume-at")) && i+1<argc) {
            bool ventilate=!strcmp(argv[i],"--ventilate-at");
            if(!temperatures(argv[++i],ventilate?options.ventilate_at_c:options.resume_below_c)) {
                fputs("Trois températures entières attendues : locale,distante,module.\n",stderr);return 2;
            }
            tuning_given=true;
        }
        else if((!strcmp(argv[i],"--seconds") || !strcmp(argv[i],"--silence-seconds") ||
                 !strcmp(argv[i],"--cooling-seconds") || !strcmp(argv[i],"--stable-seconds")) && i+1<argc) {
            const char *option=argv[i];
            char *end;errno=0;unsigned long n=strtoul(argv[++i],&end,10);
            if(errno || !*argv[i] || *end || n<1 || n>3600) {fputs("Durée attendue : 1 à 3600 secondes.\n",stderr);return 2;}
            if(!strcmp(option,"--seconds")) {seconds=(unsigned)n;seconds_given=true;}
            else {
                tuning_given=true;
                if(!strcmp(option,"--silence-seconds"))options.silence_seconds=(unsigned)n;
                else if(!strcmp(option,"--cooling-seconds"))options.cooling_seconds=(unsigned)n;
                else options.stable_seconds=(unsigned)n;
            }
        } else {usage();return 2;}
    }
    if(((silence || seconds_given || tuning_given) && strcmp(cmd,"watch")) || (tuning_given && !silence)) {usage();return 2;}
    options.session_seconds=seconds;
    if(!dock_watch_options_valid(&options)) {
        fputs("Réglages invalides : reprise < ventilation < 55/60/73 °C ; stabilité 1–120 s, automatique 30–600 s, silence 9–300 s.\n",stderr);return 2;
    }
    if(!strcmp(cmd,"features")) {print_features(json);if(json)putchar('\n');return 0;}
    if(!strcmp(cmd,"host-power")) {
        dock_host_power power;dock_error error={{0}};
        if(dock_read_host_power(&power,&error)) {fprintf(stderr,"%s\n",error.message);return 1;}
        print_host_power(&power,json);if(json)putchar('\n');return 0;
    }
    bool info=!strcmp(cmd,"info") || !strcmp(cmd,"list"),inspect=!strcmp(cmd,"inspect"),watch=!strcmp(cmd,"watch");
    if(!info && !inspect && !watch && strcmp(cmd,"thermal") && strcmp(cmd,"power") && strcmp(cmd,"connect") && strcmp(cmd,"auto")) {usage();return 2;}
    dock_error error={{0}};dock_info identity;
    if(info || inspect) {
        if(dock_read_info(&identity,&error)) {fprintf(stderr,"%s\n",error.message);return 1;}
        if(info) {print_info(&identity,json);if(json)putchar('\n');return 0;}
    }
    if(signal(SIGINT,stop)==SIG_ERR || signal(SIGTERM,stop)==SIG_ERR || signal(SIGHUP,stop)==SIG_ERR || signal(SIGPIPE,SIG_IGN)==SIG_ERR) return 1;
    dock_t *d=NULL;if(dock_open(&d,&error)) {fprintf(stderr,"%s\n",error.message);return 1;}
    int result=0;dock_thermal sample;
    if(watch && silence) {
        if(!json)printf("Silence surveillé expérimental : limite %u s ; automatique >=%u s ; stabilité %u s, hors délais I/O.\nVentilation à %d/%d/%d °C ; reprise du silence à %d/%d/%d °C.\nSeuils critiques sans temporisation : 55/60/73 °C.\nObservation initiale avant le premier silence. Garder le processus et la liaison actifs.\n",
            options.silence_seconds,options.cooling_seconds,options.stable_seconds,
            options.ventilate_at_c[0],options.ventilate_at_c[1],options.ventilate_at_c[2],
            options.resume_below_c[0],options.resume_below_c[1],options.resume_below_c[2]);
        result=dock_watch(d,&options,cancelled,watch_update,&json,&error);
    } else if(watch) {
        uint64_t start=dock_monotonic_ms();
        do {result=dock_read_thermal(d,&sample,&error);if(result)break;
            print_thermal(&sample,json);if(json)putchar('\n');fflush(stdout);
        } while(!interrupted && dock_monotonic_ms()-start<(uint64_t)seconds*1000);
    } else if(!strcmp(cmd,"auto") || !strcmp(cmd,"connect")) {
        result=!strcmp(cmd,"auto")?dock_automatic(d,&error):dock_enter(d,&error);
        if(!result)puts(json?"{\"ok\":true}":"Commande confirmée.");
    } else if(!strcmp(cmd,"power")) {print_power(d,json);if(json)putchar('\n');}
    else {
        result=dock_read_thermal(d,&sample,&error);
        if(!result) {
            if(inspect) {
                if(json)fputs("{\"info\":",stdout);print_info(&identity,json);
                if(json)fputs(",\"thermal\":",stdout);print_thermal(&sample,json);
                if(json)fputs(",\"power\":",stdout);print_power(d,json);
                dock_host_power host;dock_error host_error={{0}};
                int host_status=dock_read_host_power(&host,&host_error);
                if(json)fputs(",\"host_power\":",stdout);
                if(!host_status)print_host_power(&host,json);
                else if(json)fputs("null",stdout);
                else fprintf(stderr,"%s\n",host_error.message);
                if(json)puts("}");
            } else {print_thermal(&sample,json);if(json)putchar('\n');}
        }
    }
    if(result)fprintf(stderr,"%s\n",error.message);
    dock_close(d);return result ? result : interrupted ? 130 : 0;
}
