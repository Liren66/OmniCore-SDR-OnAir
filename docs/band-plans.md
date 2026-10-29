# Band plans

A band plan is the channel raster and the allocations of a country: the TV channels, FM, DAB, AM, airband, PMR, marine VHF, amateur and ISM bands. OnAir ships plans for 27 countries and a generic European one, and lets you import your own.

Where it shows:

- **Scan tab (TV modes): Band plan.** Pick a country (or **Automatic**, which uses the interface language and then the system's region). The range list under it holds the TV bands of the plan for the mode you are in: DVB-T / T2, ATSC, ISDB-T or DTMB. **None** keeps the old fixed ranges.
- **Toolbar, next to the channel width: the channel list** of the same plan. Choosing a channel sets the frequency.
- The ranges are named by band function: **VHF TV** and **UHF TV**. The UHF band is not only DVB-T: it carries DVB-T2, ATSC, ISDB-T and DTMB depending on the country.

## Built-in plans

Source of truth: `data/bandplans/builtin.json`, compiled into the program (no network). Each group of bands is a template with its source; a plan lists the templates it uses.

| Plan | TV | FM | DAB | MW / LW | Airband | Ham | Notes |
|---|---|---|---|---|---|---|---|
| Germany, France, Italy, Spain, Netherlands, Poland, Sweden, Finland, Denmark, Lithuania, Latvia, Estonia, Ireland, Austria | VHF ch 5-12 (7 MHz), UHF ch 21-48 (8 MHz) | 87.5-108, 100 kHz | Band III 5A-13F | MW 9 kHz, LW | 25 and 8.33 kHz | Region 1 | PMR446; Germany also LPD433 |
| United Kingdom | same; nominal centres (many transmitters add a 166.67 kHz offset) | same | same | same | same | Region 1 | |
| Norway, Switzerland | raster kept for reference: terrestrial TV is switched off (2017, 2019) | same | same | MW (Norway: no LW) | same | Region 1 | |
| USA, Canada | ATSC ch 2-36, 6 MHz (VHF 2-4, 5-6, 7-13, UHF 14-36) | ch 200-300, 87.9-107.9, 200 kHz | none | MW 540-1700, 10 kHz | 25 kHz | Region 2 | FRS / GMRS, MURS, CB (USA) |
| Australia | VHF ch 6-12 with 9A, UHF ch 28-51, 7 MHz | 87.5-108 | Band III | MW 531-1701, 9 kHz | 25 kHz | own plan | |
| Japan | UHF ch 13-52, 6 MHz ISDB-T | 76-95 | none | MW 9 kHz | 25 kHz | core | |
| Brazil | ISDB-Tb VHF 7-13, UHF 14-51 | Anatel ch 141-300: eFM 76.1-87.3, RadCom 87.5-87.9, FM 88.1-107.9 | none | MW 10 kHz | 25 kHz | Anatel (2200 m to 5 cm) | see [Brazil](#brazil-sources-and-updates) |
| China | DTMB DS6-12, DS13-24, DS25-48, 8 MHz | 87-108 | none | MW 9 kHz | 25 kHz | VHF / UHF only | |
| India, New Zealand, UAE / Gulf, South Africa | CCIR raster (8 MHz UHF), see below | 87.5-108 (NZ 88-108) | none | MW, LW (Gulf, South Africa) | 25 kHz | varies | |
| Europe (CEPT generic) | as Germany | | | | | | the fallback for Hungarian and unknown European setups |

Every plan also lists marine VHF channels 1-28 and 60-88 (ship-transmit frequencies) with AIS 1 and AIS 2, and the ISM bands used where it applies.

## Plans that are not fully verified

These are marked **Not fully verified** in the picker; the reason is in each plan's note. Treat the channel numbers as a starting point, check them against a transmitter list for your area, and send a corrected file:

- **Australia**: channel numbers and the 7 MHz UHF raster (ch 28-51) are from the ACMA digital plan; ch 6-12 and 9A should be confirmed.
- **New Zealand**: UHF is assumed to use the 8 MHz CCIR channels; the exact Freeview channel numbers are not verified. FM start (88 MHz) not verified.
- **Japan**: UHF ch 13-52 are certain; the VHF ISDB-T channels are left out because the national table was not verified. The low HF amateur bands, the 920 MHz LPWA band and 8.33 kHz airband are not listed.
- **India**: UHF assumes the CCIR 8 MHz raster, 470-862 MHz; Doordarshan's DVB-T2 channel list is not verified; no VHF TV.
- **China**: the DTMB channels (DS6-12, DS13-24, DS25-48) are from memory of the national plan, including the gap between DS24 and DS25; no amateur HF.
- **UAE / Gulf**: the Gulf states use the CCIR 7 / 8 MHz raster (a Dubai mux sits on 522 MHz, UHF ch 27); which channels each country uses is not verified; no amateur plan.
- **South Africa**: 8 MHz UHF raster; the upper limit after the 700 / 800 MHz clearances is not verified.

Left out on purpose because the plan is not known well enough: Italy's own band III channel names (A-H2), Japan's VHF ISDB-T table, China's low VHF (DS1-5), Hungary, Russia and others (there is the generic European plan). In Europe the 470-694 MHz raster (ch 21-48) is the one left to broadcasting by EU Decision 2017/899; individual countries use fewer channels, and a few still have terrestrial TV in band III. UK, Germany, France, Spain and others keep DAB in band III.

Sources used: ITU Radio Regulations Article 5 and Appendix 18, ITU-R BT.470, the Geneva 1975 and GE84 regional agreements, CEPT ERC Rec 70-03 and ECC/DEC/(15)05, ETSI EN 300 401 (DAB blocks), ICAO Annex 10, US 47 CFR 73 / 95 / 97, ARIB STD-B31, ABNT NBR 15601, Anatel Ato 926/2024, Res. 789/2026 and the PBFM (Brazil), ACMA plans, and the Wikipedia channel tables for the national lists. Every band in the file carries its own `source`.

## Brazil: sources and updates

The Brazil plan follows Anatel documents, not the US rules it started from.

| Part | Source | Date of the data |
|---|---|---|
| Amateur bands (`ham-br`) | Anatel Ato 926/2024, Tabela I (amateur service requirements; the PDFF of Res. 789/2026 cites it for each band) | Ato published in the DOU of 2024-02-05 |
| FM ranges (`fm-br`) | Anatel Res. 789/2026, the PDFF: 76-87.4 MHz extended FM (eFM, for AM stations moving to FM), 87.4-88 MHz community radio (RadCom), 88-108 MHz FM | DOU of 2026-07-13 |
| FM channel numbers | Anatel Planos Básicos, open data (CC licence): `PBFM.csv` in https://www.anatel.gov.br/dadosabertos/paineis_de_dados/radiodifusao/planos_basicos.zip | files dated 2024-11-01, downloaded 2026-10-23 |
| MW and TV check | the same ZIP: `PBOM.csv` (2074 MW stations) and `PBTVD.csv` (16218 digital TV stations) | same |

In the PBFM every row (9388, FM and RTR) has channel *n* at 87.9 + (*n* - 200) x 0.2 MHz: channels 141-197 are eFM (76.1-87.3 MHz) and 201-300 are FM (88.1-107.9 MHz). One row is off (channel 225 at 93.1 MHz in Castanhal-PA, a typo in the file). Channels 198-200 (87.5, 87.7, 87.9 MHz) are the RadCom channels; they are not in the PBFM because community radio has its own reference plan, and the Ministry of Communications' RadCom list uses the same numbering. Where 87.4-88 MHz cannot be used, a RadCom station gets a channel in 76-87.4 or 88-108 MHz instead (RCC-SRA art. 8 § 4, as changed by Res. 789/2026), so a community station can sit outside the RadCom band.

The same ZIP confirms the rest: every PBOM frequency is on the 10 kHz raster from 540 kHz (the highest in use is 1600 kHz), and every PBTVD station is on physical channel 7-13 or 14-51 (none on 37, kept for radio astronomy), listed at the nominal 6 MHz channel centre (177 + (n - 7) x 6 and 473 + (n - 14) x 6 MHz); the plan's centres sit 1/7 MHz higher, where ISDB-Tb puts them. One TV row is off as well (channel 23 at 533 MHz in Matriz de Camaragibe-AL).

To check or update:

1. Download the ZIP above (Anatel's site may refuse cloud and VPN addresses; a home connection works). `unzip -l planos_basicos.zip` shows the date of the data.
2. Run `python3 -I tools/check_bandplan_br.py planos_basicos.zip`. It reads PBFM, PBOM and PBTVD and prints every row whose channel or frequency does not land on a channel of the `br` plan, plus the channel range of each file. Exit status 0 means everything fits; a missing file or a changed CSV layout also exits 1. `python3 -m unittest tools/test_check_bandplan_br.py` tests the checker itself on small made-up ZIPs.
3. If rows do not fit, change the `fm-br`, `mw-r2` or `tv-br` template in `data/bandplans/builtin.json`, update the dates in this section and run `test_bandplan`.
4. For the amateur bands, compare Tabela I of the current Ato (Anatel, Legislação, Atos de Requisitos Técnicos de Gestão do Espectro) with the `ham-br` template. A newer Ato replaces 926/2024 when the PDFF cites it for the amateur service.

## Import and export

In the Scan tab: **Import...** reads a CSV or JSON file, checks it, and keeps it in your settings folder (`bandplans/user-<name>.json`, written atomically). A plan with the same name is replaced. **Export...** saves the selected plan (JSON, or CSV when the name ends in `.csv`); a built-in plan exported this way is the easiest start for your own. **Delete** removes one of your plans (asks twice); the built-in ones cannot be deleted. A file with a mistake changes nothing; the message names the line or band and the field.

### JSON

```json
{
  "format": "onair-bandplan-1",
  "name": "My town",
  "region": "Europe",
  "itu_region": 1,
  "countries": ["DE"],
  "source": "my notes",
  "note": "",
  "bands": [
    {"name": "UHF TV", "service": "tv", "start_hz": 470000000, "end_hz": 694000000,
     "step_hz": 8000000, "bandwidth_hz": 8000000, "first_channel": 21, "first_centre_hz": 474000000,
     "modes": ["DVB-T2", "DVB-T"], "note": "ch 21-48"},
    {"name": "DAB Band III", "service": "dab", "start_hz": 174000000, "end_hz": 240000000, "bandwidth_hz": 1536000,
     "channels": [{"label": "5A", "centre_hz": 174928000}, {"label": "5B", "centre_hz": 176640000}]},
    {"name": "20 m", "service": "ham", "start_hz": 14000000, "end_hz": 14350000}
  ]
}
```

### CSV

Optional comment lines at the top give the plan fields; then a header row and one row per band. Cells may be quoted. Lists in a cell are separated by `;`.

```
# name: My town
# region: Europe
# itu_region: 1
# countries: DE, AT
# source: my notes
band,service,start_hz,end_hz,step_hz,bandwidth_hz,first_channel,first_centre_hz,modes,channels,note,source
UHF TV,tv,470000000,694000000,8000000,8000000,21,474000000,DVB-T2;DVB-T,,ch 21-48,
"FM, local",fm,87500000,108000000,100000,,,87500000,FM,,,
```

The header may use `_khz` or `_mhz` instead of `_hz` for `start`, `end`, `step`, `bandwidth` and `first_centre` (for example `start_mhz,end_mhz,step_khz`). `band` and `name` mean the same.

### Fields of a band

| Field | Meaning |
|---|---|
| `name` | Shown in the lists ("UHF TV", "FM broadcast", "20 m"). Required. |
| `service` | `tv`, `fm`, `dab`, `am`, `air`, `pmr`, `marine`, `ham`, `ism` or `other` (default). The TV modes use the `tv` bands. |
| `start_hz`, `end_hz` | The edges of the band in Hz (the outer edges of the first and last channel when channels have a width). Required, `end_hz` above `start_hz`, at most 300 GHz. |
| `step_hz` | The channel raster. 0 or missing: no regular channels (a plain range such as a ham band). |
| `bandwidth_hz` | The width of a channel. 0: the channels are points on the raster (FM, AM, airband). |
| `first_channel` | The number of the first channel. Missing: the channels are not numbered. |
| `first_centre_hz` | The centre of the first channel. Default: `start_hz` (`start_hz` plus half the width when there is a width). Channel *k* is at `first_centre_hz + k * step_hz`, and it exists while it fits inside the band. |
| `channels` | Instead of a raster: an explicit list of `{label, centre_hz}` (DAB blocks, Australia's 9A). Cannot be combined with `step_hz`. |
| `modes` | Suggested modes, by the names in OnAir's mode list. For `tv` bands the standard decides which bands a TV mode uses: `DVB-T2` / `DVB-T`, `ATSC`, `ATSC 3.0`, `ISDB-T`, `DTMB`. |
| `note`, `source` | Free text. |

Plan fields: `name` (required), `region`, `itu_region` (1, 2 or 3), `countries` (ISO codes), `source`, `note`, `verified` (JSON: `false` shows the "not fully verified" note; CSV: `# verified: no`).

Limits, so a damaged file cannot hurt: 4 MB per file, 500 bands, 5000 explicit channels per band, 200000 channels per raster. A common mistake is a step in MHz where Hz is expected; the message says "check step_hz (in Hz)".

## Shortwave broadcast bands

The `sw-bc-r1`, `sw-bc-r2` and `sw-bc-r3` templates (each plan uses the one for its ITU region) list the shortwave broadcasting bands in kHz, with the region's own edges. Widest range, with the differences:

| Band | kHz | Region differences |
|---|---|---|
| 120 m | 2300-2495 | tropical zone only; Region 1 to 2498 |
| 90 m | 3200-3400 | tropical zone only |
| 75 m | 3900-4000 | Region 1 3950-4000, Region 2 none, Region 3 3900-4000 |
| 60 m | 4750-5060 | tropical zone only; 4750-4995 and 5005-5060 (5000-5005 is the standard-frequency service) |
| 49 m | 5900-6200 | |
| 41 m | 7200-7450 | Region 2 7300-7400 only |
| 31 m | 9400-9900 | |
| 25 m | 11600-12100 | |
| 22 m | 13570-13870 | |
| 19 m | 15100-15800 | |
| 16 m | 17480-17900 | |
| 15 m | 18900-19020 | |
| 13 m | 21450-21850 | |
| 11 m | 25670-26100 | |

Sources: ITU-R, Frequency bands allocated to terrestrial broadcasting services (https://www.itu.int/en/ITU-R/terrestrial/broadcast/Pages/bands.aspx, authoritative, used for every edge); Wikipedia, Shortwave bands (https://en.wikipedia.org/wiki/Shortwave_bands) and Shortwave radio (https://en.wikipedia.org/wiki/Shortwave_radio); hqshortwaveradio.com/bands (https://hqshortwaveradio.com/bands/, loose round-number edges, not used); Radio Regulations Art. 5 (https://search.itu.int/history/HistoryDigitalCollectionDocLibrary/1.49.48.en.101.pdf). The HFCC site (hfcc.org) was unreachable. Where Wikipedia differs (25 m to 12.2, 41 m to 7.6, 11 m from 25.6, 60 m end 4995) the ITU table wins; the third-party pages round the edges. The raster is 5 kHz. In the General receiver's AM broadcast entry each band is a button: it tunes the band centre, selects AM with the synchronous detector (when the detector is not on auto), a 9 kHz filter and a 5 kHz step; the tooltip shows each region's edges.
