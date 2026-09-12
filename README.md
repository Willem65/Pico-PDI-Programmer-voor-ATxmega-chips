# Pico PDI Programmer voor ATxmega-chips

Een volledig zelfgebouwde PDI-programmer voor Atmel/Microchip ATxmega-microcontrollers, gebouwd op een Raspberry Pi Pico van een paar euro. Werkt via standaard **avrdude**-commando's (`-c jtag2pdi`), en is byte-voor-byte geverifieerd tegen een officiële Atmel-ICE.

Ondersteunt:
- ✅ Flash lezen en schrijven
- ✅ EEPROM lezen en schrijven
- ✅ Device signature lezen
- ✅ Fuse-bits lezen en schrijven
- ✅ Lock-bits lezen
- ✅ Volledige chip-erase en pagina-specifieke erase

Getest en geverifieerd op de **ATxmega128A4U**. Zou met kleine aanpassingen (adressen/groottes) ook op andere XMEGA-chips moeten werken, omdat de PDI-sleutel en het protocol universeel zijn binnen de hele XMEGA-familie.

---

## Hoe het werkt

Dit project implementeert **twee lagen**:

1. **De PDI-fysieke laag** — bit-bangt het PDI-protocol (het twee-draads programmeerprotocol van XMEGA-chips) rechtstreeks op de GPIO-pinnen van de Pico.
2. **De JTAG ICE mkII-protocollaag** — praat via USB-seriële verbinding met avrdude, alsof de Pico een echte Atmel JTAG ICE mkII-programmer is (in PDI-modus). Dit maakt de Pico bruikbaar met **elk standaard avrdude-commando**, zonder dat avrdude ook maar weet dat het met zelfgebouwde hardware praat.

---

## Benodigdheden

