/* SPDX-License-Identifier: MIT
 *
 * Project: bclmtool
 * Author:  Björn Spåra
 * 
 * Description: Lightweight utility to set battery charge thresholds (BCLM) 
 * on Intel MacBooks by communicating directly with the Apple SMC.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/io.h>
#include <errno.h>

#define APPLESMC_DATA_PORT 0x300
#define APPLESMC_CMD_PORT  0x304
#define APPLESMC_READ_CMD  0x10
#define APPLESMC_WRITE_CMD 0x11

#define APPLESMC_MIN_WAIT_US     8

#define SMC_STATUS_AWAITING_DATA 0x01
#define SMC_STATUS_IB_CLOSED     0x02
#define SMC_STATUS_BUSY          0x04

/* BCLM should be set to the percantage of full capacity at which the charging
 * should stop. */
static const char BCLM_KEY[] = "BCLM";

/* BFCL should be set to two percentage point below BCLM target as per
 * applesmc-next mainteiners recommendations to prevent constant gate
 * flicker in the SMC and consisten green MagSafe indicato LED when in
 * the "full range". My tests show inconclusive results and the conclusion
 * is that the BFCL behaviour is not well understood.*/
static const char BFCL_KEY[] = "BFCL";

/* Wait for a specific status/mask combination on the SMC command port. */
static int wait_status(uint8_t val, uint8_t mask) {
    useconds_t delay = APPLESMC_MIN_WAIT_US;

    for (int i = 0; i < 24; i++) {
        if ((inb(APPLESMC_CMD_PORT) & mask) == val) {
            return 0;
        }
        usleep(delay);
        if (i > 9) {
            delay <<= 1;
        }
    }

    return -EIO;
}

/* Send a command byte to the SMC command port. */
static int send_command(uint8_t cmd) {
    int ret = wait_status(0, SMC_STATUS_IB_CLOSED);
    if (ret) {
        return ret;
    }

    outb(cmd, APPLESMC_CMD_PORT);
    return 0;
}

/* Send a single byte to the SMC data port after the expected handshake. */
static int send_byte(uint8_t val, uint16_t port) {
    int ret = wait_status(0, SMC_STATUS_IB_CLOSED);
    if (ret) {
        return ret;
    }

    ret = wait_status(SMC_STATUS_BUSY, SMC_STATUS_BUSY);
    if (ret) {
        return ret;
    }

    outb(val, port);
    return 0;
}

/* Reset the SMC state machine if the busy bit is stuck high. */
static int smc_sane(void) {
    int ret = wait_status(0, SMC_STATUS_BUSY);
    if (!ret) {
        return 0;
    }

    ret = send_command(APPLESMC_READ_CMD);
    if (ret) {
        return ret;
    }

    return wait_status(0, SMC_STATUS_BUSY);
}

/* Send a four-byte SMC key to the data port. */
static int send_argument(const char *key) {
    for (int i = 0; i < 4; i++) {
        if (send_byte((uint8_t)key[i], APPLESMC_DATA_PORT)) {
            return -EIO;
        }
    }

    return 0;
}

/* Drain any unread bytes left by the SMC before finishing a read transaction. */
static void flush_read_data(void) {
    for (int i = 0; i < 16; i++) {
        usleep(APPLESMC_MIN_WAIT_US);
        if (!(inb(APPLESMC_CMD_PORT) & SMC_STATUS_AWAITING_DATA)) {
            break;
        }
        (void)inb(APPLESMC_DATA_PORT);
    }
}

/* Read a 1-byte SMC key. */
static int read_key_u8(const char *key, uint8_t *val) {
    int ret = smc_sane();
    if (ret) {
        return ret;
    }

    if (send_command(APPLESMC_READ_CMD) || send_argument(key)) {
        return -EIO;
    }

    if (send_byte(1, APPLESMC_DATA_PORT)) {
        return -EIO;
    }

    ret = wait_status(SMC_STATUS_AWAITING_DATA | SMC_STATUS_BUSY,
                      SMC_STATUS_AWAITING_DATA | SMC_STATUS_BUSY);
    if (ret) {
        return ret;
    }

    *val = inb(APPLESMC_DATA_PORT);
    flush_read_data();

    return wait_status(0, SMC_STATUS_BUSY);
}

/* Write a 1-byte value to an SMC key. */
static int write_key_u8(const char *key, uint8_t val) {
    int ret = smc_sane();
    if (ret) {
        return ret;
    }

    if (send_command(APPLESMC_WRITE_CMD) || send_argument(key)) {
        return -EIO;
    }

    if (send_byte(1, APPLESMC_DATA_PORT)) {
        return -EIO;
    }

    if (send_byte(val, APPLESMC_DATA_PORT)) {
        return -EIO;
    }

    return wait_status(0, SMC_STATUS_BUSY);
}

/* Read the 1-byte BCLM key. */
int read_bclm(uint8_t *val) {
    return read_key_u8(BCLM_KEY, val);
}

/* Read the 1-byte BFCL key. */
int read_bfcl(uint8_t *val) {
    return read_key_u8(BFCL_KEY, val);
}

/* Write a 1-byte value to the BCLM key */
int write_bclm(uint8_t val) {
    return write_key_u8(BCLM_KEY, val);
}

/* Write a 1-byte value to the BFCL key. */
int write_bfcl(uint8_t val) {
    return write_key_u8(BFCL_KEY, val);
}

int main(int argc, char *argv[]) {
  
  if (ioperm(APPLESMC_DATA_PORT, 1, 1) || ioperm(APPLESMC_CMD_PORT, 1, 1)) {
        perror("Error: ioperm failed (run with sudo)");
        return 1;
    }

    uint8_t current;
    if (read_bclm(&current) != 0) {
        fprintf(stderr, "Failed to read BCLM. Your SMC might be busy or unsupported.\n");
        return 1;
    }

    if (argc == 1) {
        printf("Current BCLM limit: %d%%\n", current == 0 ? 100 : current);
        return 0;
    }

    int target = atoi(argv[1]);
    if (target < 40 || target > 100) {
        fprintf(stderr, "Error: Please specify a limit between 40 and 100.\n");
        return 1;
    }

    printf("Setting BCLM from %d%% to %d%%...\n", current == 0 ? 100 : current, target);
    if (write_bclm((uint8_t)target) != 0) {
        fprintf(stderr, "Failed to write BCLM to SMC.\n");
        return 1;
    }

    /* Verification read */
    uint8_t verify_bclm;
    /* uint8_t verify_bfcl; */
    usleep(50000); 
    if (read_bclm(&verify_bclm) != 0) {
        fprintf(stderr, "Failed to verify BCLM after write.\n");
        return 1;
    }

    printf("Verification: SMC now reports BCLM=%d%%\n", verify_bclm == 0 ? 100 : verify_bclm);
    return 0;
}
