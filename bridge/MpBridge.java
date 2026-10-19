// MP lobby bridge - joins the server lists of all regional HiveMQ brokers into one.
//
// Every region (Asia, Europe, NA, Oceania) runs its own HiveMQ broker so players get a low ping.
// Game traffic stays on the broker of the server's region. Only the small server-list messages
// ("thirtyfox/mp4/l/<token>", one every 3 s per hosted server) are copied between the brokers,
// so a player in Asia still sees the servers hosted in Europe and the mod moves to the Europe
// broker when he joins one of them.
//
// Run ONE bridge for the whole network (on any of the servers):
//     java MpBridge.java /opt/mp/bridge.conf
// No libraries needed: Java 21 runs this single file directly. MQTT 3.1.1, TCP or TLS.
//
// bridge.conf (one broker per line, # comments):
//     Asia    = mqtt://bridge:PASSWORD@127.0.0.1:1883
//     Europe  = mqtts://bridge:PASSWORD@eu.example.com:8883
//     topic   = thirtyfox/mp4/l/+          (optional, default shown)
//     expire  = 45                          (seconds without update -> listing removed everywhere)
//
// Loop protection: the bridge remembers every message it copied to a broker and drops the copy
// when that broker sends it back (the bridge is subscribed there too). Retained flag is kept.

import javax.net.ssl.SNIHostName;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.*;
import java.util.concurrent.*;

public class MpBridge {
    static String topicFilter = "thirtyfox/mp4/l/+";
    static long expireMs = 45_000;
    static final List<Link> links = new ArrayList<>();
    // token topic -> last state (payload + when a LIVE update was last seen anywhere)
    static final Map<String, Listing> listings = new ConcurrentHashMap<>();

    record Listing(byte[] payload, long lastLive, String origin) {}

