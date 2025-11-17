# Project README

## Overview

This project captures video and audio streams from configured sources (RTSP feeds from security cameras) and outputs them into a structured directory hierarchy suitable for HLS playback. Each source may have multiple audio formats and a video stream, stored in separate directories with corresponding fragment files (`.m4s`) and initialization segments (`init.mp4`).

---

## Configured Sources

### Left Garage

| Property | Value |
|----------|-------|
| Video    | Yes   |
| Audio    | Yes   |
| RTSP URL | `rtsps://<IP>:<PORT>/ggGdsd223dsdsd3?enableSrtps` |
| Output Directory | `/scratch2/Recordings/Output` |

> The source named **Left Garage** captures both video and audio from the RTSP stream and saves it to the output directory.

---

## Directory Structure

```
NVR/
└── Left Garage/
    └── 2025-11-07/
        ├── audio/
        │   ├── aac/
        │   │   ├── init.mp4
        │   │   ├── audio.m3u8
        │   │   ├── 16-00-00.m4s
        │   │   ├── 16-00-05.m4s
        │   │   └── 16-00-10.m4s
        │   └── opus/
        │       ├── init.mp4
        │       ├── audio.m3u8
        │       ├── 16-00-00.m4s
        │       ├── 16-00-05.m4s
        │       └── 16-00-10.m4s
        ├── master.m3u8
        └── video/
            └── h264/
                ├── init.mp4
                ├── video.m3u8
                ├── 16-00-00.m4s
                ├── 16-00-05.m4s
                └── 16-00-10.m4s
```

---

## Segment Listing

### Video (H.264)

| Filename           | Time       |
|-------------------|------------|
| init.mp4           | –          |
| 16-00-00.m4s       | 16:00:00   |
| 16-00-05.m4s       | 16:00:05   |
| 16-00-10.m4s       | 16:00:10   |
| video.m3u8         | –          |

---

### Audio (AAC)

| Filename           | Time       |
|-------------------|------------|
| init.mp4           | –          |
| 16-00-00.m4s       | 16:00:00   |
| 16-00-05.m4s       | 16:00:05   |
| 16-00-10.m4s       | 16:00:10   |
| audio.m3u8         | –          |

---

### Audio (Opus)

| Filename           | Time       |
|-------------------|------------|
| init.mp4           | –          |
| 16-00-00.m4s       | 16:00:00   |
| 16-00-05.m4s       | 16:00:05   |
| 16-00-10.m4s       | 16:00:10   |

---

## Usage

1. Ensure the output directory exists and the process has write access:

```bash
mkdir -p /scratch2/Recordings/Output
```

2. Start the recording process (example):

```bash
sudo service hls-nvr start
```

3. Access recordings via HLS player using `master.m3u8`. Both AAC and Opus audio are supported.

---

## Notes

- RTSP streams use **SRTP** for secure streaming.  
- Segments are generated in **2–5 second intervals** for both audio and video.  
- The directory hierarchy separates audio codecs and video for easier management and playback.  
- Segment filenames correspond to the wall clock time of recording.
