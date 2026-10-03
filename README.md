# MusicCat

MusicCat (mcat) is a macOS command-line recorder for personal Apple Music
listening experiments and DSP dataset preparation. It watches the Music app,
captures eligible playback through a CoreAudio virtual device, validates the
recording, embeds metadata and artwork, and preserves MusicCat's album-oriented
library layout.

The 0.2 series was a ground-up rewrite. The logo and the original **Mcat Library**
format remain; the old multi-listener recording logic has been replaced by one
deterministic state machine.

## What 0.3 adds

- A fully optional two-stage notation pipeline:
  `captured audio -> Transkun -> MIDI -> midiscribe/MIDI2ScoreTransformer -> MusicXML`
- `<Album>/midi/<Track>.mid` is created only after Transkun succeeds.
- `<Album>/score/<Track>.musicxml` is created only when the `midiscribe` CLI and
  its model are installed and conversion succeeds. Missing tools or weights do
  not create empty folders and never prevent FLAC/M4A publication.
- MusicXML receives the Apple Music title and composer/artist metadata. MIDI
  and MusicXML have no portable album-art attachment field, so `Cover.jpg`
  remains the artwork source and is applied to the MIDI/score folder icons when
  `fileicon` is available.
- `mcat --models` reports which optional stages are ready.
- A missing Loopback-provided capture device is detected before SoX starts and
  now explains that Loopback must be opened before `mcat --test` is retried.
