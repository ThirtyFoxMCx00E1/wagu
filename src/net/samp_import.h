#pragma once
// 4.11: import a PC SA-MP server (gamemode + filterscripts + scriptfiles + server.cfg) for an MP server.
// Takes a .rar (UnRAR source by Alexander Roshal, third_party/unrar), a .zip (zlib), a folder, or a
// single compiled .amx, and returns the folder that has gamemodes/ in it (the "server root").
#include <string>

// archive -> destDir (made if missing). root = the server folder found inside. msg = what happened.
bool SampImport(const std::string& src, const std::string& destDir, std::string& root, std::string& msg);
// the server folder under dir (dir itself, or the first sub folder with gamemodes/ / server.cfg), "" = none
std::string SampFindRoot(const std::string& dir);
