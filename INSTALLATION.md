# Installation

This port produces **five separate PS3 packages** from one source tree. Each
is a standalone game/EBOOT with its own TITLE_ID — install only the ones you
want. None of them include the copyrighted game data; you provide your own
`.pk3` files, bought legally, and FTP them to the console.

## What each build is

| Build | What it is | Notable features |
|---|---|---|
| **ioQuake3** | Quake III Arena | Full gameplay, bots, online/LAN multiplayer, mods via `fs_game` |
| **Open Arena** | Free/open Q3A-compatible game | Same engine features, uses free OA game data — no retail purchase needed |
| **Team Arena** | Q3A + the *Team Arena* mission pack | Adds TA's extra weapons/vehicles/game modes on top of ioQuake3 |
| **Quake 3 Classic** | Q3A speaking the original 1999 protocol | Crossplay with Sega Dreamcast community servers; no mod support, pak0–pak2 only |
| **Elite Force** | Star Trek Voyager: Elite Force multiplayer | Retail EF holomatch gameplay |

## Where to get the required files

Buy the base game(s) legally, then copy the `.pk3` files from your
install/disc into the paths below.

- **Quake III Arena** (needed for ioQuake3, Team Arena, and Quake 3 Classic): [Here](https://www.gog.com/en/game/quake_iii_arena)
- **Open Arena** [Here](https://openarena.ws/)
- **Star Trek Voyager: Elite Force** (needed for Elite Force only): [Here](https://www.gog.com/en/game/star_trek_voyager_elite_force)
- **Dreamcast community map pack**: [Here](https://lvlworld.com/download/id:999)

---

## Common layout

Every build FTPs its EBOOT to its own TITLE_ID folder under
`/dev_hdd0/game/`, but all builds share one game-data root,
`/dev_hdd0/data/ioq3/`, so reinstalling a PKG never wipes your paks or config.

```
/dev_hdd0/game/<TITLE_ID>/USRDIR/EBOOT.BIN   (+ PARAM.SFO, ICON0.PNG one level up)
/dev_hdd0/data/ioq3/<game dir(s) below>
```

USB fallback: `/dev_usb000/quake3/<gamedir>/pak0.pk3` is also probed if the
HDD path is missing.

Create `/dev_hdd0/data/ioq3/` via FTP before first boot.

---

## ioQuake3 (Q3A)

TITLE_ID: `IOQ3PS300`

Copy your Quake III Arena `baseq3` paks:

```
/dev_hdd0/data/ioq3/baseq3/pak0.pk3 … pak8.pk3
```

## Open Arena

TITLE_ID: `IOOAPS300`

Copy Open Arena's paks — no `baseq3` needed:

```
/dev_hdd0/data/ioq3/baseoa/pak0.pk3 …
```

## Team Arena

TITLE_ID: `IOTAPS300`

Needs **both** Q3A's `baseq3` and Team Arena's `missionpack`:

```
/dev_hdd0/data/ioq3/baseq3/pak0.pk3 … pak8.pk3
/dev_hdd0/data/ioq3/missionpack/pak0.pk3 … pak3.pk3
```

## Quake 3 Classic (Dreamcast crossplay)

TITLE_ID: `IOQCPS301`

Only `pak0`–`pak2` are loaded (byte-identical to the Dreamcast data files);
higher paks are ignored:

```
/dev_hdd0/data/ioq3/baseq3/pak0.pk3
/dev_hdd0/data/ioq3/baseq3/pak1.pk3
/dev_hdd0/data/ioq3/baseq3/pak2.pk3
```

To play on Dreamcast community servers, also add the community map pack:

```
/dev_hdd0/data/ioq3/baseq3/dc-mappack.pk3
```

## Elite Force

TITLE_ID: `IOEFPS300`

Copy your Elite Force retail `baseEF` paks:

```
/dev_hdd0/data/ioq3/baseEF/pak0.pk3 …
```
---

## Installing a PKG

Every [release](https://github.com/Mayo1970/Ioquake3-PS3/releases) already
includes a prebuilt `.pkg` for each build — no need to compile anything
yourself.

1. Download the `.pkg` for the build you want from the release.
2. Copy it to a USB drive.
3. Install via Package Manager on your CFW/HEN PS3.
4. FTP the matching game data from the sections above into
   `/dev_hdd0/data/ioq3/`.
5. Refresh the XMB game list.

## Troubleshooting

If a build misbehaves after switching between builds (wrong game name,
missing intro video, etc.), delete that build's config file on the PS3
(`q3config.cfg` / `oaconfig.cfg` / `teamarenaconfig.cfg` / `efconfig.cfg`) —
a stale config can override the correct defaults.