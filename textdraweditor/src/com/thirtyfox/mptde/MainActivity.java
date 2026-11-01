package com.thirtyfox.mptde;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.StrictMode;
import android.provider.MediaStore;
import android.view.View;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.Collections;

// MP Textdraw Editor (3.50): a full-screen landscape WebView with the editor in assets/www.
// The page talks to the MP account bot itself (MQTT over WebSocket, js/mpauth.js); this class only
// gives it files: projects in the app's own folder, a copy of every save (and every Pawn export) in
// Download/MP Textdraws so MP Launcher > Add server > Plugins > Import file can pick it up.
public class MainActivity extends Activity {
    static final String DIR = "MP Textdraws";
    static final Charset UTF8 = Charset.forName("UTF-8");
    static final int PICK = 7;
    WebView web;
    ValueCallback<Uri[]> pick;
    String pending, pendingName;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        // file:// share Uris on Android 5-8 (Android 10+ uses MediaStore content:// Uris)
        StrictMode.setVmPolicy(new StrictMode.VmPolicy.Builder().build());
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        web = new WebView(this);
        web.setBackgroundColor(Color.rgb(11, 15, 20));
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setAllowFileAccess(true);
        s.setAllowFileAccessFromFileURLs(true);
        s.setMediaPlaybackRequiresUserGesture(true);
        web.addJavascriptInterface(new Bridge(), "MPTDE");
        web.setWebViewClient(new WebViewClient() {
            @Override public boolean shouldOverrideUrlLoading(WebView v, String url) {
                if (url.startsWith("file:///android_asset/")) return false;
                openUrl(url); return true;
            }
            @Override public void onPageFinished(WebView v, String url) {
                if (pending != null && url.contains("editor.html")) { deliver(pending, pendingName); pending = null; }
            }
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override public boolean onShowFileChooser(WebView v, ValueCallback<Uri[]> cb, FileChooserParams p) {
                if (pick != null) pick.onReceiveValue(null);
                pick = cb;
                Intent i = new Intent(Intent.ACTION_GET_CONTENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                try { startActivityForResult(Intent.createChooser(i, "Import a textdraw file"), PICK); }
                catch (Exception e) { pick = null; cb.onReceiveValue(null); return false; }
                return true;
            }
        });
        setContentView(web);
        immersive();
        web.loadUrl("file:///android_asset/www/index.html");
        handle(getIntent());
    }

    void immersive() {
        if (Build.VERSION.SDK_INT >= 19)
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }
    @Override public void onWindowFocusChanged(boolean f) { super.onWindowFocusChanged(f); if (f) immersive(); }
    @Override protected void onNewIntent(Intent i) { super.onNewIntent(i); handle(i); }

    // a .mptd / .pwn opened from a file manager or shared to the app
    void handle(Intent i) {
        if (i == null) return;
        Uri u = null;
        if (Intent.ACTION_VIEW.equals(i.getAction())) u = i.getData();
        else if (Intent.ACTION_SEND.equals(i.getAction())) u = i.getParcelableExtra(Intent.EXTRA_STREAM);
        if (u == null) return;
        try {
            String txt = new String(read(getContentResolver().openInputStream(u)), UTF8);
            String name = u.getLastPathSegment(); if (name == null) name = "file";
            name = name.replaceAll(".*/", "").replaceAll("\\.[^.]*$", "");
            pending = txt; pendingName = name;
            web.loadUrl("file:///android_asset/www/editor.html?guest=1");
        } catch (Exception e) { toast("Cannot open the file: " + e.getMessage()); }
    }
    void deliver(String txt, String name) {
        web.evaluateJavascript("window.MPTDE_import&&MPTDE_import(" + js(txt) + "," + js(name) + ")", null);
    }
    static String js(String s) {
        StringBuilder o = new StringBuilder("\"");
        for (char c : s.toCharArray()) {
            if (c == '"' || c == '\\') o.append('\\').append(c);
            else if (c == '\n') o.append("\\n"); else if (c == '\r') o.append("\\r");
            else if (c < 32 || c == 0x2028 || c == 0x2029) o.append(String.format("\\u%04x", (int) c));
            else o.append(c);
        }
        return o.append('"').toString();
    }

    @Override protected void onActivityResult(int req, int res, Intent data) {
        if (req == PICK && pick != null) {
            Uri[] r = null;
            if (res == RESULT_OK && data != null && data.getData() != null) r = new Uri[] { data.getData() };
            pick.onReceiveValue(r); pick = null;
            return;
        }
        super.onActivityResult(req, res, data);
    }

    @Override public void onBackPressed() {
        web.evaluateJavascript("window.MPTDE_back?MPTDE_back():false", new ValueCallback<String>() {
            public void onReceiveValue(String v) {
                if ("true".equals(v)) return;
                if (web.canGoBack()) web.goBack(); else finish();
            }
        });
    }

