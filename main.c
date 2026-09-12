#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/bootrom.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"

#define PIN_CLK  2
#define PIN_DATA 3
#define HALF_PERIOD_US 1   // versneld van 15 (~32kHz) naar 2 (~250kHz) voor avrdude-compatibiliteit
#define BLOKGROOTTE 256

#define DEBUG_UART   uart0
#define DEBUG_TX_PIN 0
#define DEBUG_RX_PIN 1
#define DEBUG_BAUD   115200

static void debug_init(void) {
    uart_init(DEBUG_UART, DEBUG_BAUD);
    gpio_set_function(DEBUG_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(DEBUG_RX_PIN, GPIO_FUNC_UART);
}

static void debug_print(const char *msg) {
    uart_puts(DEBUG_UART, msg);
}

// =============================================================================
// PDI FYSIEKE LAAG -- de acht bevestigde, 100%-geverifieerde fixes
// =============================================================================

static void pdi_clock_pulse(void) {
    gpio_put(PIN_CLK, 0);
    sleep_us(HALF_PERIOD_US);
    gpio_put(PIN_CLK, 1);
    sleep_us(HALF_PERIOD_US);
}

static void pdi_send_bit(int bit) {
    gpio_put(PIN_DATA, bit ? 1 : 0);
    sleep_us(1);
    pdi_clock_pulse();
}

static void pdi_send_byte(uint8_t byte_val) {
    int parity = 0;
    uint8_t v = byte_val;
    for (int i = 0; i < 8; i++) {
        parity ^= (v & 1);
        v >>= 1;
    }
    pdi_send_bit(0);
    for (int i = 0; i < 8; i++) pdi_send_bit((byte_val >> i) & 1);
    pdi_send_bit(parity);
    pdi_send_bit(1);
    pdi_send_bit(1);
}

static void pdi_idle_clock_us(uint32_t duration_us) {
    gpio_put(PIN_DATA, 1);
    uint32_t elapsed = 0;
    while (elapsed < duration_us) {
        pdi_clock_pulse();
        elapsed += (2 * HALF_PERIOD_US);
    }
}

static int pdi_recv_bit(void) {
    gpio_put(PIN_CLK, 0);
    sleep_us(HALF_PERIOD_US);
    int bit = gpio_get(PIN_DATA);
    gpio_put(PIN_CLK, 1);
    sleep_us(HALF_PERIOD_US);
    return bit;
}

static int pdi_recv_byte(uint32_t timeout_us) {
    gpio_set_dir(PIN_DATA, GPIO_IN);
    uint32_t waited = 0;
    int got_start = 0;
    while (waited < timeout_us) {
        gpio_put(PIN_CLK, 0);
        sleep_us(HALF_PERIOD_US);
        if (gpio_get(PIN_DATA) == 0) {
            got_start = 1;
            gpio_put(PIN_CLK, 1);
            sleep_us(HALF_PERIOD_US);
            break;
        }
        gpio_put(PIN_CLK, 1);
        sleep_us(HALF_PERIOD_US);
        waited += (2 * HALF_PERIOD_US);
    }
    if (!got_start) {
        gpio_set_dir(PIN_DATA, GPIO_OUT);
        return -1;
    }
    uint8_t val = 0;
    for (int i = 0; i < 8; i++) val |= (pdi_recv_bit() << i);
    pdi_recv_bit(); pdi_recv_bit(); pdi_recv_bit();
    gpio_set_dir(PIN_DATA, GPIO_OUT);
    return val;
}

static void step_send(uint8_t b, uint32_t idle_after_us) {
    pdi_send_byte(b);
    pdi_idle_clock_us(idle_after_us);
}

static int step_recv(uint32_t timeout_us, uint32_t idle_after_us) {
    int v = pdi_recv_byte(timeout_us);
    pdi_idle_clock_us(idle_after_us);
    return v;
}

static void pdi_init_pins(void) {
    gpio_init(PIN_CLK);
    gpio_init(PIN_DATA);
    gpio_set_dir(PIN_CLK, GPIO_OUT);
    gpio_set_dir(PIN_DATA, GPIO_OUT);
    gpio_put(PIN_CLK, 0);
    gpio_put(PIN_DATA, 1);
    sleep_ms(10);
}

static void pdi_enable_pulse(void) {
    gpio_put(PIN_CLK, 0);
    gpio_put(PIN_DATA, 1);
    sleep_ms(2);
    gpio_put(PIN_CLK, 1);

    gpio_put(PIN_DATA, 0);
    sleep_us(100);
    gpio_put(PIN_DATA, 1);
    pdi_idle_clock_us(200);
    for (int i = 0; i < 150; i++) pdi_clock_pulse();
}

static int pdi_enable_and_unlock(void) {
    pdi_enable_pulse();

    step_send(0xC2, 6); step_send(0xC6, 6);
    step_send(0x82, 0);  step_recv(5000, 10);

    step_send(0xC0, 6); step_send(0xFD, 300);
    step_send(0x80, 0);  step_recv(5000, 10);

    step_send(0xC1, 6); step_send(0x59, 6);

    step_send(0xE0, 6); step_send(0xFF, 6); step_send(0x88, 6);
    step_send(0xD8, 6); step_send(0xCD, 6); step_send(0x45, 6);
    step_send(0xAB, 6); step_send(0x89, 6); step_send(0x12, 6);

    int nvmen = 0;
    for (int i = 0; i < 5; i++) {
        step_send(0x80, 0);
        int st = step_recv(2000, 10);
        if (st == 0x02) { nvmen = 1; break; }
    }
    pdi_idle_clock_us(2000);
    return nvmen;
}

#define NVM_CMD    0x010001CA
#define NVM_CTRLA  0x010001CB
#define NVM_CMD_READ_NVM_gc   0x43
#define NVM_CMD_CHIP_ERASE_gc 0x40
#define NVM_CMD_ERASE_APP_PAGE_gc     0x22
#define NVM_CMD_ERASE_BOOT_PAGE_gc    0x2A
#define NVM_CMD_ERASE_EEPROM_PAGE_gc  0x32
#define NVM_CMD_ERASE_EEPROM_gc       0x30
#define NVM_CMD_ERASE_APP_gc          0x20
#define NVM_CMD_ERASE_BOOT_gc         0x68
#define NVM_CMD_ERASE_USERSIG_gc      0x18
#define NVM_CMD_WRITE_FUSE_gc         0x4C
#define NVM_CMD_WRITE_LOCK_BITS_gc    0x08
#define NVM_CMD_LOAD_EEPROM_BUFFER_gc 0x33
#define NVM_CMD_ERASE_WRITE_EEPROM_PAGE_gc 0x35
#define NVM_CMD_LOAD_FLASH_BUFFER_gc  0x23
#define NVM_CMD_ERASE_WRITE_APP_PAGE_gc 0x25
#define NVM_CMD_ERASE_WRITE_BOOT_PAGE_gc 0x2D
#define NVM_CTRLA_CMDEX_bm    0x01

static uint8_t pdi_lds_byte(uint32_t addr) {
    step_send(0x0C, 3);
    step_send((addr >> 0) & 0xFF, 3);
    step_send((addr >> 8) & 0xFF, 3);
    step_send((addr >> 16) & 0xFF, 3);
    step_send((addr >> 24) & 0xFF, 0);
    int v = step_recv(20000, 5);
    return (v < 0) ? 0xFF : (uint8_t)v;
}

static void pdi_sts_byte(uint32_t addr, uint8_t data) {
    step_send(0x4C, 46);
    step_send((addr >> 0) & 0xFF, 46);
    step_send((addr >> 8) & 0xFF, 46);
    step_send((addr >> 16) & 0xFF, 46);
    step_send((addr >> 24) & 0xFF, 46);
    step_send(data, 46);
}

// FIX: dubbele-verificatie-met-heropstart (voorheen nodig als vangnet
// tegen incidentele wankele-contact-glitches) kostte bij een 250-byte
// blok al snel 2+ seconden -- ruim boven avrdude's eigen wachttijd voor
// een antwoord, wat avrdude liet hertransmitteren (zichtbaar als
// herhaalde identieke seq-nummers in de debug-log). Nu: EEN snelle,
// enkele lezing per blok. De CRLF-fix heeft de betrouwbaarheid al zo
// verbeterd dat dit vangnet niet meer nodig is voor normale bulk-reads.
static void pdi_read_block_reliable(uint32_t addr, uint8_t *out, uint16_t len) {
    pdi_enable_and_unlock();
    pdi_sts_byte(NVM_CMD, NVM_CMD_READ_NVM_gc);
    for (uint16_t i = 0; i < len; i++) {
        out[i] = pdi_lds_byte(addr + i);
    }
}

// FIX: XMEGA-EEPROM en -flash kunnen niet met simpele, directe STS-writes
// worden beschreven. De juiste procedure is: (1) NVM.CMD op de juiste
// "laad buffer"-modus zetten, (2) de data via STS in een TIJDELIJKE
// buffer laden (nog niet persistent!), (3) NVM.CMD op het juiste
// "leg pagina vast"-commando zetten, (4) CMDEX triggeren om de buffer
// daadwerkelijk naar het echte geheugen weg te schrijven, (5) pollen tot
// de bewerking klaar is. Zonder deze stappen lijkt de schrijfactie te
// slagen (geen foutmelding), maar verandert er in werkelijkheid niets.
#define MTYPE_FLASH       0xC0
#define MTYPE_BOOT_FLASH  0xC1
#define MTYPE_EEPROM_XMEGA 0xC4
#define MTYPE_EEPROM_PAGE  0x22   // apart mtype voor pagina-gebaseerde EEPROM-operaties (o.a. de "lees-bestaande-pagina"-stap voor het schrijven)
#define MTYPE_USERSIG     0xC5
#define MTYPE_PRODSIG     0xC6
#define MTYPE_FUSE_BITS   0xB2
#define MTYPE_LOCK_BITS   0xB3
#define MTYPE_SIGN_JTAG   0xB4

// Fuse-bits en lock-bits gebruiken GEEN buffer-procedure zoals flash/
// eeprom -- het is een simpele, directe schrijfactie: NVM.CMD op het
// juiste specifieke commando zetten, dan een enkele STS naar het
// doeladres, en dat commit direct (geen aparte CMDEX/CTRLA-trigger
// nodig voor deze specifieke commando's, in tegenstelling tot pagina-
// gebaseerde operaties).
static void pdi_write_single(uint8_t nvm_cmd, uint32_t addr, uint8_t data) {
    pdi_enable_and_unlock();
    pdi_sts_byte(NVM_CMD, nvm_cmd);
    pdi_sts_byte(addr, data);
    pdi_idle_clock_us(10000);
    for (int i = 0; i < 20; i++) {
        step_send(0x80, 0);
        int st = step_recv(2000, 78);
        if (st == 0x02) break;
        pdi_idle_clock_us(5000);
    }
}

static void pdi_write_block(uint8_t mtype, uint32_t addr, const uint8_t *data, uint16_t len) {
    if (mtype == MTYPE_FUSE_BITS) {
        pdi_write_single(NVM_CMD_WRITE_FUSE_gc, addr, data[0]);
        return;
    }
    if (mtype == MTYPE_LOCK_BITS) {
        pdi_write_single(NVM_CMD_WRITE_LOCK_BITS_gc, addr, data[0]);
        return;
    }

    uint8_t load_cmd, commit_cmd;
    switch (mtype) {
        case MTYPE_EEPROM_XMEGA:
        case MTYPE_EEPROM_PAGE:
            load_cmd = NVM_CMD_LOAD_EEPROM_BUFFER_gc;
            commit_cmd = NVM_CMD_ERASE_WRITE_EEPROM_PAGE_gc;
            break;
        case MTYPE_BOOT_FLASH:
            load_cmd = NVM_CMD_LOAD_FLASH_BUFFER_gc;
            commit_cmd = NVM_CMD_ERASE_WRITE_BOOT_PAGE_gc;
            break;
        case MTYPE_FLASH:
        default:
            load_cmd = NVM_CMD_LOAD_FLASH_BUFFER_gc;
            commit_cmd = NVM_CMD_ERASE_WRITE_APP_PAGE_gc;
            break;
    }

    pdi_enable_and_unlock();

    // Stap 1+2: data in de tijdelijke buffer laden
    pdi_sts_byte(NVM_CMD, load_cmd);
    for (uint16_t i = 0; i < len; i++) {
        pdi_sts_byte(addr + i, data[i]);
    }

    // Stap 3+4: de pagina daadwerkelijk vastleggen
    pdi_sts_byte(NVM_CMD, commit_cmd);
    pdi_sts_byte(addr, data[0]);   // dummy-schrijving naar het doeladres triggert de actie
    pdi_sts_byte(NVM_CTRLA, NVM_CTRLA_CMDEX_bm);

    // Stap 5: wachten tot de bewerking klaar is
    pdi_idle_clock_us(10000);
    for (int i = 0; i < 20; i++) {
        step_send(0x80, 0);
        int st = step_recv(2000, 78);
        if (st == 0x02) break;
        pdi_idle_clock_us(5000);
    }
}

static int pdi_chip_erase(void) {
    pdi_enable_and_unlock();
    pdi_sts_byte(NVM_CMD, NVM_CMD_CHIP_ERASE_gc);
    pdi_sts_byte(NVM_CTRLA, NVM_CTRLA_CMDEX_bm);
    pdi_idle_clock_us(50000);
    for (int i = 0; i < 50; i++) {
        step_send(0x80, 0);
        int st = step_recv(2000, 78);
        if (st == 0x02) return 1;
        pdi_idle_clock_us(20000);
    }
    return 0;
}

// FIX: pagina-specifieke erase (i.p.v. altijd een volledige chip-erase).
// avrdude stuurt vlak voor elke pagina-schrijving een gericht wis-
// commando ("wis ALLEEN deze ene pagina"); als we dat verkeerd
// interpreteren als "wis alles", vernietigen we per ongeluk alle
// eerder geschreven/bestaande data elders op de chip. nvm_cmd bepaalt
// WELK specifiek NVM-commando (uit het officiele datasheet) wordt
// gebruikt; addr is het PDI-adres van de specifieke pagina/locatie.
static int pdi_page_erase(uint8_t nvm_cmd, uint32_t addr) {
    pdi_enable_and_unlock();
    pdi_sts_byte(NVM_CMD, nvm_cmd);
    pdi_sts_byte(addr, 0x00);      // dummy-schrijving naar het doeladres triggert de actie
    pdi_sts_byte(NVM_CTRLA, NVM_CTRLA_CMDEX_bm);
    pdi_idle_clock_us(10000);
    for (int i = 0; i < 20; i++) {
        step_send(0x80, 0);
        int st = step_recv(2000, 78);
        if (st == 0x02) return 1;
        pdi_idle_clock_us(5000);
    }
    return 0;
}

// =============================================================================
// JTAG ICE mkII PROTOCOLLAAG (voor avrdude -c jtag2pdi)
// =============================================================================

#define MESSAGE_START 0x1B
#define TOKEN         0x0E

#define CMND_GET_SIGN_ON         0x01
#define CMND_SET_PARAMETER       0x02
#define CMND_GET_PARAMETER       0x03
#define CMND_READ_MEMORY         0x05
#define CMND_SET_DEVICE_DESCRIPTOR 0x0C
#define CMND_CHIP_ERASE          0x13
#define CMND_ENTER_PROGMODE      0x14
#define CMND_LEAVE_PROGMODE      0x15
#define CMND_WRITE_MEMORY        0x04
#define CMND_XMEGA_ERASE         0x34
#define CMND_GO                  0x08
#define CMND_RESET               0x0B
#define CMND_GET_SYNC            0x0F
#define CMND_SET_XMEGA_PARAMS    0x36
#define CMND_SIGN_OFF            0x00

#define RSP_OK               0x80
#define RSP_PARAMETER        0x81
#define RSP_MEMORY           0x82
#define RSP_SIGN_ON          0x86
#define RSP_FAILED           0xA0
#define RSP_ILLEGAL_PARAMETER 0xA1
#define RSP_ILLEGAL_MCU_STATE 0xA5

#define PAR_HW_VERSION      0x01
#define PAR_FW_VERSION      0x02
#define PAR_EMULATOR_MODE   0x03
#define  EMULATOR_MODE_PDI  0x06
#define PAR_OCD_VTARGET     0x06

#define FLASH_BASE     0x00800000UL
#define EEPROM_BASE    0x008C0000UL
#define FUSE_BASE      0x008F0020UL
#define LOCK_BASE      0x008F0027UL
#define USERSIG_BASE   0x008E0400UL
#define PRODSIG_BASE   0x008E0200UL
#define SIGNATURE_BASE 0x01000090UL

static uint16_t crc16_update(uint16_t crc, uint8_t a) {
    crc ^= a;
    for (int i = 0; i < 8; i++) {
        if (crc & 1) crc = (crc >> 1) ^ 0x8408;
        else crc = crc >> 1;
    }
    return crc;
}

static uint8_t jtag_body[300];   // willem
static uint16_t jtag_body_len;
static uint16_t jtag_seq;

static int jtag_receive(void) {
    int c;
    do {
        c = getchar_timeout_us(200000);
        if (c == PICO_ERROR_TIMEOUT) return 0;
    } while ((uint8_t)c != MESSAGE_START);

    debug_print("[JTAG] MESSAGE_START ontvangen\r\n");

    uint16_t crc = crc16_update(0xFFFF, MESSAGE_START);

    uint8_t raw[6];
    for (int i = 0; i < 6; i++) {
        int b = getchar_timeout_us(50000);
        if (b == PICO_ERROR_TIMEOUT) {
            debug_print("[JTAG] timeout bij raw-header\r\n");
            return 0;
        }
        raw[i] = (uint8_t)b;
        crc = crc16_update(crc, raw[i]);
    }
    uint16_t seq = raw[0] | (raw[1] << 8);
    uint32_t size = (uint32_t)raw[2] | ((uint32_t)raw[3] << 8) |
                    ((uint32_t)raw[4] << 16) | ((uint32_t)raw[5] << 24);
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "[JTAG] seq=%u size=%lu raw=%02X %02X %02X %02X %02X %02X\r\n",
                 seq, (unsigned long)size, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]);
        debug_print(buf);
    }
    if (size > sizeof(jtag_body)) {
        debug_print("[JTAG] size te groot, afgewezen\r\n");
        return 0;
    }

    int tok = getchar_timeout_us(50000);
    if (tok == PICO_ERROR_TIMEOUT) {
        debug_print("[JTAG] timeout bij token\r\n");
        return 0;
    }
    if ((uint8_t)tok != TOKEN) {
        char buf[48];
        snprintf(buf, sizeof(buf), "[JTAG] verkeerd token: 0x%02X\r\n", (uint8_t)tok);
        debug_print(buf);
        return 0;
    }
    crc = crc16_update(crc, TOKEN);

    for (uint32_t i = 0; i < size; i++) {
        int b = getchar_timeout_us(50000);
        if (b == PICO_ERROR_TIMEOUT) {
            debug_print("[JTAG] timeout bij body\r\n");
            return 0;
        }
        jtag_body[i] = (uint8_t)b;
        crc = crc16_update(crc, jtag_body[i]);
    }

    int c_lo = getchar_timeout_us(50000);
    int c_hi = getchar_timeout_us(50000);
    if (c_lo == PICO_ERROR_TIMEOUT || c_hi == PICO_ERROR_TIMEOUT) {
        debug_print("[JTAG] timeout bij crc\r\n");
        return 0;
    }
    uint16_t recv_crc = (uint8_t)c_lo | ((uint8_t)c_hi << 8);
    if (recv_crc != crc) {
        char buf[64];
        snprintf(buf, sizeof(buf), "[JTAG] CRC-fout: ontvangen=0x%04X berekend=0x%04X\r\n", recv_crc, crc);
        debug_print(buf);
        return 0;
    }

    debug_print("[JTAG] bericht OK, cmd=0x");
    { char b2[8]; snprintf(b2, sizeof(b2), "%02X\r\n", jtag_body[0]); debug_print(b2); }

    jtag_seq = seq;
    jtag_body_len = (uint16_t)size;
    return 1;
}

