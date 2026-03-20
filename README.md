# MusicCat

MusicCat is a macOS‑native Apple Music archiving toolkit written in C++. It captures currently playing tracks, extracts rich metadata and artwork, records lossless audio through a virtual device, transcodes outputs, and organizes the final library into album‑based folders.

MusicCat runs as a lightweight command‑line listener. It continuously monitors Apple Music playback and automatically records tracks when playback begins, then performs post‑processing such as metadata embedding, artwork extraction, format conversion, and library organization.


# Features

- Run as a lightweight macOS CLI listener
- Detect whether Apple Music is running
- Detect whether music is currently playing
- Monitor playback state and track changes
- Read detailed Apple Music metadata, including:
  - title / artist / album
  - album artist / composer / genre / lyrics
  - year / track number / disc number
  - bpm / rating / play count / sample rate / bit rate
- Export and preserve current track artwork
- Record playback audio as **24-bit FLAC** via a macOS CoreAudio virtual device
- Convert recorded audio to **M4A** while retaining the original **FLAC**
- Write metadata and embedded artwork into processed audio files
- Organize completed output into **album-based library folders automatically**
- Apply album cover as **macOS folder icon**
- Run with user-scoped cache, log, and support files outside the project directory

# Architecture

```text
Apple Music
     │
     ▼
Virtual Audio Device
     │
     ▼
     SoX
     │
     ▼
    FLAC Recording
     │
     ▼
   FFmpeg
(metadata + artwork embedding)
     │
     ▼
    M4A Output
     │
     ▼
 Album Library Organizer
```

# Output Structure

After processing, the output library will look like:

```
output/
   Mcat Library/
       Album Name/
           Track Title.m4a
           flac/
               Track Title.flac
```

Additional behaviors:

- album artwork is embedded into audio files
- album cover is applied as the **macOS folder icon**


# License

Apache-2.0

## Acknowledgements

We would like to thank Apple Music for its high-quality resources.

Once again, we are reminded that the good stuff is always public and free.