### Hardware
- Een Raspberry Pi Pico (RP2040)
- Een ATxmega-chip (getest met ATxmega128A4U)
- Een diode (bijv. 1N4148)
- Een weerstand van 4,7kΩ
- Breadboard en jumperdraadjes (of, voor betrouwbaarheid, een gesoldeerde opstelling — zie [Bekende beperkingen](#bekende-beperkingen))

### Software
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) (inclusief CMake en de ARM-toolchain)
- [avrdude](https://github.com/avrdudes/avrdude) versie 7.x (getest met 7.1)
- `picotool` (om de firmware naar de Pico te flashen)

---

## Bedrading

![Bedradingsschema](kicad-picoprogrammer.pdf)

| Pico-pin | Functie | Aangesloten op |
|---|---|---|
| GP2 | PDI_CLK / RESET | Pin 35 van de XMEGA (RESET/PDI_CLK) |
| GP3 | PDI_DATA (uitgang) | Via een diode naar de gedeelde data-lijn |
| GP4 | *(niet gebruikt in de huidige versie — zie opmerking)* | — |
| GP0 | Debug-UART TX | Optioneel: naar een USB-naar-seriële-adapter (bijv. CH340) voor foutopsporing |
| GP1 | Debug-UART RX | Optioneel, idem |
| GND | Massa | Gedeelde GND met de XMEGA |

**Diode- en pull-up-opstelling:**
- De diode zit met de **kathode** op de gedeelde data-lijn (naar pin 34 van de XMEGA), en de **anode** aan de GP3-kant.
- Een pull-up-weerstand van 4,7kΩ van de gedeelde data-lijn naar 3V3.
- De XMEGA moet gevoed worden met een spanning tussen 1,6V en 3,6V (getest op 3,3V).

**Debug-UART (optioneel, maar sterk aanbevolen tijdens het bouwen/debuggen):**
Een losse USB-naar-seriële-adapter (bijv. een CH340-module) aangesloten op GP0/GP1, op 115200 baud. Dit geeft gedetailleerde logging van elk JTAG-commando dat binnenkomt, zonder de avrdude-verbinding zelf te verstoren.

---

## Bouwen en flashen

1. **Clone de Pico SDK** (als je dat nog niet hebt) en zorg dat de omgevingsvariabele `PICO_SDK_PATH` correct staat.

2. **Clone deze repository** en maak een build-map:
   ```bash
   git clone <deze-repository>
   cd <projectmap>
   mkdir build && cd build
   cmake ..
   make
   ```
   Dit produceert een `.uf2`-bestand (bijv. `pdi_dude.uf2`).

3. **Flash de Pico:**
   - Houd de BOOTSEL-knop op de Pico ingedrukt terwijl je 'm aansluit op USB (dit zet 'm in opslagmodus), **of**
   - Gebruik `picotool` als de Pico al eerder een UF2 heeft gehad:
     ```bash
     sudo picotool load pdi_dude.uf2 -f
     ```

4. Sluit de bedrading aan zoals hierboven beschreven.

---

## Gebruik met avrdude en avrdude_gui.py

of alleen avrdude vanaf de command prompt
...

## Gebruik met avrdude alleen

### Chip-signature uitlezen (goede eerste test)
```bash
sudo avrdude -c jtag2pdi -p atxmega128a4u -P /dev/ttyACM0 -b 19200
```
Verwachte uitvoer eindigt met iets als:
```
avrdude: device signature = 0x1e9746 (probably x128a4u)
```

### Volledige flash uitlezen
```bash
sudo avrdude -c jtag2pdi -p atxmega128a4u -P /dev/ttyACM0 -b 19200 -U flash:r:mijn_flash.bin:r
```
(gebruik `:i` in plaats van `:r` voor Intel HEX-formaat)

### Flash beschrijven
```bash
sudo avrdude -c jtag2pdi -p atxmega128a4u -P /dev/ttyACM0 -b 19200 -U flash:w:firmware.hex:i
```

### EEPROM lezen/schrijven
```bash
sudo avrdude -c jtag2pdi -p atxmega128a4u -P /dev/ttyACM0 -b 19200 -U eeprom:r:mijn_eeprom.bin:r
sudo avrdude -c jtag2pdi -p atxmega128a4u -P /dev/ttyACM0 -b 19200 -U eeprom:w:0,1,2,3,4,5,6,7:m
```

### Fuse-bits lezen/schrijven
```bash
sudo avrdude -c jtag2pdi -p atxmega128a4u -P /dev/ttyACM0 -b 19200 -U fuse1:r:fuse1.bin:r
sudo avrdude -c jtag2pdi -p atxmega128a4u -P /dev/ttyACM0 -b 19200 -U fuse1:w:0x42:m
```
⚠️ **Waarschuwing:** fuse4 en fuse5 bevatten instellingen die de reset-/PDI-functionaliteit kunnen beïnvloeden. Een verkeerde waarde kan de chip onbereikbaar maken via PDI. Wees extra voorzichtig, en test eerst op een chip die je kunt missen.

### Lock-bits lezen
```bash
sudo avrdude -c jtag2pdi -p atxmega128a4u -P /dev/ttyACM0 -b 19200 -U lock:r:lock.bin:r
```

**Op Windows:** vervang `/dev/ttyACM0` door je COM-poort (bijv. `COM3`) en laat `sudo` weg.

---

## Bekende beperkingen

- **Alleen avrdude, niet Atmel/Microchip Studio.** Moderne versies van Atmel/Microchip Studio ondersteunen geen seriële/COM-poort-verbindingen meer voor de JTAGICE mkII (alleen USB met een officieel Atmel/Microchip-apparaat-ID). Dit project werkt daarom alleen via avrdude.
- **Timing is fijn afgesteld voor avrdude's wachttijden.** De PDI-klok is bewust versneld om binnen avrdude's interne timeout (standaard 100ms bij de eerste poging) te blijven. Op een andere/tragere Pico-variant of met een minder stabiele bedrading kan dit opnieuw afstemmen vereisen (zie `HALF_PERIOD_US` in de broncode).
- **Een breadboard-opstelling kan incidenteel onbetrouwbaar zijn.** Voor de meest stabiele werking wordt een gesoldeerde opstelling aangeraden.
- **Windows vereist mogelijk een driverreparatie.** Als avrdude op Windows meldt dat het niet naar de COM-poort kan schrijven, controleer dan Apparaatbeheer op een onvolledig geïnstalleerd USB-apparaat (herkenbaar aan een geel uitroepteken) en herinstalleer de driver.

---

## Probleemoplossing

| Probleem | Mogelijke oorzaak |
|---|---|
| `timeout/error communicating with programmer` | PDI-klok te traag; verhoog de snelheid via `HALF_PERIOD_US`, of controleer de bedrading op wankele verbindingen |
| `verification mismatch` bij schrijven | Controleer of het juiste geheugentype (`mtype`) correct wordt vertaald naar het PDI-adres in `translate_addr()` |
| Signature komt niet overeen | Controleer de bedrading, met name de diode-richting en de pull-up-weerstand |

Voor gedetailleerde foutopsporing: sluit een losse USB-naar-seriële-adapter aan op GP0 (TX)/GP1 (RX), 115200 baud, en bekijk de live debug-log terwijl avrdude draait.

---

## Licentie

*(Voeg hier je gewenste licentie toe, bijvoorbeeld MIT of GPL-3.0)*

## Dankwoord

Gebouwd met behulp van avrdude's eigen open-source broncode als referentie voor het JTAG ICE mkII-protocol, en geverifieerd tegen een officiële Atmel-ICE.
