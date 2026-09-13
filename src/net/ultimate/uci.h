#ifndef T80_UCI_H
#define T80_UCI_H
#include <stdint.h>
#define UCI_PAYLOAD 512
#define UCI_REPLY_SIZE (UCI_PAYLOAD + 2)
#define UCI_STATUS_SIZE 80

enum uci_result { UCI_IDLE, UCI_PENDING, UCI_DONE, UCI_ERROR, UCI_TIMEOUT };
extern uint8_t uci_reply[UCI_REPLY_SIZE];
extern uint16_t uci_reply_length;
extern char uci_status[UCI_STATUS_SIZE];
uint8_t uci_present(void);
void uci_enable(void);
void uci_reset(void);
uint8_t uci_start(const uint8_t *command, uint16_t size, uint16_t timeout);
enum uci_result uci_poll(void);
uint16_t uci_ticks(void);
uint8_t uci_status_code(void);

#ifdef ULTIMATE_TEST
uint8_t uci_test_read(uint8_t reg);
void uci_test_write(uint8_t reg, uint8_t value);
uint16_t uci_test_ticks(void);
void uci_test_unlock(uint16_t address, uint8_t value);
#endif
#endif
