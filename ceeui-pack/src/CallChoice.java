// The user's choice on a call notification (CeeSafe ADR 0278: Answer, Decline, or the notification itself), left for
// the app's program: an activity without a window that records the choice in a system property (the runtime's
// CallNotice takes it; the two load from different class loaders), brings the app's activity forward, and ends.
package org.ceeui.pack;

import android.app.Activity;
import android.app.NotificationManager;
import android.content.Intent;
import android.os.Bundle;

public final class CallChoice extends Activity {
    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        Intent intent = getIntent();
        String action = intent != null ? intent.getStringExtra("ceesafe.call.action") : null;
        String id = intent != null ? intent.getStringExtra("ceesafe.call.id") : null;
        String name = intent != null ? intent.getStringExtra("ceesafe.call.name") : null;
        if (action != null && id != null) {
            System.setProperty("ceesafe.call.choice", action + "\n" + id + "\n" + (name != null ? name : ""));
            NotificationManager manager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            if (manager != null && !"show".equals(action)) {
                manager.cancel("call:" + id, 1);
            }
        }
        Intent app = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (app != null) {
            app.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
            startActivity(app);
        }
        finish();
    }
}