    static void log(String s) {
        System.out.println(LocalDateTime.now().format(DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss")) + " " + s);
        System.out.flush();
    }

    public static void main(String[] args) throws Exception {
        String path = args.length > 0 ? args[0] : "bridge.conf";
        for (String raw : Files.readAllLines(Path.of(path))) {
            String l = raw.strip();
            int hash = l.indexOf(" #");
            if (hash >= 0) l = l.substring(0, hash).strip();
            if (l.isEmpty() || l.startsWith("#") || !l.contains("=")) continue;
            String k = l.substring(0, l.indexOf('=')).strip(), v = l.substring(l.indexOf('=') + 1).strip();
            if (k.equalsIgnoreCase("topic")) topicFilter = v;
            else if (k.equalsIgnoreCase("expire")) expireMs = Long.parseLong(v) * 1000L;
            else links.add(new Link(k, URI.create(v)));
        }
        if (links.size() < 2) { log("bridge.conf needs at least 2 brokers (found " + links.size() + ")"); System.exit(2); }
        log("MP bridge: " + links.size() + " brokers, topic " + topicFilter + ", expire " + expireMs / 1000 + " s");
        for (Link k : links) { Thread t = new Thread(k::runForever, "link-" + k.name); t.setDaemon(true); t.start(); }
        // expire listings of hosts that vanished while a broker or the bridge was down
        for (;;) {
            Thread.sleep(5000);
            long now = System.currentTimeMillis();
            for (var e : listings.entrySet()) {
                Listing li = e.getValue();
                if (li.payload.length > 0 && now - li.lastLive > expireMs) {
                    listings.remove(e.getKey());
                    log("expired " + e.getKey() + " (from " + li.origin + ")");
                    for (Link k : links) k.forward(e.getKey(), new byte[0]);
                }
            }
            StringBuilder sb = new StringBuilder();
            for (Link k : links) sb.append(k.name).append(k.connected ? " up" : " DOWN").append("  ");
            if (now / 1000 % 300 < 5) log("status: " + sb + "listings " + listings.size());
        }
    }

    // A message arrived from one broker: copy it to all the others.
    static void onMessage(Link from, String topic, byte[] payload, boolean retained) {
        long now = System.currentTimeMillis();
        if (payload.length == 0) listings.remove(topic);
        else {
            Listing old = listings.get(topic);
            // retained copies (sent on subscribe) may be old: they don't count as "alive"
            long live = retained ? (old != null ? old.lastLive : now) : now;
            listings.put(topic, new Listing(payload, live, from.name));
        }
        for (Link k : links) if (k != from) k.forward(topic, payload);
    }

    static final class Link {
        final String name; final URI uri;
        volatile boolean connected;
        private volatile OutputStream out;
        private final Object wlock = new Object();
        // copies we sent to this broker, waiting to come back (key -> expiry times)
        private final Map<String, ArrayDeque<Long>> echoes = new HashMap<>();

        Link(String name, URI uri) { this.name = name; this.uri = uri; }

        static String key(String topic, byte[] p) { return topic + '\0' + new String(p, StandardCharsets.ISO_8859_1); }

        void forward(String topic, byte[] payload) {
            if (!connected) return;
            String k = key(topic, payload);
            synchronized (echoes) {
                echoes.computeIfAbsent(k, x -> new ArrayDeque<>()).add(System.currentTimeMillis() + 10_000);
            }
            try { send(publishPacket(topic, payload, true)); }
            catch (IOException e) { log(name + ": send failed: " + e.getMessage()); close(); }
        }

        boolean isEcho(String topic, byte[] payload) {
            long now = System.currentTimeMillis();
            synchronized (echoes) {
                if (echoes.size() > 5000) echoes.clear();
                ArrayDeque<Long> q = echoes.get(key(topic, payload));
                if (q == null) return false;
                while (!q.isEmpty() && q.peekFirst() < now) q.pollFirst();
                boolean hit = !q.isEmpty();
                if (hit) q.pollFirst();
                if (q.isEmpty()) echoes.remove(key(topic, payload));
                return hit;
            }
        }

        private Socket sock;
        void close() { connected = false; try { if (sock != null) sock.close(); } catch (IOException ignored) {} }

        void send(byte[] pkt) throws IOException {
            synchronized (wlock) { OutputStream o = out; if (o == null) throw new IOException("not connected"); o.write(pkt); o.flush(); }
        }

        void runForever() {
            long backoff = 1000;
            for (;;) {
                long t0 = System.currentTimeMillis();
                try { session(); }
                catch (Exception e) { log(name + ": " + e.getClass().getSimpleName() + ": " + e.getMessage()); }
                close();
                if (System.currentTimeMillis() - t0 > 60_000) backoff = 1000;
                try { Thread.sleep(backoff); } catch (InterruptedException ignored) {}
                backoff = Math.min(backoff * 2, 30_000);
            }
        }

        void session() throws IOException {
            String scheme = uri.getScheme() == null ? "mqtt" : uri.getScheme().toLowerCase();
            boolean tls = scheme.equals("mqtts") || scheme.equals("ssl") || scheme.equals("tls");
            String host = uri.getHost();
            int port = uri.getPort() > 0 ? uri.getPort() : (tls ? 8883 : 1883);
            String user = null, pass = null;
            if (uri.getRawUserInfo() != null) {
                String ui = uri.getRawUserInfo();
                int c = ui.indexOf(':');
                user = URLDecoder.decode(c >= 0 ? ui.substring(0, c) : ui, StandardCharsets.UTF_8);
                if (c >= 0) pass = URLDecoder.decode(ui.substring(c + 1), StandardCharsets.UTF_8);
            }
            Socket s = new Socket();
            s.connect(new InetSocketAddress(host, port), 8000);
            s.setTcpNoDelay(true);
            if (tls) {
                SSLSocket ss = (SSLSocket) ((SSLSocketFactory) SSLSocketFactory.getDefault()).createSocket(s, host, port, true);
                SSLParameters p = ss.getSSLParameters();
                p.setEndpointIdentificationAlgorithm("HTTPS");   // check the certificate name
                p.setServerNames(List.of(new SNIHostName(host)));
                ss.setSSLParameters(p);
                ss.startHandshake();
                s = ss;
            }
            sock = s;
            s.setSoTimeout(45_000);
            DataInputStream in = new DataInputStream(new BufferedInputStream(s.getInputStream()));
            out = new BufferedOutputStream(s.getOutputStream());
            String cid = "mpbridge-" + name.toLowerCase().replaceAll("[^a-z0-9]", "") + "-" + Integer.toHexString(new Random().nextInt() & 0xFFFFFF);
            send(connectPacket(cid, user, pass, 30));
            int[] type = new int[1];
            byte[] body = readPacket(in, type);
            if ((type[0] >> 4) != 2 || body.length < 2 || body[1] != 0)
                throw new IOException("broker refused the login (CONNACK " + (body.length >= 2 ? body[1] : -1) + ")");
            send(subscribePacket(1, topicFilter));
            connected = true;
            log(name + ": connected to " + host + ":" + port + (tls ? " (TLS)" : ""));
            // keepalive
            ScheduledExecutorService ka = Executors.newSingleThreadScheduledExecutor(r -> { Thread t = new Thread(r); t.setDaemon(true); return t; });
            ka.scheduleAtFixedRate(() -> { try { send(new byte[] { (byte) 0xC0, 0 }); } catch (IOException e) { close(); } }, 20, 20, TimeUnit.SECONDS);
            try {
                for (;;) {
                    body = readPacket(in, type);
                    int kind = type[0] >> 4;
                    if (kind == 9) {   // SUBACK
                        if (body.length >= 3 && (body[2] & 0xFF) == 0x80) throw new IOException("subscribe to " + topicFilter + " refused (check the ACL)");
                        continue;
                    }
                    if (kind != 3) continue;   // only PUBLISH matters
                    boolean retained = (type[0] & 1) != 0;
                    int qos = (type[0] >> 1) & 3;
                    int tl = ((body[0] & 0xFF) << 8) | (body[1] & 0xFF);
                    String topic = new String(body, 2, tl, StandardCharsets.UTF_8);
                    int off = 2 + tl + (qos > 0 ? 2 : 0);
                    byte[] payload = Arrays.copyOfRange(body, off, body.length);
                    if (qos == 1) send(new byte[] { 0x40, 2, body[2 + tl], body[3 + tl] });   // PUBACK
                    if (isEcho(topic, payload)) continue;
                    onMessage(this, topic, payload, retained);
                }
            } finally { ka.shutdownNow(); connected = false; out = null; }
        }
    }

    // ---- MQTT 3.1.1 packets ----
    static void str(ByteArrayOutputStream b, String s) { byte[] d = s.getBytes(StandardCharsets.UTF_8); b.write(d.length >> 8); b.write(d.length & 0xFF); b.writeBytes(d); }
    static byte[] packet(int type, byte[] v) {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        b.write(type);
        int n = v.length;
        do { int d = n % 128; n /= 128; if (n > 0) d |= 0x80; b.write(d); } while (n > 0);
        b.writeBytes(v);
        return b.toByteArray();
    }
    static byte[] connectPacket(String cid, String user, String pass, int keepAlive) {
        ByteArrayOutputStream v = new ByteArrayOutputStream();
        str(v, "MQTT"); v.write(4);
        int flags = 0x02;
        if (user != null) flags |= 0x80;
        if (user != null && pass != null) flags |= 0x40;
        v.write(flags); v.write(keepAlive >> 8); v.write(keepAlive & 0xFF);
        str(v, cid);
        if (user != null) str(v, user);
        if (user != null && pass != null) str(v, pass);
        return packet(0x10, v.toByteArray());
    }
    static byte[] subscribePacket(int id, String filter) {
        ByteArrayOutputStream v = new ByteArrayOutputStream();
        v.write(id >> 8); v.write(id & 0xFF); str(v, filter); v.write(0);
        return packet(0x82, v.toByteArray());
    }
    static byte[] publishPacket(String topic, byte[] payload, boolean retain) {
        ByteArrayOutputStream v = new ByteArrayOutputStream();
        str(v, topic); v.writeBytes(payload);
        return packet(0x30 | (retain ? 1 : 0), v.toByteArray());
    }
    static byte[] readPacket(DataInputStream in, int[] type) throws IOException {
        type[0] = in.readUnsignedByte();
        int len = 0, mul = 1, d;
        do { d = in.readUnsignedByte(); len += (d & 0x7F) * mul; mul *= 128; if (mul > 128 * 128 * 128 * 128) throw new IOException("bad length"); } while ((d & 0x80) != 0);
        if (len > 1 << 20) throw new IOException("packet too big");
        byte[] b = new byte[len];
        in.readFully(b);
        return b;
    }
}
