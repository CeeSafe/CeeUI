// SPDX-License-Identifier: MIT
// The counter on Android: the app's activity is a NativeActivity whose library is the binding's JNI library
// (libceeui_jni.so). CeeSafe's runtime calls that library's `main` on a thread of its own (CeeSafe ADR 0200), which
// calls `ceeuiMain()` here; the app ends when it returns.
package org.ceeui.counter

import android.app.NativeActivity

class CounterActivity : NativeActivity() {
    /** Called by the JNI library on CeeUI's thread, not Android's main thread: no Android views from here. */
    fun ceeuiMain() {
        counter()
    }
}
