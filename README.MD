# bclmtool

A lightweight C utility for Linux to control the Battery Charge Level Max (BCLM) on Intel-based MacBooks. This tool communicates directly with the Apple System Management Controller (SMC) via I/O ports.

## Why this exists
Standard Linux drivers for older MacBooks often lack the sysfs interface to set battery charge thresholds. This tool fills that gap by writing directly to the BCLM register.

## Building
Run `make` to compile the binary.
```bash
make
```

## Usage
Use this code at your own risk.

Read current limit:
```bash
sudo ./bclmtool
```

Set new limit (e.g., 80%):
```bash
sudo ./bclmtool 80
```

When setting a limit, the tool also writes BFCL to one percentage point below the requested BCLM value. This helps the MagSafe LED switch to green even if the battery charge shown by the OS stays slightly below the stop threshold.

## SMC details
The Apple SMC is accessed through two I/O ports:

- 0x300: data port
- 0x304: command/status port

This tool uses the standard Apple SMC command bytes used by the Linux applesmc driver:

- 0x10: read key
- 0x11: write key

SMC keys are four-character identifiers. The keys used here are both ui8 values, meaning they store a single unsigned byte:

| Key | Type | Meaning |
| --- | --- | --- |
| BCLM | ui8 | Battery Charge Level Max, the charge stop threshold |
| BFCL | ui8 | Battery Full Charge Level, used here as the charger LED threshold |

When you run sudo ./bclmtool 80, the tool:

1. Reads the current BCLM
2. Writes BCLM = 80
3. Writes BFCL = 79
4. Reads both keys back for verification

BCLM is the real battery charging limit. BFCL does not raise or lower that cap; it affects when the charger may consider the battery "full enough" for LED behavior. On some machines the desktop UI may sit at 79% even when BCLM is set to 80, because the battery firmware and Linux power-supply reporting use finer-grained values than the displayed integer percentage.

The SMC command/status port exposes three status bits that matter for this handshake:

- bit 0 (0x01): data is waiting to be read
- bit 1 (0x02): input buffer closed, the SMC will ignore input
- bit 2 (0x04): command in progress / busy

The tool waits for the expected status transitions before each byte transfer, then flushes any unread data at the end of a read transaction. This is intended to match the maintained `applesmc` transaction flow closely enough to avoid reading stale bytes or interrupting an active SMC exchange.

## Safety
This tool uses the same SMC handshaking pattern as the maintained Linux applesmc driver: it polls the command port status bits, waits for the expected busy/input-buffer transitions, and flushes unread data before completing read transactions. When writing a new charge limit it updates the BCLM key and also updates BFCL for the charger LED threshold.

Reading is generally the safer operation, but direct SMC writes can still race with other SMC users or behave differently across MacBook models and firmware revisions. An error could leave the controller in a bad state or cause hardware issues. Use it carefully and at your own risk.
