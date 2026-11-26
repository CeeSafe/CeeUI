// SPDX-License-Identifier: MIT
// The app's notification extension (CeeSafe ADR 0263), as the APK's push service sees it: the library
// libextension.so, which ceeui-pack builds from --extension's package, loaded once in the service's process. Its
// runtime registers the two natives below when it is loaded (JNI_OnLoad): start runs the extension's main on a thread
// of its own, push hands it a push and waits until it is delivered (true) or the time runs out (false). An APK built
// without --extension has no such library: deliver then answers false, and the service shows the push as it came.
package org.ceesafe;

import android.content.Context;

public final class Extension {
    private static boolean loaded;
    private static boolean failed;

    private Extension() {}

    private static native void start(Context context);

    private static native boolean push(String title, String body, String json, String tag, int milliseconds);

    /** Whether the extension showed the push within `milliseconds`. */
    public static boolean deliver(Context context, String title, String body, String json, String tag,
                                  int milliseconds) {
        synchronized (Extension.class) {
            if (failed) {
                return false;
            }
            if (!loaded) {
                try {
                    System.loadLibrary("extension");
                    start(context.getApplicationContext());
                    loaded = true;
                } catch (Throwable t) {
                    failed = true; // no extension in this APK, or it could not start
                    return false;
                }
            }
        }
        return push(title, body, json, tag, milliseconds);
    }
}