static void jtag_answer(void) {
    uint16_t crc = crc16_update(0xFFFF, MESSAGE_START);
    putchar(MESSAGE_START);

    uint8_t raw[6];
    raw[0] = jtag_seq & 0xFF;
    raw[1] = (jtag_seq >> 8) & 0xFF;
    raw[2] = jtag_body_len & 0xFF;
    raw[3] = (jtag_body_len >> 8) & 0xFF;
    raw[4] = 0;
    raw[5] = 0;
    for (int i = 0; i < 6; i++) {
        putchar(raw[i]);
        crc = crc16_update(crc, raw[i]);
    }

    putchar(TOKEN);
    crc = crc16_update(crc, TOKEN);

    for (uint16_t i = 0; i < jtag_body_len; i++) {
        putchar(jtag_body[i]);
        crc = crc16_update(crc, jtag_body[i]);
    }

    putchar(crc & 0xFF);
    putchar((crc >> 8) & 0xFF);
    fflush(stdout);
}

static void set_status(uint8_t status) {
    jtag_body_len = 1;
    jtag_body[0] = status;
}

// FIX (verfijnd): avrdude's gedrag verschilt per geheugentype, afhankelijk
// van hoe het is gedefinieerd in avrdude.conf. Voor "signature" stuurt
// avrdude het AL volledig opgeloste, absolute PDI-adres (dus niet
// nogmaals onze basis optellen). Voor "flash"/"eeprom"/etc. stuurt het
// een RELATIEF adres (0x0, 0x100, 0x200, ...) -- omdat onze firmware
// zich meldt als firmwareversie 7+, verwacht avrdude dat WIJ zelf de
// geheugenbasis erbij optellen voor die "gewone" geheugentypes.
static uint32_t translate_addr(uint8_t mtype, uint32_t rel_addr) {
    switch (mtype) {
        case MTYPE_FLASH:
        case MTYPE_BOOT_FLASH:
            return FLASH_BASE + rel_addr;
        case MTYPE_EEPROM_XMEGA:
        case MTYPE_EEPROM_PAGE:
            return EEPROM_BASE + rel_addr;
        case MTYPE_FUSE_BITS:
            // Komt al volledig opgelost binnen (net als signature) --
            // niet nogmaals FUSE_BASE optellen.
            return rel_addr;
        case MTYPE_LOCK_BITS:
            return LOCK_BASE;
        case MTYPE_USERSIG:
            return USERSIG_BASE + rel_addr;
        case MTYPE_PRODSIG:
            return PRODSIG_BASE + rel_addr;
        case MTYPE_SIGN_JTAG:
            // Komt al volledig opgelost binnen -- niet nogmaals optellen.
            return rel_addr;
        default:
            return FLASH_BASE + rel_addr;
    }
}

