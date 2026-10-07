// SPDX-License-Identifier: Apache-2.0
// AppleHPMLib interface: AsahiLinux/macvdmtool (Apache-2.0).
// Passive by default. Experimental entry is separate and never followed by Exit.
// Never invokes Write, Command, LOCK, DBMa or reset. Forced settings use bounded tests.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/file.h>
#include <fcntl.h>
#include <CoreFoundation/CoreFoundation.h>
#include "macvdmtool/AppleHPMLib.h"

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

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool dell_identity(const uint8_t *data, uint64_t length) {
    return length == 25 && (data[0] & 0xc0) == 0x40 && (data[0] & 7) >= 3 &&
           (le32(data + 1) & 0xffff) == 0x413c && le32(data + 9) >> 16 == 0xb070;
}

static bool structured_reply(const uint8_t *data, uint64_t length, uint32_t sop, uint32_t query) {
    return sop == 0 && length >= 4 && length <= 28 && length % 4 == 0 &&
           (le32(data) & 0xffff9f3f) == (query & 0xffff9f3f) && (le32(data) & 0xc0) != 0;
}

static bool modes_reply(const uint8_t *data, uint64_t length, uint32_t sop, uint16_t svid) {
    return structured_reply(data, length, sop, (uint32_t)svid << 16 | 0x8003);
}

static bool dell_mode1_reply(const uint8_t *data, uint64_t length, uint32_t sop) {
    return modes_reply(data, length, sop, 0x413c) && length >= 8 &&
           (le32(data) & 0xc0) == 0x40 && le32(data + 4) == 1;
}

static int check_partner(HPMCapture **device) {
    uint8_t data[64] = {0};
    uint64_t length = 0;
    IOReturn status = (*device)->Read(device, 0, 0x3f, data, sizeof(data), 0, &length);
    if (status || length != 10 || !(data[0] & 1)) {
        fputs("Requête refusée : aucune connexion PD confirmée.\n", stderr); return 1;
    }
    status = (*device)->Read(device, 0, 0x48, data, sizeof(data), 0, &length);
    if (status || !dell_identity(data, length)) {
        fputs("Requête refusée : identité WD22TB4 413c:b070 non confirmée.\n", stderr);
        return 1;
    }
    return 0;
}

static int discover_modes(HPMCapture **device, uint16_t svid, bool require_mode1) {
    uint8_t data[64] = {0}, counter = 0;
    uint64_t length = 0;
    uint32_t sop = 0;
    if (check_partner(device)) return 1;
    IOReturn status = (*device)->ReceiveVDM(device, 0, data, sizeof(data), 0, &sop, &counter, &length);
    if (status) return 1;
    uint8_t previous = counter;
    // Structured VDM v2.0, object position 0, initiator, Discover Modes (3).
    const uint8_t query[] = {0x03, 0xa0, svid & 0xff, svid >> 8};
    if (svid != 0x413c && svid != 0xff01) return 1;
    status = (*device)->SendVDM(device, 0, 0, query, sizeof(query), 0);
    printf("SVID %04x Discover Modes: send=0x%08x\n", svid, (unsigned)status);
    if (status) return 1;
    for (unsigned attempt = 0; attempt < 20; attempt++) {
        usleep(50000);
        length = 0;
        status = (*device)->ReceiveVDM(device, 0, data, sizeof(data), 0, &sop, &counter, &length);
        if (status) { fprintf(stderr, "ReceiveVDM: 0x%08x\n", (unsigned)status); return 1; }
        if (counter != previous && modes_reply(data, length, sop, svid)) {
            printf("SVID %04x Discover Modes: reply", svid);
            for (uint64_t i = 0; i < length; i++) printf(" %02x", data[i]);
            printf("\nACK=%u NAK=%u BUSY=%u\n", (le32(data) & 0xc0) == 0x40,
                   (le32(data) & 0xc0) == 0x80, (le32(data) & 0xc0) == 0xc0);
            if (require_mode1 && !dell_mode1_reply(data, length, sop)) {
                fputs("Position Dell 1 non confirmée par Discover Modes : aucun Enter envoyé.\n", stderr);
                return 1;
            }
            return (le32(data) & 0xc0) == 0x40 ? 0 : 1;
        }
    }
    printf("Pas de réponse SVID %04x en 1 seconde. Dernier RX: SOP=%u count=%u->%u len=%llu",
           svid, sop, previous, counter, (unsigned long long)length);
    if (length <= sizeof(data))
        for (uint64_t i = 0; i < length; i++) printf(" %02x", data[i]);
    putchar('\n');
    const uint8_t registers[] = {0x08, 0x09, 0x4f};
    for (unsigned i = 0; i < sizeof(registers); i++) {
        length = 0;
        status = (*device)->Read(device, 0, registers[i], data, sizeof(data), 0, &length);
        printf("  Diagnostic reg %02x: status=0x%08x len=%llu", registers[i], (unsigned)status,
               (unsigned long long)length);
        if (!status && length <= sizeof(data))
            for (uint64_t j = 0; j < length; j++) printf(" %02x", data[j]);
        putchar('\n');
    }
    return 1;
}

