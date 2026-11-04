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
import android.widget.FrameLayout;
import android.widget.ImageView;
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
        getWindow().setStatusBarColor(Color.BLACK); getWindow().setNavigationBarColor(Color.BLACK);
        try {
            sp = new SoundPool.Builder().setMaxStreams(2).setAudioAttributes(new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build()).build();
            click = sp.load(getAssets().openFd("global_click.mp3"), 1);
        } catch (Exception e) { sp = null; }
        // 4.05.3: the MP Launcher look (4.05.x mod panel): blurred night city (ui_bg.jpg) behind a
        // see-through slate panel that fills almost the whole screen, PLAY title bar, tab row with
        // a blue underline, square glass boxes, the green action button at the bottom right
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        ImageView bgv = new ImageView(this);
        try (java.io.InputStream in = getAssets().open("ui_bg.jpg")) { bgv.setImageBitmap(android.graphics.BitmapFactory.decodeStream(in)); } catch (Exception e) { }
        bgv.setScaleType(ImageView.ScaleType.CENTER_CROP);
        root.addView(bgv, new FrameLayout.LayoutParams(-1, -1));
        View tint = new View(this); tint.setBackgroundColor(Color.argb(95, 8, 10, 14));
        root.addView(tint, new FrameLayout.LayoutParams(-1, -1));
        // the panel (launcher: 3 % margin, VGlass)
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        GradientDrawable glass = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[]{ Color.argb(95, 58, 66, 76), Color.argb(150, 18, 21, 26) });
        glass.setCornerRadius(0); glass.setStroke(2, LINE);
        panel.setBackground(glass);
        FrameLayout.LayoutParams pp = new FrameLayout.LayoutParams(-1, -1);
        int m = dp(14); pp.setMargins(m, m, m, m);
        root.addView(panel, pp);
        // title bar: PLAY  MP Voice vX ............ powered by HiveMQ/ThirtyFoxMC
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL); bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(dp(16), 0, dp(16), 0);
        bar.setBackgroundColor(Color.argb(60, 0, 0, 0));
        TextView t = new TextView(this);
        t.setText("PLAY"); t.setTextSize(22); t.setTextColor(Color.WHITE); t.setTypeface(Typeface.DEFAULT_BOLD);
        bar.addView(t);
        TextView ver = new TextView(this);
        ver.setText("MP Voice  v" + VERSION); ver.setTextSize(14); ver.setTextColor(DIM); ver.setPadding(dp(14), 0, 0, 0);
        bar.addView(ver, new LinearLayout.LayoutParams(0, -2, 1));
        TextView pw = new TextView(this);
        pw.setText("powered by HiveMQ/ThirtyFoxMC"); pw.setTextSize(11); pw.setTextColor(Color.rgb(110, 200, 90)); pw.setTypeface(Typeface.DEFAULT_BOLD);
        bar.addView(pw);
        panel.addView(bar, new LinearLayout.LayoutParams(-1, dp(48)));
        panel.addView(line());
        // tab row: Microphone (selected, blue underline) - like Internet / Featured / Favorites / Lan
        LinearLayout tabs = new LinearLayout(this);
        tabs.setOrientation(LinearLayout.HORIZONTAL); tabs.setPadding(dp(16), dp(6), dp(16), 0);
        tabs.addView(tab("Microphone", true));
        panel.addView(tabs);
        panel.addView(line());
        // content
        android.widget.ScrollView sv = new android.widget.ScrollView(this);
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        l.setPadding(dp(16), dp(14), dp(16), dp(14));
        sv.addView(l);
        panel.addView(sv, new LinearLayout.LayoutParams(-1, 0, 1));
        l.addView(label("VOICE CHAT"));
        TextView info = new TextView(this);
        info.setText("Microphone for MP Multiplayer voice chat.\n\nYour GTA SA app has no microphone permission, so this app records your voice and passes it to the game on this phone only (127.0.0.1, nothing goes anywhere else).\n\n1. Tap START MICROPHONE and allow it.\n2. Go back to GTA SA and tap the mic button.\n\nThe notification stays while it is on. Stop it there or here.");
        info.setTextSize(14); info.setTextColor(TEXT);
        info.setPadding(dp(14), dp(12), dp(14), dp(12));
        info.setBackground(box(Color.argb(110, 20, 23, 28), LINE));
        l.addView(info);
        l.addView(label("PREVIEW IMAGE"));
        TextView info2 = new TextView(this);
        info2.setText("MP Launcher > + Add server > Upload preview image opens the Files app through this app (JPG / PNG, cropped to 512 x 256).");
        info2.setTextSize(13); info2.setTextColor(DIM);
        info2.setPadding(dp(14), dp(12), dp(14), dp(12));
        info2.setBackground(box(Color.argb(110, 20, 23, 28), LINE));
        l.addView(info2);
        // bottom bar (launcher: "8 servers  6 players" left, CONNECT right)
        panel.addView(line());
        LinearLayout bottom = new LinearLayout(this);
        bottom.setOrientation(LinearLayout.VERTICAL);
        bottom.setPadding(dp(16), dp(8), dp(12), dp(10));
        bottom.setBackgroundColor(Color.argb(60, 0, 0, 0));
        status = new TextView(this);
        status.setTextSize(14); status.setTypeface(Typeface.DEFAULT_BOLD); status.setPadding(0, 0, 0, dp(8));
        bottom.addView(status);
        LinearLayout btns = new LinearLayout(this);
        btns.setOrientation(LinearLayout.HORIZONTAL); btns.setGravity(Gravity.END);
        btns.addView(button("OPEN GTA SA", GREY, v -> openGame()), bl(1));
        btns.addView(button("STOP", RED, v -> { stopService(new Intent(this, MicService.class)); h.postDelayed(tick, 300); }), bl(1));
        btns.addView(button("START MICROPHONE", GREEN, v -> start()), bl(1.5f));
        bottom.addView(btns);
        panel.addView(bottom);
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

    static final String VERSION = "4.05.3";
    private int dp(int v) { return (int) (v * getResources().getDisplayMetrics().density + 0.5f); }
    private LinearLayout.LayoutParams bl(float w) { LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0, dp(46), w); p.setMargins(dp(4), 0, 0, 0); return p; }
    private View line() { View v = new View(this); v.setBackgroundColor(LINE); v.setLayoutParams(new LinearLayout.LayoutParams(-1, 2)); return v; }
    private TextView label(String s) {   // launcher group label (SERVERS / STORAGE / ADMIN)
        TextView t = new TextView(this); t.setText(s); t.setTextSize(11); t.setTextColor(DIM); t.setTypeface(Typeface.DEFAULT_BOLD); t.setLetterSpacing(0.08f);
        t.setPadding(dp(2), dp(12), 0, dp(6)); return t;
    }
    private View tab(String s, boolean on) {   // launcher top tab: white + blue underline when selected
        LinearLayout c = new LinearLayout(this); c.setOrientation(LinearLayout.VERTICAL); c.setPadding(0, 0, dp(22), 0);
        TextView t = new TextView(this); t.setText(s); t.setTextSize(14); t.setTextColor(on ? Color.WHITE : DIM); t.setTypeface(on ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
        t.setPadding(dp(2), dp(4), dp(2), dp(6)); c.addView(t);
        View u = new View(this); u.setBackgroundColor(on ? BLUE : Color.TRANSPARENT); c.addView(u, new LinearLayout.LayoutParams(-1, dp(2)));
        return c;
    }

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
        b.setAllCaps(false);
        b.setPadding(dp(14), 0, dp(14), 0); b.setMinWidth(0); b.setMinimumWidth(0); b.setTextSize(13);
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-2, dp(44));
        b.setLayoutParams(p); b.setGravity(Gravity.CENTER);
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
        else { status.setTextColor(DIM); status.setText("Microphone OFF"); }
        h.removeCallbacks(tick);
        if (MicService.running) h.postDelayed(tick, 1000);
    }

    private void openGame() {
        Intent g = getPackageManager().getLaunchIntentForPackage("com.rockstargames.gtasa");
        if (g != null) { g.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK); try { startActivity(g); } catch (Exception e) { moveTaskToBack(true); } }
        else moveTaskToBack(true);
    }
}
