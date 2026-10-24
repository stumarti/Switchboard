package io.github.stumarti.switchboardtv;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * Opens the app when the TV starts, if that's on (Setup) and a server is
 * chosen. (Some TVs only let an app start itself like this once it may
 * "display over other apps"; the screensaver works either way.)
 */
public class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        if (!Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) return;
        if (!Prefs.startAtBoot(context) || Prefs.server(context).isEmpty()) return;
        Intent open = new Intent(context, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        context.startActivity(open);
    }
}
