# RDNA4 OC+

Simple and lightweight OC utility for RDNA 4 GPUs.
Additional min/max clock controls for GPU core, Fabric, and SoC not found in the default driver.

![RDNA4 OC+](screenshots/app.png)

## Features

- Adrenalin controls: max clock offset, voltage offset, power limit, VRAM clock limit, and memory timings.
- Additional SMU controls: GFXCLK soft min/max, FCLK, and SOCCLK.
- 5-point fan control.

Download the ready-to-run EXE from **Releases**.

## Compatibility

- Windows 10 or later (64-bit)
- AMD RDNA4 GPU with AMD drivers installed

## How it works

- On startup, the app reads the available GFXCLK, FCLK, and SOCCLK ranges from the SMU, along with the current ADLX settings.
- ADLX handles the regular driver controls. For SMU clocks, the app sends soft min/max requests directly to the SMU through its mailbox.
- When you click **Apply**, it checks the values against the available ranges, applies ADLX changes first, then sends the SMU requests.

## Disclaimer

Overclocking can cause instability, crashes, or hardware damage. Use at your own risk; the author is not responsible for damage or data loss.
