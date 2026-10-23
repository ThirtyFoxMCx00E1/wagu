import net from "node:net";
import readline from "node:readline";

const port = Number(process.env.PORT || 6767);
const rooms = new Map();
let nextId = 1;
let nextNpcId = 1;
let nextVehicleId = 1;
const playersById = new Map();
const blockedAddresses = new Map();

function roomFor(name) { if (!rooms.has(name)) rooms.set(name, new Map()); return rooms.get(name); }
function leaderFor(room) { return room.values().next().value; }
function timePacket(time) { return `TIME|${time.hour}|${time.minute}|${time.day}\n`; }
function announceTimeAuthority(room) { const leader=leaderFor(room); if(leader) broadcast(room,`TIME_OWNER|${leader.id}\n`); else room.clock=null; }
function num(value, fallback) { const n = Number(value); return Number.isFinite(n) ? n : fallback; }
function clamp(value) { return Math.max(-3000, Math.min(3000, value)); }
function model(value) { const n = Number(value); return Number.isInteger(n) && n >= 0 && n <= 311 ? n : 0; }
const specialNames = new Set(["ANDRE","BBTHIN","BB","CAT","CESAR","COPGRL1","COPGRL2","COPGRL3","CLAUDE","CROGRL1","CROGRL2","CROGRL3","DWAYNE","EMMET","FORELLI","GANGRL1","GANGRL2","GANGRL3","GUNGRL1","GUNGRL2","GUNGRL3","HERN","JANITOR","JETHRO","JIZZY","KENDL","MACCER","MADDOGG","MECGRL1","MECGRL2","MECGRL3","NURGRL1","NURGRL2","NURGRL3","OGLOC","PAUL","PULASKI","ROSE","RYDER1","RYDER2","RYDER3","SINDACO","SMOKE","SMOKEV","SUZIE","SWEET","TBONE","TENPEN","TORINO","TRUTH","WUZIMU","ZERO"]);
function specialModelName(value) { const name = String(value || "").trim().toUpperCase(); return specialNames.has(name) ? name : ""; }

function fireAnimationFields(group, anim) { const g = Number(group), a = Number(anim); return Number.isInteger(g) && g > 0 && g <= 0xFFFF && (a === 224 || a === 225) ? `|${g}|${a}` : ""; }
function weapon(value) { const n = Number(value); return Number.isInteger(n) && (n >= 0 && n <= 0x2E || n === 0x3A) ? n : 0; }
function move(value) { const n = Number(value); return Number.isInteger(n) && n >= 1 && n <= 7 ? n : 1; }
function color(value) { const n = Number(value); return Number.isInteger(n) && n >= 0 && n <= 7 ? n : 0; }
function damageFor(weaponId) { return weaponId >= 22 ? 18 : 12; }
function findHit(shooter, shot) {
 const length = Math.hypot(shot.dx, shot.dy, shot.dz);
 if (!Number.isFinite(length) || length < 0.001) return null;
 const dx = shot.dx / length, dy = shot.dy / length, dz = shot.dz / length;
 let best = null, bestT = 45;
 const consider = (target, kind, radius) => {
 if (target.health <= 0) return;
 const centerZ = target.z + 0.85;
 const rx = target.x - shot.x, ry = target.y - shot.y, rz = centerZ - shot.z;
 const t = rx * dx + ry * dy + rz * dz;
 if (t < 0 || t > bestT) return;
 const qx = rx - t * dx, qy = ry - t * dy, qz = rz - t * dz;
 if (qx*qx + qy*qy + qz*qz <= radius*radius && Math.abs(centerZ - (shot.z + t*dz)) <= 1.15) {
 best = { kind, target };
 bestT = t;
 }
 };
 for (const target of shooter.room.values()) if (target.id !== shooter.id) consider(target, "player", 1.35);
 return best;
}
function send(socket, line) { if (!socket.destroyed) socket.write(line); }
function broadcast(room, line, exceptId = 0) {
 for (const player of room.values()) if (player.id !== exceptId) send(player.socket, line);
}



