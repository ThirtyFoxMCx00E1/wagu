package com.thirtyfox.mp;

import android.app.Activity;
import android.app.AlertDialog;
import android.text.InputFilter;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.WindowManager;
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
        try {
            a.getFragmentManager().beginTransaction().add(new FilePick(), "mp_filepick").commitAllowingStateLoss();
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
    public static volatile int tstate = 0;       // 0 idle, 1 open, 3 done (text), 4 failed, 5 cancelled
    public static volatile String text = "";

    public static void askText(final Activity a, String title, String init, int max, boolean numeric) {
        tstate = 1; text = ""; error = "";
        try {
            final EditText et = new EditText(a);
            et.setSingleLine(true);
            if (max > 0) et.setFilters(new InputFilter[]{ new InputFilter.LengthFilter(max) });
            et.setInputType(numeric ? (InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL)
                                    : (InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS));
            et.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN);
            et.setText(init == null ? "" : init);
            et.setSelection(et.getText().length());
            final AlertDialog d = new AlertDialog.Builder(a)
                .setTitle(title == null || title.isEmpty() ? "MP" : title)
                .setView(et)
                .setPositiveButton("OK", (x, w) -> { text = et.getText().toString(); tstate = 3; })
                .setNegativeButton("Cancel", (x, w) -> { tstate = 5; })
                .create();
            d.setOnCancelListener(x -> { if (tstate == 1) tstate = 5; });
            d.setOnDismissListener(x -> { if (tstate == 1) tstate = 5; });
            et.setOnEditorActionListener((v, id, ev) -> {
                boolean enter = ev != null && ev.getKeyCode() == KeyEvent.KEYCODE_ENTER && ev.getAction() == KeyEvent.ACTION_DOWN;
                if (id == EditorInfo.IME_ACTION_DONE || id == EditorInfo.IME_ACTION_SEND || id == EditorInfo.IME_ACTION_GO || enter) {
                    text = et.getText().toString(); tstate = 3; d.dismiss(); return true;
                }
                return false;
            });
            if (d.getWindow() != null) d.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
            d.show();
            et.requestFocus();
        } catch (Throwable t) { error = String.valueOf(t); tstate = 4; }
    }

    public static void closeText() { tstate = 0; }
}