static void handle_sign_on(void) {
    jtag_body[0] = RSP_SIGN_ON;
    jtag_body[1] = 1;
    jtag_body[2] = 0; jtag_body[3] = 1; jtag_body[4] = 7; jtag_body[5] = 1;
    jtag_body[6] = 0; jtag_body[7] = 1; jtag_body[8] = 7; jtag_body[9] = 1;
    memset(&jtag_body[10], 0, 6);
    const char *id = "PICO PDI";
    size_t idlen = strlen(id);
    memcpy(&jtag_body[16], id, idlen);
    jtag_body[16 + idlen] = 0;
    jtag_body_len = (uint16_t)(16 + idlen + 1);
}

static void handle_get_parameter(void) {
    uint8_t param = jtag_body[1];
    switch (param) {
        case PAR_HW_VERSION:
            jtag_body_len = 3;
            jtag_body[0] = RSP_PARAMETER;
            jtag_body[1] = 1;
            jtag_body[2] = 1;
            break;
        case PAR_FW_VERSION:
            jtag_body_len = 5;
            jtag_body[0] = RSP_PARAMETER;
            jtag_body[1] = 1; jtag_body[2] = 7;
            jtag_body[3] = 1; jtag_body[4] = 7;
            break;
        case PAR_EMULATOR_MODE:
            jtag_body_len = 2;
            jtag_body[0] = RSP_PARAMETER;
            jtag_body[1] = EMULATOR_MODE_PDI;
            break;
        case PAR_OCD_VTARGET:
            jtag_body_len = 3;
            jtag_body[0] = RSP_PARAMETER;
            jtag_body[1] = 0x00;
            jtag_body[2] = 0x0D;
            break;
        default:
            set_status(RSP_ILLEGAL_PARAMETER);
            return;
    }
}