typedef struct {
    uint8_t data[64], counter;
    uint32_t sop;
    uint64_t length;
} Capture;

static bool same_capture(const Capture *a, const Capture *b) {
    return a->length <= sizeof(a->data) && b->length == a->length &&
           a->counter == b->counter && a->sop == b->sop &&
           !memcmp(a->data, b->data, (size_t)a->length);
}

static int receive_capture(HPMCapture **device, unsigned attention, Capture *out) {
    memset(out, 0, sizeof(*out));
    IOReturn status = attention
        ? (*device)->ReceiveVDMAttention(device, 0, out->data, sizeof(out->data), 0,
                                        &out->sop, &out->counter, &out->length)
        : (*device)->ReceiveVDM(device, 0, out->data, sizeof(out->data), 0,
                               &out->sop, &out->counter, &out->length);
    if (status || out->length > 28 || out->length % 4 || out->sop > 2 || out->counter > 7) {
        fprintf(stderr, "RX %s invalide : status=0x%08x SOP=%u count=%u len=%llu\n",
                attention ? "Attention" : "VDM", (unsigned)status, out->sop,
                out->counter, (unsigned long long)out->length);
        return 1;
    }
    return 0;
}

static uint64_t milliseconds(void) {
    return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) / 1000000;
}

static void print_capture(const Capture *c, unsigned attention, uint64_t elapsed) {
    printf("  +%llums RX %s SOP=%u count=%u len=%llu", (unsigned long long)elapsed,
           attention ? "Attention" : "VDM", c->sop, c->counter,
           (unsigned long long)c->length);
    for (uint64_t i = 0; i < c->length; i++) printf(" %02x", c->data[i]);
    putchar('\n');
    fflush(stdout);
}

static bool thermal_signature(const Capture *c, uint8_t opcode) {
    if (opcode != 0x0f && opcode != 0x10 && opcode != 0x11) return false;
    if (c->length < (opcode == 0x10 ? 8 : 12) || c->length > 28 || c->length % 4 || c->sop != 0) return false;
    uint32_t h = le32(c->data);
    return h >> 16 == 0x413c && (h & 0x8000) && (h & 0xc0) <= 0x40 &&
           ((h & 0x1f) == 6 || (h & 0x1f) == 0x12) &&
           c->data[4] == 1 && (opcode == 0x10 ? c->data[5] == 0x80 :
           (c->data[5] == 0x81 && c->data[6] == (opcode == 0x0f ? 0x0b : 0x0c)));
}

static bool decode_thermal(const Capture *c, uint8_t opcode, char *out, size_t capacity) {
    if (!thermal_signature(c, opcode)) return false;
    if (opcode == 0x0f) {
        unsigned mode = c->data[7], speed = c->data[8];
        const char *classes[] = {"arrêt ou mesure indisponible", "0 < vitesse < 2750 tr/min",
                                "vitesse >= 2750 tr/min"};
        snprintf(out, capacity, "Mode thermique : %u (%s) ; classe vitesse : %u (%s)",
                 mode, mode == 0 ? "automatique" : mode == 1 ? "consigne forcée" : "inconnu",
                 speed, speed < 3 ? classes[speed] : "inconnue");
    } else if (opcode == 0x11) {
        // Only these three bytes belong to the temperature reply; padding may be stale.
        snprintf(out, capacity, "Températures EC : locale %d °C ; distante %d °C ; module %d °C",
                 (int8_t)c->data[7], (int8_t)c->data[8], (int8_t)c->data[9]);
    } else snprintf(out, capacity, "Résultat EC du réglage : %u (%s)", c->data[6],
                    c->data[6] == 1 ? "succès" : c->data[6] == 4 ? "refus" : "inconnu");
    return true;
}

static bool thermal_receipt(const Capture *c, uint8_t opcode, uint8_t out[4]) {
    // Dell Message Received (0x13), only for the observed Attention envelope.
    if (!thermal_signature(c, opcode) || c->length != 28 || le32(c->data) != 0x413ca106)
        return false;
    const uint8_t receipt[] = {0x13,0xa1,0x3c,0x41};
    memcpy(out, receipt, sizeof(receipt));
    return true;
}

static bool pending_receipt(const Capture *c, uint8_t opcode, unsigned limit,
                            unsigned sent, uint8_t out[4]) {
    return sent < limit && (thermal_receipt(c, opcode, out) ||
           (limit > 1 && (thermal_receipt(c, 0x0f, out) || thermal_receipt(c, 0x11, out))));
}

enum { THERMAL_AUTOMATIC, THERMAL_LOW, THERMAL_HIGH, THERMAL_STOP };

