# YR Nedbørsvarsel App for RoseOS
all rettigheter reservert for appen YR_regn https://github.com/kamerat


Dette er en Lua-versjon av `rein_app/yr-regn-display` som er konvertert til å fungere som en RoseOS app.

## Status

⚠️ **VIKTIG**: Denne appen vil **ikke fungere** før RoseOS får full Lua-interpreter implementert. For øyeblikket er `runGenericApp()` i RoseOS bare en placeholder.

## Funksjonalitet

Appen viser nedbørsvarsel for neste 90 minutter fra YR.no API:
- Grafisk visning av nedbør over tid
- Visning når det ikke er nedbør
- Håndtering når radar er nede
- Batteri-indikator (valgfritt)
- Automatisk oppdatering hvert 5. minutt
- Manuell oppdatering via knapp

## Konfigurasjon

### YR Location ID

Appen trenger en YR location ID for å fungere. Dette kan settes via BLE:

```
CMD:CONFIG:YR_LOCATION:1-92416
```

Finn location ID ved å:
1. Gå til yr.no
2. Søk etter din lokasjon
3. Kopier "x-xxxxx" ID-en fra URL-en (f.eks. `yr.no/nn/vêrvarsel/dagleg-tabell/1-92416/...`)

### GitHub-brukernavn (User Agent)

**VIKTIG**: YR.no API krever en identifiserbar user agent. Du må konfigurere ditt GitHub-brukernavn for å unngå å bli blokkert.

#### Metode 1: Via BLE (Anbefalt)
Send følgende kommando via BLE:
```
CMD:CONFIG:GITHUB_USER:dittBrukernavn
```

Du vil få bekreftelse tilbake:
```
OK:GitHub-brukernavn satt: dittBrukernavn
OK:User agent: epaper/1.0 github.com/dittBrukernavn
```

#### Metode 2: Direkte i koden
I `yr-regn.lua` filen, endre denne linjen:
```lua
local GITHUB_USERNAME = "dittBrukernavn"  -- ENDRE DETTE!
```

Dette vil sette user agent til: `epaper/1.0 github.com/dittBrukernavn`

### Sjekke konfigurasjon

For å se nåværende konfigurasjon, send:
```
CMD:CONFIG:STATUS
```

Du vil få tilbake:
```
CONFIG:YR Location: 1-92416|GitHub User: dittBrukernavn|User Agent: epaper/1.0 github.com/dittBrukernavn
```

## Nødvendige RoseOS API-er

For at denne appen skal fungere, må RoseOS implementere følgende API-er:

### Display API
- `display.clear()` - Tøm skjermen
- `display.text(x, y, text, size)` - Tegn tekst
- `display.line(x1, y1, x2, y2, filled?)` - Tegn linje
- `display.rect(x, y, w, h, filled?)` - Tegn rektangel
- `display.circle(x, y, radius, filled?)` - Tegn sirkel
- `display.triangle(x1, y1, x2, y2, x3, y3, filled?)` - Tegn trekant
- `display.refresh()` - Oppdater skjermen
- `display.width()` - Skjermbredde
- `display.height()` - Skjermhøyde
- `display.textWidth(text, size)` - Beregn tekstbredde

### System API
- `system.time()` - Nåværende tid (millisekunder siden oppstart eller Unix timestamp)
- `system.sleep(ms)` - Sov i millisekunder
- `system.exit()` - Avslutt app
- `system.batteryVoltage()` - Batterispenning
- `system.batteryPercent()` - Batteriprosent

### WiFi API
- `wifi.isConnected()` - Sjekk om WiFi er tilkoblet

### HTTP API
- `http.get(url, headers?)` - Hent data fra URL med valgfrie headers (returnerer response, status)

### Storage API
- `storage.get(key)` - Hent lagret verdi
- `storage.set(key, value)` - Lagre verdi

### BLE API
- `ble.send(message)` - Send melding tilbake via BLE

### JSON API
- `json.parse(jsonString)` - Parse JSON-streng til Lua-tabell

## Alternativ: Implementer som built-in app

Hvis du ønsker at appen skal fungere umiddelbart, kan den implementeres som en built-in app i RoseOS (som `clock`, `notes`, etc.). Dette krever:

1. Legge til `"yr-regn"` i `appList` i `loadAppsFromSD()`
2. Legge til en `runYrRegnApp()` funksjon i RoseOS.ino
3. Legge til case i `executeApp()` for å kalle `runYrRegnApp()`

Dette vil gi full funksjonalitet uten å vente på Lua-støtte.

## Referanser

- Original app: `rein_app/yr-regn-display/`
- YR API: https://www.yr.no/api/v0/locations/{location}/forecast/now
- YR API dokumentasjon: https://api.met.no/weatherapi/locationforecast/2.0/documentation
