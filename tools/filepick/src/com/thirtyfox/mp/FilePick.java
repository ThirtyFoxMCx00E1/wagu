package com.thirtyfox.mp;

import android.app.Activity;
import android.app.AlertDialog;
import android.text.InputFilter;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.Window;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.content.Context;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.app.Fragment;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

// MP 4.11: Add server > Plugins > SA-MP gamemode > "Import from Files".
// The GTA activity never hands a Files app answer to the mod, but a Fragment added to it gets its own
// onActivityResult (Fragments need no manifest entry). The mod loads this class from a dex it carries
// (src/ui/filepick.cpp) and polls state / name.
public class FilePick extends Fragment {
    public static volatile int state = 0;        // 0 idle, 1 picking, 2 copying, 3 done (name), 4 failed, 5 cancelled
    public static volatile String name = "";     // the saved file name in dir
    public static volatile String error = "";
    public static volatile long copied = 0;
    static String dir = "";
    static String[] mimes = null;

    public static void open(Activity a, String toDir, String mimeList) {
        dir = toDir; name = ""; error = ""; copied = 0;
        mimes = (mimeList == null || mimeList.isEmpty()) ? null : mimeList.split(",");
        state = 1;
        try {   // 4.13.42: on the UI thread, whichever thread the mod called from
            a.runOnUiThread(() -> {
                try { a.getFragmentManager().beginTransaction().add(new FilePick(), "mp_filepick").commitAllowingStateLoss(); }
                catch (Throwable t) { error = String.valueOf(t); state = 4; }
            });
        } catch (Throwable t) { error = String.valueOf(t); state = 4; }
    }

    @Override public void onCreate(Bundle b) {
        super.onCreate(b);
        if (state != 1) { remove(); return; }
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        if (mimes != null) i.putExtra(Intent.EXTRA_MIME_TYPES, mimes);
        try { startActivityForResult(i, 4711); }
        catch (Throwable e) {
            Intent g = new Intent(Intent.ACTION_GET_CONTENT); g.setType("*/*"); g.addCategory(Intent.CATEGORY_OPENABLE);
            try { startActivityForResult(g, 4711); } catch (Throwable e2) { error = "no Files app"; state = 4; remove(); }
        }
    }

    @Override public void onActivityResult(int req, int res, Intent data) {
        if (req != 4711) return;
        final Uri u = (res == Activity.RESULT_OK && data != null) ? data.getData() : null;
        if (u == null) { state = 5; remove(); return; }
        final Activity a = getActivity();
        state = 2;
        new Thread(() -> { copy(a, u); }).start();
        remove();
    }

    private void remove() {
        try { getFragmentManager().beginTransaction().remove(this).commitAllowingStateLoss(); } catch (Throwable ignored) { }
    }

    private static void copy(Activity a, Uri u) {
        try {
            String n = null;
            try (Cursor c = a.getContentResolver().query(u, new String[]{ OpenableColumns.DISPLAY_NAME }, null, null, null)) {
                if (c != null && c.moveToFirst()) n = c.getString(0);
            } catch (Throwable ignored) { }
            if (n == null || n.isEmpty()) { n = u.getLastPathSegment(); if (n == null) n = "import.bin"; }
            n = n.replaceAll("[/\\\\:*?\"<>|]", "_");
            new File(dir).mkdirs();
            File out = new File(dir, n);
            try (InputStream in = a.getContentResolver().openInputStream(u); OutputStream o = new FileOutputStream(out)) {
                byte[] buf = new byte[1 << 16]; int r;
                while ((r = in.read(buf)) > 0) { o.write(buf, 0, r); copied += r; }
            }
            name = n; state = 3;
        } catch (Throwable t) { error = String.valueOf(t.getMessage()); state = 4; }
    }

