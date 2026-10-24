package io.github.stumarti.switchboardtv;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;
import android.webkit.RenderProcessGoneDetail;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import android.widget.TextView;

/**
 * The app: the server's display page on the whole screen, kept awake. The
 * remote's arrows and OK go to the page (previous / next screen, the first
 * one). Hold BACK, or press MENU, to choose another server; BACK leaves.
 */
public class MainActivity extends Activity {
    private static final long RETRY_MS = 30_000;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private FrameLayout root;
    private WebView web;
    private TextView message;
    private String shown = "";

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        message = new TextView(this);
        message.setTextColor(Color.LTGRAY);
        message.setTextSize(22);
        message.setGravity(Gravity.CENTER);
        message.setPadding(80, 80, 80, 80);
        message.setVisibility(View.GONE);
        setContentView(root);
        newWebView();
        root.addView(message, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
    }

    private void newWebView() {
        if (web != null) {
            root.removeView(web);
            web.destroy();
        }
        web = new WebView(this);
        Display.setUp(web);
        web.setWebViewClient(new WebViewClient() {
            @Override
            public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
                if (request.isForMainFrame()) unreachable();
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                message.setVisibility(View.GONE);
            }

            @Override
            public boolean onRenderProcessGone(WebView view, RenderProcessGoneDetail detail) {
                // The page's renderer crashed or was killed for memory: start again.
                shown = "";
                newWebView();
                load();
                return true;
            }
        });
        root.addView(web, 0, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
        if (Prefs.server(this).isEmpty()) {
            startActivity(new Intent(this, SetupActivity.class));
            return;
        }
        load();
        web.onResume();
    }

    @Override
    protected void onPause() {
        web.onPause();
        handler.removeCallbacksAndMessages(null);
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        handler.removeCallbacksAndMessages(null);
        web.destroy();
        super.onDestroy();
    }

    /** The page, unless it's already showing (a different server: the new one). */
    private void load() {
        String url = Display.url(this);
        if (url.equals(shown)) return;
        shown = url;
        web.loadUrl(url);
        web.requestFocus();
    }

    private void unreachable() {
        message.setText("Can't reach Switchboard Server at " + Prefs.server(this)
                + ".\n\nTrying again in 30 seconds. Hold BACK to choose another server.");
        message.setVisibility(View.VISIBLE);
        handler.removeCallbacksAndMessages(null);
        handler.postDelayed(() -> {
            shown = "";
            load();
        }, RETRY_MS);
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    private void openSetup() {
        startActivity(new Intent(this, SetupActivity.class));
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            event.startTracking();
            return true;
        }
        if (keyCode == KeyEvent.KEYCODE_MENU) {
            openSetup();
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyLongPress(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            openSetup();
            return true;
        }
        return super.onKeyLongPress(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            if (event.isTracking() && !event.isCanceled()) finish();
            return true;
        }
        return super.onKeyUp(keyCode, event);
    }
}
