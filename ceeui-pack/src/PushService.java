// SPDX-License-Identifier: MIT
// The Firebase messaging service of an APK that ceeui-pack builds with --firebase (CeeSafe ADR 0249): it is compiled
// into the APK itself, since the system starts it for a push while the app is not running, before any of the
// program's code is loaded. It runs in the app's own process, where Firebase's receiver hands it the message (held in
// that process's memory, so a service in another process would never see it). A push is data only (the server's title
// and body). While the program runs in this process (the system property ceesafe.program, which CeeSafe's runtime
// sets), the push is left to it: the program has the message over its own connection and shows it itself, decrypted.
// Otherwise, in an APK with a notification extension (--extension, CeeSafe ADR 0263), the push is sent to
// ExtensionHost in the process ":push", where the extension shows its real content, decrypted, within 8 seconds;
// failing that it is shown as it came, on the channel CeeSafe's Notifications use ("ceesafe", named after the app),
// and a tap opens the app. The token is the program's to ask for (CeeSafe's Push.register), so a new one is sent at
// the app's next start.
package org.ceeui.pack;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;
import android.util.Log;

import com.google.firebase.messaging.FirebaseMessagingService;
import com.google.firebase.messaging.RemoteMessage;

import org.json.JSONObject;

import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

public final class PushService extends FirebaseMessagingService {
    private static final String CHANNEL = "ceesafe";
    private static final String TAG = "ceesafe-push";

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
        String conversation = data.get("conversationId");
        String tag = conversation != null ? "push:" + conversation : "push";
        String shownTitle = title != null ? title : "";
        String shownBody = body != null ? body : "";
        if (extensionShows(this, shownTitle, shownBody, new JSONObject(data).toString(), tag)) {
            Log.i(TAG, "a push shown by the app's notification extension");
            return;
        }
        show(this, tag, shownTitle, shownBody);
    }

    /**
     * Whether the app's notification extension showed the push: it is sent to ExtensionHost (in ":push"), bound for the
     * call, and its answer waited for a little longer than the extension's time. False without an extension.
     */
    private static boolean extensionShows(Context context, String title, String body, String json, String tag) {
        ComponentName host = new ComponentName(context, ExtensionHost.class);
        try {
            context.getPackageManager().getServiceInfo(host, 0);
        } catch (PackageManager.NameNotFoundException e) {
            return false; // an APK without --extension
        }
        final CountDownLatch answered = new CountDownLatch(1);
        final AtomicBoolean result = new AtomicBoolean(false);
        HandlerThread replies = new HandlerThread("ceesafe-push-reply");
        replies.start();
        final Messenger replyTo = new Messenger(new Handler(replies.getLooper()) {
            @Override
            public void handleMessage(Message message) {
                result.set(message.arg1 == 1);
                answered.countDown();
            }
        });
        final Bundle push = new Bundle();
        push.putString("title", title);
        push.putString("body", body);
        push.putString("json", json);
        push.putString("tag", tag);
        ServiceConnection connection = new ServiceConnection() {
            @Override
            public void onServiceConnected(ComponentName name, IBinder binder) {
                Message message = Message.obtain(null, ExtensionHost.PUSH);
                message.setData(push);
                message.replyTo = replyTo;
                try {
                    new Messenger(binder).send(message);
                } catch (RemoteException e) {
                    answered.countDown();
                }
            }

            @Override
            public void onServiceDisconnected(ComponentName name) {
                answered.countDown(); // the extension's process ended
            }
        };
        try {
            if (!context.bindService(new Intent().setComponent(host), connection, Context.BIND_AUTO_CREATE)) {
                return false;
            }
            try {
                // The host's process may need starting, and its library loading, before the extension's own time.
                answered.await(ExtensionHost.MILLISECONDS + 3000, TimeUnit.MILLISECONDS);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            context.unbindService(connection);
            return result.get();
        } catch (RuntimeException e) {
            return false;
        } finally {
            replies.quitSafely();
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
