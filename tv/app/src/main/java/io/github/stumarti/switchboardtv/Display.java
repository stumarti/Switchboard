package io.github.stumarti.switchboardtv;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.pm.PackageInfo;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.webkit.WebSettings;
import android.webkit.WebView;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;

/**
 * The server's display page (/display), as the app and its screensaver show
 * it: the page pairs and draws by itself; this says who it is.
 */
final class Display {
    private Display() {}

    /** The page for this TV: its version, a stable id and its name. */
    static String url(Context c) {
        return Uri.parse(Prefs.server(c) + "/display").buildUpon()
                .appendQueryParameter("app", version(c))
                .appendQueryParameter("id", id(c))
                .appendQueryParameter("name", name(c))
                .build()
                .toString();
    }

    static String version(Context c) {
        try {
            PackageInfo p = c.getPackageManager().getPackageInfo(c.getPackageName(), 0);
            return p.versionName == null ? "" : p.versionName;
        } catch (Exception e) {
            return "";
        }
    }

    /**
     * A locally administered MAC address made from the TV's Android id: the
     * same every time (so a reinstall finds its approval again), and never
     * a real network card's.
     */
    @SuppressLint("HardwareIds")
    static String id(Context c) {
        String androidId = Settings.Secure.getString(c.getContentResolver(), Settings.Secure.ANDROID_ID);
        byte[] b;
        try {
            b = MessageDigest.getInstance("SHA-256").digest(("switchboard-tv:" + androidId).getBytes(StandardCharsets.UTF_8));
        } catch (Exception e) {
            b = new byte[6];
        }
        b[0] = (byte) ((b[0] & 0xfc) | 0x02);
        StringBuilder s = new StringBuilder();
        for (int i = 0; i < 6; i++) {
            if (i > 0) s.append(':');
            s.append(String.format("%02x", b[i] & 0xff));
        }
        return s.toString();
    }

    /** What the server calls it at first: the TV's own name. */
    static String name(Context c) {
        String n = null;
        if (Build.VERSION.SDK_INT >= 25) n = Settings.Global.getString(c.getContentResolver(), Settings.Global.DEVICE_NAME);
        if (n == null || n.trim().isEmpty()) n = Build.MODEL;
        return n == null ? "TV" : n.trim();
    }

    @SuppressLint("SetJavaScriptEnabled")
    static void setUp(WebView web) {
        web.setBackgroundColor(Color.BLACK);
        web.setFocusable(true);
        web.setFocusableInTouchMode(true);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        // The page keeps its id and token in localStorage.
        s.setDomStorageEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setLoadWithOverviewMode(true);
        s.setUseWideViewPort(true);
    }
}