    void openUrl(String url) {
        try { startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url))); } catch (Exception e) { toast("No app can open " + url); }
    }
    void toast(final String m) { runOnUiThread(new Runnable() { public void run() { Toast.makeText(MainActivity.this, m, Toast.LENGTH_LONG).show(); } }); }

    static byte[] read(InputStream in) throws Exception {
        try { ByteArrayOutputStream o = new ByteArrayOutputStream(); byte[] b = new byte[16384]; int n;
              while ((n = in.read(b)) > 0) { o.write(b, 0, n); if (o.size() > 8 << 20) break; } return o.toByteArray(); }
        finally { in.close(); }
    }
    File projects() { File d = new File(getFilesDir(), "projects"); d.mkdirs(); return d; }
    static String safe(String n) {
        n = n == null ? "" : n.replaceAll("[^A-Za-z0-9 _.\\-()]", "_").replaceAll("^\\.+", "");
        if (n.length() > 64) n = n.substring(0, 64);
        return n.isEmpty() ? "Untitled" : n;
    }

    // a copy in the public Download/MP Textdraws folder; returns where it went
    String publish(String file, byte[] data) {
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                ContentResolver cr = getContentResolver();
                Uri col = MediaStore.Downloads.EXTERNAL_CONTENT_URI;
                String rel = Environment.DIRECTORY_DOWNLOADS + "/" + DIR + "/";
                Uri u = null;
                Cursor c = cr.query(col, new String[] { MediaStore.MediaColumns._ID },
                    MediaStore.MediaColumns.DISPLAY_NAME + "=? AND " + MediaStore.MediaColumns.RELATIVE_PATH + "=?", new String[] { file, rel }, null);
                if (c != null) { if (c.moveToFirst()) u = Uri.withAppendedPath(col, String.valueOf(c.getLong(0))); c.close(); }
                if (u == null) {
                    ContentValues v = new ContentValues();
                    v.put(MediaStore.MediaColumns.DISPLAY_NAME, file);
                    v.put(MediaStore.MediaColumns.MIME_TYPE, "application/octet-stream");
                    v.put(MediaStore.MediaColumns.RELATIVE_PATH, rel);
                    u = cr.insert(col, v);
                }
                if (u == null) return "";
                OutputStream o = cr.openOutputStream(u, "wt");
                try { o.write(data); } finally { o.close(); }
                return "Download/" + DIR + "/" + file;
            }
            File d = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), DIR);
            d.mkdirs();
            FileOutputStream o = new FileOutputStream(new File(d, file));
            try { o.write(data); } finally { o.close(); }
            return "Download/" + DIR + "/" + file;
        } catch (Exception e) { return ""; }
    }
    Uri publicUri(String file) {
        if (Build.VERSION.SDK_INT >= 29) {
            Uri col = MediaStore.Downloads.EXTERNAL_CONTENT_URI;
            Cursor c = getContentResolver().query(col, new String[] { MediaStore.MediaColumns._ID },
                MediaStore.MediaColumns.DISPLAY_NAME + "=? AND " + MediaStore.MediaColumns.RELATIVE_PATH + "=?",
                new String[] { file, Environment.DIRECTORY_DOWNLOADS + "/" + DIR + "/" }, null);
            Uri u = null;
            if (c != null) { if (c.moveToFirst()) u = Uri.withAppendedPath(col, String.valueOf(c.getLong(0))); c.close(); }
            return u;
        }
        File f = new File(new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), DIR), file);
        return f.exists() ? Uri.fromFile(f) : null;
    }

    class Bridge {
        @JavascriptInterface public String listProjects() {
            ArrayList<String> l = new ArrayList<String>();
            File[] fs = projects().listFiles();
            if (fs != null) for (File f : fs) if (f.getName().endsWith(".mptd")) l.add(f.getName().substring(0, f.getName().length() - 5));
            Collections.sort(l, String.CASE_INSENSITIVE_ORDER);
            StringBuilder o = new StringBuilder("[");
            for (int i = 0; i < l.size(); i++) { if (i > 0) o.append(','); o.append(js(l.get(i))); }
            return o.append(']').toString();
        }
        @JavascriptInterface public String loadProject(String name) {
            try { return new String(read(new FileInputStream(new File(projects(), safe(name) + ".mptd"))), UTF8); } catch (Exception e) { return ""; }
        }
        @JavascriptInterface public String saveProject(String name, String json) {
            byte[] d = json.getBytes(UTF8);
            String f = safe(name) + ".mptd";
            try { FileOutputStream o = new FileOutputStream(new File(projects(), f)); try { o.write(d); } finally { o.close(); } }
            catch (Exception e) { toast("Save failed: " + e.getMessage()); return ""; }
            String p = publish(f, d);
            return p.isEmpty() ? f : p;
        }
        @JavascriptInterface public void deleteProject(String name) { new File(projects(), safe(name) + ".mptd").delete(); }
        @JavascriptInterface public String exportFile(String file, String text) {
            String p = publish(safe(file), text.getBytes(UTF8));
            if (p.isEmpty()) { toast("Export failed"); return ""; }
            return p;
        }
        @JavascriptInterface public void share(String name) {
            Uri u = publicUri(safe(name) + ".mptd");
            if (u == null) { toast("Save the project first"); return; }
            Intent i = new Intent(Intent.ACTION_SEND);
            i.setType("application/octet-stream");
            i.putExtra(Intent.EXTRA_STREAM, u);
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            try { startActivity(Intent.createChooser(i, "Share " + safe(name) + ".mptd")); } catch (Exception e) { toast("Nothing can share it"); }
        }
        @JavascriptInterface public void openUrl(String url) { MainActivity.this.openUrl(url); }
    }
}