    // MP 4.12.3: Settings > Keyboard > "Phone keyboard": a small dialog with an EditText, so the phone's own
    // keyboard (Gboard, SwiftKey ...) types the text. The mod polls tstate / text.
    // MP 4.13.42: the dialog is always built on the Android UI thread (runOnUiThread) - called from the game's
    // GL thread, AlertDialog threw "Can't create handler inside thread that has not called Looper.prepare()",
    // tstate went to 4 and the mod fell back to its own keyboard. The phone keyboard is also asked to show
    // explicitly (immersive fullscreen games often keep it hidden), and chat gets a "GIF" button (tstate 7).
    public static volatile int tstate = 0;       // 0 idle, 1 open, 3 done (text), 4 failed, 5 cancelled, 7 GIF
    public static volatile String text = "";
    static AlertDialog dlg = null;

    public static void askText(final Activity a, final String title, final String init, final int max, final boolean numeric, final boolean gif) {
        tstate = 1; text = ""; error = "";
        try {
            a.runOnUiThread(() -> show(a, title, init, max, numeric, gif));
        } catch (Throwable t) { error = String.valueOf(t); tstate = 4; }
    }

    private static void show(final Activity a, String title, String init, int max, boolean numeric, boolean gif) {
        try {
            if (dlg != null) { try { dlg.setOnDismissListener(null); dlg.dismiss(); } catch (Throwable ignored) { } dlg = null; }
            final EditText et = new EditText(a);
            et.setSingleLine(true);
            if (max > 0) et.setFilters(new InputFilter[]{ new InputFilter.LengthFilter(max) });
            et.setInputType(numeric ? (InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL)
                                    : (InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS));
            et.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN);
            et.setText(init == null ? "" : init);
            et.setSelection(et.getText().length());
            AlertDialog.Builder bd = new AlertDialog.Builder(a)
                .setTitle(title == null || title.isEmpty() ? "MP" : title)
                .setView(et)
                .setPositiveButton("OK", (x, w) -> { text = et.getText().toString(); tstate = 3; })
                .setNegativeButton("Cancel", (x, w) -> { tstate = 5; });
            if (gif) bd.setNeutralButton("GIF", (x, w) -> { text = et.getText().toString(); tstate = 7; });
            final AlertDialog d = bd.create();
            dlg = d;
            d.setCanceledOnTouchOutside(false);
            d.setOnCancelListener(x -> { if (tstate == 1) tstate = 5; });
            d.setOnDismissListener(x -> { if (tstate == 1) tstate = 5; if (dlg == d) dlg = null; });
            et.setOnEditorActionListener((v, id, ev) -> {
                boolean enter = ev != null && ev.getKeyCode() == KeyEvent.KEYCODE_ENTER && ev.getAction() == KeyEvent.ACTION_DOWN;
                if (id == EditorInfo.IME_ACTION_DONE || id == EditorInfo.IME_ACTION_SEND || id == EditorInfo.IME_ACTION_GO || enter) {
                    text = et.getText().toString(); tstate = 3; d.dismiss(); return true;
                }
                return false;
            });
            Window w = d.getWindow();
            if (w != null) {
                w.clearFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM);
                w.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
            }
            d.show();
            et.requestFocus();
            final Runnable ime = () -> {
                try {
                    InputMethodManager m = (InputMethodManager) a.getSystemService(Context.INPUT_METHOD_SERVICE);
                    if (m != null && !m.showSoftInput(et, InputMethodManager.SHOW_IMPLICIT)) m.showSoftInput(et, InputMethodManager.SHOW_FORCED);
                } catch (Throwable ignored) { }
            };
            et.post(ime);
            et.postDelayed(ime, 250);   // some phones need the window focused first
        } catch (Throwable t) { error = String.valueOf(t); tstate = 4; }
    }

    public static void cancelText() {
        tstate = 0;
        final AlertDialog d = dlg;
        if (d != null) { try { d.getWindow().getDecorView().post(() -> { try { d.setOnDismissListener(null); d.dismiss(); } catch (Throwable ignored) { } }); } catch (Throwable ignored) { } }
        dlg = null;
    }

    public static void closeText() { tstate = 0; }
}
