package io.github.stumarti.switchboardtv;

import android.content.Context;
import android.content.SharedPreferences;

/** What the app remembers: the server it shows, and whether it starts with the TV. */
final class Prefs {
    private static final String FILE = "switchboard";
    private static final String SERVER = "server";
    private static final String START_AT_BOOT = "startAtBoot";

    private Prefs() {}

    private static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences(FILE, Context.MODE_PRIVATE);
    }

    /** "http://host:port", or "" before one is chosen. */
    static String server(Context c) {
        return prefs(c).getString(SERVER, "");
    }

    static void setServer(Context c, String url) {
        prefs(c).edit().putString(SERVER, url).apply();
    }

    static boolean startAtBoot(Context c) {
        return prefs(c).getBoolean(START_AT_BOOT, true);
    }

    static void setStartAtBoot(Context c, boolean on) {
        prefs(c).edit().putBoolean(START_AT_BOOT, on).apply();
    }

    /**
     * An address as typed ("switchboard.local", "192.168.1.20:45678",
     * "http://...") as a base URL, the server's port 45678 when none is given.
     */
    static String normalize(String typed) {
        String s = typed == null ? "" : typed.trim();
        if (s.isEmpty()) return "";
        if (!s.startsWith("http://") && !s.startsWith("https://")) s = "http://" + s;
        while (s.endsWith("/")) s = s.substring(0, s.length() - 1);
        String rest = s.substring(s.indexOf("://") + 3);
        if (rest.indexOf(':') < 0 && rest.indexOf('/') < 0) s = s + ":45678";
        return s;
    }
}
