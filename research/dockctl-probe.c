// SPDX-License-Identifier: MIT
// Dell HID-I2C protocol reference: fwupd/plugins/dell-dock (Dell/Realtek, MIT).
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { REPORT_SIZE = 192, DATA_SIZE = 103, INFO_SIZE = 183 };

static bool request(uint8_t cmd, size_t length, uint8_t out[REPORT_SIZE]) {
    // Only the three known EC read commands are allowed. No arbitrary writes.
    if (!((cmd == 5 && length == 1) || (cmd == 3 && length == DATA_SIZE) ||
          (cmd == 2 && length == INFO_SIZE))) return false;
    memset(out, 0, REPORT_SIZE);
    out[0] = 0x40; out[1] = 0xd6; out[2] = cmd;
    out[6] = (uint8_t)(length + 1);
    out[8] = 0xec; out[9] = 1; out[10] = 0x80;
    return true;
}

static bool valid_reply(const uint8_t *data, size_t received, size_t expected) {
    return received == REPORT_SIZE && data[0] == expected;
}

static bool valid_info(const uint8_t *data) {
    return data[0] > 0 && data[0] <= (INFO_SIZE - 3) / 9;
}

static bool exchange(IOHIDDeviceRef device, uint8_t report[REPORT_SIZE]) {
    IOReturn status = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 0,
                                          report, REPORT_SIZE);
    if (status != kIOReturnSuccess) {
        fprintf(stderr, "SET_REPORT : 0x%08x\n", (unsigned)status);
        return false;
    }
    memset(report, 0, REPORT_SIZE);
    CFIndex received = REPORT_SIZE;
    status = IOHIDDeviceGetReport(device, kIOHIDReportTypeInput, 0, report, &received);
    if (status != kIOReturnSuccess) {
        fprintf(stderr, "GET_REPORT : 0x%08x\n", (unsigned)status);
        return false;
    }
    if (received != REPORT_SIZE) {
        fprintf(stderr, "Rapport HID tronqué : %ld octets.\n", received); return false;
    }
    return true;
}

static bool read_ec(IOHIDDeviceRef device, uint8_t cmd, size_t length, uint8_t *result) {
    uint8_t report[REPORT_SIZE];
    if (!request(cmd, length, report) || !exchange(device, report)) return false;
    if (!valid_reply(report, REPORT_SIZE, length)) {
        fprintf(stderr, "Réponse EC invalide : taille annoncée %u, attendue %zu.\n",
                report[0], length);
        return false;
    }
    memcpy(result, report + 1, length);
    return true;
}

// Experimental: only read AMC6821 identification registers at its documented addresses.
static bool amc_request(uint8_t address, uint8_t reg, uint8_t out[REPORT_SIZE]) {
    const uint8_t addresses[] = {0x18,0x19,0x1a,0x2c,0x2d,0x2e,0x4c,0x4d,0x4e};
    if (!memchr(addresses, address, sizeof(addresses)) || (reg != 0x3d && reg != 0x3e))
        return false;
    memset(out, 0, REPORT_SIZE);
    out[0] = 0x40; out[1] = 0xd6; out[2] = reg; out[6] = 1;
    out[8] = address << 1; out[9] = 1; out[10] = 0x80;
    return true;
}

static bool probe_fan(IOHIDDeviceRef device) {
    const uint8_t addresses[] = {0x18,0x19,0x1a,0x2c,0x2d,0x2e,0x4c,0x4d,0x4e};
    bool found = false;
    for (size_t i = 0; i < sizeof(addresses); i++) {
        uint8_t report[REPORT_SIZE], id;
        if (!amc_request(addresses[i], 0x3d, report) || !exchange(device, report)) return false;
        id = report[0];
        if (!amc_request(addresses[i], 0x3e, report) || !exchange(device, report)) return false;
        printf("I2C 0x%02x : device=0x%02x company=0x%02x%s\n", addresses[i], id, report[0],
               id == 0x21 && report[0] == 0x49 ? " — signature AMC6821" : "");
        found |= id == 0x21 && report[0] == 0x49;
    }
    puts(found ? "Signature trouvée ; télémétrie et contrôle restent à valider."
               : "Aucune signature AMC6821 trouvée via ce pont. Cela ne prouve pas son absence.");
    return true;
}

