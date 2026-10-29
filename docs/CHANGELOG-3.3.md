# OnAir 3.3

3.3 follows 3.2. The big item is a full rebuild of the Inmarsat mode. The rest is a long list of fixes for reports from users, a lot of trunked-radio work (P25 especially), new controls, and community pull requests.

**Measured** numbers come from OnAir's own test signals, not from over-the-air recordings, unless a line says so. The Android APK follows this release.

## Features

- **Inmarsat, rebuilt (all carriers at once).** One wide capture is searched for every Inmarsat carrier; Aero P and Inmarsat-C decode in parallel, with a voice mixer, a call recorder and a codec hook for the Aero C voice channels (the voice codec is installed by you, as with the other voice modes). The satellite is identified automatically from content, frequencies and geometry. User page: docs/INMARSAT.md.
- **Shortwave broadcast bands:** one button per ITU band (120, 90, 75, 60, 49, 41, 31, 25, 22, 19, 16, 15, 13 and 11 m) in the AM broadcast entry; band edges are per ITU region and checked against the ITU-R table.
- **Wenet:** the Project Horus 115.2 kbaud LDPC link and the 96 kbaud I2S link are received as two speeds of the SSDV mode.
- **P25 voice radio (#349):** the follower can use a second receiver for the voice channel while the first one stays on the control channel.
- **View > Controls:** rebind keys, mouse buttons and the wheel (docs/CONTROLS.md).
- **Fast waterfall:** 50 to 500 rows a second for Morse, with a CW preset; big FFT sizes now keep 30 frames a second at low sample rates.
- **Copy any text (#344):** hover and Ctrl+C, or right-click, Copy.
- **DMR channel raster:** Whole span reports channel centres on a raster (automatic, 6.25, 12.5 or 25 kHz) and can snap found frequencies.
- **AIS map labels:** a button for zoomed-in, off or all labels, saved.
- **Satellite modes keep the frequency** when you switch between them (#338).
- **Digital video generator** (tool, from #343) with help on every setting and a user guide.
- **Device selection:** Auto gain and each receiver's tab are remembered per receiver (#342); a Windows camera is opened by its name and listed through DirectShow (#346); Pluto scans libiio's network backend (#345).
- **P25 Phase 2:** the SACCH and FACCH MAC PDUs are decoded with the coding of the standard (shortened and punctured RS(63,35) with an erasure decoder, CRC-12); Phase 2 grants are placed by the receiver (IDEN_UP_TDMA, slots).

## Fixes

- **RTL-SDR Blog V4 Lite (#337, #340):** the driver and the bundled library (1.4.0) know the V4L model, its input switching and the V4 tracking-filter bypass.
- **Linux CJK font (#336):** found through fontconfig and the font folders.
- **Automation (#334):** a visible Delete button for rules and scripts.
- **DMR (#335):** a position report whose packet CRC fails but whose IPv4 checksum holds is kept.
- **P25 follower (#348, #347):** regains its control channel; stale identifiers are dropped on a site change; control-channel decoding continues in follow-only mode and the first call is heard; no replay of the message history as fresh grants; an encrypted hopped call is left.
- **P25 Phase 2 lock:** locks on a carrier up to 6 kHz off (was about 700 Hz).
- **TETRAPOL:** the decoder no longer freezes after a frequency change.
- **TX (#351):** modes that listen offset (APRS 50 kHz low) now transmit on the dial.
- **FM RDS (#350):** text with Chinese (GB2312) and EBU Latin characters.
- **Network radios:** an rtl_tcp radio pointed at the OnAir server's web port now says so; the server prints the exact share address.
- **Transcription:** no status line while no speech model is installed.
- **Whole span:** the sound goes to a voice call, not to a carrier only locked (#352).
- **HFDL (#327)** and other pull requests listed below.
- **Issues #331 to #341:** #332 and #339 were reports from 3.1.0 and are the Windows crashes already fixed in 3.2 (#329, #324).
- **Windows / CI:** LF line endings kept in Windows checkouts (#355); actions pinned to Node 24 releases (#353, #354).

## Pull requests merged, with thanks

- #352 DMR Whole span: sound to a voice call (drfruct, Dmitry Obedkov).
- #347 P25 follower keeps decoding the control channel (drelectro, Mike).
- #345 Pluto network scan (F5OEO).
- #343 Digital video generator, #346 Windows cameras, #353 #354 #355 CI (peterdewitte).
- #327 HFDL ICAO octet bit order (frank-1225).

## Known issues

- **Needs a user test (not tried on real hardware or signals):** the RTL-SDR Blog V4L driver, the Windows crash fixes, TETRAPOL retune, P25 Phase 2 MAC decoding and capture range, the two-radio P25 follower, Wenet, Inmarsat (all carriers, satellite identification, Aero voice), the Windows camera listing, the HackRF firmware sweep, the IC-R8600 driver, ROS and EBICab (from 3.2).
- Tests that fail in the project's CI on main as well (not new): see the list in the release notes below; `dvbt_real` can still differ in the number of damaged packets on x86 CPUs.
- DMR next to a +30 dB adjacent channel still does not decode.
