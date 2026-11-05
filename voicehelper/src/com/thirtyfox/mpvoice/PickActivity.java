package com.thirtyfox.mpvoice;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Bundle;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.Socket;

// 4.04: MP Launcher > + Add server > Upload preview image. The game can't get an answer from the Files
// app, so it opens this (no screen of its own): Files app (JPG / PNG, one file) -> 512x256 JPEG ->
// 127.0.0.1:<port> as "MPIM" + uint32 length (little endian) + bytes, then back to the game.
public class PickActivity extends Activity {
    private int port = 19231;
    private int mode = 0;   // 4.07: 1 = server title logo (keeps the shape + transparency, PNG)

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        if (getIntent() != null) { port = getIntent().getIntExtra("port", 19231); mode = getIntent().getIntExtra("mode", 0); }
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("image/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[]{ "image/jpeg", "image/png" });
        i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, false);
        try { startActivityForResult(i, 7); }
        catch (Exception e) {
            Intent g = new Intent(Intent.ACTION_GET_CONTENT); g.setType("image/*"); g.addCategory(Intent.CATEGORY_OPENABLE);
            try { startActivityForResult(g, 7); } catch (Exception e2) { send(null); finish(); }
        }
    }

    @Override protected void onActivityResult(int req, int res, Intent data) {
        final Uri u = (res == RESULT_OK && data != null) ? data.getData() : null;
        new Thread(() -> { send(u == null ? null : (mode == 1 ? toLogo(u) : toJpeg(u))); runOnUiThread(this::finish); }).start();
    }

    private byte[] toJpeg(Uri u) {
        try (InputStream in = getContentResolver().openInputStream(u)) {
            BitmapFactory.Options o = new BitmapFactory.Options(); o.inJustDecodeBounds = true;
            BitmapFactory.decodeStream(in, null, o);
            int s = 1; while (o.outWidth / (s * 2) >= 512 && o.outHeight / (s * 2) >= 256) s *= 2;
            BitmapFactory.Options d = new BitmapFactory.Options(); d.inSampleSize = s;
            Bitmap src;
            try (InputStream in2 = getContentResolver().openInputStream(u)) { src = BitmapFactory.decodeStream(in2, null, d); }
            if (src == null) return null;
            int w = src.getWidth(), h = src.getHeight(), cw = w, ch = w / 2;   // centre crop to 2:1
            if (ch > h) { ch = h; cw = h * 2; }
            Rect from = new Rect((w - cw) / 2, (h - ch) / 2, (w - cw) / 2 + cw, (h - ch) / 2 + ch);
            Bitmap out = Bitmap.createBitmap(512, 256, Bitmap.Config.ARGB_8888);
            new Canvas(out).drawBitmap(src, from, new Rect(0, 0, 512, 256), new Paint(Paint.FILTER_BITMAP_FLAG));
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            out.compress(Bitmap.CompressFormat.JPEG, 82, bo);
            return bo.toByteArray();
        } catch (Exception e) { return null; }
    }

    // 4.07: title logo: the whole picture (no crop), at most 1024x256, PNG with its transparency; still too big
    // for the lobby (200 KB) -> smaller until it fits
    private byte[] toLogo(Uri u) {
        try {
            BitmapFactory.Options o = new BitmapFactory.Options(); o.inJustDecodeBounds = true;
            try (InputStream in = getContentResolver().openInputStream(u)) { BitmapFactory.decodeStream(in, null, o); }
            int s = 1; while (o.outWidth / (s * 2) >= 1024 || o.outHeight / (s * 2) >= 256) s *= 2;
            BitmapFactory.Options d = new BitmapFactory.Options(); d.inSampleSize = s; d.inPreferredConfig = Bitmap.Config.ARGB_8888;
            Bitmap src;
            try (InputStream in2 = getContentResolver().openInputStream(u)) { src = BitmapFactory.decodeStream(in2, null, d); }
            if (src == null) return null;
            float maxW = 1024, maxH = 256;
            for (int tries = 0; tries < 5; ++tries) {
                float k = Math.min(1f, Math.min(maxW / src.getWidth(), maxH / src.getHeight()));
                int w = Math.max(1, Math.round(src.getWidth() * k)), h = Math.max(1, Math.round(src.getHeight() * k));
                Bitmap out = Bitmap.createScaledBitmap(src, w, h, true);
                ByteArrayOutputStream bo = new ByteArrayOutputStream();
                out.compress(Bitmap.CompressFormat.PNG, 100, bo);
                if (bo.size() <= 200 * 1024) return bo.toByteArray();
                maxW *= 0.75f; maxH *= 0.75f;
            }
            return null;
        } catch (Exception e) { return null; }
    }

    private void send(byte[] jpg) {
        try (Socket s = new Socket(InetAddress.getByName("127.0.0.1"), port)) {
            OutputStream o = s.getOutputStream();
            int n = jpg == null ? 0 : jpg.length;
            o.write(new byte[]{ 'M', 'P', 'I', 'M', (byte) n, (byte) (n >> 8), (byte) (n >> 16), (byte) (n >> 24) });
            if (n > 0) o.write(jpg);
            o.flush();
        } catch (Exception ignored) { }
    }
}