static unsigned le16(const uint8_t *data) { return data[0] | ((unsigned)data[1] << 8); }

static void print_text(const char *label, const uint8_t *data, size_t length) {
    printf("%s : ", label);
    for (size_t i = 0; i < length && data[i]; i++)
        putchar(data[i] >= 32 && data[i] <= 126 ? data[i] : '?');
    putchar('\n');
}

static void print_version(const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; i++) printf("%s%02x", i ? "." : "", data[i]);
}

static bool known_thermal_ec(const uint8_t *entry) {
    return entry[0] == 0 && entry[1] == 0 && entry[4] == 0 &&
           entry[5] == 1 && entry[6] == 1 && entry[7] == 0 &&
           (entry[8] == 0x03 || entry[8] == 0x16);
}

static bool show_info(IOHIDDeviceRef device) {
    uint8_t type, data[DATA_SIZE], info[INFO_SIZE];
    if (!read_ec(device, 5, 1, &type)) return false;
    if (type != 4) {
        fprintf(stderr, "Base non prise en charge : 0x%02x (attendu : WD19/WD22, 0x04).\n", type);
        return false;
    }
    if (!read_ec(device, 3, sizeof(data), data) || !read_ec(device, 2, sizeof(info), info))
        return false;
    if (!valid_info(info)) { fputs("Table des composants invalide.\n", stderr); return false; }
    if (data[1] != type || le16(data + 4) != 8) {
        fputs("Module non pris en charge : WD22TB4 (type 8) requis.\n", stderr); return false;
    }
    print_text("Modèle", data + 39, 64);
    print_text("Service Tag", data + 32, 7);
    printf("Alimentation déclarée par le dock : %u W\nCarte : %u\nModule : %u\n",
           le16(data + 2), le16(data + 6), le16(data + 4));
    printf("Package firmware : ");
    print_version(data + 12, 4); putchar('\n');
    printf("État ports (brut) : 0x%04x / 0x%04x\n", le16(data + 8), le16(data + 10));
    bool thermal_status_known = false;
    for (unsigned i = 0; i < info[0]; i++) {
        const uint8_t *entry = info + 3 + i * 9, *version = entry + 5;
        thermal_status_known |= known_thermal_ec(entry);
        const char *name;
        size_t offset = 0, count = 4;
        switch (entry[1]) {
            case 0: name = "Contrôleur EC"; break;
            case 1: name = "Power Delivery"; break;
            case 3: name = entry[2] == 0 ? "Hub USB Gen2" : "Hub USB Gen1"; break;
            case 4: name = "DisplayPort MST"; offset = 1; count = 3; break;
            case 5: name = "Thunderbolt"; offset = 2; count = 2; break;
            default: name = "Composant inconnu";
        }
        printf("%s (type %u, %s, instance %u) : ", name, entry[1],
               entry[0] == 0 ? "base" : "module", entry[4]);
        print_version(version + offset, count); putchar('\n');
    }
    if (thermal_status_known) {
        for (unsigned port = 0; port < 2; port++)
            printf("Prérequis thermique EC, port %u : bit 0x0008 %s\n", port,
                   le16(data + 8 + port * 2) & 8 ? "présent (condition nécessaire)" :
                                                              "absent (commandes bloquées)");
    }
    puts("Lectures thermiques via Power Delivery : research/hpm-probe ; aucun réglage via USB HID établi.");
    return true;
}

