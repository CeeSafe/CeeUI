// SPDX-License-Identifier: MIT
// The counter on Android: the app's activity is a NativeActivity whose library is the binding's JNI library
// (libclaywind_jni.so). CeeSafe's runtime calls that library's `main` on a thread of its own (CeeSafe ADR 0200), which
// calls `claywindMain()` here; the app ends when it returns.
package org.claywind.counter

import android.app.NativeActivity

class CounterActivity : NativeActivity() {
    /** Called by the JNI library on ClayWind's thread, not Android's main thread: no Android views from here. */
    fun claywindMain() {
        counter()
    }
}
