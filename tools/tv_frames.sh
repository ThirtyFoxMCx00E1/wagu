#!/bin/sh
# TV Player 1.5: turn a video into an image sequence for moonloader/tvplayer/videos/<name>/
# Works on a PC or in Termux (pkg install ffmpeg).
#   sh tv_frames.sh input.mp4 [name] [fps] [size] [crop]
#   name: the folder name (default: the file name)   fps: 30   size: 256 (square, power of two: 256 or 512)
#   crop: 1 = cut a 4:3 picture out of the middle (good for CRT TVs and 4:3 videos with black side bars), 0 = whole picture
# Then copy the folder to Android/data/com.rockstargames.gtasa/files/TYFMC/MP/moonloader/tvplayer/videos/
set -e
in="$1"; [ -n "$in" ] || { echo "usage: sh tv_frames.sh input.mp4 [name] [fps] [size] [crop]"; exit 1; }
name="${2:-$(basename "${in%.*}")}"; fps="${3:-30}"; size="${4:-256}"; crop="${5:-1}"
vf="fps=$fps"
[ "$crop" = "1" ] && vf="$vf,crop='min(iw,ih*4/3)':ih"
vf="$vf,scale=$size:$size:flags=lanczos"
mkdir -p "$name"
ffmpeg -v error -y -i "$in" -vf "$vf" -q:v 5 -pix_fmt yuvj420p "$name/%05d.jpg"
ffmpeg -v error -y -i "$in" -vn -ac 2 -ar 44100 -b:a 96k "$name/audio.mp3" || echo "no sound in $in"
n=$(ls "$name" | grep -c '\.jpg$')
printf '; TV Player image sequence (tools/tv_frames.sh)\nFps=%s\nFrames=%s\n' "$fps" "$n" > "$name/info.ini"
echo "$name: $n frames at $fps fps"