static void self_test(void) {
    uint8_t report[REPORT_SIZE], info[INFO_SIZE] = {0};
    assert(request(2, INFO_SIZE, report));
    const uint8_t prefix[] = {0x40,0xd6,2,0,0,0,184,0,0xec,1,0x80};
    assert(memcmp(report, prefix, sizeof(prefix)) == 0);
    for (size_t i = sizeof(prefix); i < sizeof(report); i++) assert(report[i] == 0);
    assert(request(3, DATA_SIZE, report) && report[6] == 104);
    assert(request(5, 1, report) && report[6] == 2);
    assert(!request(0x0b, 1, report)); // EC reset must never be sent.
    assert(!request(2, REPORT_SIZE, report));
    report[0] = INFO_SIZE;
    assert(valid_reply(report, REPORT_SIZE, INFO_SIZE));
    assert(!valid_reply(report, 0, INFO_SIZE));
    assert(!valid_reply(report, REPORT_SIZE, DATA_SIZE));
    assert(!valid_info(info)); info[0] = 20; assert(valid_info(info));
    info[0] = 21; assert(!valid_info(info));
    const uint8_t endian[] = {0xb4,0}; assert(le16(endian) == 180);
    uint8_t ec[] = {0,0,0,0,0,1,1,0,3};
    assert(known_thermal_ec(ec)); ec[8] = 0x16; assert(known_thermal_ec(ec));
    ec[8] = 0x17; assert(!known_thermal_ec(ec));
    ec[8] = 3; ec[0] = 1; assert(!known_thermal_ec(ec));
    ec[0] = 0; ec[1] = 1; assert(!known_thermal_ec(ec));
    const uint8_t closed[] = {0x60,0}, open[] = {0x68,0};
    assert(!(le16(closed) & 8) && (le16(open) & 8));
    assert(amc_request(0x18, 0x3d, report) && report[8] == 0x30 && report[6] == 1);
    assert(amc_request(0x4e, 0x3e, report) && report[8] == 0x9c);
    assert(!amc_request(0x76, 0x3d, report));
    assert(!amc_request(0x18, 0x22, report)); // No PWM register access.
    puts("OK : trames, commandes autorisées et validation des réponses.");
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "self-test") == 0) { self_test(); return 0; }
    if (argc != 2 || (strcmp(argv[1], "info") && strcmp(argv[1], "list") &&
                     strcmp(argv[1], "fan-probe"))) {
        fputs("Usage : dockctl list | info | fan-probe | self-test\n", stderr); return 2;
    }
    int vendor = 0x413c, product = 0xb06e;
    CFNumberRef vid = CFNumberCreate(NULL, kCFNumberIntType, &vendor);
    CFNumberRef pid = CFNumberCreate(NULL, kCFNumberIntType, &product);
    const void *keys[] = {CFSTR(kIOHIDVendorIDKey), CFSTR(kIOHIDProductIDKey)};
    const void *values[] = {vid, pid};
    CFDictionaryRef match = CFDictionaryCreate(NULL, keys, values, 2,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    IOHIDManagerRef manager = IOHIDManagerCreate(NULL, kIOHIDOptionsTypeNone);
    IOHIDManagerSetDeviceMatching(manager, match);
    CFRelease(match); CFRelease(vid); CFRelease(pid);
    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    CFIndex count = devices ? CFSetGetCount(devices) : 0;
    int exit_code = 1;
    if (count != 1) fprintf(stderr, "Attendu : un dock HID 413c:b06e ; détecté : %ld.\n", count);
    else {
        const void *value;
        CFSetGetValues(devices, &value);
        IOHIDDeviceRef device = (IOHIDDeviceRef)value;
        if (strcmp(argv[1], "list") == 0) {
            puts("Dock HID Dell détecté : 413c:b06e"); exit_code = 0;
        } else {
            IOReturn status = IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone);
            if (status != kIOReturnSuccess) {
                fprintf(stderr, "Ouverture HID refusée : 0x%08x\n", (unsigned)status);
                if (status == kIOReturnNotPermitted)
                    fputs("kIOReturnNotPermitted : l'accès HID est bloqué dans ce contexte.\n"
                          "Essayez la même commande dans votre Terminal macOS.\n", stderr);
            }
            else {
                exit_code = (strcmp(argv[1], "fan-probe") == 0 ? probe_fan(device)
                                                               : show_info(device)) ? 0 : 1;
                IOHIDDeviceClose(device, 0);
            }
        }
    }
    if (devices) CFRelease(devices);
    CFRelease(manager);
    return exit_code;
}
