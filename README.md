# BKR_NWIPE

Angepasste Version von [nwipe](https://github.com/martijnvanbrummelen/nwipe) mit BKR-Branding und erweiterten Funktionen.

Basiert auf nwipe (Fork von `dwipe` / Darik's Boot and Nuke) mit folgenden Erweiterungen:

- **BKR-Branding**: Logo, angepasstes PDF-Zertifikat-Layout, modernisierte GUI
- **PDF-Zertifikate**: Device hostname, Inventory number, Disposition of Device (Checkboxen), Technician/Operator ID
- **Secure Erase / Sanitize**: Hardware-basierte Loesch-Methoden fuer ATA, NVMe und SCSI (inkl. SSDs)
- **Per-Disk Metadaten**: Hostname und Inventarnummer pro Festplatte direkt in der GUI eingeben (H/I Tasten)
- **E-Mail-Versand**: Automatischer Versand der PDF-Zertifikate per SMTP nach Wipe-Abschluss

---

## Installation auf Debian 13 (Trixie)

### 1. System vorbereiten

```bash
sudo apt update && sudo apt upgrade -y
```

### 2. Abhaengigkeiten installieren

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
  hdparm
```

### 3. Repository klonen und kompilieren

```bash
cd /root
git clone https://github.com/NKr-BKRennen/bkr_nwipe.git
cd bkr_nwipe
./autogen.sh
./configure
make -j$(nproc)
sudo make install
```

### 4. Testen

```bash
sudo nwipe
```

Oder direkt aus dem Build-Verzeichnis:

```bash
cd /root/bkr_nwipe/src
sudo ./nwipe
```

### 5. Autostart einrichten (optional)

Damit nwipe beim Booten automatisch auf tty1 startet (z.B. fuer dedizierte Loesch-Stationen):

**Auto-Login fuer root auf tty1 aktivieren:**

```bash
sudo mkdir -p /etc/systemd/system/getty@tty1.service.d
sudo tee /etc/systemd/system/getty@tty1.service.d/override.conf > /dev/null << 'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear %I $TERM
EOF
```

**nwipe beim Login automatisch starten:**

```bash
cat >> /root/.bash_profile << 'PROFILE'

# BKR_NWIPE Autostart auf tty1
if [[ "$(tty)" == "/dev/tty1" ]]; then
    nwipe
fi
PROFILE
```

**System neustarten:**

```bash
sudo systemctl daemon-reload
sudo reboot
```

Nach dem Neustart startet nwipe automatisch auf dem ersten Terminal.

### 6. Aktualisieren

```bash
cd /root/bkr_nwipe
git pull
./autogen.sh
./configure
make -j$(nproc)
sudo make install
```

---

## Schnellinstallation (Copy-Paste)

Alles in einem Block fuer eine frische Debian 13 Installation:

```bash
sudo apt update && sudo apt install -y \
  build-essential pkg-config automake autoconf libtool git \
  libncurses-dev libparted-dev libconfig-dev libconfig++-dev \
  dmidecode coreutils smartmontools hdparm && \
cd /root && \
git clone https://github.com/NKr-BKRennen/bkr_nwipe.git && \
cd bkr_nwipe && \
./autogen.sh && ./configure && make -j$(nproc) && sudo make install && \
echo "Installation abgeschlossen. Starten mit: sudo nwipe"
```

---

## Loesch-Methoden

### Software-basierte Methoden

| Methode | Beschreibung | Paesse |
|---------|-------------|--------|
| Fill With Zeros | Fuellt mit Nullen (`0x00`) | 1 |
| Fill With Ones | Fuellt mit Einsen (`0xFF`) | 1 |
| RCMP TSSIT OPS-II | Royal Canadian Mounted Police Standard | 8 |
| DoD Short | US DoD 5220.22-M (kurz) | 3 |
| DoD 5220.22-M | US DoD 5220.22-M (voll) | 7 |
| Gutmann Wipe | Peter Gutmann 35-Pass Methode | 35 |
| PRNG Stream | Zufallsdaten vom gewaehlten PRNG | 1 |
| HMG IS5 Enhanced | UK HMG IS5 (Enhanced) | 3 |
| Schneier Wipe | Bruce Schneier 7-Pass Methode | 7 |
| BMB21-2019 | Chinesischer Standard fuer Datensanitisierung | 6 |

### Hardware-basierte Methoden (Secure Erase / Sanitize)

Diese Methoden arbeiten auf Firmware-Ebene und erreichen auch versteckte/reservierte SSD-Bereiche:

| Methode | CLI-Flag | Beschreibung |
|---------|----------|-------------|
| Secure Erase | `--method=secure_erase` | ATA/NVMe Secure Erase + Zero-Verifikation |
| Secure Erase + PRNG | `--method=secure_erase_prng` | Secure Erase + PRNG-Pass + Verifikation |
| Sanitize Crypto Erase | `--method=sanitize_crypto` | Zerstoert den Encryption Key (NVMe/SCSI) |
| Sanitize Block Erase | `--method=sanitize_block` | Block Erase (NVMe/SCSI) |
| Sanitize Overwrite | `--method=sanitize_overwrite` | Sanitize Overwrite (SCSI) |

Verfuegbar ueber **GUI** unter "Secure Erase / Sanitize >" im Methoden-Menue.

> **Voraussetzungen:** `hdparm` (ATA), `nvme-cli` (NVMe), `sg3_utils` (SCSI)

---

## GUI-Bedienung

### Tastenbelegung (Hauptbildschirm)

| Taste | Funktion |
|-------|----------|
| **Space** | Festplatte auswaehlen/abwaehlen |
| **S** | Wipe starten (Shift+S) |
| **H** | Hostname fuer fokussierte Festplatte setzen |
| **I** | Inventarnummer fuer fokussierte Festplatte setzen |
| **m** | Loesch-Methode waehlen |
| **p** | PRNG waehlen |
| **v** | Verifikation einstellen |
| **r** | Anzahl Durchlaeufe |
| **b** | Blanking ein/aus |
| **d** | Schreibrichtung |
| **c** | Konfiguration (Organisation, Kunde, PDF, E-Mail) |
| **Ctrl+A** | Alle Festplatten auswaehlen |
| **Ctrl+C** | Beenden |

### Per-Disk Metadaten

Vor dem Starten des Wipes koennen pro Festplatte **Hostname** und **Inventarnummer** gesetzt werden.
Diese Werte werden direkt in das PDF-Zertifikat geschrieben.

1. Mit Pfeiltasten die gewuenschte Festplatte fokussieren
2. **H** druecken → Hostname eingeben → Enter
3. **I** druecken → Inventarnummer eingeben → Enter
4. In der Disk-Liste erscheint `[H:hostname I:INV-001]` als Bestaetigung

---

## E-Mail-Konfiguration

Nach Abschluss eines Wipes kann das PDF-Zertifikat automatisch per E-Mail versendet werden.

Konfiguration in `/etc/nwipe/nwipe.conf`:

```
Email_Settings: {
  Email_Enable = "ENABLED"
  SMTP_Server = "mailserver.example.com"
  SMTP_Port = "25"
  Sender_Address = "nwipe@example.com"
  Recipient_Address = "it-team@example.com"
}
```

> Standardmaessig deaktiviert. Unterstuetzt SMTP ohne Authentifizierung (Port 25, internes Netz).

---

## Bugs

* BKR_NWIPE: [https://github.com/NKr-BKRennen/bkr_nwipe](https://github.com/NKr-BKRennen/bkr_nwipe)
* Original nwipe: [https://github.com/martijnvanbrummelen/nwipe](https://github.com/martijnvanbrummelen/nwipe)

## Lizenz

nwipe ist lizenziert unter der **GNU General Public License v2.0**.
Siehe `LICENSE` fuer Details.
