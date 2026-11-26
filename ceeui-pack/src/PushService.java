// SPDX-License-Identifier: MIT
// The Firebase messaging service of an APK that ceeui-pack builds with --firebase (CeeSafe ADR 0249): it is compiled
// into the APK itself, since the system starts it for a push while the app is not running, before any of the
// program's code is loaded. It runs in a process of its own (":push", CeeSafe ADR 0263). A push is data only (the
// server's title and body). While the app's program runs (it holds files/ceesafe-program.lock; in this process, the
// system property ceesafe.program), the push is left to it: the program has the message over its own connection and
// shows it itself, decrypted. Otherwise the app's notification extension, when the APK has one (--extension), is
// given it to show its real content, decrypted (8 seconds); failing that it is shown as it came, on the channel
// CeeSafe's Notifications use ("ceesafe", named after the app), and a tap opens the app. The token is the program's
// to ask for (CeeSafe's Push.register), so a new one is sent at the app's next start.
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

import org.ceesafe.Extension;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.channels.OverlappingFileLockException;
import java.util.Map;

public final class PushService extends FirebaseMessagingService {
    private static final String CHANNEL = "ceesafe";

    /** How long the extension has to show a push before it is shown as it came (CeeSafe ADR 0263 §6.2). */
    private static final int EXTENSION_MILLISECONDS = 8000;

    @Override
    public void onMessageReceived(RemoteMessage message) {
        if ("running".equals(System.getProperty("ceesafe.program")) || programRunning(this)) {
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
        String conversation = data.get("conversationId");
        String tag = conversation != null ? "push:" + conversation : "push";
        String shownTitle = title != null ? title : "";
        String shownBody = body != null ? body : "";
        if (Extension.deliver(this, shownTitle, shownBody, new JSONObject(data).toString(), tag,
                EXTENSION_MILLISECONDS)) {
            return;
        }
        show(this, tag, shownTitle, shownBody);
    }

    /**
     * Whether the app's program runs, in its own process: CeeSafe's runtime holds a lock on files/ceesafe-program.lock
     * from its start until its process ends (CeeSafe ADR 0263 §2.4). A lock taken here for a moment says it is free.
     */
    private static boolean programRunning(Context context) {
        File file = new File(context.getFilesDir(), "ceesafe-program.lock");
        if (!file.exists()) {
            return false;
        }
        try (RandomAccessFile access = new RandomAccessFile(file, "rw"); FileChannel channel = access.getChannel()) {
            FileLock lock = channel.tryLock();
            if (lock == null) {
                return true;
            }
            lock.release();
            return false;
        } catch (OverlappingFileLockException e) {
            return true;
        } catch (IOException e) {
            return false;
        }
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
