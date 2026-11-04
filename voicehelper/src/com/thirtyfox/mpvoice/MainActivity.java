package com.thirtyfox.mpvoice;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.media.AudioAttributes;
import android.media.SoundPool;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

// 4.04: same square CS:GO look as the MP Launcher (dark slate, blue accent, green action, square boxes)
// and the same Global_click.mp3 on every tap.
// One screen: Start microphone / Stop / Open GTA SA. Opened by the mod with "auto" = start the
// microphone right away and go back to the game.
public class MainActivity extends Activity {
    private TextView status;
    private boolean auto;
    private final Handler h = new Handler();
    private final Runnable tick = new Runnable() { public void run() { refresh(); } };

    static final int BG = Color.rgb(26, 29, 34), PANEL = Color.rgb(40, 45, 52), LINE = Color.argb(40, 255, 255, 255);
    static final int TEXT = Color.rgb(226, 229, 233), DIM = Color.rgb(150, 157, 166), BLUE = Color.rgb(126, 178, 226);
    static final int GREEN = Color.rgb(76, 160, 48), RED = Color.rgb(170, 58, 46), GREY = Color.rgb(60, 66, 74);
    private SoundPool sp; private int click;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().setStatusBarColor(BG); getWindow().setNavigationBarColor(BG);
        try {
            sp = new SoundPool.Builder().setMaxStreams(2).setAudioAttributes(new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build()).build();
            click = sp.load(getAssets().openFd("global_click.mp3"), 1);
        } catch (Exception e) { sp = null; }
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BG);
        // title bar like the launcher (PLAY / tabs row): text left, blue underline
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.VERTICAL); bar.setPadding(40, 36, 40, 0);
        TextView t = new TextView(this);
        t.setText("MP VOICE"); t.setTextSize(24); t.setTextColor(TEXT); t.setTypeface(Typeface.DEFAULT_BOLD); t.setLetterSpacing(0.08f);
        bar.addView(t);
        View u = new View(this); u.setBackgroundColor(BLUE);
        LinearLayout.LayoutParams up = new LinearLayout.LayoutParams(160, 6); up.setMargins(0, 14, 0, 0); u.setLayoutParams(up);
        bar.addView(u);
        root.addView(bar);
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        l.setPadding(32, 28, 32, 32);
        root.addView(l);
        TextView info = new TextView(this);
        info.setText("Microphone for MP Multiplayer voice chat.\n\nYour GTA SA app has no microphone permission, so this app records your voice and passes it to the game on this phone only (127.0.0.1, nothing goes anywhere else).\n\n1. Tap Start microphone and allow it.\n2. Go back to GTA SA and tap the mic button.\n\nThe notification stays while it is on. Stop it there or here.\n\nIt also opens the Files app for MP Launcher > + Add server > Upload preview image.");
        info.setTextSize(14); info.setTextColor(DIM);
        info.setPadding(28, 24, 28, 24);
        info.setBackground(box(PANEL, LINE));
        l.addView(info);
        status = new TextView(this);
        status.setTextSize(15); status.setPadding(28, 22, 28, 22); status.setTypeface(Typeface.DEFAULT_BOLD);
        status.setBackground(box(Color.rgb(32, 36, 42), LINE));
        LinearLayout.LayoutParams sp2 = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        sp2.setMargins(0, 20, 0, 20); status.setLayoutParams(sp2);
        l.addView(status);
        l.addView(button("START MICROPHONE", GREEN, v -> start()));
        l.addView(button("STOP", RED, v -> { stopService(new Intent(this, MicService.class)); h.postDelayed(tick, 300); }));
        l.addView(button("OPEN GTA SA", GREY, v -> openGame()));
        setContentView(root);
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

    // square box (no round corners), 1 px light line - the launcher's VSlate / square buttons
    static GradientDrawable box(int fill, int line) {
        GradientDrawable d = new GradientDrawable(); d.setShape(GradientDrawable.RECTANGLE); d.setCornerRadius(0);
        d.setColor(fill); d.setStroke(2, line); return d;
    }

    private void tapSound() { if (sp != null && click != 0) sp.play(click, 0.8f, 0.8f, 1, 0, 1f); }

    private Button button(String text, int color, View.OnClickListener c) {
        Button b = new Button(this);
        b.setText(text); b.setTextColor(Color.WHITE); b.setTypeface(Typeface.DEFAULT_BOLD); b.setLetterSpacing(0.06f);
        StateListDrawable st = new StateListDrawable();
        int dark = Color.rgb(Color.red(color) * 7 / 10, Color.green(color) * 7 / 10, Color.blue(color) * 7 / 10);
        st.addState(new int[]{ android.R.attr.state_pressed }, box(dark, LINE));
        st.addState(new int[]{}, box(color, LINE));
        b.setBackground(st); b.setStateListAnimator(null);
        b.setAllCaps(false); b.setTextSize(16);
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 140);
        p.setMargins(0, 0, 0, 16); b.setLayoutParams(p); b.setGravity(Gravity.CENTER);
        b.setOnClickListener(v -> { tapSound(); c.onClick(v); });
        return b;
    }

    @Override protected void onDestroy() { if (sp != null) sp.release(); super.onDestroy(); }

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
        if (MicService.running) { status.setTextColor(Color.rgb(120, 210, 90)); status.setText(MicService.sending ? "ON - sending your voice to GTA SA" : "ON - ready (turn the mic on in the game)"); }
        else { status.setTextColor(DIM); status.setText("OFF"); }
        h.removeCallbacks(tick);
        if (MicService.running) h.postDelayed(tick, 1000);
    }

    private void openGame() {
        Intent g = getPackageManager().getLaunchIntentForPackage("com.rockstargames.gtasa");
        if (g != null) { g.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK); try { startActivity(g); } catch (Exception e) { moveTaskToBack(true); } }
        else moveTaskToBack(true);
    }
}