static bool thermal_query(uint8_t opcode, unsigned setting, uint8_t out[28]) {
    if (opcode != 0x0f && opcode != 0x10 && opcode != 0x11) return false;
    if (setting > THERMAL_STOP || (opcode != 0x10 && setting)) return false;
    const uint8_t query[28] = {0x12, 0xa1, 0x3c, 0x41, 0x02, opcode};
    memcpy(out, query, sizeof(query));
    // Fixed settings only; forced stop is exposed exclusively as a short test.
    // EC stores mode before validating class, so never send a placeholder class.
    if (opcode == 0x10) {
        out[6] = setting != THERMAL_AUTOMATIC;
        out[7] = setting == THERMAL_STOP ? 0 : setting == THERMAL_HIGH ? 2 : 1;
    }
    return true;
}

static int thermal_exchange(HPMCapture **device, uint8_t opcode, unsigned setting, unsigned receipt_limit,
                            Capture *reply) {
    uint8_t query[28];
    if (!thermal_query(opcode, setting, query)) return 1;
    memset(reply, 0, sizeof(*reply));
    if (check_partner(device)) return 1;
    Capture previous[2];
    for (unsigned a = 0; a < 2; a++) {
        if (receive_capture(device, a, &previous[a])) return 1;
        print_capture(&previous[a], a, 0);
    }
    // Read payloads confirmed on EC 01.01.00.03 through Dell Attention replies.
    uint64_t start = milliseconds(), next_check = start;
    IOReturn status = (*device)->SendVDM(device, 0, 0, query, sizeof(query), 0);
    printf("Dell %s %02x : send=0x%08x\n", opcode == 0x10 ?
           (setting == THERMAL_STOP ? "arrêt temporaire" : setting == THERMAL_HIGH ? "consigne 3600 tr/min" : setting == THERMAL_LOW ?
            "consigne 1900 tr/min" : "mode automatique") : "lecture", opcode, (unsigned)status);
    fflush(stdout);
    if (status) return 1;
    bool signature = false;
    unsigned receipts = 0;
    do {
        for (unsigned a = 0; a < 2; a++) {
            Capture current;
            if (receive_capture(device, a, &current)) return 1;
            if (!same_capture(&current, &previous[a])) {
                print_capture(&current, a, milliseconds() - start);
                char decoded[256];
                if (decode_thermal(&current, opcode, decoded, sizeof(decoded))) {
                    if (!signature || !same_capture(&current, reply)) puts(decoded);
                    signature = true;
                    *reply = current;
                }
                uint8_t receipt[4];
                // A previous EC reply can remain queued after the first receipt.
                if (a && pending_receipt(&current, opcode, receipt_limit, receipts, receipt)) {
                    if (check_partner(device)) return 1;
                    status = (*device)->SendVDM(device, 0, 0, receipt, sizeof(receipt), 0);
                    printf("  Dell Message Received 0x13 (%u/%u) : send=0x%08x\n",
                           ++receipts, receipt_limit, (unsigned)status);
                    fflush(stdout);
                    if (status) return 1;
                }
                previous[a] = current;
            }
        }
        if (milliseconds() >= next_check) {
            if (check_partner(device)) return 1;
            next_check = milliseconds() + 250;
        }
        usleep(10000);
    } while (milliseconds() - start < 3000);
    printf("  Réponse thermique %s.\n", signature ? "décodée" : "non détectée");
    if (receipt_limit && receipts == receipt_limit)
        puts("  Budget ACK atteint ; aucun envoi supplémentaire dans cette phase.");
    if (receipt_limit > 1 && receipts == receipt_limit) return 3;
    return signature ? 0 : 3;
}

static int thermal_read(HPMCapture **device, unsigned receipt_limit) {
    puts("Lectures thermiques 0f/11 : aucun Enter/Exit dans cette phase.");
    printf("ACK Dell 0x13 : plafond de %u envoi(s) par requête, observation pendant 3 secondes.\n", receipt_limit);
    const uint8_t opcodes[] = {0x0f, 0x11};
    for (unsigned q = 0; q < sizeof(opcodes); q++) {
        Capture reply;
        int result = thermal_exchange(device, opcodes[q], 0, receipt_limit, &reply);
        if (result) return result;
    }
    puts("Lectures terminées. Aucune consigne de ventilateur envoyée.");
    return 0;
}

static int thermal_set_mode(HPMCapture **device, unsigned setting) {
    unsigned mode = setting != THERMAL_AUTOMATIC;
    Capture status, profile;
    int written = thermal_exchange(device, 0x10, setting, 16, &status);
    int readback = thermal_exchange(device, 0x0f, 0, 16, &profile);
    if (written || status.data[6] != 1 || readback || profile.data[7] != mode) {
        fprintf(stderr, "Mode %u non confirmé par succès EC et relecture.\n", mode);
        return 1;
    }
    printf("Succès EC reçu ; mode %u (%s) confirmé par relecture.\n",
           mode, mode ? "consigne forcée" : "automatique");
    return 0;
}

static volatile sig_atomic_t stop_requested;

