Claude's idea of a secure bootloader demo for my Nucleo F429ZI. I plan to try it out and go over the details soon...

There were two issues, possibly because I didn't have the board hooked up when Claude first did its thing. The console commands (which I didn't expect but was a nice touch) didn't work via the 'make monitor' script. Also the memory protect wasn't working, as indicated by an error message. Most amazingly of all, when I told Claude to took at these two issues with the board connected, it fixed them both, complete with code changes and comments! This is truly a new era...

# Secure-Bootloader

================ Claude README below ============

A secure A/B bootloader demo for the **NUCLEO-F429ZI** (STM32F429ZI). Everything is register-level C: no HAL, no CMSIS downloads.

What it demonstrates:

| Feature | How |
|---|---|
| **Authenticated boot** | Every image is checked with ECDSA P-256 + SHA-256 before *every* boot ([micro-ecc](third_party/micro-ecc), vendored) |
| **A/B updates** | The running app writes the new image into the *other* slot over UART, so the current one stays intact |
| **Trial boot + automatic rollback** | A new image boots as `PENDING`. If it doesn't confirm itself within 3 boots, the bootloader reverts to the previous slot |
| **Watchdog** | The bootloader arms the IWDG before jumping, so a hung image resets and counts as a failed trial |
| **Anti-rollback** | A signed security counter. Confirming an image raises a persistent floor, and older images are refused |
| **Power-loss safety** | The boot state is an append-only, CRC-protected log. Image headers are written last, so a partial upload reads as an empty slot |
| **Recovery mode** | With no valid image, or the USER button held at reset, the bootloader accepts a signed image over UART |
| **Lock-down** | `make protect` write-protects the bootloader sectors. `RDP=1` also enables readout protection |

## Requirements

- `arm-none-eabi-gcc` (tested with 15.3)
- Python 3 with `cryptography` (`pip install cryptography`)
- [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html) (`STM32_Programmer_CLI` on `PATH`), used for `make flash`/`protect`
- A host C compiler for `make test`

## Memory map

```
Bank 1                                     Bank 2
0x08000000 sec 0-1  bootloader   32K       0x08100000 sec 12-16  (unused)
0x08008000 sec 2    boot state log 0       0x08120000 sec 17-19  SLOT B  384K
0x0800C000 sec 3    boot state log 1       0x08180000 sec 20-23  (unused)
0x08010000 sec 4    (unused)
0x08020000 sec 5-7  SLOT A       384K
```

The two slots sit in different flash banks, so the running app can erase and program the other slot without stalling (read-while-write). Each slot starts with a 512-byte signed header, and the app's vector table follows at `+0x200`. The app is linked twice (once per slot address), and both builds are signed.

## Quick start

```sh
make                 # generates keys/signing_key.pem on first run, builds everything
make flash           # full chip erase, then bootloader + app v1.0.0 in slot A
make monitor         # or: picocom -b 115200 /dev/ttyACM0
```

Press the black RESET button. You should see:

```
 Secure A/B bootloader - NUCLEO-F429ZI
[boot] reset cause: reset pin
[sec]  RDP level 0 (debug port open), bootloader write-protect OFF
[boot] no boot state found - scanning slots
[boot] slot A: v1.0.0  security counter 1  signature OK
[boot] starting slot A
 Demo application v1.0.0  running from slot A
```

The green LED blinks for slot A and the blue LED for slot B. A fast blink means the image is on trial, a slow blink means it is confirmed. App console commands: `i` info, `c` confirm, `r` reboot, `h` help.

> The ST-LINK VCP is the only UART. Close your terminal before `make update`, because two programs can't share the port.

## Demo script

**1. Signed A/B update**

```sh
make update VERSION=1.1.0 SECURITY_COUNTER=2
```

The app on slot A receives the image into slot B and verifies it before marking it `PENDING`, then reboots. The bootloader trial-boots B (blue fast blink). After its 5 s self-test, the app confirms itself, and the anti-rollback floor becomes 2.

**2. Broken release → automatic rollback**

```sh
make update VERSION=1.2.0 SECURITY_COUNTER=2 DEMO_FAULT=1
```

This build hangs at start-up (red LED) without kicking the watchdog. You'll see `trial boot 1/3`, `2/3`, `3/3` about 8 s apart, then `ROLLBACK to slot B`, and v1.1.0 is running again.

**3. Tampered or foreign images are rejected**

```sh
for s in A B; do
  cp build/app-v1.1.0-sc2-$s.signed.bin /tmp/evil-$s.bin
  printf '\x00' | dd of=/tmp/evil-$s.bin bs=1 seek=4000 conv=notrunc
done
tools/update.py --port /dev/ttyACM0 /tmp/evil-A.bin /tmp/evil-B.bin
# -> error: device reported image rejected by device: payload hash mismatch
```

