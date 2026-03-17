# WYPE

Angepasste Version von [nwipe](https://github.com/martijnvanbrummelen/nwipe) mit eigenem Branding und erweiterten Funktionen.

Basiert auf nwipe (Fork von `dwipe` / Darik's Boot and Nuke) mit folgenden Erweiterungen:

- **Wype-Branding**: Logo, angepasstes PDF-Zertifikat-Layout, modernisierte GUI
- **PDF-Zertifikate**: Device hostname, Inventory number, Disposition of Device (Checkboxen), Technician/Operator ID
- **Secure Erase / Sanitize**: Hardware-basierte Lösch-Methoden für ATA, NVMe und SCSI (inkl. SSDs)
- **Per-Disk Metadaten**: Hostname und Inventarnummer pro Festplatte direkt in der GUI eingeben (H/I Tasten)
- **E-Mail-Versand**: Automatischer Versand der PDF-Zertifikate per SMTP nach Wipe-Abschluss

---

## Installation auf Debian 13 (Trixie)

### 1. System vorbereiten

```bash
sudo apt update && sudo apt upgrade -y
```

### 2. Abhängigkeiten installieren

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
git clone https://github.com/NKr-BKRennen/wype.git
cd wype
chmod +x build.sh
./build.sh
```

Alternativ manuell:

```bash
./autogen.sh
./configure
make -j$(nproc)
sudo make install
```

### 4. Testen

```bash
sudo wype
```

Oder direkt aus dem Build-Verzeichnis:

```bash
cd /root/wype/src
sudo ./wype
```

### 5. Autostart einrichten (optional)

Damit wype beim Booten automatisch auf tty1 startet (z.B. für dedizierte Lösch-Stationen):

**Auto-Login für root auf tty1 aktivieren:**

```bash
sudo mkdir -p /etc/systemd/system/getty@tty1.service.d
sudo tee /etc/systemd/system/getty@tty1.service.d/override.conf > /dev/null << 'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear %I $TERM
EOF
```

**wype beim Login automatisch starten:**

```bash
cat >> /root/.bash_profile << 'PROFILE'

# WYPE Autostart auf tty1
if [[ "$(tty)" == "/dev/tty1" ]]; then
    wype
fi
PROFILE
```

**System neustarten:**

```bash
sudo systemctl daemon-reload
sudo reboot
```

Nach dem Neustart startet wype automatisch auf dem ersten Terminal.

### 6. Aktualisieren

```bash
cd /root/wype
./update.sh
```

---

## Schnellinstallation (Copy-Paste)

Alles in einem Block für eine frische Debian 13 Installation:

```bash
sudo apt update && sudo apt install -y \
  build-essential pkg-config automake autoconf libtool git \
  libncurses-dev libparted-dev libconfig-dev libconfig++-dev \
  dmidecode coreutils smartmontools hdparm && \
cd /root && \
git clone https://github.com/NKr-BKRennen/wype.git && \
cd wype && chmod +x build.sh && ./build.sh && \
echo "Installation abgeschlossen. Starten mit: sudo wype"
```

---

## Lösch-Methoden

### Software-basierte Methoden

| Methode | Beschreibung | Pässe |
|---------|-------------|--------|
| Fill With Zeros | Füllt mit Nullen (`0x00`) | 1 |
| Fill With Ones | Füllt mit Einsen (`0xFF`) | 1 |
| RCMP TSSIT OPS-II | Royal Canadian Mounted Police Standard | 8 |
| DoD Short | US DoD 5220.22-M (kurz) | 3 |
| DoD 5220.22-M | US DoD 5220.22-M (voll) | 7 |
| Gutmann Wipe | Peter Gutmann 35-Pass Methode | 35 |
| PRNG Stream | Zufallsdaten vom gewählten PRNG | 1 |
| HMG IS5 Enhanced | UK HMG IS5 (Enhanced) | 3 |
| Schneier Wipe | Bruce Schneier 7-Pass Methode | 7 |
| BMB21-2019 | Chinesischer Standard für Datensanitisierung | 6 |

### Hardware-basierte Methoden (Secure Erase / Sanitize)

Diese Methoden arbeiten auf Firmware-Ebene und erreichen auch versteckte/reservierte SSD-Bereiche:

| Methode | CLI-Flag | Beschreibung |
|---------|----------|-------------|
| Secure Erase | `--method=secure_erase` | ATA/NVMe Secure Erase + Zero-Verifikation |
| Secure Erase + PRNG | `--method=secure_erase_prng` | Secure Erase + PRNG-Pass + Verifikation |
| Sanitize Crypto Erase | `--method=sanitize_crypto` | Zerstört den Encryption Key (NVMe/SCSI) |
| Sanitize Block Erase | `--method=sanitize_block` | Block Erase (NVMe/SCSI) |
| Sanitize Overwrite | `--method=sanitize_overwrite` | Sanitize Overwrite (SCSI) |

Verfügbar über **GUI** unter "Secure Erase / Sanitize >" im Methoden-Menü.

> **Voraussetzungen:** `hdparm` (ATA), `nvme-cli` (NVMe), `sg3_utils` (SCSI)

---

## GUI-Bedienung

### Tastenbelegung (Hauptbildschirm)

| Taste | Funktion |
|-------|----------|
| **Space** | Festplatte auswählen/abwählen |
| **S** | Wipe starten (Shift+S) |
| **H** | Hostname für fokussierte Festplatte setzen |
| **I** | Inventarnummer für fokussierte Festplatte setzen |
| **m** | Lösch-Methode wählen |
| **p** | PRNG wählen |
| **v** | Verifikation einstellen |
| **r** | Anzahl Durchläufe |
| **b** | Blanking ein/aus |
| **d** | Schreibrichtung |
| **c** | Konfiguration (Organisation, Kunde, PDF) |
| **Ctrl+A** | Alle Festplatten auswählen |
| **Ctrl+C** | Beenden |

---

## Konfiguration

Alle Einstellungen werden in `/etc/wype/wype.conf` gespeichert (libconfig-Format).
Die Datei wird beim ersten Start automatisch mit Standardwerten erstellt.
Änderungen können direkt in der Datei oder über das Config-Menü (**c**-Taste) vorgenommen werden.

### Organisations-Details (PDF-Zertifikat)

Diese Angaben erscheinen auf jedem PDF-Lösch-Zertifikat:

```
Organisation_Details: {
  Business_Name = "Meine Firma GmbH"
  Business_Address = "Musterstr. 1, 12345 Berlin"
  Contact_Name = "Max Mustermann"
  Contact_Phone = "+49 30 123456"
  Op_Tech_Name = "Techniker Name"
}
```

> Konfigurierbar über **c** → "PDF Report - Edit Organisation" in der GUI.

### Kunden-Zuordnung (PDF-Zertifikat)

Optional kann ein Kunde zugeordnet werden, der auf dem Zertifikat erscheint:

```
Selected_Customer: {
  Customer_Name = "Kunde AG"
  Customer_Address = "Kundenweg 5, 54321 Hamburg"
  Contact_Name = "Ansprechpartner"
  Contact_Phone = "+49 40 654321"
}
```

> Kunden können über **c** → "PDF Report - Select/Add/Delete Customer" verwaltet werden.
> Kundendaten liegen in `/etc/wype/wype_customers.csv`.

### PDF-Zertifikat-Einstellungen

```
PDF_Certificate: {
  PDF_Enable = "ENABLED"
  PDF_Preview = "DISABLED"
  PDF_Host_Visibility = "DISABLED"
  PDF_tag = "DISABLED"
  User_Defined_Tag = "Empty Tag"
}
```

| Einstellung | Beschreibung |
|-------------|-------------|
| `PDF_Enable` | PDF-Zertifikate nach Wipe erstellen (`ENABLED`/`DISABLED`) |
| `PDF_Preview` | PDF nach Erstellung öffnen (nur mit Desktop) |
| `PDF_Host_Visibility` | Hostname des Systems auf dem Zertifikat anzeigen |
| `PDF_tag` | Benutzerdefinierten Tag auf dem Zertifikat anzeigen |
| `User_Defined_Tag` | Freitext-Tag für das Zertifikat |

### Per-Disk Metadaten (Hostname / Inventarnummer)

Zusätzlich zu den globalen Einstellungen können **pro Festplatte** individuelle Werte gesetzt werden, die direkt auf dem jeweiligen PDF-Zertifikat erscheinen:

1. Mit Pfeiltasten die gewünschte Festplatte fokussieren
2. **H** drücken → Hostname eingeben → Enter
3. **I** drücken → Inventarnummer eingeben → Enter
4. In der Disk-Liste erscheint `[H:hostname I:INV-001]` als Bestätigung

> Diese Werte werden **nicht** in `wype.conf` gespeichert, sondern nur während der Laufzeit gehalten und ins PDF geschrieben.

### E-Mail-Versand (SMTP)

Nach Abschluss eines Wipes kann das PDF-Zertifikat automatisch per E-Mail versendet werden.

```
Email_Settings: {
  Email_Enable = "ENABLED"
  SMTP_Server = "mailserver.example.com"
  SMTP_Port = "25"
  Sender_Address = "wype@example.com"
  Recipient_Address = "it-team@example.com"
}
```

| Einstellung | Beschreibung |
|-------------|-------------|
| `Email_Enable` | E-Mail-Versand aktivieren (`ENABLED`/`DISABLED`) |
| `SMTP_Server` | Hostname oder IP des SMTP-Servers |
| `SMTP_Port` | SMTP-Port (Standard: `25`) |
| `Sender_Address` | Absender-Adresse |
| `Recipient_Address` | Empfänger-Adresse (leer = kein Versand) |

> Standardmäßig deaktiviert. Unterstützt SMTP ohne Authentifizierung (Port 25, internes Netz).
> E-Mail-Einstellungen müssen direkt in `/etc/wype/wype.conf` editiert werden (kein GUI-Menü dafür).

---

## Bootbares ISO

Für ein bootbares USB/ISO-Image das direkt in Wype startet: [WypeOS](https://github.com/NKr-BKRennen/wypeOS)

---

## Bugs

* WYPE: [https://github.com/NKr-BKRennen/wype](https://github.com/NKr-BKRennen/wype)
* Original nwipe: [https://github.com/martijnvanbrummelen/nwipe](https://github.com/martijnvanbrummelen/nwipe)

## Lizenz

Wype ist lizenziert unter der **GNU General Public License v2.0**.
Siehe `LICENSE` für Details.

---

## Versionierung

Wype verwendet [Semantic Versioning](https://semver.org/): `MAJOR.MINOR.PATCH`

- **MAJOR**: Inkompatible Änderungen (z.B. neues Config-Format, Breaking Changes)
- **MINOR**: Neue Features, rückwärtskompatibel
- **PATCH**: Bugfixes, kleine Verbesserungen

---

## Changelog

### v1.1.0 (2026-03-17)

**Add:**
- Per-Disk Metadaten: Hostname und Inventarnummer pro Festplatte (H/I Tasten in der GUI)
- Automatischer E-Mail-Versand der PDF-Zertifikate per SMTP nach Wipe-Abschluss
- Secure Erase / Sanitize Methoden für ATA, NVMe und SCSI (Hardware-basiert)
- ASCII-Art "BK RENNEN" Logo im GUI-Header
- `build.sh` und `update.sh` Skripte für vereinfachtes Bauen und Aktualisieren
- E-Mail-Konfiguration in `/etc/wype/wype.conf`

**Fix:**
- Terminal-Hintergrund bleibt nach Beenden (Ctrl+C) nicht mehr blau
- Footer-Leiste hat jetzt einheitlichen Hintergrund (kein weißer Balken mehr)
- [IN USE] und [HS? YES] Tags sind jetzt rot auf blau statt rot auf weiß
- Hostname/Inventarnummer werden jetzt zuverlässig auf das PDF-Zertifikat geschrieben
- Barcode im PDF-Zertifikat deaktiviert

**Change:**
- Projekt umbenannt von nwipe/BKR_NWIPE zu Wype
- Modernisierte GUI: Farbschema (Teal/Navy/Yellow), farbige Status-Tags, Progress Bars
- Eigenes Versionierungsschema (Semantic Versioning)
- README komplett auf Deutsch, nur Debian 13

**Remove:**
- Unterstützung für andere Distributionen (nur noch Debian 13)
- Barcode auf PDF-Zertifikat (Code bleibt erhalten, nur deaktiviert)

### v1.0.0 (2026-03-16)

**Add:**
- Initiales Release basierend auf nwipe 0.40
- BKR-Branding: Logo im PDF-Zertifikat
- PDF-Lösch-Zertifikate mit Organisations- und Kundendetails