function snapshot(player) {
 const safeName = String(player.name || "CJ").replace(/[|\r\n]/g, "_").slice(0, 14);
 return `PLAYER|${player.id}|${player.modelId}|${player.animationState}|${player.x.toFixed(3)}|${player.y.toFixed(3)}|${player.z.toFixed(3)}|${player.heading.toFixed(3)}|${player.health}|${player.armour}|${player.weapon}|${player.vehicleId || 0}|${player.vehicleSeat || 0}|${safeName}|${player.color}|${player.specialModelName || ""}|${player.areaCode || 0}\n`;
}
function animationPacket(player) {
 const a = player.animation || {};
 return `ANIM2|${player.id}|${a.sequence >>> 0}|${a.locomotion}|${a.action}|${a.flags}|${a.weaponState}|${a.animGroup}|${a.phase.toFixed(4)}|${a.blend.toFixed(4)}|${a.aimYaw.toFixed(4)}|${a.aimPitch.toFixed(4)}\n`;
}
function associationPacket(player) {
 const a = player.associations || [];
 return `ANIM3|${player.id}|${a.length}${a.map(x => `|${x.group}|${x.animationId}|${x.flags}|${x.blend.toFixed(5)}|${x.blendDelta.toFixed(5)}|${x.currentTime.toFixed(5)}|${x.speed.toFixed(5)}|${x.timeStep.toFixed(5)}`).join("")}\n`;
}
function parseAssociations(parts) {
 if (parts.length < 2) return null;
 const id = Number(parts[0]), count = Number(parts[1]);
 if (!Number.isInteger(id) || id <= 0 || !Number.isInteger(count) || count < 0 || count > 64 || parts.length !== 2 + count * 8) return null;
 const out = [];
 for (let i = 0; i < count; i++) {
 const o = 2 + i * 8, n = parts.slice(o, o + 8).map(Number);
 if (!n.every(Number.isFinite) || !Number.isInteger(n[0]) || n[0] < 0 || n[0] > 65535 || !Number.isInteger(n[1]) || n[1] < -32768 || n[1] > 32767 || !Number.isInteger(n[2]) || n[2] < 0 || n[2] > 65535 || Math.abs(n[3]) > 100 || Math.abs(n[4]) > 100 || Math.abs(n[5]) > 100000 || Math.abs(n[6]) > 100 || Math.abs(n[7]) > 100) return null;
 out.push({ group: n[0], animationId: n[1], flags: n[2], blend: n[3], blendDelta: n[4], currentTime: n[5], speed: n[6], timeStep: n[7] });
 }
 return { id, associations: out };
}
function parseAnimation(parts) {
 if (parts.length !== 11) return null;
 const n = parts.map(Number);
 if (!n.every(Number.isFinite)) return null;
 const [id, sequence, locomotion, action, flags, weaponState, animGroup, phase, blend, aimYaw, aimPitch] = n;
 if (!Number.isInteger(sequence) || sequence < 0 || sequence > 0xFFFFFFFF || !Number.isInteger(locomotion) || locomotion < 1 || locomotion > 7 ||
 !Number.isInteger(action) || action < 0 || action > 17 || !Number.isInteger(flags) || flags < 0 || flags > 0xFFFF ||
 !Number.isInteger(weaponState) || weaponState < 0 || weaponState > 255 || !Number.isInteger(animGroup) || animGroup < 0 || animGroup > 0xFFFF ||
 phase < 0 || phase > 1 || blend < 0 || blend > 1 || Math.abs(aimYaw) > 10000 || Math.abs(aimPitch) > 10000) return null;
 return { id, sequence, locomotion, action, flags, weaponState, animGroup, phase, blend, aimYaw, aimPitch };
}