Editing a header field (version, counter, load address) instead gives `SIGNATURE INVALID`. So does an image signed with any other key, e.g. `make KEY=/tmp/other.pem ...` after `tools/imgtool.py keygen /tmp/other.pem`.

**4. Anti-rollback**

```sh
make update VERSION=1.0.0 SECURITY_COUNTER=1
# -> rollback blocked (security counter too low)
```

The bootloader enforces the same check, so an old image already sitting in a slot can't be booted either.

**5. Recovery mode**

Hold the blue USER button and tap RESET. The red LED blinks and the bootloader waits for an upload:

```sh
make update VERSION=1.3.0 SECURITY_COUNTER=3
```

Recovery never overwrites the only good image. It writes into an empty or invalid slot, or into the inactive slot when both are valid.

**6. Lock it down**

```sh
make protect          # WRP on sectors 0-1: the bootloader can't be overwritten
make protect RDP=1    # also readout protection (SWD access to flash blocked)
make unprotect        # back to RDP 0 - this MASS-ERASES the chip from RDP 1
```

The bootloader reports the current RDP/WRP status at every boot. Never set RDP level 2 on a dev board: it is permanent.

## Image format

```
+0x000  magic "SBI1" | hdr_version | hdr_size=0x200 | load_addr | img_size
        version (maj<<24|min<<16|patch) | security_counter | flags | reserved
+0x020  SHA-256(payload)
+0x040  ECDSA-P256 signature (r||s) over SHA-256(header[0x00..0x40])
+0x080  0xFF padding
+0x200  payload (application vector table + code)
```

`tools/imgtool.py` handles keys and signing: `keygen`, `pubkey-c`, `sign`, and `info --key <pem> <img>`. The public key is compiled into the bootloader from `keys/signing_key.pem` at build time (`build/pubkey.c`).

## Boot flow

```
reset ─► USER button held? ──yes──► recovery (UART upload)
           │no
           ▼
     load boot state ──none──► verify both slots, pick newest valid, mark CONFIRMED
           │
     PENDING? ──yes──► attempts < 3 and image valid? ──yes──► attempts++, trial boot
           │                     │no
           │                     └──► ROLLBACK: other slot, CONFIRMED
           ▼
     verify active ──ok──► arm IWDG, jump
           │fail
     verify other ──ok──► switch, jump
           │fail
           └──► recovery
```

The policy is in `common/bootstate.c` (`boot_decide`), so the host tests exercise exactly the code the bootloader runs.

## Tests

```sh
make test
```

These build the core logic for the host against a simulated NOR flash, using images signed by `imgtool.py`. They cover the SHA-256 vectors, signature, hash, slot, size and counter rejection, and log rotation across 1500 writes. They simulate power loss mid-record and run full A/B scenarios: trial → rollback, confirm, downgrade attempt, tampered pending image, corrupted confirmed slot, recovery, and fallback.

## Layout

```
bootloader/main.c       boot flow, recovery mode, hand-over
app/main.c              demo application (slot-aware, confirms itself, receives updates)
common/                 image verification, boot-state log + policy, update protocol,
                        flash driver, board support, SHA-256, CRC-32, startup
linker/                 bootloader + per-slot app linker scripts
tools/imgtool.py        key generation, C key export, signing, inspection
tools/update.py         UART uploader (no pyserial needed)
test/                   host unit tests + fake flash
third_party/micro-ecc   ECDSA (BSD-2-Clause), unmodified
```

## Security notes and limitations

This is a teaching demo. Before you build a product on it:

- **Key handling.** `keys/` is git-ignored and the key is generated locally without a passphrase. Real deployments keep the signing key in an HSM or offline signer. There's no key rotation or revocation.
- **Trust boundary.** The application is trusted to write the boot state and the inactive slot. The bootloader still verifies everything it boots, so a misbehaving app can cause a rollback or a denial of service but can't get unsigned code executed. The F429 has no TrustZone, and the MPU isn't used to isolate the bootloader at run time.
- **Protection.** WRP (write protection) can be removed by anyone with SWD access unless RDP is raised. RDP 1 still allows option-byte regression (with a mass erase). Only RDP 2 is final.
- **No confidentiality.** Images are signed, not encrypted.
- **Fault injection.** There is light hardening only: non-trivial success codes and a redundant re-check. Real glitch resistance needs more (double verification, random delays, hardware support).
- **Not hardware-certified.** The flash geometry and register usage follow RM0090, and the core logic is unit-tested on the host.
