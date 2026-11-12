#!/usr/bin/env python3
"""Prepare the numbered PS1 soundtrack WAVs from Church of Kondo's archive."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from http.client import IncompleteRead
from pathlib import Path
import shutil
import subprocess
import tempfile
from time import sleep
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import urlopen

ARCHIVE = "https://archive.org/download/cok_collection/CoK.rar/"
FOLDER = "gdrive_backup_2/Super Mario 64 Remastered/"

# The order matches the upstream WAV slot list. Course Select is the
# remaster of the short jingle listed there as Game Start.
TRACKS = (
    "Title Theme",
    "Peach_s Letter",
    "Opening",
    "Bob-Omb Battlefield (Super Mario 64 Main Theme)",
    "Slider",
    "Inside the Castle Walls",
    "Looping Steps",
    "Dire, Dire Docks (Phase 1)",
    "Dire, Dire Docks (Phase 2)",
    "Dire, Dire Docks (Phase 3)",
    "Lethal Lava Land",
    "Snow Mountain",
    "Haunted House (Phase 1)",
    "Merry-Go-Round (2022 Update)",
    "Cave Dungeon",
    "Cave Dungeon (Hazy Maze)",
    "Cave Dungeon (Both Variations)",
    "Piranha Plant_s Lullaby",
    "Powerful Mario (2022 Update)",
    "Metallic Mario",
    "File Select",
    "Correct Solution",
    "Toad_s Message",
    "Power Star (2022 Update)",
    "Race Fanfare",
    "Star Catch Fanfare",
    "Course Select",
    "Course Clear",
    "Stage Boss",
    "Koopa_s Message",
    "Koopa_s Road",
    "Koopa_s Theme",
    "Koopa Clear",
    "Ultimate Koopa",
    "Ultimate Koopa Clear",
    "Ending Demo",
    "Staff Roll (2022 Update)",
    "Lakitu_s Message",
)


def download(url, destination):
    for attempt in range(4):
        try:
            with urlopen(url, timeout=90) as source, destination.open("wb") as output:
                shutil.copyfileobj(source, output)
            return
        except (URLError, TimeoutError, ConnectionError, IncompleteRead) as error:
            if attempt == 3 or (isinstance(error, HTTPError) and error.code in (403, 404)):
                raise
            sleep(2 ** attempt)


def import_track(index, output_dir):
    destination = output_dir / f"{index}.wav"
    if destination.is_file() and destination.stat().st_size > 44:
        return

    title = TRACKS[index]
    url = ARCHIVE + quote(f"{FOLDER}Super Mario 64 Remastered - {title}.flac", safe="")
    with tempfile.TemporaryDirectory(prefix=f"kondo-{index}-", dir=output_dir) as work:
        source = Path(work) / "track.flac"
        wav = Path(work) / "track.wav"
        download(url, source)
        subprocess.run(
            ["ffmpeg", "-nostdin", "-v", "error", "-xerror", "-i", str(source), "-map", "0:a:0",
             "-map_metadata", "-1", "-ar", "37800", "-ac", "2", "-c:a", "pcm_s16le", str(wav)],
            check=True,
        )
        wav.replace(destination)
    display_title = title.replace("_s", "'s")
    print(f"{index:02d}  {display_title}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / ".local")
    args = parser.parse_args()
    if shutil.which("ffmpeg") is None:
        parser.error("ffmpeg is required")

    args.output.mkdir(parents=True, exist_ok=True)
    failures = []
    with ThreadPoolExecutor(max_workers=4) as workers:
        tasks = [workers.submit(import_track, index, args.output) for index in range(len(TRACKS))]
        for index, task in enumerate(tasks):
            try:
                task.result()
            except (OSError, IncompleteRead, subprocess.CalledProcessError) as error:
                failures.append(f"{index}.wav ({TRACKS[index]}): {error}")

    if failures:
        parser.exit(1, "Could not import:\n" + "\n".join(failures) + "\n")
    print(f"{len(TRACKS)} soundtrack WAVs available in {args.output}")


if __name__ == "__main__":
    main()
