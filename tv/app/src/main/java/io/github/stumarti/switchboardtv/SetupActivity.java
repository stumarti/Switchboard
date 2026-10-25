package io.github.stumarti.switchboardtv;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.view.Gravity;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.net.Inet6Address;
import java.net.InetAddress;
import java.util.ArrayDeque;
import java.util.HashSet;
import java.util.Set;

/**
 * Choosing the server: those found on the network (Switchboard Server
 * advertises _switchboard._tcp by mDNS), or an address typed in. Then
 * whether the app opens when the TV starts.
 */
public class SetupActivity extends Activity {
    private static final String SERVICE = "_switchboard._tcp.";

    private NsdManager nsd;
    private NsdManager.DiscoveryListener discovery;
    private WifiManager.MulticastLock multicast;
    private final ArrayDeque<NsdServiceInfo> toResolve = new ArrayDeque<>();
    private boolean resolving;
    private final Set<String> found = new HashSet<>();

    private LinearLayout servers;
    private TextView searching;
    private EditText address;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(120, 64, 120, 64);
        col.setBackgroundColor(Color.rgb(17, 17, 17));

        col.addView(text("Switchboard", 40, Color.WHITE));
        col.addView(text("Choose your Switchboard Server. This TV then shows a viewport layout from it: approve it on the server's Viewports page and pick the layout.", 20, Color.LTGRAY));

        searching = text("Looking for Switchboard Server on your network…", 20, Color.GRAY);
        col.addView(searching);
        servers = new LinearLayout(this);
        servers.setOrientation(LinearLayout.VERTICAL);
        col.addView(servers);

        col.addView(text("Or type its address", 20, Color.LTGRAY));
        address = new EditText(this);
        address.setSingleLine(true);
        address.setTextColor(Color.WHITE);
        address.setHintTextColor(Color.GRAY);
        address.setHint("switchboard.local:45678");
        address.setText(Prefs.server(this).replaceFirst("^https?://", ""));
        address.setImeOptions(EditorInfo.IME_ACTION_DONE);
        col.addView(address);
        Button use = new Button(this);
        use.setText("Use this address");
        use.setOnClickListener(v -> choose(Prefs.normalize(address.getText().toString())));
        col.addView(use);

        CheckBox boot = new CheckBox(this);
        boot.setText("Open Switchboard when the TV turns on");
        boot.setTextColor(Color.LTGRAY);
        boot.setChecked(Prefs.startAtBoot(this));
        boot.setOnCheckedChangeListener((b, on) -> Prefs.setStartAtBoot(this, on));
        col.addView(boot);
        col.addView(text("For whenever the TV is idle, choose Switchboard as its screen saver: Settings → Device Preferences → Screen saver.", 18, Color.GRAY));

        ScrollView scroll = new ScrollView(this);
        scroll.addView(col);
        setContentView(scroll);
        nsd = (NsdManager) getSystemService(Context.NSD_SERVICE);
    }

    private TextView text(String s, float size, int color) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(size);
        t.setTextColor(color);
        t.setPadding(0, 16, 0, 16);
        return t;
    }

    private void choose(String url) {
        if (url.isEmpty()) return;
        Prefs.setServer(this, url);
        finish();
    }

    @Override
    protected void onResume() {
        super.onResume();
        startDiscovery();
    }

    @Override
    protected void onPause() {
        stopDiscovery();
        super.onPause();
    }

    private void startDiscovery() {
        WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wifi != null) {
            multicast = wifi.createMulticastLock("switchboard-setup");
            multicast.setReferenceCounted(false);
            multicast.acquire();
        }
        discovery = new NsdManager.DiscoveryListener() {
            @Override public void onDiscoveryStarted(String type) {}
            @Override public void onDiscoveryStopped(String type) {}
            @Override public void onStartDiscoveryFailed(String type, int code) {
                runOnUiThread(() -> searching.setText("Couldn't look for it on the network: type its address below."));
            }
            @Override public void onStopDiscoveryFailed(String type, int code) {}
            @Override public void onServiceFound(NsdServiceInfo info) {
                runOnUiThread(() -> {
                    toResolve.add(info);
                    resolveNext();
                });
            }
            @Override public void onServiceLost(NsdServiceInfo info) {}
        };
        try {
            nsd.discoverServices(SERVICE, NsdManager.PROTOCOL_DNS_SD, discovery);
        } catch (Exception e) {
            discovery = null;
            searching.setText("Couldn't look for it on the network: type its address below.");
        }
    }

    // One at a time: older Androids refuse a second resolve while one runs.
    @SuppressWarnings("deprecation")
    private void resolveNext() {
        if (resolving || toResolve.isEmpty()) return;
        resolving = true;
        nsd.resolveService(toResolve.poll(), new NsdManager.ResolveListener() {
            @Override public void onResolveFailed(NsdServiceInfo info, int code) {
                runOnUiThread(() -> {
                    resolving = false;
                    resolveNext();
                });
            }
            @Override public void onServiceResolved(NsdServiceInfo info) {
                InetAddress host = info.getHost();
                String url = host == null ? "" : "http://" + hostPart(host) + ":" + info.getPort();
                String label = info.getServiceName();
                runOnUiThread(() -> {
                    resolving = false;
                    if (!url.isEmpty() && found.add(url)) addServer(label, url);
                    resolveNext();
                });
            }
        });
    }

    // An IPv6 address in brackets, without its interface (fe80::1%wlan0).
    private static String hostPart(InetAddress host) {
        String a = host.getHostAddress();
        if (!(host instanceof Inet6Address)) return a;
        int zone = a.indexOf('%');
        return "[" + (zone < 0 ? a : a.substring(0, zone)) + "]";
    }

    private void addServer(String name, String url) {
        searching.setText("Found on your network:");
        Button b = new Button(this);
        b.setText(name + "  ·  " + url.replaceFirst("^http://", ""));
        b.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        b.setOnClickListener(v -> choose(url));
        servers.addView(b);
        if (servers.getChildCount() == 1) b.requestFocus();
    }

    private void stopDiscovery() {
        if (discovery != null) {
            try {
                nsd.stopServiceDiscovery(discovery);
            } catch (Exception ignored) {
                // already stopped
            }
            discovery = null;
        }
        if (multicast != null && multicast.isHeld()) multicast.release();
        toResolve.clear();
        resolving = false;
    }
}
