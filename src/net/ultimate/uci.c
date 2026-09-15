/* Register protocol follows GideonZ's Core UCI Architecture. */
#include "uci.h"
#ifndef ULTIMATE_TEST
#include "platform.h"
#pragma code(ultcode)
#pragma data(ultdata)
#define READ(reg) (*(volatile uint8_t *)(0xdf1c + (reg)))
#define WRITE(reg, value) (*(volatile uint8_t *)(0xdf1c + (reg)) = (value))
#else
#define READ(reg) uci_test_read(reg)
#define WRITE(reg, value) uci_test_write(reg, value)
#endif

uint8_t uci_reply[UCI_REPLY_SIZE];
uint16_t uci_reply_length;
char uci_status[UCI_STATUS_SIZE];
static uint16_t status_length;
static uint16_t started, deadline;
static uint8_t stage;
static uint8_t last_part;
static enum uci_result result;
enum { STOPPED, RESETTING, REPLY, ACKNOWLEDGE };

uint16_t uci_ticks(void)
{
#ifdef ULTIMATE_TEST
    return uci_test_ticks();
#else
    return platform_ticks();
#endif
}
uint8_t uci_present(void)
{
    /* Bit 7 may be cleared by a 3.15 UCI IRQ left by previous software. */
    return (READ(1) & 0x7f) == 0x49;
}
void uci_reset(void)
{
    /* Like the cc65 startup, leave an idle interface alone. Recover a
     * pending transaction or error with ABORT plus CLR_ERR. */
    if (READ(0) & 0x3f) WRITE(0, 0x0c);
    stage = RESETTING;
    result = UCI_PENDING;
    started = uci_ticks(); deadline = 120;
    uci_reply_length = status_length = 0;
    uci_status[0] = 0;
}
uint8_t uci_start(const uint8_t *command, uint16_t size, uint16_t timeout)
{
    uint16_t i;
    if (stage != STOPPED || !size || size > 896 || (READ(0) & 0x3f)) return 0;
    uci_reply_length = status_length = 0;
    uci_status[0] = 0;
    for (i = 0; i < size; ++i) WRITE(1, command[i]);
    WRITE(0, 1);
    stage = REPLY; result = UCI_PENDING;
    started = uci_ticks(); deadline = timeout;
    return 1;
}
static enum uci_result fail(enum uci_result error)
{
    WRITE(0, 4);
    stage = STOPPED;
    result = error;
    return error;
}
enum uci_result uci_poll(void)
{
    uint16_t budget = 128;
    uint8_t status;
    if (stage == STOPPED) return result;
    if ((uint16_t)(uci_ticks() - started) >= deadline) return fail(UCI_TIMEOUT);
    status = READ(0);
    if (stage == RESETTING) {
        if (!(status & 0x3f)) { stage = STOPPED; result = UCI_DONE; }
        return result;
    }
    if (status & 8) return fail(UCI_ERROR);
    if (stage == ACKNOWLEDGE) {
        if (status & 2) return UCI_PENDING;
        if (last_part) {
            if (status & 0x31) return UCI_PENDING;
            stage = STOPPED; result = UCI_DONE;
            return result;
        }
        stage = REPLY;
    }
    if ((status & 1) || (status & 0x30) < 0x20) return UCI_PENDING;
    last_part = (status & 0x30) == 0x20;
    while (budget && (READ(0) & 0x80)) {
        if (uci_reply_length == UCI_REPLY_SIZE) return fail(UCI_ERROR);
        uci_reply[uci_reply_length++] = READ(2);
        --budget;
    }
    if (READ(0) & 0x80) return UCI_PENDING;
    while (budget && (READ(0) & 0x40)) {
        uint8_t byte = READ(3);
        if (status_length < UCI_STATUS_SIZE - 1) uci_status[status_length] = byte;
        if (++status_length > 256) return fail(UCI_ERROR);
        --budget;
    }
    uci_status[status_length < UCI_STATUS_SIZE ? status_length : UCI_STATUS_SIZE - 1] = 0;
    if (READ(0) & 0x40) return UCI_PENDING;
    WRITE(0, 2);
    stage = ACKNOWLEDGE;
    return UCI_PENDING;
}
uint8_t uci_status_code(void)
{
    if (status_length < 2 || uci_status[0] < '0' || uci_status[0] > '9' ||
        uci_status[1] < '0' || uci_status[1] > '9') return 255;
    return (uint8_t)((uci_status[0] - '0') * 10 + uci_status[1] - '0');
}
#ifndef ULTIMATE_TEST
#pragma code(code)
#pragma data(data)
#endif