static void request_stop(int signo) {
    (void)signo;
    stop_requested = 1;
}

static bool temperatures_allow_test(const Capture *c, unsigned setting) {
    if (setting != THERMAL_LOW && setting != THERMAL_HIGH && setting != THERMAL_STOP) return false;
    if (!thermal_signature(c, 0x11)) return false;
    // EC03 type-8 curves request more than 1900 RPM at these rising thresholds.
    const int low_limits[] = {62, 70, 73};
    // Experimental stop guardrails, not a validated passive-cooling envelope.
    const int stop_limits[] = {40, 45, 66};
    const int *limits = setting == THERMAL_STOP ? stop_limits : low_limits;
    for (unsigned i = 0; i < 3; i++) {
        int temperature = (int8_t)c->data[7 + i];
        if (temperature < 0 || temperature >= limits[i]) return false;
    }
    return true;
}

static bool automatic_fan_running(const Capture *c) {
    return thermal_signature(c, 0x0f) && c->data[7] == 0 &&
           (c->data[8] == 1 || c->data[8] == 2);
}

static bool valid_test_duration(unsigned setting, unsigned duration) {
    return setting == THERMAL_STOP ? duration == 9 || duration == 60 :
           (setting == THERMAL_LOW || setting == THERMAL_HIGH) && duration == 12;
}

static int thermal_speed_test(HPMCapture **device, unsigned setting, unsigned duration) {
    if (!valid_test_duration(setting, duration)) return 1;
    printf("Essai %u tr/min, environ %u secondes, puis retour automatique vérifié.\n",
           setting == THERMAL_STOP ? 0 : setting == THERMAL_HIGH ? 3600 : 1900, duration);
    if (setting == THERMAL_STOP)
        puts("Arrêt expérimental bref. Limites locale/distante/module : 40/45/66 °C. Aucun mode silencieux permanent.");
    puts("Ctrl-C demande le retour automatique ; laisser le processus terminer.");
    Capture profile, temperatures;
    if (thermal_exchange(device, 0x0f, 0, 16, &profile) || profile.data[7] != 0 ||
        profile.data[8] > 1 || thermal_exchange(device, 0x11, 0, 16, &temperatures) ||
        !temperatures_allow_test(&temperatures, setting) || stop_requested) {
        fputs("Essai annulé avant écriture : mode, vitesse, températures ou interruption.\n", stderr);
        return 1;
    }
    int initial[3], peaks[3];
    for (unsigned i = 0; i < 3; i++) initial[i] = peaks[i] = (int8_t)temperatures.data[7 + i];
    uint64_t started = milliseconds(), deadline = started + duration * 1000;
    int result = thermal_set_mode(device, setting);
    bool high_observed = false;
    while (!result && !stop_requested && milliseconds() < deadline) {
        result = thermal_exchange(device, 0x11, 0, 16, &temperatures);
        if (!result) {
            printf("Suivi thermique +%llums (fin de lecture) : %d / %d / %d °C\n",
                   (unsigned long long)(milliseconds() - started),
                   (int8_t)temperatures.data[7], (int8_t)temperatures.data[8], (int8_t)temperatures.data[9]);
            for (unsigned i = 0; i < 3; i++)
                if ((int8_t)temperatures.data[7 + i] > peaks[i]) peaks[i] = (int8_t)temperatures.data[7 + i];
        }
        if (!result && !temperatures_allow_test(&temperatures, setting)) {
            fputs("Seuil de température atteint : retour automatique.\n", stderr);
            result = 1;
        }
        if (!result && !stop_requested && setting == THERMAL_HIGH) {
            result = thermal_exchange(device, 0x0f, 0, 16, &profile);
            if (!result && profile.data[7] == 1 && profile.data[8] == 2) {
                high_observed = true;
                puts("Classe mesurée 2 : vitesse >= 2750 tr/min observée pendant le mode forcé.");
            }
        }
    }
    // Always attempt restoration after any attempted write, even a failed/ambiguous send.
    // Signals only set a flag; restoration must finish before this process exits.
    printf("Bilan thermique — départ : %d/%d/%d °C ; maxima observés : %d/%d/%d °C\n",
           initial[0], initial[1], initial[2], peaks[0], peaks[1], peaks[2]);
    puts("Retour en mode automatique…");
    if (thermal_set_mode(device, 0)) {
        fputs("RETOUR AUTOMATIQUE NON CONFIRMÉ. Reprendre avec hpm-probe thermal-auto dès que le dock répond.\n", stderr);
        return 1;
    }
    puts("Essai terminé ; retour automatique confirmé.");
    if (setting == THERMAL_STOP) {
        bool spinning = false;
        for (unsigned i = 0; i < 3 && !spinning; i++) {
            if (thermal_exchange(device, 0x0f, 0, 16, &profile)) break;
            spinning = automatic_fan_running(&profile);
        }
        if (!spinning) {
            fputs("Mode automatique restauré, mais rotation non confirmée par les lectures suivantes.\n", stderr);
            result = 1;
        } else puts("Rotation du ventilateur confirmée après restauration automatique.");
    }
    if (setting == THERMAL_HIGH && !high_observed) {
        puts("La montée en classe 2 n'a pas été observée ; effet physique non confirmé.");
        result = 1;
    }
    return stop_requested ? 130 : result;
}