- The companion [midiscribe](https://github.com/opus-arc/midiscribe) CLI wraps a
  separately installed MIDI2ScoreTransformer checkout and either its complete
  upstream checkpoint or a compatible inference-only checkpoint. MusicCat does
  not download or redistribute the unlicensed upstream source or weights.

## What 0.2 adds

- A bbpl-style CLI built around getopt_long, one command per invocation,
  centralized exception handling, RAII lifetime guards, stop tokens, jthreads,
  and sigwait
- A single owner for playback state, recording state, and track identity
- Atomic lightweight Apple Music snapshots with hard timeouts
- Separate one-shot rich-metadata and artwork reads so network metadata cannot
  block the real-time monitoring loop
- Explicit recording outcomes:
  - local downloads are recommended but not mandatory; when no local reference
    is available, MusicCat records without trying to repair network playback
  - brief Apple Music observation failures at startup are tolerated
  - a track is accepted only when the armed capture actually contains its beginning
  - confirmed pause, seek, prolonged stall, early track change, or app exit
    rejects only the affected recording and keeps the service usable
  - missing tools, an unusable device, an unwritable output, an unexpected SoX
    exit, or failed post-processing is a fatal error
- Source-aware pre-roll alignment: ordinary local audio files use envelope
  plus waveform correlation to locate the Apple Music output exactly; other
  playback uses repeated player-position observations with conservative
  head/tail padding and never predicts network latency
- Natural-end drain: capture remains open briefly after the player reports the
  end so CoreAudio can flush the final buffered samples before validation
- Gapless album transitions: a second CoreAudio capture is pre-armed while the
  current track is playing, so stopping and processing one track cannot miss
  the beginning of the next
- Continuous idle pre-roll: the armed recorder is never rotated through a gap
  while Music is paused or changing state
- An explicit pre-roll coverage check instead of a guessed query/network-delay
  threshold, plus a visible message when the recorder did not capture the beginning
- A bounded background post-processing queue with error propagation
- Non-destructive rejection: unsuitable captures are moved to
  **Mcat Library/.Rejected** and never mixed into the usable dataset
- Built-in audio QC for effective duration and non-silence before publication
- Atomic temporary outputs and matching collision-safe M4A/FLAC names
- **--record-once** for controlled tests and one-track experiments
- **--status**, **--list-devices**, and an end-to-end **--test** command
- Silent optional Transkun integration: when a `transkun` executable is on
  `PATH`, MusicCat creates `<Album>/midi/<Track>.mid`; otherwise it does nothing
- Compatibility aliases for **ready**, **log**, **zh**, and **ja**

## 0.2.4 reliability fixes

- Refuses to record while Apple Music AutoMix or Crossfade is enabled. Those
  modes overlap adjacent songs, so the captured output cannot be separated into
  complete, isolated tracks after the fact.
- Warns when Apple Music's playback queue jumps over track numbers within the
  same album instead of silently making the resulting album look complete.
- Adds `designed by Ziyang Tan` to the CLI identity block.
- Release executables are checked for an available Apple Developer signing
  identity, signed when possible, and verified before publication.

## 0.2.3 reliability fixes

- Replaced the fixed start-time window with a direct check that the live
  CoreAudio pre-roll covers the observed Apple Music position.
- Removed idle capture rotation, which could restart SoX in the same instant a
  user clicked Play and lose the opening seconds.
- Kept the reserve recorder armed through album transitions and ignored the
  transient `old track + position 0` snapshot Music can expose before updating
  the next track's metadata.
- Added a visible waiting message for a track whose beginning was not captured;
  restarting that track or allowing the next track to begin remains automatic.

## Requirements

- macOS with the Music app
- SoX with CoreAudio support
- FFmpeg and ffprobe
- A CoreAudio input device that receives Apple Music's output, such as the
  default **Apple Music Virtual Device**
- Automation permission for the terminal or host that launches mcat
- Apple Music song transitions disabled: AutoMix and Crossfade combine adjacent
  tracks and cannot produce isolated, complete per-track recordings
- Optional: fileicon, used only to apply album artwork as the Finder folder icon
- Optional: Transkun CLI, discovered at runtime for WAV-to-MIDI transcription
- Optional: `midiscribe`, a separately installed MIDI2ScoreTransformer checkout,
  and a separately downloaded model, discovered at runtime for MIDI-to-MusicXML
  conversion

MusicCat does not bypass Apple Music access controls or DRM. Use it only with
audio you are authorized to play and capture.

On first use, macOS asks whether the launching app (for example Terminal) may
control Music. Choose **Allow**, then verify the setup with **mcat --test**. If
the prompt was previously denied, enable Music under **System Settings → Privacy
& Security → Automation → Terminal** (or the actual host application).

## Build

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure

The executable is **build/mcat**.

### Optional model tools

MusicCat does not install Python, model code, or weights automatically. A
Python 3.11 environment may opt into Transkun with its existing package:

    python3.11 -m pip install transkun

Install the small CLI wrapper, then separately clone MIDI2ScoreTransformer and
download `MIDI2ScoreTF.ckpt` from its upstream v0.0.1 release:

    python3.11 -m pip install git+https://github.com/opus-arc/midiscribe.git
    midiscribe --status
    mcat --models

Set `MIDISCRIBE_SOURCE` and `MIDISCRIBE_MODEL` when the checkout or checkpoint
is outside the default locations. These optional downloads are never performed
by MusicCat. Without them, MusicCat publishes audio and MIDI normally and does
not create `score/`.

## CLI

    mcat --record
    mcat --record-once
    mcat --output "/path/to/output"
    mcat --device "Apple Music Virtual Device"
    mcat --status
    mcat --models
    mcat --list-devices
    mcat --test
    mcat --log
    mcat --help
    mcat --version

`--output`/`-o` and `--device`/`-d` are persistent settings, stored in
`~/Library/Application Support/MusicCat/config`; they are not one-run options.

For the most reliable capture, download the album in Apple Music first. Run
**mcat --record**, then play a track from its beginning. Downloads are not
mandatory: MusicCat also accepts streaming playback, but it does not conceal,
repair, or splice network interruptions. It stays idle when Music is stopped,
ignores tracks already in progress, and continues listening after a rejected
attempt. Press Ctrl-C to stop cleanly. For decodable local
files, the file is used only as a correlation reference; protected `.movpkg`
packages remain inside Music. In both cases the saved FLAC and M4A contain the
captured CoreAudio output.

**mcat --test** checks the required tools, output directory, Music automation
access, capture device, FLAC creation, and ffprobe readability without adding a
track to the library.

## Preserved library format

Successful recordings retain the original MusicCat organization:

    <output>/
      Mcat Library/
        <Album>/
          Cover.jpg
          <Track>.m4a
          flac/
            <Track>.flac
          midi/
            <Track>.mid
          score/
            <Track>.musicxml
        .Rejected/
          <Track>--<reason>.flac
      .mcat-work/

- M4A is the convenient 256 kbit/s AAC listening copy.
- FLAC is the 24-bit lossless capture used for analysis and DSP work.
- Both files receive the available Apple Music metadata and embedded artwork.
- The first cover saved for an album becomes **Cover.jpg**; with fileicon
  installed it is also applied as the Finder folder icon and the optional MIDI
  and score folder icons. Standard MIDI and MusicXML have no portable
  embedded-cover field, so the album `Cover.jpg` remains their artwork source.
- Name collisions use the same numbered suffix for the M4A and FLAC pair.
- **.mcat-work** contains only in-flight material. A clean shutdown drains
  queued post-processing before returning.

## Recording policy

MusicCat distinguishes recoverable playback conditions from fatal infrastructure
errors:

| Situation | Result |
| --- | --- |
| Apple Music not running or the beginning is absent from pre-roll | Explain that it is waiting for the next track or a restart |
| Track has no local source reference | Record it using player-position alignment and recommend downloading first |
| Track starts cleanly | Record pre-roll; correlate a normal file when available or use player-position alignment |
| A few timed-out Music queries | Continue the current attempt |
| Confirmed pause, seek, long stall, early switch, or app exit | Stop and isolate that capture |
| Natural end followed by pause or the next track | Validate and publish |
| Transkun missing | Publish audio normally and skip MIDI silently |
| Transkun invocation fails | Keep the published audio and log a non-fatal warning |
| midiscribe CLI or model missing | Keep audio/MIDI and do not create a score folder |
| midiscribe conversion fails | Keep audio/MIDI and log a non-fatal warning |
| Wrong/missing device, SoX exit, unwritable output, FFmpeg failure | Stop service with a non-zero exit |

For decodable files, the acceptance gate requires strong envelope and waveform
correlation with the downloaded reference. Every mode rejects an aligned
recording that is more than 1.25 seconds short, more than 5 seconds long after
alignment, or effectively silent before any file enters a normal album
directory.

## License

Apache-2.0

## Acknowledgements

Thanks to Apple Music for the listening source, and to the open-source SoX and
FFmpeg projects for the audio tooling used by MusicCat.