function npcSnapshot(n) {
 return `NPC_NET|${n.id}|${n.ownerId}|${n.localRef}|${n.modelId}|${n.x.toFixed(3)}|${n.y.toFixed(3)}|${n.z.toFixed(3)}|${n.heading.toFixed(3)}|${n.health}|${n.armour}|${n.weapon}|${n.moveState}|${n.vehicleId || 0}|${n.vehicleSeat || 0}|${n.specialModelName || ""}\n`;
}
function npcVitals(n) { return `NPC_VITALS|${n.id}|${n.ownerId}|${n.localRef}|${n.health}|${n.armour}\n`; }
function vehicleKeyFor(playerId, localKey) { return `${playerId}:${localKey}`; }
function vehicleSnapshot(v) {
 return `VEHICLE|${v.id}|${v.modelId}|${v.ownerId}|${v.driverSeat}|${v.x.toFixed(3)}|${v.y.toFixed(3)}|${v.z.toFixed(3)}|${v.heading.toFixed(3)}|${v.vx.toFixed(4)}|${v.vy.toFixed(4)}|${v.vz.toFixed(4)}|${v.health.toFixed(1)}|${v.primaryColor}|${v.secondaryColor}|${v.engineOn}|${v.lightsOn}|${v.sirenOn}|${v.doorLock}|${v.dirt.toFixed(3)}|${v.tyreStatus}|${v.doorStatus}|${v.lightStatus}|${v.panelStatus}|${v.trailerId}|${v.rightX.toFixed(5)}|${v.rightY.toFixed(5)}|${v.rightZ.toFixed(5)}|${v.upX.toFixed(5)}|${v.upY.toFixed(5)}|${v.upZ.toFixed(5)}|${v.atX.toFixed(5)}|${v.atY.toFixed(5)}|${v.atZ.toFixed(5)}\n`;
}
function allocateVehicleId(room) {
 room.vehicles ??= new Map();
 for (let i = 0; i < 65535; i++) {
  const id = nextVehicleId++;
  if (nextVehicleId > 65535) nextVehicleId = 1;
  if (id && !room.vehicles.has(id)) return id;
 }
 return 0;
}
function removeVehicle(room, vehicle, exceptId = 0) {
 if (!vehicle) return;
 room.vehicles.delete(vehicle.id);
 if (room.vehicleKeys) for (const [key,id] of room.vehicleKeys) if (id===vehicle.id) room.vehicleKeys.delete(key);
 broadcast(room, `VEHICLE_REMOVE|${vehicle.id}\n`, exceptId);
}
function reconcileNpcHealth(n) {
 let health = n.health, armour = n.armour;
 for (const report of n.healthReports.values()) {
 health = Math.min(health, report.health);
 armour = Math.min(armour, report.armour);
 }
 const changed = health !== n.health || armour !== n.armour;
 n.health = health;
 n.armour = armour;
 return changed;
}
function playerVitals(player) { return `PLAYER_VITALS|${player.id}|${player.health}|${player.armour}\n`; }
function reconcilePlayerVitals(player) {
 let health = player.health, armour = player.armour;
 for (const report of player.healthReports.values()) {
 health = Math.min(health, report.health);
 armour = Math.min(armour, report.armour);
 }
 const changed = health !== player.health || armour !== player.armour;
 player.health = health;
 player.armour = armour;
 return changed;
}
function removePlayer(player, reason = "kicked") {
 if (!player || player.removed) return;
 player.removed = true;
 if (player.room.npcs) for (const [key,n] of player.room.npcs) if (n.ownerId===player.id) { broadcast(player.room, `NPC_REMOVE|${n.id}\n`, player.id); player.room.npcs.delete(key); }
 if (player.room.vehicles) for (const vehicle of [...player.room.vehicles.values()]) if (vehicle.ownerId===player.id) removeVehicle(player.room, vehicle, player.id);
 if (player.room.vehicleKeys) for (const key of [...player.room.vehicleKeys.keys()]) if (key.startsWith(`${player.id}:`)) player.room.vehicleKeys.delete(key);
 for (const other of player.room.values()) other.healthReports.delete(player.id);
 player.room.delete(player.id);
 announceTimeAuthority(player.room);
 playersById.delete(player.id);
 send(player.socket, `CHAT|SERVER|${reason}\n`);
 player.socket.destroy();
 console.log(`PLAYER_REMOVE id=${player.id} reason=${reason}`);
 broadcast(player.room, `REMOVE|${player.id}\n`);
}
function adminCommand(line) {
 const parts = line.trim().split(/\s+/);
 const command = (parts[0] || "").toLowerCase();
 const id = Number(parts[1]);
 if (command === "/kick" && Number.isInteger(id)) {
 const player = playersById.get(id);
 if (!player) return console.log(`ADMIN player ${id} was not found`);
 removePlayer(player, `Player ${id} was kicked by the server`);
 return;
 }
 if (command === "/timeout" && Number.isInteger(id)) {
 const seconds = Math.max(1, Math.min(86400, Number(parts[2] || 60)));
 const player = playersById.get(id);
 if (!player) return console.log(`ADMIN player ${id} was not found`);
 blockedAddresses.set(player.address, Date.now() + seconds * 1000);
 removePlayer(player, `Player ${id} timed out for ${seconds}s`);
 return;
 }
 if (command === "/players") {
 console.log([...playersById.values()].map(p => `${p.id}:${p.name}`).join(" ") || "No players connected");
 return;
 }
 if (command === "/help") {
 console.log("Commands: /players, /kick <playerid>, /timeout <playerid> [seconds]");
 return;
 }
 if (line.trim()) console.log("Unknown command. Use /help");
}

