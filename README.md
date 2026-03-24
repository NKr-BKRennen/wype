# WYPE

Customized version of [nwipe](https://github.com/martijnvanbrummelen/nwipe) with custom branding and extended features.

Based on nwipe (fork of `dwipe` / Darik's Boot and Nuke) with the following extensions:

- **Wype Branding**: Logo, customized PDF certificate layout, modernized GUI
- **PDF Certificates**: Hostname, Inventory Number, organization, customer on the certificate
- **Secure Erase / Sanitize**: Hardware-based wipe methods for ATA, NVMe and SCSI (incl. SSDs)
- **Per-Disk Metadata**: Hostname, Inventory Number and optional Comment per disk directly editable in the GUI
- **Email Delivery**: Batch delivery of all PDF certificates via SMTP after confirmation
- **Help & Changelog**: Accessible directly in the GUI (`h` and `l`)
- **Live Clock**: Current time always visible in the header
- **Smart Warnings**: Internet/NTP check on startup, SSD/HDD method mismatch detection, firmware erase pre-flight check
- **Dashboard API**: Built-in HTTP/JSON API for central monitoring via [wype-dashboard](https://github.com/NKr-BKRennen/wype-dashboard)
- **IP Address Display**: System IP shown in the header next to the clock

> **For a bootable USB/ISO image** that starts directly into Wype (without an installed OS): [wypeOS](https://github.com/NKr-BKRennen/wypeOS)
> **For central monitoring of multiple wype nodes**: [wype-dashboard](https://github.com/NKr-BKRennen/wype-dashboard)

---

## Typical Workflow

This is what a typical wipe process with Wype looks like:

1. **Start Wype** → `sudo wype`
2. **Startup Overview** → review organisation and customer details → select or change customer → press `A` to continue
3. **Internet Check** → if no internet is available, Wype warns about NTP and shows the current system time for verification
4. **Select Wipe Method** → press `m` → e.g. "DoD 5220.22-M" for HDDs or "Secure Erase / Sanitize >" for SSDs
5. **Adjust options** (optional) → `v` Verification, `r` Rounds, `p` PRNG, `b` Blanking
6. **Select disks** → Arrow keys + `Space` (or `Ctrl+A` for all)
7. **Enter metadata** → press `e` on each disk → enter Hostname, Inventory Number and optional Comment
8. **Start wipe** → `S` (Shift+S) — Wype automatically warns if metadata is missing and if the wipe method doesn't match the drive type (SSD/HDD)
9. **Wait** → Progress is displayed live. The current time is shown in the header. If email delivery is active, a notification is sent when all wipes are finished.
10. **Confirm** → press `Enter` → PDFs are created and sent via email

> **Tip:** `h` shows a Help page with all key bindings at any time. `l` shows the Changelog.

---

## Configuration

Wype stores all settings in `/etc/wype/wype.conf` (libconfig format).
The file is automatically created with default values on **first startup**.

There are **three ways** to change settings:

| Way | What | When |
|-----|------|------|
| **GUI Config Menu** (`c` key) | Organization, customer, PDF options | At runtime, saved to `wype.conf` |
| **GUI Settings** (keys `m`, `v`, `r`, `p`, `b`, `d`) | Wipe Method, Verification, Rounds etc. | At runtime, only for current session |
| **Edit `/etc/wype/wype.conf` directly** | Everything incl. email settings | Before startup, persistent |

### Organization Details

These details appear on every PDF wipe certificate. **Configure before first use:**

```
Organisation_Details: {
  Business_Name = "Meine Firma GmbH"
  Business_Address = "Musterstr. 1, 12345 Berlin"
  Contact_Name = "Max Mustermann"
  Contact_Phone = "+49 30 123456"
  Op_Tech_Name = "Techniker Name"
}
```

> Configurable via **c** → "PDF Report - Edit Organisation" in the GUI.

### Customer Assignment

Optionally, a customer can be assigned that appears on the certificate:

```
Selected_Customer: {
  Customer_Name = "Kunde AG"
  Customer_Address = "Kundenweg 5, 54321 Hamburg"
  Contact_Name = "Ansprechpartner"
  Contact_Phone = "+49 40 654321"
}
```

> Customers can be managed via **c** → "PDF Report - Select/Add/Delete Customer".
> Customer data is stored in `/etc/wype/wype_customers.csv`.

### PDF Certificate

```
PDF_Certificate: {
  PDF_Enable = "ENABLED"
  PDF_Preview = "ENABLED"
  PDF_Host_Visibility = "DISABLED"
  PDF_tag = "DISABLED"
  User_Defined_Tag = "Empty Tag"
}
```

| Setting | Description |
|---------|-------------|
| `PDF_Enable` | Create PDF certificates after wipe (`ENABLED`/`DISABLED`) |
| `PDF_Preview` | Show organisation/customer overview before drive selection (`ENABLED`/`DISABLED`, default: `ENABLED`) |
| `PDF_Host_Visibility` | Show system hostname on the certificate |
| `PDF_tag` | Show user-defined tag on the certificate |
| `User_Defined_Tag` | Free-text tag for the certificate |

### Per-Disk Metadata (Hostname / Inventory Number / Comment)

Individual values can be set per disk:

1. Focus a disk with arrow keys
2. Press **e** → enter Hostname, Inventory Number and optional Comment (Tab switches between fields) → Enter

| Field | On PDF Certificate | In Email |
|-------|-------------------|----------|
| Hostname | Yes | Yes |
| Inventory Number | Yes | Yes |
| Comment | **No** | Yes |

> These values are **not** saved in `wype.conf`, but only held at runtime.
> Hostname and Inventory Number are written to the PDF certificate. The Comment field is included in the summary email only.
> When starting a wipe, Wype automatically warns if Hostname or Inventory Number is missing for a selected disk.

### Email Delivery (SMTP)

After all wipes are completed, the PDF certificates are sent in a batch via email:

1. All wipes finished → notification email ("Wipe finished, please confirm at the device")
2. User presses Enter → PDFs are created and sent in **one** email
3. On success: local PDFs are deleted. On failure: PDFs are kept locally.

The email status is displayed in the options window (green "Active" / red "Disabled").

```
Email_Settings: {
  Email_Enable = "ENABLED"
  SMTP_Server = "mailserver.example.com"
  SMTP_Port = "25"
  Sender_Address = "wype@example.com"
  Recipient_Address = "it-team@example.com"
}
```

| Setting | Description |
|---------|-------------|
| `Email_Enable` | Enable email delivery (`ENABLED`/`DISABLED`) |
| `SMTP_Server` | Hostname or IP of the SMTP server |
| `SMTP_Port` | SMTP port (default: `25`) |
| `Sender_Address` | Sender address |
| `Recipient_Address` | Recipient address (empty = no delivery) |

> Disabled by default. Supports SMTP without authentication (port 25, internal network).
> Email can be toggled on/off in the GUI settings menu (`c` key). SMTP server details must be edited in `/etc/wype/wype.conf`.

### Dashboard API

Wype includes a built-in HTTP/JSON API server for central monitoring via the [wype-dashboard](https://github.com/NKr-BKRennen/wype-dashboard). When enabled, other systems can query the wipe status of all disks in real time.

```
Dashboard:
{
  API_Port = "5000";
  API_Password = "wype";
  Dashboard_URL = "";
};
```

| Setting | Description |
|---------|-------------|
| `API_Port` | HTTP port the API listens on (default: `5000`) |
| `API_Password` | Shared password for `X-API-Key` authentication (default: `wype`) |
| `Dashboard_URL` | `host:port` of the central dashboard (empty = self-registration disabled) |

The API password can also be set via environment variable: `WYPE_API_PASSWORD=your-password`

**Self-Registration:** When `Dashboard_URL` is set (e.g. `192.168.1.5:8080`), wype automatically registers itself with the central dashboard every 30 seconds. New nodes appear immediately and are polled for status right away.

**Endpoints:**

| Endpoint | Description |
|----------|-------------|
| `GET /api/v1/health` | Health check |
| `GET /api/v1/status` | Full JSON status (node info + all disks with progress, throughput, ETA, temperature, errors) |

All requests require the `X-API-Key: <password>` header. The API status ("Enabled" or "Disabled") is shown in the options window.

> The API server requires `libmicrohttpd` and `cjson` libraries at compile time. If not available, wype builds without the API (no runtime impact).
> For central monitoring of multiple wype nodes, see [wype-dashboard](https://github.com/NKr-BKRennen/wype-dashboard).

---

## Installation on Debian 13 (Trixie)

### 1. Prepare the system

```bash
sudo apt update && sudo apt upgrade -y
```

### 2. Install dependencies

```bash
sudo apt install -y \
  build-essential \
  pkg-config \
  automake \
  autoconf \
  libtool \
  git \
  libncurses-dev \
  libparted-dev \
  libconfig-dev \
  libconfig++-dev \
  dmidecode \
  coreutils \
  smartmontools \
  hdparm \
  libmicrohttpd-dev \
  libcjson-dev
```

> `libmicrohttpd-dev` and `libcjson-dev` are optional — they enable the Dashboard API server. Without them, wype builds and runs normally but without the API.

### 3. Clone repository and compile

```bash
cd /root
git clone https://github.com/NKr-BKRennen/wype.git
cd wype
chmod +x build.sh autogen.sh
./build.sh
```

Alternatively, manually:

```bash
chmod +x autogen.sh
./autogen.sh
./configure
make -j$(nproc)
sudo make install
```

### 4. Adjust configuration

On first startup, `/etc/wype/wype.conf` is created with default values. Then adjust:

```bash
sudo nano /etc/wype/wype.conf
```

Or in the GUI via the **c** key (organization, customer, PDF settings).

### 5. Start

```bash
sudo wype
```

### 6. Set up autostart (optional)

To have wype start automatically on tty1 at boot (e.g. for dedicated wipe stations):

```bash
chmod +x setup.sh
sudo ./setup.sh
```

The interactive menu lets you enable/disable:
- **Auto-login** — root logs in automatically on tty1 (no password prompt)
- **Autostart** — wype launches automatically after login on tty1

Choose option **1** for the recommended setup (both enabled), or configure individually. Reboot after setup.

### 7. Update

```bash
cd /root/wype
./update.sh
```

---

## Quick Installation (Copy-Paste)

Everything in one block for a fresh Debian 13 installation:

```bash
sudo apt update && sudo apt install -y \
  build-essential pkg-config automake autoconf libtool git \
  libncurses-dev libparted-dev libconfig-dev libconfig++-dev \
  dmidecode coreutils smartmontools hdparm \
  libmicrohttpd-dev libcjson-dev && \
cd /root && \
git clone https://github.com/NKr-BKRennen/wype.git && \
cd wype && chmod +x build.sh autogen.sh && ./build.sh && \
echo "Installation complete. Start with: sudo wype"
```

---

## Wipe Methods

### Software-based Methods

| Method | Description | Passes |
|--------|-------------|--------|
| Fill With Zeros | Fills with zeros (`0x00`) | 1 |
| Fill With Ones | Fills with ones (`0xFF`) | 1 |
| RCMP TSSIT OPS-II | Royal Canadian Mounted Police Standard | 8 |
| DoD Short | US DoD 5220.22-M (short) | 3 |
| DoD 5220.22-M | US DoD 5220.22-M (full) — **recommended for HDDs** | 7 |
| Gutmann Wipe | Peter Gutmann 35-pass method | 35 |
| PRNG Stream | Random data from the selected PRNG | 1 |
| HMG IS5 Enhanced | UK HMG IS5 (Enhanced) | 3 |
| Schneier Wipe | Bruce Schneier 7-pass method | 7 |
| BMB21-2019 | Chinese standard for data sanitization | 6 |

### Hardware-based Methods (Secure Erase / Sanitize)

These methods operate at the firmware level and reach hidden/reserved SSD areas.
**Recommended for SSDs/NVMe** — software overwriting is not reliable for flash storage.

| Method | CLI Flag | Description |
|--------|----------|-------------|
| Secure Erase | `--method=secure_erase` | ATA/NVMe Secure Erase + zero verification |
| Secure Erase + PRNG | `--method=secure_erase_prng` | Secure Erase + PRNG pass + verification |
| Sanitize Crypto Erase | `--method=sanitize_crypto` | Destroys the encryption key — **recommended for SSDs** |
| Sanitize Crypto Erase + PRNG + Verify | `--method=sanitize_crypto_verify` | Crypto Erase + full read-back verification for PDF proof |
| Sanitize Block Erase | `--method=sanitize_block` | Block Erase (NVMe/SCSI) |
| Sanitize Overwrite | `--method=sanitize_overwrite` | Sanitize Overwrite (SCSI) |

Available via **GUI**: `m` → "Secure Erase / Sanitize >" in the method menu.

> **Prerequisites:** `hdparm` (ATA), `nvme-cli` (NVMe), `sg3_utils` (SCSI)

---

## GUI Operation

### Key Bindings (Main Screen)

| Key | Function |
|-----|----------|
| **Space** | Select/deselect disk |
| **S** | Start wipe (Shift+S) — checks for missing metadata |
| **e** | EditDisk: edit Hostname/Inventory Number/Comment for the focused disk |
| **m** | Select Wipe Method |
| **p** | Select PRNG |
| **v** | Set Verification |
| **r** | Number of Rounds |
| **b** | TFT saver (dim) → blank screen → normal (cycle) |
| **d** | Write Direction |
| **c** | Configuration (organization, customer, PDF) |
| **t** | Details for the focused disk |
| **h** | Show Help |
| **l** | Show Changelog |
| **F5** | Rescan disks (hot-plug) |
| **Ctrl+A** | Select all disks |
| **Ctrl+C** | Quit |

### Config Menu (`c` key)

The **c** key opens the configuration menu with the following options:

- **PDF Report - Edit Organisation** → Company name, address, contact person, technician
- **PDF Report - Select/Add/Delete Customer** → Customer management for the certificate
- **PDF Report - Enable/Disable PDF** → Enable/disable PDF creation
- **PDF Report - Preview** → Show organisation/customer overview before drive selection
- **PDF Report - Host Visibility** → Show system hostname on certificate
- **PDF Report - Custom Tag** → Free-text tag on the certificate
- **Email Delivery** → Toggle email on/off (SMTP details in `wype.conf`)

All changes made here are persistently saved in `/etc/wype/wype.conf`.

---

## Bugs

* WYPE: [https://github.com/NKr-BKRennen/wype](https://github.com/NKr-BKRennen/wype)
* Original nwipe: [https://github.com/martijnvanbrummelen/nwipe](https://github.com/martijnvanbrummelen/nwipe)

## License

Wype is licensed under the **GNU General Public License v2.0**.
See `LICENSE` for details.

---

## Versioning

Wype uses [Semantic Versioning](https://semver.org/): `MAJOR.MINOR.PATCH`

- **MAJOR**: Incompatible changes (e.g. new config format, breaking changes)
- **MINOR**: New features, backwards compatible
- **PATCH**: Bug fixes, minor improvements

---

## Changelog

### v1.8.0 (2026-03-24)

**Add:**
- Pre-flight firmware erase check: before starting a firmware wipe (Sanitize Crypto/Block Erase), Wype checks if the drive actually supports it (NVMe sanicap / ATA hdparm) and aborts with an info window if not

**Fix:**
- GUI: options window bottom border now visible (height reduced to prevent overlap with main window)
- GUI: stats window height matches options window, IP address and clock moved from header to stats window
- NVMe device type now shows "NVME" instead of "NVME-SSD"
- Renamed "Sanitize Crypto Erase + Verify" to "Sanitize Crypto Erase + PRNG + Verify"

### v1.7.1 (2026-03-24)

**Fix:**
- NVMe drives now correctly detected as SSD — prevents incorrect wipe method warnings when using SSD-specific methods (e.g. Sanitize Crypto Erase) on NVMe devices

### v1.7.0 (2026-03-24)

**Add:**
- SMART attributes exposed via Dashboard API: health status, power-on hours, power cycles, reallocated/pending sectors, UDMA CRC errors, spin-up time, firmware version, SSD percentage used
- SMART data read via smartctl during device enumeration (supports ATA and NVMe)

### v1.6.1 (2026-03-24)

**Fix:**
- Race condition in device enumeration: device context was added to the array before being zeroed, causing the API to return garbage values (uninitialized memory)

### v1.6.0 (2026-03-24)

**Add:**
- Per-disk comment field: optional free-text comment per disk, included in email only (not on PDF certificate)
- Comment editable in metadata editor (`e` key) — Tab/Arrow cycles through Hostname, Inventory Number and Comment
- Comment exposed via Dashboard API (`comment` field in disk JSON)

### v1.4.0 (2026-03-23)

**Add:**
- Dashboard API: built-in HTTP/JSON API server (libmicrohttpd + cJSON) for central monitoring via [wype-dashboard](https://github.com/NKr-BKRennen/wype-dashboard)
- IP address display in the header next to the clock — always visible
- API status display in the options window (port number or "Disabled")
- Live clock display (HH:MM) in the header — always visible during device selection and wipe
- Internet connectivity check on startup: warns if NTP time sync is unavailable
- SSD/HDD method mismatch warning when starting a wipe (SSDs with software methods / HDDs with firmware methods)
- Kernel console message suppression in autostart to prevent ATA errors from overwriting the UI

**Fix:**
- Timezone forced to `Europe/Berlin` for correct certificate timestamps
- Internet check with dual DNS fallback (Google + Cloudflare)

### v1.3.0 (2026-03-20)

**Add:**
- Startup overview screen: shows all organisation and customer details on every launch
- Direct customer selection from the startup screen
- Edit organisation directly from the startup screen
- Email delivery can be toggled on/off directly in the settings menu
- Boot device detection via `/proc/mounts` — USB sticks running wypeOS now show `[IN USE]`

**Fix:**
- TFT saver mode (`b` key during wipe) now dims all colors consistently (was partially unreadable)
- NVMe Sanitize Crypto Erase now logs the actual error message instead of discarding it
- Startup overview now respects the PDF_Preview setting (was always shown regardless)

**Change:**
- PDF_Preview default changed to `ENABLED`
- Startup overview shown before drive selection only when PDF_Preview is enabled
- Updated workflow: review org/customer before drive selection

### v1.2.0 (2026-03-17)

**Add:**
- Sanitize Crypto Erase + PRNG + Verify: new wipe method — crypto erase with full read-back verification for PDF certificate proof
- Unified settings menu (`c` key) with arrow navigation (method, PRNG, Verification, Rounds, Blanking, Write Direction, Organization & PDF, Email)
- Disk metadata editor (`e` key): Hostname + Inventory Number in a single dialog with Tab switching
- Help page (`h` key) with all key bindings, explanations and workflow
- Changelog view (`l` key) directly in the GUI
- Email status display in the options window (Active/Disabled)
- Warning when starting a wipe if Hostname/Inventory Number is missing (confirm per disk)
- Batch email: all PDF certificates in one email after Enter confirmation
- Notification email when wipe is finished ("please confirm at the device")
- Local PDFs are automatically deleted after successful email delivery
- Post-wipe email status feedback in the log
- Disk rescan (`F5`): hot-plug detection of new disks without restart

**Change:**
- All settings accessible via a central menu (`c` key)
- Footer redesigned: clearer key bindings
- Key bindings: `e`=EditDisk, `c`=Settings, `h`=Help, `l`=Changelog (instead of H/I for individual fields)
- "Inventory Number" is now written out in full in the GUI (instead of abbreviated form)

### v1.1.0 (2026-03-17)

**Add:**
- Per-disk metadata: Hostname and Inventory Number per disk
- Automatic email delivery of PDF certificates via SMTP
- Secure Erase / Sanitize methods for ATA, NVMe and SCSI (hardware-based)
- ASCII art "BK RENNEN" logo in the GUI header
- `build.sh` and `update.sh` scripts for simplified building and updating
- Email configuration in `/etc/wype/wype.conf`

**Fix:**
- Terminal background no longer stays blue after quitting (Ctrl+C)
- Footer bar now has a uniform background (no more white bar)
- [IN USE] and [HS? YES] tags are now red on blue instead of red on white
- Hostname/Inventory Number are now reliably written to the PDF certificate
- Barcode on PDF certificate disabled

**Change:**
- Project renamed from nwipe/BKR_NWIPE to Wype
- Modernized GUI: color scheme (Teal/Navy/Yellow), colored status tags, progress bars
- Custom versioning scheme (Semantic Versioning)
- README completely in English, Debian 13 only

**Remove:**
- Support for other distributions (Debian 13 only now)
- Barcode on PDF certificate (code remains, just disabled)

### v1.0.0 (2026-03-16)

**Add:**
- Initial release based on nwipe 0.40
- BKR branding: logo on PDF certificate
- PDF wipe certificates with organization and customer details
