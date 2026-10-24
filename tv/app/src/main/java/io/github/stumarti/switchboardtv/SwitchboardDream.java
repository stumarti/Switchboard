package io.github.stumarti.switchboardtv;

import android.graphics.Color;
import android.service.dreams.DreamService;
import android.view.Gravity;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.TextView;

/**
 * The TV's screensaver (Settings → Device Preferences → Screen saver →
 * Switchboard): the same page, whenever the TV is idle. Any button ends it.
 */
public class SwitchboardDream extends DreamService {
    private WebView web;

    @Override
    public void onAttachedToWindow() {
        super.onAttachedToWindow();
        setInteractive(false);
        setFullscreen(true);
        setScreenBright(true);
        if (Prefs.server(this).isEmpty()) {
            TextView t = new TextView(this);
            t.setTextColor(Color.LTGRAY);
            t.setTextSize(22);
            t.setGravity(Gravity.CENTER);
            t.setText("Open the Switchboard app to choose your Switchboard Server.");
            setContentView(t);
            return;
        }
        web = new WebView(this);
        Display.setUp(web);
        web.setWebViewClient(new WebViewClient());
        setContentView(web);
        web.loadUrl(Display.url(this));
    }

    @Override
    public void onDetachedFromWindow() {
        if (web != null) {
            web.destroy();
            web = null;
        }
        super.onDetachedFromWindow();
    }
}
