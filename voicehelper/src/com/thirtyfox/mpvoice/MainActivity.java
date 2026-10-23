package com.thirtyfox.mpvoice;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

// One screen: Start microphone / Stop / Open GTA SA. Opened by the mod with "auto" = start the
// microphone right away and go back to the game.
public class MainActivity extends Activity {
    private TextView status;
    private boolean auto;
    private final Handler h = new Handler();
    private final Runnable tick = new Runnable() { public void run() { refresh(); } };

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        l.setPadding(48, 64, 48, 48);
        l.setBackgroundColor(Color.rgb(16, 16, 18));
        TextView t = new TextView(this);
        t.setText("MP Voice");
        t.setTextSize(28); t.setTextColor(Color.WHITE);
        l.addView(t);
        TextView info = new TextView(this);
        info.setText("Microphone for MP Multiplayer voice chat.\n\nYour GTA SA app has no microphone permission, so this app records your voice and passes it to the game on this phone only (127.0.0.1, nothing goes anywhere else).\n\n1. Tap Start microphone and allow it.\n2. Go back to GTA SA and tap the mic button.\n\nThe notification stays while it is on. Stop it there or here.");
        info.setTextSize(15); info.setTextColor(Color.rgb(210, 210, 210));
        info.setPadding(0, 24, 0, 24);
        l.addView(info);
        status = new TextView(this);
        status.setTextSize(16); status.setPadding(0, 0, 0, 24);
        l.addView(status);
        l.addView(button("Start microphone", Color.rgb(30, 140, 60), v -> start()));
        l.addView(button("Stop", Color.rgb(150, 30, 30), v -> { stopService(new Intent(this, MicService.class)); h.postDelayed(tick, 300); }));
        l.addView(button("Open GTA SA", Color.rgb(60, 60, 66), v -> openGame()));
        setContentView(l);
        auto = getIntent() != null && getIntent().getBooleanExtra("auto", false);
        if (auto) start();
        refresh();
    }

    @Override protected void onNewIntent(Intent i) {
        super.onNewIntent(i);
        auto = i != null && i.getBooleanExtra("auto", false);
        if (auto) start();
    }

    @Override protected void onResume() { super.onResume(); refresh(); }

    private Button button(String text, int color, View.OnClickListener c) {
        Button b = new Button(this);
        b.setText(text); b.setTextColor(Color.WHITE); b.setBackgroundColor(color);
        b.setAllCaps(false); b.setTextSize(17);
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 150);
        p.setMargins(0, 0, 0, 24); b.setLayoutParams(p); b.setGravity(Gravity.CENTER);
        b.setOnClickListener(c);
        return b;
    }

    private boolean hasMic() {
        return Build.VERSION.SDK_INT < 23 || checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED;
    }

    private void start() {
        if (!hasMic()) {
            if (Build.VERSION.SDK_INT >= 33) requestPermissions(new String[]{ Manifest.permission.RECORD_AUDIO, Manifest.permission.POST_NOTIFICATIONS }, 1);
            else requestPermissions(new String[]{ Manifest.permission.RECORD_AUDIO }, 1);
            return;
        }
        Intent i = new Intent(this, MicService.class);
        if (Build.VERSION.SDK_INT >= 26) startForegroundService(i); else startService(i);
        h.postDelayed(tick, 300);
        if (auto) { auto = false; h.postDelayed(this::openGame, 600); }   // straight back to the game
    }

    @Override public void onRequestPermissionsResult(int code, String[] p, int[] r) {
        if (hasMic()) start();
        else { status.setTextColor(Color.rgb(255, 90, 90)); status.setText("Microphone not allowed. Allow it in Settings > Apps > MP Voice > Permissions."); }
    }

    private void refresh() {
        if (status == null) return;
        if (MicService.running) { status.setTextColor(Color.rgb(90, 230, 120)); status.setText(MicService.sending ? "ON - sending your voice to GTA SA" : "ON - ready (turn the mic on in the game)"); }
        else { status.setTextColor(Color.rgb(200, 200, 200)); status.setText("OFF"); }
        h.removeCallbacks(tick);
        if (MicService.running) h.postDelayed(tick, 1000);
    }

    private void openGame() {
        Intent g = getPackageManager().getLaunchIntentForPackage("com.rockstargames.gtasa");
        if (g != null) { g.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK); try { startActivity(g); } catch (Exception e) { moveTaskToBack(true); } }
        else moveTaskToBack(true);
    }
}
