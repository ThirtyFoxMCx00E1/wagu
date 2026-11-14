-- MonetLoader for MP: example PC MoonLoader script, unchanged API.
-- Put any PC .lua script in this folder (files/TYFMC/MP/moonloader) and its libraries in lib/.
script_name('MP Hello')
script_author('ThirtyFoxMCx00E1')
script_version('1.0')

local sampev = require 'samp.events'

function main()
  while not isSampAvailable() do wait(100) end
  sampRegisterChatCommand('hello', function(arg)
    local _, myid = sampGetPlayerIdByCharHandle(PLAYER_PED)
    sampAddChatMessage(string.format('{38BDF8}[MP Lua]{FFFFFF} Hi %s (id %d) on %s', sampGetPlayerNickname(myid), myid, sampGetCurrentServerName()), -1)
  end)
  sampAddChatMessage('{38BDF8}[MP Lua]{FFFFFF} MonetLoader for MP is running. Type /hello', -1)
  wait(-1)
end

function sampev.onServerMessage(color, text)
  -- return false to hide a line, or return {color, newText} to edit it
end

function sampev.onSendChat(text)
  -- runs when you send chat from MP's chat box
end
