# corrupted-windows

**Righting a Windows install that hadn't taken a cumulative update in 6 months — four cascading root causes, an ESP32 keyboard emulator, a dedicated IP KVM, and an AI agent that spent two days reading CBS logs.**

> Built entirely through agentic coding with [Claude Code](https://claude.ai/code). The conversation ran across two marathon sessions, with the human in Shenzhen while the agent operated the machine overnight via SSH and IPKVM.

![Build 26100.8039 — the update that finally stuck](archive/images/hero_banner.jpg)

---

## The Machine

A Windows 11 IoT Enterprise LTSC 2024 desktop ("DeskPC") that had been through more than any install should survive:

- **ServerRdsh -> LTSC edition migration** via registry hack (leaving 3,879 zombie server packages)
- **MBR -> GPT disk conversion** (which silently mislabeled the EFI partition)
- **Multiple failed in-place upgrade attempts** and legacy boot misadventures
- **90,830 power cycles** on the boot SSD
- **Dead WinRE**, broken BCD, and a 128 GB pagefile "from crazy needs before"

As the user put it:

> *"This windows install has marched through several editions with it once being ServerRdsh at one point, then force-migrated to LTSC via registry trick. And several partition master migrations as well, with the disk once being MBR in the beginning and changed to GPT somehow in the middle where the WinRE kicked the bucket in between. It's a crazy install."*

The machine was valuable — peak Windows compute for the homelab, and the ultimate test of agentic abilities. Clean install was not on the table.

## The Problem

The session started with SSD diagnostics and disk cleanup, but the real challenge emerged when checking the Windows Update logs: **every cumulative update had been failing for 6+ months**. KB5079473 (March 2026 security update) failed repeatedly. In-place upgrade attempts bounced with `0x80070003`. The machine was stuck on build 26100.4946, unpatched since October 2025.

## The Journey

### Four Root Causes

What looked like one problem turned out to be four independent issues stacked on top of each other, each masking the next.

#### 1. Component Store Corruption (0x80073712)

Every cumulative update failed at the staging phase. DISM RestoreHealth reported success, but CBS still couldn't resolve the execution chain.

**Root cause**: Orphaned server roles — Hyper-V, WAS, WCF, Containers — from the ServerRdsh era. These features had been disabled but their CBS package references remained, creating broken dependency chains that poisoned the staging pipeline.

**Fix**: Stripped all server features via `Disable-WindowsOptionalFeature`, removed the en-GB language pack that was pulling in WCF/WAS references, then RestoreHealth from the ISO WIM to repopulate sources.

#### 2. Reboot Crash — DPC_WATCHDOG_VIOLATION (0x133)

Every reboot crashed with a black-screen BSOD. The fault function: `nt!HvlSwitchToVsmVtl1`.

**Root cause**: VBS (Virtualization-Based Security) was enforced by 24H2, even with `hypervisorlaunchtype off` in BCD. The hypervisor attempted a VTL1 context switch on every shutdown, hung in a DPC, and crashed. Setting `hypervisorlaunchtype off` in BCD was not enough — 24H2 enforces VBS independently via the registry.

**Temporary workaround**: Disabled VBS, HVCI, and Credential Guard via registry keys under `HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard`. Clean reboots achieved for the first time. This is **not a permanent fix** — these are important hardware security features. The plan is to restore them after an in-place upgrade rebuilds the servicing stack cleanly (see [Future Actions](#future-actions)).

#### 3. Update Rollback — AdvancedInstallersFailed (0x800F0922)

With the component store clean and reboots working, updates staged and installed to 93% — further than ever before. But after every reboot, Windows rolled back: "Something didn't go as planned."

CBS logs showed `CbsExecuteStateFlagAdvancedInstallersFailed` with a specific CSI error: **`Bfs Hypervisor Launch Type mirroring failed`**. The AdvancedInstallers phase runs during boot to mirror BCD settings into the component store — but it couldn't find the BCD store.

**Root cause**: The EFI System Partition had the wrong GPT type GUID. The MBR-to-GPT conversion had set it to `{ebd0a0a2}` (Basic Data Partition) instead of `{c12a7328}` (EFI System Partition). The partition contained the correct EFI structure (`D:\EFI\Microsoft\Boot\BCD`), but `bcdedit` refused to find it because the partition type was wrong. CBS couldn't write to BCD during the AdvancedInstallers phase, so every update rolled back.

**Fix**: One diskpart command:
```
select disk 0
select partition 1
set id=c12a7328-f81f-11d2-ba4b-00a0c93ec93b
```

#### 4. Source Missing (0x800F081F)

After fixing the ESP GUID, staging failed with a new error — CBS couldn't find source files for components that referenced the removed en-GB language pack.

**Fix**: Set a permanent repair source to the ISO WIM via Group Policy registry (`HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\Servicing`), then RestoreHealth to repopulate the component store.

### Victory

![96% complete — the update finalizing during reboot](archive/images/boot_check.jpg)
*96% complete. Past where it always crashed. No BSOD, no rollback.*

With all four fixes applied, KB5079473 staged, installed, rebooted, finalized the AdvancedInstallers, and **persisted**. Then KB5085516 (the emergency fix for KB5079473's sign-in bug) installed on the next cycle.

Build: 26100.4946 -> **26100.8039**. Two cumulative updates in one night, after 6 months of nothing.

### The IPKVM

SSH is the brain — but when the machine is rebooting, in BIOS, or showing a boot screen, SSH is gone. The agent needs **eyes** (screen capture) and **hands** (keyboard input) that work at every stage, independent of the OS.

```
┌─────────┐  HDMI out   ┌──────────────┐         ┌─────────┐
│ DeskPC  │────────────→│  IPKVM Eyes  │────────→│   Mac   │ ← agent runs here
│ (Win11) │             │ (capture)    │  HTTP   │  (SSH)  │
│         │  USB HID    ┌──────────────┐  TCP/WS │         │
│         │←────────────│ IPKVM Hands  │←────────│         │
└────┬────┘             │ (keyboard)   │         └─────────┘
     │                  └──────────────┘
     │  fallback boot
     ▼
┌─────────┐
│ Ubuntu  │ ← safety net (USB SATA SSD, default boot)
│  SSH    │   efibootmgr --bootnext for Windows
└─────────┘
```

Two approaches were built — either one gives full remote control:

| Approach | Eyes | Hands | Cost |
|----------|------|-------|------|
| **Commercial** | YiShu ES2 IP KVM (HDMI capture + HTTP snapshot API) | YiShu ES2 (built-in USB HID) | ¥268 (~$37) |
| **DIY** | HDMI capture card via Linux node | ESP32-S3 USB HID keyboard over WiFi TCP | ¥80 (~$11) |

For comparison: commercial IPKVM solutions (PiKVM, TinyPilot, Lantronix) start at $200–500, and IPMI-equipped server motherboards carry a $100+ premium. With agentic AI providing the software intelligence — writing its own firmware, protocols, and automation — the hardware cost drops to $11–37.

![IPKVM eyes — crystal clear remote view of Windows Settings](archive/images/ipkvm_real.jpg)
*Eyes: the IPKVM providing a crystal clear view of the DeskPC desktop.*

![IPKVM hands — typing to the DeskPC remotely](archive/images/ipkvm_test2.png)
*Hands: "IPKVM HANDS ARE WORKING" — the ESP32-S3 USB HID keyboard typing commands to the DeskPC via WiFi TCP.*

**Safety net**: Ubuntu on a USB SATA SSD found in a drawer. Boot order defaults to Ubuntu — `efibootmgr --bootnext 0003` for Windows. Survives any Windows crash.

![Ubuntu — the accidental safety net](archive/images/ubuntu_boot.jpg)
*Ubuntu booted accidentally from a USB SATA SSD. Became the most reliable part of the recovery environment — always SSH-accessible, always boots, always has `efibootmgr`.*

The combination of IPKVM + Ubuntu safety net turned out to be better than any purpose-built recovery environment: always-on SSH to Linux, visual access to boot screens, keyboard input at any stage, and `efibootmgr` to switch between OSes without touching hardware.

## The Root Cause Chain

```
MBR->GPT disk conversion (years ago)
  -> EFI partition gets wrong GUID (Basic Data instead of ESP)
    -> bcdedit can't find BCD store
      -> CBS AdvancedInstallers can't mirror hypervisor setting
        -> Every cumulative update's reboot phase fails
          -> Windows rolls back every update for 6+ months

ServerRdsh -> LTSC registry hack (years ago)
  -> 3,879 zombie Hyper-V/Container/Server packages in component store
    -> CBS dependency chains broken
      -> Component store "corruption" (0x80073712)

VBS enforcement on 24H2
  -> Hypervisor VTL1 switch hangs during reboot
    -> DPC_WATCHDOG_VIOLATION BSOD on every restart
```

Three unrelated historical decisions — a disk conversion, an edition hack, and a Windows version upgrade — combined to create a machine that booted fine, ran fine, but could never update. Each fix revealed the next layer.

## Lessons Learned

### For Windows users

1. **After any MBR-to-GPT conversion, verify the EFI partition GUID.** Run `Get-Partition` in PowerShell or `diskpart list partition`. The EFI System Partition must be type `{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}`, not `{ebd0a0a2}` (Basic Data). This silently breaks `bcdedit` and Windows Update servicing.

2. **Windows edition migration via registry leaves deep scars.** Changing `EditionID` from ServerRdsh to IoTEnterpriseS doesn't remove the server packages — it orphans 3,879 of them. CBS can't transition packages that belong to a different edition. There is no clean path back.

3. **VBS on Windows 11 24H2 overrides BCD.** `bcdedit /set hypervisorlaunchtype off` is not enough. VBS is enforced via `HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\EnableVirtualizationBasedSecurity`. You must disable it in the registry AND BCD, or the hypervisor still loads. **Note:** VBS, HVCI, and Credential Guard are important security features. Disabling them should be a temporary diagnostic step, not a permanent solution. Plan to restore them after the underlying issue is resolved (e.g., via in-place upgrade).

4. **DISM RestoreHealth can report success while CBS still fails.** The tool verifies file integrity but not logical consistency of package dependency chains. "No component store corruption detected" doesn't mean updates will install.

5. **The reboot IS the update.** The percentage bar in Windows Update is just staging. The real work — AdvancedInstallers, driver registration, BCD mirroring — happens during the reboot's boot phase. A crash during this 2-minute window rolls back the entire update.

### For agentic AI users

6. **Keep a Linux USB SSD as a safety net.** Ubuntu as default boot with `efibootmgr --bootnext` for Windows means you always have SSH access even when Windows is broken. You can mount NTFS, run DISM offline, fix boot records, and inspect CBS logs. Never change the default boot order back to Windows — always use `--bootnext` for one-shot Windows boots.

7. **SSH to Windows requires upfront investment.** Set PowerShell as default shell, install busybox for coreutils, add tools to PATH, set `LocalAccountTokenFilterPolicy=1` for remote admin. Without this, every command fights escaping issues.

8. **HDMI capture on macOS is unreliable for long sessions.** USB capture cards drop out after ~10 minutes. Use a dedicated IPKVM device or route capture through a Linux node.

9. **Build the IPKVM before you need it.** Having eyes (HDMI capture) and hands (USB HID keyboard) during boot screens, BIOS, and WinPE is the difference between "wait for the human" and "fix it now." At $11–37, there's no excuse not to have one.

## On Agentic Philosophy

Back in secondary school, I got myself a home computer for the explicit purpose of having a machine I could experiment on freely — no locked-down group policies, no restrictions on what I could install or break. That same philosophy extends to how I work with AI agents: give them a real environment with real access, and let them fight real battles.

This project was built under that approach. The agent had full SSH access, root on Linux nodes, admin on Windows, hardware control via IPKVM, and the trust to operate overnight while I was in another city. The constraint wasn't permissions — it was judgement. Don't break what you can't physically reach to fix.

## Key Files

```
esp32-hid/                  ESP32-S3 USB HID keyboard firmware (PlatformIO)
  src/main.cpp              WiFi TCP + OTA + serial, USB keyboard emulation
  platformio.ini            Board config for YD-ESP32-23 (ESP32-S3-WROOM-1)
docs/
  offline-update-guide.md   DISM offline update via USB boot or WinRE
archive/
  images/                   Screenshots from the session
```

## Hardware

| Component | Details |
|-----------|---------|
| CPU | AMD (integrated Radeon Graphics) |
| RAM | 7 GB |
| Boot SSD | Samsung 970 EVO Plus 1TB |
| OS | Windows 11 IoT Enterprise LTSC 2024 (build 26100.8039) |
| IPKVM | YiShu ES2 + ESP32-S3 USB HID |
| Safety net | Ubuntu on USB SATA SSD |

## Future Actions

This machine is functional and receiving updates, but the job isn't done:

1. **In-place upgrade** — Run `setup.exe /auto upgrade` from the LTSC 2024 ISO to fully rebuild the CBS servicing stack. This should eliminate the 3,879 zombie ServerRdsh packages and resolve the edition migration scars permanently.

2. **Restore hardware security features** — VBS, HVCI, and Credential Guard were disabled as a temporary workaround for the DPC_WATCHDOG_VIOLATION. After the in-place upgrade, bisect to determine the minimum security configuration needed for stable reboots. The goal is full hardware integrity with all security features enabled.

3. **Improve the IPKVM** — The ESP32-S3 USB HID firmware needs OTA reliability improvements. The HDMI capture path should be standardized on the YiShu ES2 (most reliable). Consider adding mouse support for GUI-heavy scenarios.