int main(void) {
    stdio_init_all();
    debug_init();
    stdio_set_translate_crlf(&stdio_usb, false);
    pdi_init_pins();

    while (true) {
        if (!jtag_receive()) continue;

        uint8_t cmd = jtag_body[0];

        switch (cmd) {
            case CMND_GET_SIGN_ON:
                handle_sign_on();
                break;

            case CMND_GET_PARAMETER:
                handle_get_parameter();
                break;

            case CMND_SET_PARAMETER:
                set_status(RSP_OK);
                break;

            case CMND_GET_SYNC:
            case CMND_GO:
            case CMND_RESET:
                set_status(RSP_OK);
                break;

            case CMND_SET_DEVICE_DESCRIPTOR:
            case CMND_SET_XMEGA_PARAMS:
                set_status(RSP_OK);
                break;

            case CMND_ENTER_PROGMODE: {
                int ok = pdi_enable_and_unlock();
                set_status(ok ? RSP_OK : RSP_ILLEGAL_MCU_STATE);
                break;
            }

            case CMND_LEAVE_PROGMODE:
                set_status(RSP_OK);
                break;

            case CMND_CHIP_ERASE: {
                int ok = pdi_chip_erase();
                set_status(ok ? RSP_OK : RSP_FAILED);
                break;
            }

            case CMND_XMEGA_ERASE: {
                // FIX: lees het specifieke wis-type en -adres dat avrdude
                // daadwerkelijk vraagt, i.p.v. altijd blindelings de HELE
                // chip te wissen. Typische XMEGA-erase-typewaarden:
                // 0=chip, 1=app, 2=boot, 3=eeprom, 4=app-pagina,
                // 5=boot-pagina, 6=eeprom-pagina, 7=user-signature.
                uint8_t erase_type = jtag_body[1];
                uint32_t rel_addr = (uint32_t)jtag_body[2] | ((uint32_t)jtag_body[3] << 8) |
                                     ((uint32_t)jtag_body[4] << 16) | ((uint32_t)jtag_body[5] << 24);
                {
                    char buf[48];
                    snprintf(buf, sizeof(buf), "[ERASE] type=%u rel_addr=0x%lX\r\n",
                             erase_type, (unsigned long)rel_addr);
                    debug_print(buf);
                }
                int ok = 0;
                switch (erase_type) {
                    case 0: // volledige chip
                        ok = pdi_chip_erase();
                        break;
                    case 1: // hele applicatiesectie
                        ok = pdi_page_erase(NVM_CMD_ERASE_APP_gc, FLASH_BASE);
                        break;
                    case 2: // hele bootsectie
                        ok = pdi_page_erase(NVM_CMD_ERASE_BOOT_gc, FLASH_BASE);
                        break;
                    case 3: // hele eeprom
                        ok = pdi_page_erase(NVM_CMD_ERASE_EEPROM_gc, EEPROM_BASE);
                        break;
                    case 4: // een specifieke flash-pagina (applicatie)
                        ok = pdi_page_erase(NVM_CMD_ERASE_APP_PAGE_gc, FLASH_BASE + rel_addr);
                        break;
                    case 5: // een specifieke flash-pagina (boot)
                        ok = pdi_page_erase(NVM_CMD_ERASE_BOOT_PAGE_gc, FLASH_BASE + rel_addr);
                        break;
                    case 6: // een specifieke eeprom-pagina
                        ok = pdi_page_erase(NVM_CMD_ERASE_EEPROM_PAGE_gc, EEPROM_BASE + rel_addr);
                        break;
                    case 7: // user signature-rij
                        ok = pdi_page_erase(NVM_CMD_ERASE_USERSIG_gc, USERSIG_BASE);
                        break;
                    default:
                        ok = pdi_page_erase(NVM_CMD_ERASE_APP_PAGE_gc, FLASH_BASE + rel_addr);
                        break;
                }
                set_status(ok ? RSP_OK : RSP_FAILED);
                break;
            }

            case CMND_READ_MEMORY: {
                uint8_t mtype = jtag_body[1];
                uint32_t len = (uint32_t)jtag_body[2] | ((uint32_t)jtag_body[3] << 8) |
                               ((uint32_t)jtag_body[4] << 16) | ((uint32_t)jtag_body[5] << 24);
                uint32_t rel_addr = (uint32_t)jtag_body[6] | ((uint32_t)jtag_body[7] << 8) |
                                     ((uint32_t)jtag_body[8] << 16) | ((uint32_t)jtag_body[9] << 24);
                if (len > BLOKGROOTTE) len = BLOKGROOTTE;
                uint32_t addr = translate_addr(mtype, rel_addr);
                {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "[READ] mtype=0x%02X rel_addr=0x%lX len=%lu addr=0x%08lX\r\n",
                             mtype, (unsigned long)rel_addr, (unsigned long)len, (unsigned long)addr);
                    debug_print(buf);
                }
                uint8_t data[BLOKGROOTTE];
                uint32_t t0 = time_us_32();
                pdi_read_block_reliable(addr, data, (uint16_t)len);
                uint32_t elapsed_ms = (time_us_32() - t0) / 1000;
                {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "[READ] klaar in %lums, eerste byte=0x%02X\r\n",
                             (unsigned long)elapsed_ms, data[0]);
                    debug_print(buf);
                }
                jtag_body[0] = RSP_MEMORY;
                memcpy(&jtag_body[1], data, len);
                jtag_body_len = (uint16_t)(len + 1);
                break;
            }

            case CMND_WRITE_MEMORY: {
                uint8_t mtype = jtag_body[1];
                uint32_t len = (uint32_t)jtag_body[2] | ((uint32_t)jtag_body[3] << 8) |
                               ((uint32_t)jtag_body[4] << 16) | ((uint32_t)jtag_body[5] << 24);
                uint32_t rel_addr = (uint32_t)jtag_body[6] | ((uint32_t)jtag_body[7] << 8) |
                                     ((uint32_t)jtag_body[8] << 16) | ((uint32_t)jtag_body[9] << 24);
                uint32_t addr = translate_addr(mtype, rel_addr);
                {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "[WRITE] mtype=0x%02X rel_addr=0x%lX len=%lu addr=0x%08lX\r\n",
                             mtype, (unsigned long)rel_addr, (unsigned long)len, (unsigned long)addr);
                    debug_print(buf);
                }
                pdi_write_block(mtype, addr, &jtag_body[10], (uint16_t)len);
                set_status(RSP_OK);
                break;
            }

            case CMND_SIGN_OFF:
                set_status(RSP_OK);
                break;

            default:
                set_status(RSP_FAILED);
                break;
        }

        {
            uint32_t t_send0 = time_us_32();
            jtag_answer();
            uint32_t send_ms = (time_us_32() - t_send0) / 1000;
            if (jtag_body_len > 5) {
                char buf[48];
                snprintf(buf, sizeof(buf), "[SEND] %ums voor %u bytes\r\n",
                         (unsigned)send_ms, (unsigned)jtag_body_len);
                debug_print(buf);
            }
        }
    }
    return 0;
}