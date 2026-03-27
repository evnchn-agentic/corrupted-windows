---
name: deskpc-offline-update-guide
description: Step-by-step guides for applying Windows cumulative updates offline via WinRE or USB WinPE on DeskPC
type: reference
---

# DeskPC Offline Update Guides

The DeskPC's component store has a recurring corruption issue that prevents cumulative updates from installing while Windows is running. Offline servicing bypasses the online CBS pipeline entirely.

## Prerequisites (both approaches)

- MSU files on C: drive (already there from previous attempts):
  - `C:\Users\User\Downloads\windows11.0-kb5085516-x64_*.msu`
  - `C:\Users\User\Downloads\windows11.0-kb5043080-x64_*.msu` (SSU)
- Or download fresh ones from Microsoft Update Catalog

## Approach 1: Boot from LTSC ISO USB

### Preparation (while in Windows)

1. **Create bootable USB** from the LTSC 2024 ISO:
   ```
   C:\Users\User\Downloads\en-us_windows_11_iot_enterprise_ltsc_2024_x64_dvd_f6b14814.iso
   ```
   Use Rufus or `C:\SpaceSniffer` folder has it, or any USB tool.

2. **Copy MSU files to a known location** on C: (they're already in Downloads)

3. **Note the MSU filenames** — you'll need to type them in WinPE command prompt

### Boot & Apply

1. Boot from USB (F12/F2 for boot menu on DeskPC)
2. At the Windows Setup screen, press **Shift+F10** to open Command Prompt
3. **Find the Windows partition** — in WinPE it might not be C:
   ```cmd
   dir C:\Windows
   dir D:\Windows
   dir E:\Windows
   ```
   Whichever has `\Windows\System32` is your target. Call it `X:` below.

4. **Find the MSU files**:
   ```cmd
   dir X:\Users\User\Downloads\*.msu
   ```

5. **Apply SSU first** (required before cumulative):
   ```cmd
   DISM /Image:X:\ /Add-Package /PackagePath:X:\Users\User\Downloads\windows11.0-kb5043080-x64_953449672073f8fb99badb4cc6d5d7849b9c83e8.msu
   ```

6. **Apply cumulative update**:
   ```cmd
   DISM /Image:X:\ /Add-Package /PackagePath:X:\Users\User\Downloads\windows11.0-kb5085516-x64_52aef89bc1afc5e67eec927556ec6926122936ad.msu
   ```

7. **Wait** — this takes 10-30 minutes. Don't interrupt.

8. **Also repair the component store while you're here**:
   ```cmd
   DISM /Image:X:\ /Cleanup-Image /RestoreHealth /Source:WIM:X:\Setup\sources\install.wim:2
   ```

9. **Close command prompt and reboot** — remove USB, boot normally.

### Expected Result
Windows boots with the update already applied. No online CBS staging needed.

---

## Approach 3: WinRE (No USB Required)

### Preparation (while in Windows, via SSH)

1. **Enable WinRE**:
   ```powershell
   reagentc /enable
   reagentc /info   # verify: Windows RE status = Enabled
   ```

2. **Copy MSU files somewhere WinRE can access** — WinRE mounts C: as a different letter, but files on the NTFS partition are accessible:
   ```powershell
   New-Item C:\OfflineUpdate -ItemType Directory -Force
   Copy-Item "C:\Users\User\Downloads\windows11.0-kb5043080-x64_*.msu" C:\OfflineUpdate\
   Copy-Item "C:\Users\User\Downloads\windows11.0-kb5085516-x64_*.msu" C:\OfflineUpdate\
   ```

3. **Schedule boot into WinRE**:
   ```powershell
   # Option A: From Settings > System > Recovery > Advanced startup > Restart now
   # Option B: From command line:
   reagentc /boottore
   shutdown /r /t 0
   ```

### In WinRE

1. Select **Troubleshoot > Advanced options > Command Prompt**
2. **Find the Windows partition**:
   ```cmd
   dir C:\Windows
   dir D:\Windows
   ```
   Also find the OfflineUpdate folder:
   ```cmd
   dir C:\OfflineUpdate
   dir D:\OfflineUpdate
   ```

3. **Apply SSU first**:
   ```cmd
   DISM /Image:D:\ /Add-Package /PackagePath:D:\OfflineUpdate\windows11.0-kb5043080-x64_953449672073f8fb99badb4cc6d5d7849b9c83e8.msu
   ```
   (Adjust D: to wherever Windows lives)

4. **Apply cumulative update**:
   ```cmd
   DISM /Image:D:\ /Add-Package /PackagePath:D:\OfflineUpdate\windows11.0-kb5085516-x64_52aef89bc1afc5e67eec927556ec6926122936ad.msu
   ```

5. **Optional — repair component store**:
   ```cmd
   DISM /Image:D:\ /Cleanup-Image /RestoreHealth /Source:WIM:D:\Setup\sources\install.wim:2
   ```

6. **Reboot** — type `exit` to leave command prompt, then select "Continue" to boot Windows.

### WinRE Caveats

- WinRE has limited DISM — it might not support MSU files directly. If so, extract the CAB first:
  ```cmd
  mkdir D:\OfflineUpdate\extracted
  expand D:\OfflineUpdate\windows11.0-kb5085516-x64_*.msu -F:* D:\OfflineUpdate\extracted
  DISM /Image:D:\ /Add-Package /PackagePath:D:\OfflineUpdate\extracted\SSU-26100.8035-x64.cab
  ```
  Then apply each CAB individually.
- WinRE's DISM version may be older — USB/WinPE from the ISO has the latest DISM.
- Drive letters WILL be different from normal Windows. Always verify with `dir`.

---

## Which to Choose?

| | USB (Approach 1) | WinRE (Approach 3) |
|---|---|---|
| Needs USB drive | Yes | No |
| DISM version | Latest (from ISO) | Older (from recovery) |
| MSU support | Full | May need CAB extraction |
| Reliability | Higher | Medium |
| Can repair component store | Yes (ISO has install.wim) | Yes (if C:\Setup exists) |
| Remote-friendly | No (need boot menu) | Semi (can trigger via SSH, but need keyboard for WinRE menu) |

**Recommendation: Approach 1 (USB) is more reliable.** Approach 3 is a good fallback if no USB is handy.

## Remote Preparation (can do via SSH before going physical)

```powershell
# Ensure MSU files are in a simple path
New-Item C:\OfflineUpdate -ItemType Directory -Force
Copy-Item "C:\Users\User\Downloads\windows11.0-kb5043080-x64_*.msu" C:\OfflineUpdate\ -ErrorAction SilentlyContinue
Copy-Item "C:\Users\User\Downloads\windows11.0-kb5085516-x64_*.msu" C:\OfflineUpdate\ -ErrorAction SilentlyContinue

# Also pre-extract CABs in case WinRE DISM can't handle MSU
C:\tools\7z.exe x "C:\OfflineUpdate\*.msu" -oC:\OfflineUpdate\extracted -y

# Enable WinRE
reagentc /enable

# Verify
reagentc /info
dir C:\OfflineUpdate\
dir C:\OfflineUpdate\extracted\
```
