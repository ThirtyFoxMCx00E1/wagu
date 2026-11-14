TV Player videos (TV Player 1.5-mp, MP 4.13.44+)

Best: an IMAGE SEQUENCE folder. Every frame is a small JPG that MonetLoader loads like MP's other pictures,
so it shows on every phone (no video decoder needed):

  videos/tvplayer/00001.jpg, 00002.jpg, ...   frames, 30 a second, square 256x256 (or 512x512)
  videos/tvplayer/audio.mp3                   the sound (optional)
  videos/tvplayer/info.ini                    Fps=30 and Frames=<count> (optional, counted when missing)

Make one from any video with ffmpeg (PC, or Termux: pkg install ffmpeg), tools/tv_frames.sh in the MP source:
  sh tv_frames.sh myvideo.mp4 myvideo
or by hand:
  ffmpeg -i myvideo.mp4 -vf "fps=30,crop='min(iw,ih*4/3)':ih,scale=256:256" -q:v 5 -pix_fmt yuvj420p myvideo/%05d.jpg
  ffmpeg -i myvideo.mp4 -vn -b:a 96k myvideo/audio.mp3

.mp4 / .3gp / .webm / .mkv files here still work too, through the phone's video decoder (black on some phones).
The bundled "tvplayer" folder is tvplayer.mp4 from the tvplayer mod (Monet loader/tvplayer.zip) as frames.
