// SPDX-License-Identifier: MIT
// The process the app's notification extension runs in (":push", CeeSafe ADR 0263): a service the push service binds to
// and sends each push, as a message (title, body, the data as JSON, the notification's tag). Here the extension's
// library is loaded and given the push (org.ceesafe.Extension); the answer, whether the extension showed it, goes
// back as the reply's arg1 (1 or 0). The pushes are taken one at a time, on a thread of their own, since an answer may
// take the extension's whole time. Only in an APK built with --extension.
package org.ceeui.pack;

import android.app.Service;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;

import org.ceesafe.Extension;

public final class ExtensionHost extends Service {
    static final int PUSH = 1;
    static final int MILLISECONDS = 8000; // the extension's time (CeeSafe ADR 0263 §6.2)

    private HandlerThread thread;
    private Messenger messenger;

    @Override
    public void onCreate() {
        super.onCreate();
        thread = new HandlerThread("ceesafe-extension");
        thread.start();
        messenger = new Messenger(new Handler(thread.getLooper()) {
            @Override
            public void handleMessage(Message message) {
                if (message.what != PUSH) {
                    return;
                }
                Bundle push = message.getData();
                boolean shown = Extension.deliver(ExtensionHost.this, push.getString("title", ""),
                        push.getString("body", ""), push.getString("json", "{}"), push.getString("tag", "push"),
                        MILLISECONDS);
                Messenger reply = message.replyTo;
                if (reply != null) {
                    try {
                        reply.send(Message.obtain(null, PUSH, shown ? 1 : 0, 0));
                    } catch (RemoteException e) {
                        // the push service is gone: nothing to tell
                    }
                }
            }
        });
    }

    @Override
    public IBinder onBind(Intent intent) {
        return messenger.getBinder();
    }

    @Override
    public void onDestroy() {
        thread.quitSafely();
        super.onDestroy();
    }
}