const server = net.createServer(socket => {
 let player;
 let buffer = "";
 socket.setNoDelay(true);
 const address = socket.remoteAddress || "unknown";

 socket.on("data", data => {
 buffer += data.toString();
 if (buffer.length > 65536) { socket.destroy(); return; }
 let end;
 while ((end = buffer.indexOf("\n")) !== -1) {
 const line = buffer.slice(0, end).trim();
 buffer = buffer.slice(end + 1);
 if (!line || line.length > 4096) continue;
 const [type, ...parts] = line.split("|");

 if (type === "JOIN" && !player) {
 const blockedUntil = blockedAddresses.get(address) || 0;
 if (blockedUntil > Date.now()) { send(socket, "CHAT|SERVER|You are temporarily timed out\n"); socket.destroy(); continue; }
 if (blockedUntil) blockedAddresses.delete(address);
	 const room = roomFor("default"); room.vehicles ??= new Map(); room.vehicleKeys ??= new Map();
 player = { id: nextId++, name: (parts[0] || "CJ").slice(0, 14), color: color(parts[1]),
 x: 0, y: 0, z: 3, heading: 0, areaCode: 0, health: 100, armour: 0, weapon: 0,
 modelId: 7, specialModelName: "", animationState: 1, vehicleId: 0, vehicleSeat: 0,
 socket, address, room, removed: false,
 healthReports: new Map(), vitalsInitialized: false, healthReportResumeAtMs: 0,
 animation: { sequence: 0, locomotion: 1, action: 1, flags: 0x30, weaponState: 0, animGroup: 0, phase: 0, blend: 1, aimYaw: 0, aimPitch: 0 }, associations: [],
 input: { fire: 0, jump: 0, enterVehicle: 0, moveX: 0, moveY: 0, aimX: 0, aimY: 0 } };
 room.set(player.id, player);
 playersById.set(player.id, player);
 send(socket, `WELCOME|${player.id}\n`);
	 for (const other of room.values()) if (other.id !== player.id) { send(socket, snapshot(other)); send(socket, animationPacket(other)); send(socket, associationPacket(other)); }
	 if (room.vehicles) for (const v of room.vehicles.values()) send(socket, vehicleSnapshot(v));
	 if (room.npcs) for (const n of room.npcs.values()) if (n.ownerId !== player.id) send(socket, npcSnapshot(n));
 broadcast(room, `CHAT|SERVER|${player.name} joined the crew\n`);


 broadcast(room, `NAME|${player.id}|${String(player.name).replace(/[|\r\n]/g, "_").slice(0, 14)}|${player.color}\n`, player.id);
 announceTimeAuthority(room);
	 if(room.clock) send(socket,timePacket(room.clock));
	 console.log(`PLAYER_JOIN id=${player.id} name=${player.name}`);
	 continue;
 }
 if (!player) continue;

 if (type === "CHATMSG") {
   const message = String(parts.join("|")).replace(/[|\r\n]/g, " ").trim().slice(0, 160);
   if (message) broadcast(player.room, `CHAT|${String(player.name).replace(/[|\r\n]/g, "_").slice(0, 14)}|${message}\n`);
 } else if (type === "TIME" && parts.length === 3) {
 const hour=Number(parts[0]), minute=Number(parts[1]), day=Number(parts[2]);
 if(leaderFor(player.room)?.id===player.id&&Number.isInteger(hour)&&hour>=0&&hour<=23&&Number.isInteger(minute)&&minute>=0&&minute<=59&&Number.isInteger(day)&&day>=1&&day<=31){
 player.room.clock={hour,minute,day};
 broadcast(player.room,timePacket(player.room.clock),player.id);
 }
 } else if (type === "INPUT") {
 const oldWeapon = player.weapon;
 const oldColor = player.color;
 const oldModelId = player.modelId;
 const oldSpecialName = player.specialModelName;
 player.x = clamp(num(parts[0], player.x)); player.y = clamp(num(parts[1], player.y)); player.z = clamp(num(parts[2], player.z));
 player.heading = num(parts[3], player.heading);

 const incomingArea = Number(parts[21]);
 if (Number.isInteger(incomingArea) && incomingArea >= 0 && incomingArea <= 255) player.areaCode = incomingArea;
 player.input.fire = num(parts[4], 0) ? 1 : 0; player.input.jump = num(parts[5], 0) ? 1 : 0; player.input.enterVehicle = num(parts[6], 0) ? 1 : 0;
 player.input.moveX = num(parts[7], 0); player.input.moveY = num(parts[8], 0); player.input.aimX = num(parts[9], 0); player.input.aimY = num(parts[10], 0);
 player.modelId = model(parts[11]); player.animationState = move(parts[12]);
	 player.specialModelName = player.modelId >= 290 && player.modelId <= 299 && parts.length > 17
	 ? specialModelName(parts[17]) : "";
	 const claimedVehicleId = Number(parts[18] || 0), claimedSeat = Number(parts[19] || 0), localVehicleKey = Number(parts[20] || 0);
	 let resolvedVehicleId = 0;
	 if (player.room.vehicles && Number.isInteger(claimedVehicleId) && claimedVehicleId > 0 && player.room.vehicles.has(claimedVehicleId)) resolvedVehicleId = claimedVehicleId;
	 if (!resolvedVehicleId && player.room.vehicleKeys && Number.isInteger(localVehicleKey) && localVehicleKey > 0) resolvedVehicleId = player.room.vehicleKeys.get(vehicleKeyFor(player.id, localVehicleKey)) || 0;
	 player.vehicleId = resolvedVehicleId;
	 player.vehicleSeat = resolvedVehicleId && Number.isInteger(claimedSeat) && claimedSeat >= 0 && claimedSeat <= 8 ? claimedSeat : 0;
	 const ownerHealth = parts.length > 14 ? Math.max(0, Math.min(100, Math.trunc(num(parts[14], player.health)))) : player.health;
 const ownerArmour = parts.length > 15 ? Math.max(0, Math.min(100, Math.trunc(num(parts[15], player.armour)))) : player.armour;
 const previousOwnerReport = player.healthReports.get(player.id);
 const ownerRespawned = player.vitalsInitialized && parts.length > 15 && previousOwnerReport &&
 previousOwnerReport.health === 0 && ownerHealth > 0;
 if (!player.vitalsInitialized || ownerRespawned) {
 if (ownerRespawned) {
 player.healthReports.clear();
 player.healthReportResumeAtMs = Date.now() + 1000;
 }
 player.health = ownerHealth;
 player.armour = ownerArmour;
 player.vitalsInitialized = true;
 }
 player.healthReports.set(player.id, { health: ownerHealth, armour: ownerArmour });
 if (ownerRespawned || reconcilePlayerVitals(player)) broadcast(player.room, playerVitals(player));
 if (parts.length > 13) player.weapon = weapon(parts[13]);
 if (parts.length > 16) player.color = color(parts[16]);
 if (player.weapon !== oldWeapon || player.color !== oldColor ||
 player.modelId !== oldModelId || player.specialModelName !== oldSpecialName) {
 console.log(`WEAPON_SERVER_RECEIVE id=${player.id} weapon=${player.weapon}`);
 console.log(`WEAPON_SERVER_STORED id=${player.id} weapon=${player.weapon}`);
 const packet = snapshot(player);
 for (const other of player.room.values()) if (other.id !== player.id && !other.socket.destroyed) {
 console.log(`WEAPON_SERVER_BROADCAST from=${player.id} to=${other.id} weapon=${player.weapon}`);
 send(other.socket, packet);
 }
 }
 } else if (type === "ANIM3") {
 const parsed = parseAssociations(parts);
 if (parsed && parsed.id === player.id) { player.associations = parsed.associations; broadcast(player.room, associationPacket(player), player.id); }
 } else if (type === "ANIM2") {
 const animation = parseAnimation(parts);
 if (animation && animation.id === player.id && animation.sequence >= player.animation.sequence) {
 player.animation = animation;
 player.animationState = animation.locomotion;
 broadcast(player.room, animationPacket(player), player.id);
 }
 } else if (type === "PLAYER_HEALTH" && parts.length === 3) {
 const targetId = Number(parts[0]), health = Number(parts[1]), armour = Number(parts[2]);
 const target = player.room.get(targetId);
 if (Number.isInteger(targetId) && targetId > 0 && Number.isInteger(health) && health >= 0 && health <= 100 &&
 Number.isInteger(armour) && armour >= 0 && armour <= 100 && target && target.vitalsInitialized &&
 target.id !== player.id && Date.now() >= target.healthReportResumeAtMs) {
 target.healthReports.set(player.id, { health, armour });
 if (reconcilePlayerVitals(target)) broadcast(player.room, playerVitals(target));
 }
 } else if (type === "BONES2" && parts.length >= 2) {

 const count = Number(parts[1]);
 if (Number.isInteger(count) && count >= 0 && count <= 19 && parts.length === 2 + count * 5)
 broadcast(player.room, `BONES2|${player.id}|${count}|${parts.slice(2).join("|")}\n`, player.id);
	 } else if (type === "VEHICLE_STATE" && parts.length === 32) {
	 const key=Number(parts[0]), modelId=Number(parts[1]), n=parts.slice(2).map(Number);
	 if (Number.isInteger(key) && key>0 && key<=0xFFFFFFFF && Number.isInteger(modelId) && modelId>=400 && modelId<=611 && n.every(Number.isFinite) && n.length===30 && n.every(v=>Math.abs(v)<1000000)) {
		 player.room.vehicles ??= new Map(); player.room.vehicleKeys ??= new Map();
		 const sourceKey=vehicleKeyFor(player.id,key), mapped=player.room.vehicleKeys.get(sourceKey);
		 if (mapped === 0) return;
		 let vehicle = mapped ? player.room.vehicles.get(mapped) : null;
		 if (mapped && !vehicle) player.room.vehicleKeys.delete(sourceKey);
		 if (vehicle && vehicle.ownerId !== player.id) return;
		 if (vehicle && vehicle.modelId !== modelId) { removeVehicle(player.room,vehicle,player.id); vehicle=null; }
		 if (!vehicle) {
			 const id=allocateVehicleId(player.room); if(!id) return;
			 vehicle={id,ownerId:player.id,localKey:key}; player.room.vehicleKeys.set(sourceKey,id);
			 player.room.vehicles.set(id,vehicle);
		 }
		 const clampNum=(v,lo,hi)=>Math.max(lo,Math.min(hi,v));
		 vehicle.modelId=modelId; vehicle.x=clampNum(n[0],-3000,3000); vehicle.y=clampNum(n[1],-3000,3000); vehicle.z=clampNum(n[2],-1000,10000); vehicle.heading=n[3]; vehicle.vx=clampNum(n[4],-100,100); vehicle.vy=clampNum(n[5],-100,100); vehicle.vz=clampNum(n[6],-100,100); vehicle.health=clampNum(n[7],0,1000); vehicle.driverSeat=Math.trunc(clampNum(n[8],0,7));
		 vehicle.primaryColor=Math.trunc(clampNum(n[9],0,255)); vehicle.secondaryColor=Math.trunc(clampNum(n[10],0,255)); vehicle.engineOn=n[11]?1:0; vehicle.lightsOn=n[12]?1:0; vehicle.sirenOn=n[13]?1:0; vehicle.doorLock=Math.trunc(clampNum(n[14],0,7)); vehicle.dirt=clampNum(n[15],0,15); vehicle.tyreStatus=Math.trunc(clampNum(n[16],0,255)); vehicle.doorStatus=Math.trunc(clampNum(n[17],0,0xFFFFFF)); vehicle.lightStatus=Math.trunc(clampNum(n[18],0,0xFFFFFFFF)); vehicle.panelStatus=Math.trunc(clampNum(n[19],0,0xFFFFFFFF)); vehicle.trailerId=Math.trunc(clampNum(n[20],0,65535));
		 [vehicle.rightX,vehicle.rightY,vehicle.rightZ,vehicle.upX,vehicle.upY,vehicle.upZ,vehicle.atX,vehicle.atY,vehicle.atZ]=n.slice(21,30).map(v=>clampNum(v,-2,2));
		 vehicle.lastSeen=Date.now();
		 if (!mapped) send(socket,`VEHICLE_ID|${key}|${vehicle.id}\n`);
		 broadcast(player.room,vehicleSnapshot(vehicle),player.id);
	 }
	 } else if (type === "VEHICLE_CLAIM" && parts.length === 3) {
	 const id=Number(parts[0]), seat=Number(parts[1]), key=Number(parts[2]);
	 const vehicle=player.room.vehicles?.get(id);
	 if(vehicle&&Number.isInteger(id)&&id>0&&Number.isInteger(seat)&&seat===0&&Number.isInteger(key)&&key>0&&key<=0xFFFFFFFF){
		 const newKey=vehicleKeyFor(player.id,key), existing=player.room.vehicleKeys.get(newKey);
		 if(existing && existing!==id) return;


         if (player.room.npcs) for (const [npcKey,n] of [...player.room.npcs.entries()]) {
           if (n.vehicleId === id && n.vehicleSeat === 0) {
             player.room.npcs.delete(npcKey);
             broadcast(player.room, `NPC_REMOVE|${n.id}\n`);
           }
         }
		 vehicle.ownerId=player.id; vehicle.localKey=key; vehicle.driverSeat=0; player.room.vehicleKeys.set(newKey,id); player.vehicleId=id; player.vehicleSeat=0;
		 send(socket,`VEHICLE_ID|${key}|${id}\n`); broadcast(player.room,vehicleSnapshot(vehicle),player.id);
	 }
	 } else if (type === "VEHICLE_ENTER" && parts.length === 2) {
	 const id=Number(parts[0]), seat=Number(parts[1]);
	 if(player.room.vehicles?.has(id)&&Number.isInteger(seat)&&seat>=0&&seat<=8){player.vehicleId=id;player.vehicleSeat=seat;}
	 } else if (type === "VEHICLE_REMOVE_KEY" && parts.length === 1) {
	 const key=Number(parts[0]), id=player.room.vehicleKeys?.get(vehicleKeyFor(player.id,key));
	 const vehicle=id&&player.room.vehicles?.get(id);
	 if(Number.isInteger(key)&&key>0&&vehicle&&vehicle.ownerId===player.id) removeVehicle(player.room,vehicle,player.id);
	 } else if (type === "NPC_STATE" && parts.length >= 10 && parts.length <= 14) {



 const ref=Number(parts[0]), modelId=Number(parts[1]); const vals=parts.slice(2,10).map(Number);
 const weaponId=Number(parts[8]), moveState=Number(parts[9]);
 const hasVehicle = parts.length >= 13;
 const vehicleKey = hasVehicle ? Number(parts[10]) : 0;
 const vehicleSeat = hasVehicle ? Number(parts[11]) : 0;
	 const specialName = (parts.length === 11 || parts.length === 13 || parts.length === 14) && modelId>=290&&modelId<=299
	 ? specialModelName(parts.length===11?parts[10]:parts[12]) : "";
 if(Number.isInteger(ref)&&ref>0&&Number.isInteger(modelId)&&modelId>=0&&modelId<=2000&&vals.length===8&&vals.every(Number.isFinite)&&Number.isInteger(weaponId)&&weaponId>=0&&weaponId<=0x3A&&Number.isInteger(moveState)&&moveState>=1&&moveState<=7){
 if(!player.room.npcs) player.room.npcs=new Map();
 const key=`${player.id}:${ref}`, incomingHealth=Math.max(0,Math.min(100,Math.trunc(vals[4]))), incomingArmour=Math.max(0,Math.min(100,Math.trunc(vals[5])));
	 const directVehicleId = parts.length >= 14 ? Number(parts[13]) : 0;
	 const npcVehicleId = Number.isInteger(directVehicleId) && directVehicleId > 0 && directVehicleId <= 65535 && player.room.vehicles?.has(directVehicleId) ? directVehicleId : (Number.isInteger(vehicleKey) && vehicleKey > 0 ? (player.room.vehicleKeys?.get(vehicleKeyFor(player.id, vehicleKey)) || 0) : 0);
 const npcVehicleSeat = Number.isFinite(vehicleSeat) ? Math.max(0, Math.min(8, Math.trunc(vehicleSeat))) : 0;


     if (npcVehicleId && npcVehicleSeat === 0 && player.room.vehicles?.get(npcVehicleId)?.ownerId !== player.id) {
       const stale = player.room.npcs.get(key);
       if (stale) {
         player.room.npcs.delete(key);
         broadcast(player.room, `NPC_REMOVE|${stale.id}\n`, player.id);
       }
       continue;
     }
 let n=player.room.npcs.get(key);
 if(!n){
 n={id:nextNpcId++,ownerId:player.id,localRef:ref,modelId,specialModelName:specialName,x:vals[0],y:vals[1],z:vals[2],heading:vals[3],health:incomingHealth,armour:incomingArmour,weapon:weaponId,moveState,vehicleId:npcVehicleId,vehicleSeat:npcVehicleSeat,healthReports:new Map()};
 player.room.npcs.set(key,n);
 } else {
 n.modelId=modelId;n.specialModelName=specialName;n.x=vals[0];n.y=vals[1];n.z=vals[2];n.heading=vals[3];
 n.weapon=weaponId;n.moveState=moveState;n.vehicleId=npcVehicleId;n.vehicleSeat=npcVehicleSeat;
 }
 n.healthReports.set(player.id,{health:incomingHealth,armour:incomingArmour});
 const vitalsChanged=reconcileNpcHealth(n);
 broadcast(player.room,npcSnapshot(n),player.id);
 if(vitalsChanged) broadcast(player.room,npcVitals(n));
 }
 } else if (type === "NPC_HEALTH" && parts.length === 3) {
 const id=Number(parts[0]), health=Number(parts[1]), armour=Number(parts[2]);
 const npcs=player.room.npcs;
 if(Number.isInteger(id)&&id>0&&Number.isInteger(health)&&health>=0&&health<=100&&Number.isInteger(armour)&&armour>=0&&armour<=100&&npcs){
 const npc=[...npcs.values()].find(n=>n.id===id);
 if(npc&&npc.ownerId!==player.id){
 npc.healthReports.set(player.id,{health,armour});
 if(reconcileNpcHealth(npc)){
 broadcast(player.room,npcSnapshot(npc),npc.ownerId);
 broadcast(player.room,npcVitals(npc));
 }
 }
 }
 } else if (type === "NPC_REMOVE" && parts.length >= 1) {
 const ref=Number(parts[0]); if(Number.isInteger(ref)&&ref>0&&player.room.npcs){const key=`${player.id}:${ref}`,n=player.room.npcs.get(key);if(n){player.room.npcs.delete(key);broadcast(player.room,`NPC_REMOVE|${n.id}\n`,player.id);}}
 } else if (type === "SHOT" && (parts.length === 7 || parts.length === 9)) {
 const w = weapon(parts[0]); const values = parts.slice(1, 7).map(Number); const fireAnim = parts.length === 9 ? fireAnimationFields(parts[7], parts[8]) : "";
 if (values.every(Number.isFinite)) {
 broadcast(player.room, `SHOT|${player.id}|${w}|${values.map(v => v.toFixed(3)).join("|")}${fireAnim}\n`, player.id);
 const hit = findHit(player, {x:values[0],y:values[1],z:values[2],dx:values[3],dy:values[4],dz:values[5]});
 if (hit?.kind === "player") {
 const target = hit.target, damage = damageFor(w);
 const armourDamage = Math.min(target.armour, Math.ceil(damage * 0.6));
 target.armour -= armourDamage;
 target.health = Math.max(0, target.health - (damage - armourDamage));
 target.healthReports.set(target.id, { health: target.health, armour: target.armour });
 send(target.socket, `DAMAGE|${target.id}|${target.health}|${target.armour}|${w}\n`);
 broadcast(player.room, playerVitals(target));
 console.log(`DAMAGE_HIT attacker=${player.id} target=${target.id} health=${target.health} armour=${target.armour}`);
 }
 }
 }
 }
 });

 socket.on("close", () => {
	 if (player && !player.removed) {
	 player.removed = true;
	 if (player.room.npcs) for (const [key,n] of player.room.npcs) if (n.ownerId===player.id) { broadcast(player.room, `NPC_REMOVE|${n.id}\n`, player.id); player.room.npcs.delete(key); }
	 if (player.room.vehicles) for (const vehicle of [...player.room.vehicles.values()]) if (vehicle.ownerId===player.id) removeVehicle(player.room, vehicle, player.id);
	 if (player.room.vehicleKeys) for (const key of [...player.room.vehicleKeys.keys()]) if (key.startsWith(`${player.id}:`)) player.room.vehicleKeys.delete(key);
 player.room.delete(player.id);
 announceTimeAuthority(player.room);
 playersById.delete(player.id);
 console.log(`PLAYER_LEAVE id=${player.id}`);
 broadcast(player.room, `REMOVE|${player.id}\n`);
 }
 });
 socket.on("error", () => socket.destroy());
});

server.on("error", error => console.error(`Server error: ${error.code || error.message}`));
process.on("uncaughtException", error => console.error(`Fatal server error: ${error.stack || error.message}`));
process.on("unhandledRejection", error => console.error(`Fatal server error: ${error}`));
const consoleInput = readline.createInterface({ input: process.stdin, output: process.stdout });
consoleInput.on("line", adminCommand);
console.log("Admin commands enabled: /help");
let lastVehicleBroadcast = 0;
setInterval(() => { const now=Date.now(); for (const room of rooms.values()) { for (const player of room.values()) for (const other of room.values()) if (other.id !== player.id) send(other.socket, snapshot(player)); if (room.npcs) for (const n of room.npcs.values()) for (const other of room.values()) if (other.id !== n.ownerId) send(other.socket, npcSnapshot(n)); if (now-lastVehicleBroadcast>=50 && room.vehicles) for (const v of room.vehicles.values()) for (const other of room.values()) if (other.id !== v.ownerId) send(other.socket,vehicleSnapshot(v)); } if(now-lastVehicleBroadcast>=50) lastVehicleBroadcast=now; }, 15);
server.listen(port, "0.0.0.0", () => console.log(`CoopFusion32 server listening on ${port}`));
