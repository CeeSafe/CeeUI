// SPDX-License-Identifier: MIT
// The Firebase messaging service of an APK that ceeui-pack builds with --firebase (CeeSafe ADR 0249): it is compiled
// into the APK itself, since the system starts it for a push while the app is not running, before any of the
// program's code is loaded. A push is data only (the server's title and body); it is shown here, on the channel
// CeeSafe's Notifications use ("ceesafe", named after the app), and a tap opens the app. While the program runs in
// this process (the system property ceesafe.program, which CeeSafe's runtime sets), the push is left to it: the
// program has the message over its own connection and shows it itself, decrypted. The token is the program's to ask
// for (CeeSafe's Push.register), so a new one is sent at the app's next start.
package org.ceeui.pack;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.os.Build;

import com.google.firebase.messaging.FirebaseMessagingService;
import com.google.firebase.messaging.RemoteMessage;

import java.util.Map;

public final class PushService extends FirebaseMessagingService {
    private static final String CHANNEL = "ceesafe";

    @Override
    public void onMessageReceived(RemoteMessage message) {
        if ("running".equals(System.getProperty("ceesafe.program"))) {
            return;
        }
        Map<String, String> data = message.getData();
        String title = data.get("title");
        String body = data.get("body");
        RemoteMessage.Notification shown = message.getNotification();
        if (title == null && shown != null) {
            title = shown.getTitle();
        }
        if (body == null && shown != null) {
            body = shown.getBody();
        }
        if (title == null && body == null) {
            return;
        }
        String tag = data.get("conversationId");
        show(this, tag != null ? "push:" + tag : "push", title != null ? title : "", body != null ? body : "");
    }

    @Override
    public void onNewToken(String token) {
        // The program registers the token it is given at its next start.
    }

    private static void show(Context context, String tag, String title, String body) {
        try {
            NotificationManager manager = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
            if (manager == null || !manager.areNotificationsEnabled()) {
                return;
            }
            Notification.Builder builder;
            if (Build.VERSION.SDK_INT >= 26) {
                if (manager.getNotificationChannel(CHANNEL) == null) {
                    CharSequence name = context.getApplicationInfo().loadLabel(context.getPackageManager());
                    manager.createNotificationChannel(
                            new NotificationChannel(CHANNEL, name, NotificationManager.IMPORTANCE_HIGH));
                }
                builder = new Notification.Builder(context, CHANNEL);
            } else {
                builder = new Notification.Builder(context);
                builder.setPriority(Notification.PRIORITY_HIGH);
                builder.setDefaults(Notification.DEFAULT_ALL);
            }
            Intent launch = context.getPackageManager().getLaunchIntentForPackage(context.getPackageName());
            if (launch == null) {
                return;
            }
            launch.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED);
            PendingIntent open = PendingIntent.getActivity(context, tag.hashCode(), launch,
                    PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
            int icon = context.getApplicationInfo().icon;
            builder.setSmallIcon(icon != 0 ? icon : android.R.drawable.sym_action_chat)
                    .setContentTitle(title)
                    .setContentText(body)
                    .setStyle(new Notification.BigTextStyle().bigText(body))
                    .setCategory(Notification.CATEGORY_MESSAGE)
                    .setShowWhen(true)
                    .setWhen(System.currentTimeMillis())
                    .setAutoCancel(true)
                    .setContentIntent(open);
            manager.notify(tag, 1, builder.build());
        } catch (RuntimeException e) {
            // A notification that cannot be shown is not shown; the app syncs at its next start.
        }
    }
}
