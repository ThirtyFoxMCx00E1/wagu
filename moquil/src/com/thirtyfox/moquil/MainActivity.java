package com.thirtyfox.moquil;

import android.Manifest;
import android.app.Activity;
import android.app.DownloadManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.view.KeyEvent;
import android.webkit.JavascriptInterface;
import android.webkit.PermissionRequest;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

// Moquil 1.0: a WebView with the Moquil app in assets/www (the same files as the website).
// The page does all networking itself (MQTT over WebSocket to broker.hivemq.com, WebRTC voice);
// this class gives it Android permissions (with an Allow / Don't allow / Never ask popup drawn by
// the page), downloads to Download/Moquil, notifications, links and the back button.
public class MainActivity extends Activity {
    static final int PICK = 7, PERM = 9;
    WebView web;
    ValueCallback<Uri[]> pick;
    String permAsked;
    PermissionRequest webPerm;
    SharedPreferences prefs;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        prefs = getSharedPreferences("moquil", MODE_PRIVATE);
        getWindow().setStatusBarColor(Color.rgb(11, 13, 17));
        getWindow().setNavigationBarColor(Color.rgb(11, 13, 17));
        web = new WebView(this);
        web.setBackgroundColor(Color.rgb(11, 13, 17));
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setDatabaseEnabled(true);
        s.setAllowFileAccess(true);
        s.setAllowFileAccessFromFileURLs(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setUserAgentString(s.getUserAgentString() + " Moquil/1.0");
        web.addJavascriptInterface(new Bridge(), "Moquil");
        web.setWebViewClient(new WebViewClient() {
            @Override public boolean shouldOverrideUrlLoading(WebView v, String url) {
                if (url.startsWith("file:///android_asset/")) return false;
                openLink(url); return true;
            }
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override public boolean onShowFileChooser(WebView v, ValueCallback<Uri[]> cb, FileChooserParams p) {
                if (pick != null) pick.onReceiveValue(null);
                pick = cb;
                Intent i = new Intent(Intent.ACTION_GET_CONTENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("image/*");
                try { startActivityForResult(Intent.createChooser(i, "Choose an image"), PICK); }
                catch (Exception e) { pick = null; cb.onReceiveValue(null); return false; }
                return true;
            }
            @Override public void onPermissionRequest(final PermissionRequest r) {
                // getUserMedia from the voice page: only granted when Android gave the app the mic
                runOnUiThread(new Runnable() { public void run() {
                    boolean wantsMic = false;
                    for (String x : r.getResources()) if (PermissionRequest.RESOURCE_AUDIO_CAPTURE.equals(x)) wantsMic = true;
                    if (!wantsMic) { r.deny(); return; }
                    if (has(Manifest.permission.RECORD_AUDIO)) r.grant(new String[] { PermissionRequest.RESOURCE_AUDIO_CAPTURE });
                    else if (Build.VERSION.SDK_INT >= 23 && !prefs.getBoolean("never_mic", false)) { webPerm = r; permAsked = "mic"; requestPermissions(new String[] { Manifest.permission.RECORD_AUDIO }, PERM); }
                    else r.deny();
                }});
            }
        });
        setContentView(web);
        if (b != null) web.restoreState(b);
        else web.loadUrl("file:///android_asset/www/index.html");
    }

    @Override protected void onSaveInstanceState(Bundle b) { super.onSaveInstanceState(b); web.saveState(b); }

    boolean has(String p) {
        return Build.VERSION.SDK_INT < 23 || checkSelfPermission(p) == PackageManager.PERMISSION_GRANTED;
    }
    // page names -> Android permission (null = nothing to ask on this Android version)
    String androidPerm(String p) {
        if ("mic".equals(p) || "voice".equals(p)) return Manifest.permission.RECORD_AUDIO;
        if ("notif".equals(p)) return Build.VERSION.SDK_INT >= 33 ? "android.permission.POST_NOTIFICATIONS" : null;
        if ("storage".equals(p)) return Build.VERSION.SDK_INT <= 28 ? Manifest.permission.WRITE_EXTERNAL_STORAGE : null;
        return null;   // audio (MODIFY_AUDIO_SETTINGS) and the rest are normal permissions
    }
    void js(final String code) { runOnUiThread(new Runnable() { public void run() { web.evaluateJavascript(code, null); } }); }
    static String q(String s) { return "'" + (s == null ? "" : s.replace("\\", "\\\\").replace("'", "\\'")) + "'"; }

    void openLink(String url) {
        try { startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url))); }
        catch (Exception e) { Toast.makeText(this, "No app can open this link", Toast.LENGTH_SHORT).show(); }
    }

    @Override public void onRequestPermissionsResult(int code, String[] perms, int[] res) {
        if (code != PERM || permAsked == null) return;
        boolean ok = res.length > 0 && res[0] == PackageManager.PERMISSION_GRANTED;
        String p = permAsked; permAsked = null;
        if (webPerm != null) { if (ok) webPerm.grant(new String[] { PermissionRequest.RESOURCE_AUDIO_CAPTURE }); else webPerm.deny(); webPerm = null; }
        js("window.MOQ_perm && MOQ_perm(" + q(p) + "," + ok + ")");
    }

    @Override protected void onActivityResult(int req, int res, Intent data) {
        if (req == PICK && pick != null) {
            Uri u = (res == RESULT_OK && data != null) ? data.getData() : null;
            pick.onReceiveValue(u == null ? null : new Uri[] { u }); pick = null; return;
        }
        super.onActivityResult(req, res, data);
    }

    @Override public boolean onKeyDown(int k, KeyEvent e) {
        if (k == KeyEvent.KEYCODE_BACK) {
            web.evaluateJavascript("(window.MOQ_back && MOQ_back()) ? 1 : 0", new ValueCallback<String>() {
                public void onReceiveValue(String v) { if (!"1".equals(v)) moveTaskToBack(true); }
            });
            return true;
        }
        return super.onKeyDown(k, e);
    }

    class Bridge {
        @JavascriptInterface public String permState(String p) {
            String a = androidPerm(p);
            if (a == null || has(a)) return "granted";
            if (prefs.getBoolean("never_" + p, false)) return "denied";
            return prefs.getBoolean("asked_" + p, false) && Build.VERSION.SDK_INT >= 23 && !shouldShowRequestPermissionRationale(a) ? "denied" : "ask";
        }
        @JavascriptInterface public void requestPerm(final String p) {
            final String a = androidPerm(p);
            if (a == null || has(a)) { js("window.MOQ_perm && MOQ_perm(" + q(p) + ",true)"); return; }
            prefs.edit().putBoolean("asked_" + p, true).apply();
            runOnUiThread(new Runnable() { public void run() {
                permAsked = p;
                if (Build.VERSION.SDK_INT >= 23) requestPermissions(new String[] { a }, PERM);
            }});
        }
        @JavascriptInterface public void neverAsk(String p) { prefs.edit().putBoolean("never_" + p, true).apply(); }
        @JavascriptInterface public void openUrl(final String u) { runOnUiThread(new Runnable() { public void run() { openLink(u); } }); }
        @JavascriptInterface public void openAppSettings() {
            runOnUiThread(new Runnable() { public void run() {
                try { startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:" + getPackageName()))); } catch (Exception e) {}
            }});
        }
        @JavascriptInterface public String appVersion() { return "1.0"; }
        @JavascriptInterface public String deviceInfo() { return Build.MANUFACTURER + " " + Build.MODEL + " - Android " + Build.VERSION.RELEASE; }
        @JavascriptInterface public void clearCache() { runOnUiThread(new Runnable() { public void run() { web.clearCache(true); } }); }
        @JavascriptInterface public boolean download(String url, String name) {
            try {
                DownloadManager dm = (DownloadManager) getSystemService(Context.DOWNLOAD_SERVICE);
                DownloadManager.Request r = new DownloadManager.Request(Uri.parse(url));
                r.setTitle(name); r.setDescription("Moquil");
                r.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
                r.setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, "Moquil/" + name);
                dm.enqueue(r);
                return true;
            } catch (Exception e) { openLink(url); return false; }
        }
        @JavascriptInterface public void notify(String title, String text) {
            try {
                NotificationManager nm = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
                Notification.Builder nb;
                if (Build.VERSION.SDK_INT >= 26) {
                    nm.createNotificationChannel(new NotificationChannel("moquil", "Messages", NotificationManager.IMPORTANCE_DEFAULT));
                    nb = new Notification.Builder(MainActivity.this, "moquil");
                } else nb = new Notification.Builder(MainActivity.this);
                Intent i = new Intent(MainActivity.this, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
                PendingIntent pi = PendingIntent.getActivity(MainActivity.this, 0, i, Build.VERSION.SDK_INT >= 23 ? PendingIntent.FLAG_IMMUTABLE : 0);
                nb.setSmallIcon(getApplicationInfo().icon).setContentTitle(title).setContentText(text).setAutoCancel(true).setContentIntent(pi);
                nm.notify((int) (System.currentTimeMillis() & 0xffff), nb.build());
            } catch (Exception e) {}
        }
    }
}
