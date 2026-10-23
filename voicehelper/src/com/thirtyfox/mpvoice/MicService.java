package com.thirtyfox.mpvoice;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.media.audiofx.AcousticEchoCanceler;
import android.media.audiofx.AutomaticGainControl;
import android.media.audiofx.NoiseSuppressor;
import android.os.Build;
import android.os.IBinder;
import android.os.SystemClock;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketTimeoutException;

// Foreground service (type microphone). The game sends "MPVH" + 1 to 127.0.0.1:19228 every
// second while its mic is on; while it does, this records 48 kHz mono (voice-call preset: echo
// cancel + noise suppression) and sends every 20 ms frame straight back:
//   "MPVF" + seq (u16 LE) + rate (u16 LE, /10) + 960 x int16 LE
// No hello for 3 s (or "MPVH" + 0) = the recorder is released (Android's mic dot goes off).
public class MicService extends Service {
    static final int PORT = 19228, RATE = 48000, FRAME = 960;
    static volatile boolean running = false, sending = false;
    private volatile boolean quit = false;
    private Thread net;
    private volatile AudioRecord rec;
    private volatile InetSocketAddress client;
    private volatile long lastHello = 0;
    private DatagramSocket sock;

    @Override public IBinder onBind(Intent i) { return null; }

    @Override public int onStartCommand(Intent intent, int flags, int id) {
        if (intent != null && "stop".equals(intent.getAction())) { stopSelf(); return START_NOT_STICKY; }
        Notification n = notification();
        if (Build.VERSION.SDK_INT >= 29) startForeground(1, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE);
        else startForeground(1, n);
        if (!running) { running = true; quit = false; net = new Thread(this::netLoop, "mpvoice-net"); net.start(); }
        return START_STICKY;
    }

    private Notification notification() {
        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationChannel ch = new NotificationChannel("mic", "Microphone", NotificationManager.IMPORTANCE_LOW);
            ((NotificationManager) getSystemService(NOTIFICATION_SERVICE)).createNotificationChannel(ch);
            b = new Notification.Builder(this, "mic");
        } else b = new Notification.Builder(this);
        int pf = Build.VERSION.SDK_INT >= 23 ? PendingIntent.FLAG_IMMUTABLE : 0;
        PendingIntent stop = PendingIntent.getService(this, 2, new Intent(this, MicService.class).setAction("stop"), pf);
        PendingIntent open = PendingIntent.getActivity(this, 3, new Intent(this, MainActivity.class), pf);
        return b.setContentTitle("MP Voice microphone is on")
                .setContentText("GTA SA voice chat can use your microphone")
                .setSmallIcon(android.R.drawable.ic_btn_speak_now)
                .setContentIntent(open)
                .addAction(android.R.drawable.ic_menu_close_clear_cancel, "Stop", stop)
                .setOngoing(true).build();
    }

    private void netLoop() {
        try {
            sock = new DatagramSocket(null);
            sock.setReuseAddress(true);
            sock.bind(new InetSocketAddress(InetAddress.getByName("127.0.0.1"), PORT));
            sock.setSoTimeout(250);
        } catch (Exception e) { running = false; stopSelf(); return; }
        byte[] buf = new byte[64];
        while (!quit) {
            try {
                DatagramPacket p = new DatagramPacket(buf, buf.length);
                sock.receive(p);
                if (p.getLength() >= 5 && buf[0] == 'M' && buf[1] == 'P' && buf[2] == 'V' && buf[3] == 'H') {
                    client = new InetSocketAddress(p.getAddress(), p.getPort());
                    if (buf[4] == 1) { lastHello = SystemClock.elapsedRealtime(); startRec(); }
                    else { lastHello = 0; stopRec(); }
                }
            } catch (SocketTimeoutException t) {
                // no packet
            } catch (Exception e) {
                if (quit) break;
            }
            if (rec != null && SystemClock.elapsedRealtime() - lastHello > 3000) stopRec();
        }
        stopRec();
        try { sock.close(); } catch (Exception e) {}
    }

    private synchronized void startRec() {
        if (rec != null) return;
        int min = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
        AudioRecord r;
        try {
            r = new AudioRecord(MediaRecorder.AudioSource.VOICE_COMMUNICATION, RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, Math.max(min, FRAME * 2 * 4));
            if (r.getState() != AudioRecord.STATE_INITIALIZED) { r.release(); return; }
            int sid = r.getAudioSessionId();
            if (AcousticEchoCanceler.isAvailable()) { AcousticEchoCanceler a = AcousticEchoCanceler.create(sid); if (a != null) a.setEnabled(true); }
            if (NoiseSuppressor.isAvailable()) { NoiseSuppressor a = NoiseSuppressor.create(sid); if (a != null) a.setEnabled(true); }
            if (AutomaticGainControl.isAvailable()) { AutomaticGainControl a = AutomaticGainControl.create(sid); if (a != null) a.setEnabled(true); }
            r.startRecording();
        } catch (Exception e) { return; }
        rec = r;
        final AudioRecord rr = r;
        new Thread(() -> readLoop(rr), "mpvoice-rec").start();
    }

    private synchronized void stopRec() {
        AudioRecord r = rec; rec = null; sending = false;
        if (r != null) { try { r.stop(); } catch (Exception e) {} r.release(); }
    }

    private void readLoop(AudioRecord r) {
        short[] pcm = new short[FRAME];
        byte[] out = new byte[8 + FRAME * 2];
        out[0] = 'M'; out[1] = 'P'; out[2] = 'V'; out[3] = 'F';
        int seq = 0;
        while (rec == r && !quit) {
            int got = 0;
            while (got < FRAME) { int n = r.read(pcm, got, FRAME - got); if (n <= 0) break; got += n; }
            if (got < FRAME || rec != r) break;
            InetSocketAddress c = client;
            if (c == null) continue;
            out[4] = (byte) seq; out[5] = (byte) (seq >> 8); seq = (seq + 1) & 0xFFFF;
            out[6] = (byte) (RATE / 10); out[7] = (byte) ((RATE / 10) >> 8);
            for (int i = 0; i < FRAME; ++i) { out[8 + i * 2] = (byte) pcm[i]; out[9 + i * 2] = (byte) (pcm[i] >> 8); }
            try { sock.send(new DatagramPacket(out, out.length, c)); sending = true; } catch (Exception e) {}
        }
    }

    @Override public void onDestroy() {
        quit = true; running = false; sending = false;
        stopRec();
        try { if (sock != null) sock.close(); } catch (Exception e) {}
        super.onDestroy();
    }
}
