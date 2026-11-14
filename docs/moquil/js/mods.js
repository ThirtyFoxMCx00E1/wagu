/* Moquil - Library / Store catalog. 4.13.21: no generated cover art, every mod / app shows the plain "unknown" tile in the Library / Store. 4.13.22: the "playing" activity of friends (Active Now, Home) uses act (the real MP icon), never the unknown tile. Every download is a file on the MP website (never a GitHub page). */
(function (G) {
  'use strict';
  var SITE = 'https://mpsamp-open.github.io/';
  G.MOQ_SITE = SITE;
  G.MOQ_MODS = [
    {
      id: 'mp', name: 'MP Multiplayer', ver: 'latest', kind: 'Multiplayer mod', size: 'about 46 MB', tag: 'Recommended',
      cover: 'img/mods/unknown.svg', icon: 'img/mods/unknown.svg', act: 'img/mods/mp.png', file: SITE + 'downloads/MP-client.zip', fileName: 'MP-client.zip', action: 'Install',
      short: 'Online multiplayer for GTA San Andreas on Android: servers, Social, Mission sync, voice and more.',
      desc: 'MP (libMP32.so / libMP64.so) turns GTA San Andreas on Android into an online game. Pick a server in MP Launcher, log in with your MP account and play with up to 1000 players. The Social page in the game is the same network as Moquil: your friends, private chats, group chats, party, communities and inbox show on both.',
      features: ['Server browser with 24/7 servers, Battle Royale, DayZ, Role Play and your own servers', 'Mission sync: up to 16 players in the same story mission, visible in the cutscenes', 'Social: friends, chats, party and communities (shared with Moquil)', 'Voice chat, textdraws, custom modpacks', 'One MP account for the game, Moquil, MP Voice and MP Textdraw Editor'],
      gallery: ['img/mods/unknown.svg']
    },
    {
      id: 'mlg', name: 'MLG Production', ver: '1.6.4', kind: 'AML mod', size: '1.6 MB', tag: 'Movie maker',
      cover: 'img/mods/unknown.svg', icon: 'img/mods/unknown.svg', file: SITE + 'mods/MLG-Production-1.6.4.zip', fileName: 'MLG-Production-1.6.4.zip', action: 'Download',
      short: 'A movie-making panel: free cam, camera paths, ped tools, chroma key, scenes, weather and a recording timeline.',
      desc: 'MLG Production adds a top-right menu with the MLG wolf logo and eight photo buttons (all still, only the title shines), plus a bottom recording timeline. Record the player on foot or in a car at 30 fps, replay takes on any ped, pose bones and make your own animations, then fly the camera along a path of 3D arrows. The zip has libMLG32.so (32-bit) and libMLG64.so (64-bit): copy the one for your game into the AML mods folder.',
      features: ['Free Cam: free fly, orbit and follow, smooth movement, FOV', 'Camera paths made of 3D arrows (ease in-out, smooth, linear)', 'Ped Tools: select, bring, freeze, hide, spawn any ped', 'Chroma Key backdrop (green, blue, black, white, pink)', 'Scene: game speed, density, triggers; all 23 weathers and time of day', 'Entity Tools: bone posing, animation keys, timeline like PumaStudio', 'Record / Stop while driving from the folded timeline bar'],
      gallery: ['img/mods/unknown.svg']
    },
    {
      id: 'camfx', name: 'CamFX', ver: '2.1', kind: 'AML mod', size: '160 KB', tag: 'Camera',
      cover: 'img/mods/unknown.svg', icon: 'img/mods/unknown.svg', file: SITE + 'mods/CamFX-2.1.zip', fileName: 'CamFX-2.1.zip', action: 'Download',
      short: 'Camera FOV effects: sprint, fall, parachute, vehicle and weapon zoom with smooth blends.',
      desc: 'CamFX (CameraFX 2.1) is the native version of the SACamFX CLEO script. The field of view widens while you sprint or fall, follows your speed in a vehicle and zooms with rifles while aiming. Settings are in Settings > CameraFX in the game. Needs AML and SAUtils. The zip has libCameraFX32.so and libCameraFX64.so.',
      features: ['Sprint zoom and fall zoom', 'Parachute, vehicle (follows speed) and weapon zoom', 'Change FOV when falling, sky dive only, drunk effect when falling', 'Smooth blend transitions, 8 languages', '2.1: 64-bit fix for SAUtils script outputs'],
      gallery: ['img/mods/unknown.svg']
    },
    {
      id: 'tde', name: 'MP Textdraw Editor', ver: 'latest', kind: 'Android app', size: 'about 2 MB', tag: 'Tool',
      cover: 'img/mods/unknown.svg', icon: 'img/mods/unknown.svg', file: SITE + 'downloads/MP-Textdraw-Editor.apk', fileName: 'MP-Textdraw-Editor.apk', action: 'Download',
      short: 'Design textdraws for your MP server, the same tools as the in-game /tde editor.',
      desc: 'Make textdraws on the phone, save a .mptd file, then import it in MP Launcher > Add server > Plugins > Textdraw. Log in with your MP account.',
      features: ['Boxes, text, sprites and preview models', 'Same fonts and colors as the game', 'Export .mptd for MP Launcher'],
      gallery: ['img/mods/unknown.svg']
    },
    {
      id: 'voice', name: 'MP Voice', ver: 'latest', kind: 'Android app', size: 'about 1 MB', tag: 'Voice',
      cover: 'img/mods/unknown.svg', icon: 'img/mods/unknown.svg', file: SITE + 'downloads/MP-Voice.apk', fileName: 'MP-Voice.apk', action: 'Download',
      short: 'Voice chat helper for MP servers.',
      desc: 'MP Voice keeps the voice chat of MP servers running next to the game. Moquil also has voice rooms of its own in every community and group chat.',
      features: ['Push to talk or open mic', 'Works with MP servers that have voice chat'],
      gallery: ['img/mods/unknown.svg']
    }
  ];
})(window);