static int thermal_enter_hold(HPMCapture **device) {
    // TI assigns the advertised object positions during Discover Modes.
    puts("Découverte Dell préalable : vérification du VDO 1 avant Enter.");
    if (discover_modes(device, 0x413c, true)) return 1;
    if (check_partner(device)) return 1;
    Capture previous[2];
    for (unsigned a = 0; a < 2; a++)
        if (receive_capture(device, a, &previous[a])) return 1;
    puts("Essai Enter Mode 1 : un seul envoi, puis attente de 3 secondes.");
    puts("Aucun Exit automatique : le mode peut rester engagé jusqu'à la déconnexion.");
    puts("L'effet d'Enter sur les écrans n'est pas encore établi.");
    fflush(stdout);
    const uint8_t enter[] = {0x04, 0xa1, 0x3c, 0x41};
    uint64_t start = milliseconds(), next_check = start;
    IOReturn status = (*device)->SendVDM(device, 0, 0, enter, sizeof(enter), 0);
    printf("Dell Enter Mode 1 : send=0x%08x\n", (unsigned)status);
    fflush(stdout);
    if (status) return 1;
    bool acknowledged = false;
    do {
        for (unsigned a = 0; a < 2; a++) {
            Capture current;
            if (receive_capture(device, a, &current)) goto uncertain;
            if (!same_capture(&current, &previous[a])) {
                print_capture(&current, a, milliseconds() - start);
                if (!a && structured_reply(current.data, current.length, current.sop, le32(enter))) {
                    if (current.length != 4) goto uncertain;
                    unsigned type = (le32(current.data) >> 6) & 3;
                    if (type != 1) {
                        printf("Enter Mode 1 : %s. Aucune lecture thermique ni Exit envoyé.\n",
                               type == 2 ? "NAK (entrée refusée)" : "BUSY (partenaire occupé)");
                        puts("Cette réponse ne précise pas la cause ni l'état du mode avant l'essai.");
                        return 1;
                    }
                    acknowledged = true;
                }
                previous[a] = current;
            }
        }
        if (milliseconds() >= next_check) {
            if (check_partner(device)) goto uncertain;
            next_check = milliseconds() + 250;
        }
        if (!acknowledged && milliseconds() - start >= 1000) goto uncertain;
        usleep(10000);
    } while (milliseconds() - start < 3000);
    puts("Enter ACK reçu ; connexion PD présente après l'attente. Début des lectures.");
    int result = thermal_read(device, 16);
    puts("Fin sans Exit : état du mode non relu, aucune tentative de restauration automatique.");
    return result;
uncertain:
    fputs("Session interrompue : entrée ou connexion non confirmée. Aucun autre envoi.\n"
          "Le mode peut être engagé ; aucun Exit automatique n'est tenté.\n", stderr);
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "self-test")) {
        uint8_t identity[25] = {0x45,0x3c,0x41,0x60,0x4c,0,0,0,0,0x12,7,0x70,0xb0};
        uint8_t ack[] = {0x43,0xa0,0x3c,0x41,1,0,0,0};
        assert(dell_identity(identity, sizeof(identity)));
        assert(!dell_identity(identity, 12));
        identity[0] = 5; assert(!dell_identity(identity, sizeof(identity))); identity[0] = 0x45;
        identity[11] = 0x71; assert(!dell_identity(identity, sizeof(identity)));
        assert(modes_reply(ack, sizeof(ack), 0, 0x413c));
        assert(dell_mode1_reply(ack, sizeof(ack), 0));
        assert(!dell_mode1_reply(ack, 4, 0));
        assert(!dell_mode1_reply(ack, sizeof(ack), 1));
        ack[4] = 2; assert(!dell_mode1_reply(ack, sizeof(ack), 0)); ack[4] = 1;
        ack[0] = 0x83; assert(!dell_mode1_reply(ack, sizeof(ack), 0)); ack[0] = 0x43;
        assert(!modes_reply(ack, sizeof(ack), 1, 0x413c));
        assert(!modes_reply(ack, sizeof(ack), 0, 0xff01));
        assert(!modes_reply(ack, 3, 0, 0x413c));
        ack[0] = 3; assert(!modes_reply(ack, sizeof(ack), 0, 0x413c));
        ack[0] = 0x44; assert(!modes_reply(ack, sizeof(ack), 0, 0x413c));
        ack[1] = 0xa1;
        assert(structured_reply(ack, 4, 0, 0x413ca104));
        assert(!structured_reply(ack, 4, 1, 0x413ca104));
        ack[0] = 0x84; // Exact Enter NAK observed on the WD22TB4.
        assert(structured_reply(ack, 4, 0, 0x413ca104));
        assert(((le32(ack) >> 6) & 3) == 2);
        assert(!structured_reply(ack, 3, 0, 0x413ca104));
        assert(!structured_reply(ack, 4, 0, 0x413ca204));
        ack[0] = 0xc4;
        assert(structured_reply(ack, 4, 0, 0x413ca104));
        assert(((le32(ack) >> 6) & 3) == 3);
        ack[0] = 0x52; assert(!structured_reply(ack, 4, 0, 0x413ca104));
        Capture a = {.length = 4, .counter = 7, .data = {0x52,0xa1,0x3c,0x41}}, b = a;
        assert(same_capture(&a, &b));
        b.counter = 0; assert(!same_capture(&a, &b)); // Counter wraps at 7.
        b = a; b.data[0] = 0x46; assert(!same_capture(&a, &b));
        assert(!thermal_signature(&a, 0x0f)); // Empty ACK is not a thermal reply.
        a = (Capture){.length = 12, .data = {6,0xa1,0x3c,0x41,1,0x81,0x0b,0,1}};
        assert(thermal_signature(&a, 0x0f));
        assert(!thermal_signature(&a, 0x11));
        a.sop = 1; assert(!thermal_signature(&a, 0x0f)); a.sop = 0;
        a.length = 8; assert(!thermal_signature(&a, 0x0f));
        b.length = 65; assert(!same_capture(&b, &b));
        a = (Capture){.length = 28, .data = {6,0xa1,0x3c,0x41,1,0x81,0x0b,0,1,0x40}};
        char decoded[256]; uint8_t receipt[4];
        assert(decode_thermal(&a, 0x0f, decoded, sizeof(decoded)));
        assert(strstr(decoded, "automatique") && strstr(decoded, "0 < vitesse < 2750"));
        assert(thermal_receipt(&a, 0x0f, receipt) && le32(receipt) == 0x413ca113);
        assert(!thermal_receipt(&a, 0x11, receipt));
        assert(!pending_receipt(&a, 0x0f, 0, 0, receipt));
        assert(pending_receipt(&a, 0x0f, 1, 0, receipt));
        assert(!pending_receipt(&a, 0x0f, 1, 1, receipt));
        assert(!pending_receipt(&a, 0x11, 1, 0, receipt));
        assert(pending_receipt(&a, 0x11, 16, 15, receipt)); // Queued profile during temperature read.
        assert(!pending_receipt(&a, 0x11, 16, 16, receipt));
        a.data[6] = 0x0c; a.data[7] = 0x22; a.data[8] = 0x29;
        assert(decode_thermal(&a, 0x11, decoded, sizeof(decoded)));
        assert(!strcmp(decoded, "Températures EC : locale 34 °C ; distante 41 °C ; module 64 °C"));
        assert(thermal_receipt(&a, 0x11, receipt));
        assert(!thermal_signature(&a, 0x10));
        a.sop = 1; assert(!thermal_receipt(&a, 0x11, receipt)); a.sop = 0;
        a.length = 8; assert(!decode_thermal(&a, 0x11, decoded, sizeof(decoded))); a.length = 28;
        a.data[0] = 0x46; assert(!thermal_receipt(&a, 0x11, receipt));
        uint8_t query[28];
        assert(thermal_query(0x10, 0, query));
        const uint8_t automatic[28] = {0x12,0xa1,0x3c,0x41,2,0x10,0,1};
        assert(!memcmp(query, automatic, sizeof(query)));
        assert(thermal_query(0x10, 1, query) && query[6] == 1 && query[7] == 1);
        assert(thermal_query(0x10, THERMAL_HIGH, query));
        const uint8_t high[28] = {0x12,0xa1,0x3c,0x41,2,0x10,1,2};
        assert(!memcmp(query, high, sizeof(query)));
        assert(thermal_query(0x10, THERMAL_STOP, query));
        const uint8_t stopped[28] = {0x12,0xa1,0x3c,0x41,2,0x10,1,0};
        assert(!memcmp(query, stopped, sizeof(query)));
        assert(!thermal_query(0x10, 4, query));
        assert(!thermal_query(0x0f, 1, query));
        assert(thermal_query(0x0f, 0, query) && query[5] == 0x0f && query[7] == 0);
        assert(thermal_query(0x11, 0, query) && query[5] == 0x11 && query[7] == 0);
        assert(!thermal_query(0x12, 0, query));
        a = (Capture){.length = 28, .data = {6,0xa1,0x3c,0x41,1,0x80,1}};
        assert(decode_thermal(&a, 0x10, decoded, sizeof(decoded)) && strstr(decoded, "succès"));
        assert(!thermal_signature(&a, 0x0f) && !thermal_signature(&a, 0x11));
        assert(pending_receipt(&a, 0x10, 16, 0, receipt));
        assert(!pending_receipt(&a, 0x0f, 16, 0, receipt));
        a.data[6] = 4;
        assert(decode_thermal(&a, 0x10, decoded, sizeof(decoded)) && strstr(decoded, "refus"));
        a.length = 4; assert(!thermal_signature(&a, 0x10));
        a = (Capture){.length = 28, .data = {6,0xa1,0x3c,0x41,1,0x81,0x0c,33,35,64}};
        assert(temperatures_allow_test(&a, THERMAL_LOW));
        assert(temperatures_allow_test(&a, THERMAL_STOP));
        assert(!temperatures_allow_test(&a, 4));
        const uint8_t limits[] = {62,70,73};
        for (unsigned i = 0; i < 3; i++) {
            uint8_t original = a.data[7 + i];
            a.data[7 + i] = limits[i] - 1; assert(temperatures_allow_test(&a, THERMAL_LOW));
            a.data[7 + i] = limits[i]; assert(!temperatures_allow_test(&a, THERMAL_LOW));
            a.data[7 + i] = 0xff; assert(!temperatures_allow_test(&a, THERMAL_LOW));
            a.data[7 + i] = original;
        }
        const uint8_t stop_limits[] = {40,45,66};
        for (unsigned i = 0; i < 3; i++) {
            uint8_t original = a.data[7 + i];
            a.data[7 + i] = stop_limits[i] - 1; assert(temperatures_allow_test(&a, THERMAL_STOP));
            a.data[7 + i] = stop_limits[i]; assert(!temperatures_allow_test(&a, THERMAL_STOP));
            a.data[7 + i] = 0xff; assert(!temperatures_allow_test(&a, THERMAL_STOP));
            a.data[7 + i] = original;
        }
        a.data[6] = 0x0b; assert(!temperatures_allow_test(&a, THERMAL_LOW));
        a.data[7] = 0; a.data[8] = 1; assert(automatic_fan_running(&a));
        a.data[8] = 2; assert(automatic_fan_running(&a));
        a.data[8] = 0; assert(!automatic_fan_running(&a));
        a.data[8] = 1; a.data[7] = 1; assert(!automatic_fan_running(&a));
        a.data[7] = 0; a.length = 4; assert(!automatic_fan_running(&a));
        assert(valid_test_duration(THERMAL_STOP, 9));
        assert(valid_test_duration(THERMAL_STOP, 60));
        assert(!valid_test_duration(THERMAL_STOP, 61));
        assert(!valid_test_duration(THERMAL_STOP, 0));
        assert(valid_test_duration(THERMAL_LOW, 12));
        assert(valid_test_duration(THERMAL_HIGH, 12));
        assert(!valid_test_duration(THERMAL_HIGH, 60));
        assert(!valid_test_duration(4, 60));
        puts("OK : identité Dell, réponses, capture double canal et offsets ABI AppleHPM.");
        return 0;
    }
    bool discover = argc == 2 && !strcmp(argv[1], "discover");
    bool thermal = argc == 2 && !strcmp(argv[1], "thermal-read");
    bool thermal_ack = argc == 2 && !strcmp(argv[1], "thermal-read-ack");
    bool thermal_ack_loop = argc == 2 && !strcmp(argv[1], "thermal-read-ack-loop");
    bool automatic = argc == 2 && !strcmp(argv[1], "thermal-auto");
    bool low_test = argc == 2 && !strcmp(argv[1], "thermal-low-test");
    bool high_test = argc == 2 && !strcmp(argv[1], "thermal-high-test");
    bool stop_test = argc == 2 && !strcmp(argv[1], "thermal-stop-test");
    bool silence_test = argc == 2 && !strcmp(argv[1], "thermal-silence-test");
    bool speed_test = low_test || high_test || stop_test || silence_test;
    bool enter_hold = argc == 2 && !strcmp(argv[1], "thermal-enter-hold");
    bool active = discover || thermal || thermal_ack || thermal_ack_loop || automatic || speed_test || enter_hold;
    if (argc == 2 && (!strcmp(argv[1], "thermal-probe") || !strcmp(argv[1], "thermal-session"))) {
        fputs("Essai désactivé après une interruption du dock. Aucun accès matériel effectué.\n", stderr);
        return 2;
    }
    if (argc > 2 || (argc == 2 && !active)) {
        fputs("Usage: hpm-probe [discover | thermal-read | thermal-read-ack | thermal-read-ack-loop | thermal-auto | thermal-low-test | thermal-high-test | thermal-stop-test | thermal-silence-test | thermal-enter-hold | self-test]\n", stderr); return 2;
    }
    if (speed_test && (signal(SIGINT, request_stop) == SIG_ERR ||
                    signal(SIGTERM, request_stop) == SIG_ERR ||
                    signal(SIGHUP, request_stop) == SIG_ERR || signal(SIGPIPE, SIG_IGN) == SIG_ERR)) {
        fputs("Impossible d'installer les gestionnaires d'interruption.\n", stderr);
        return 1;
    }
    // Share the production library lock; no competing VDM sessions.
    if (active) {
        int lock = open("/var/run/dockctl-hpm.lock", O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC, 0600);
        if (lock < 0 || flock(lock, LOCK_EX|LOCK_NB)) {
            fputs("Session HPM déjà active ou verrou inaccessible (sudo requis).\n", stderr);
            if (lock >= 0) close(lock);
            return 1;
        }
        // Kept until process exit, including restoration.
    }
    io_iterator_t iter = 0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault,
                                    IOServiceMatching("AppleHPMARMI2C"), &iter)) return 1;
    io_service_t service;
    int result = 1;
    while ((service = IOIteratorNext(iter))) {
        int rid = -1;
        CFTypeRef property = IORegistryEntryCreateCFProperty(service, CFSTR("RID"), NULL, 0);
        if (property && CFGetTypeID(property) == CFNumberGetTypeID())
            CFNumberGetValue(property, kCFNumberIntType, &rid);
        if (property) CFRelease(property);
        if (active && rid != 2) { IOObjectRelease(service); continue; }
        printf("HPM RID %d\n", rid);
        io_connect_t connection = 0;
        // AppleHPMLib startStatic uses user-client type 42 on this macOS.
        IOReturn opened = IOServiceOpen(service, mach_task_self(), 42, &connection);
        printf("  IOServiceOpen: 0x%08x\n", (unsigned)opened);
        if (!opened) IOServiceClose(connection);
        IOCFPlugInInterface **plugin = NULL;
        HPMCapture **device = NULL;
        SInt32 score = 0;
        IOReturn status = IOCreatePlugInInterfaceForService(service, kAppleHPMLibType,
                                                           kIOCFPlugInInterfaceID, &plugin, &score);
        printf("  Plugin: 0x%08x\n", (unsigned)status);
        if (status == kIOReturnSuccess && plugin) {
            HRESULT hr = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kAppleHPMLibInterface),
                                                   (LPVOID *)&device);
            printf("  Interface: 0x%08x\n", (unsigned)hr);
            if (hr == S_OK && device) {
                printf("  API version: %u.%u\n", (*device)->version, (*device)->revision);
                if (active) {
                    if ((*device)->version == 3 && enter_hold) result = thermal_enter_hold(device);
                    else if ((*device)->version == 3 && automatic) result = thermal_set_mode(device, 0);
                    else if ((*device)->version == 3 && speed_test)
                        result = thermal_speed_test(device,
                            (stop_test || silence_test) ? THERMAL_STOP : high_test ? THERMAL_HIGH : THERMAL_LOW,
                            silence_test ? 60 : stop_test ? 9 : 12);
                    else if ((*device)->version == 3 && (thermal || thermal_ack || thermal_ack_loop))
                        result = thermal_read(device, thermal_ack ? 1 : 16);
                    else if ((*device)->version == 3) {
                        // Positive control: the dock replied to this DP query at connection.
                        discover_modes(device, 0xff01, false);
                        result = discover_modes(device, 0x413c, false);
                    } else result = 1;
                    (*device)->Release(device);
                    IODestroyPlugInInterface(plugin);
                    IOObjectRelease(service);
                    break;
                }
                const uint8_t registers[] = {0x03, 0x3f, 0x21, 0x48, 0x49, 0x4d, 0x4e, 0x4f};
                for (unsigned i = 0; i < sizeof(registers); i++) {
                    uint8_t data[64] = {0};
                    uint64_t length = 0;
                    status = (*device)->Read(device, 0, registers[i], data, sizeof(data), 0, &length);
                    printf("  Reg 0x%02x: status=0x%08x len=%llu", registers[i], (unsigned)status,
                           (unsigned long long)length);
                    if (!status && length <= sizeof(data)) {
                        for (uint64_t j = 0; j < length; j++) printf(" %02x", data[j]);
                        result = 0;
                    }
                    putchar('\n');
                }
                if (rid == 2 && (*device)->version == 3) {
                    for (unsigned attention = 0; attention < 2; attention++) {
                        uint8_t data[64] = {0}, counter = 0;
                        uint32_t sop = 0;
                        uint64_t length = 0;
                        status = attention
                            ? (*device)->ReceiveVDMAttention(device, 0, data, sizeof(data), 0,
                                                            &sop, &counter, &length)
                            : (*device)->ReceiveVDM(device, 0, data, sizeof(data), 0,
                                                   &sop, &counter, &length);
                        printf("  ReceiveVDM%s: status=0x%08x SOP=%u counter=%u len=%llu",
                               attention ? "Attention" : "",
                               (unsigned)status, sop, counter, (unsigned long long)length);
                        if (!status && length <= sizeof(data))
                            for (uint64_t j = 0; j < length; j++) printf(" %02x", data[j]);
                        putchar('\n');
                    }
                }
                (*device)->Release(device);
            }
            IODestroyPlugInInterface(plugin);
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(iter);
    return result;
}
