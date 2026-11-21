/* SPDX-License-Identifier: MIT
 * ClayWind for Kotlin and Java: the JNI functions behind org.claywind.Native, each a call into claywind.h. Text comes
 * as UTF-8 in byte arrays (JNI's own strings are modified UTF-8, which differs for characters past U+FFFF). The frame
 * callback calls the Kotlin side's FrameCallback.frame(long) with the frame's handle; an exception it throws is
 * printed and cleared, and the frames after it lay out nothing until the window is closed. */
#include <jni.h>
#include <stdlib.h>
#include <string.h>

#include "ceesafe/embed.h"
#include "claywind.h"

/* A byte array's contents for the length of a call; NULL with length 0 for a null array. */
typedef struct bytes {
    JNIEnv *env;
    jbyteArray array;
    jbyte *data;
    jsize length;
} bytes;

static bytes bytes_of(JNIEnv *env, jbyteArray array) {
    bytes b = {env, array, NULL, 0};
    if (array != NULL) {
        b.length = (*env)->GetArrayLength(env, array);
        b.data = (*env)->GetByteArrayElements(env, array, NULL);
        if (b.data == NULL) {
            b.length = 0;
        }
    }
    return b;
}

/* Gives the contents back; `keep` copies what C wrote into them back to the array. */
static void bytes_done(bytes *b, int keep) {
    if (b->data != NULL) {
        (*b->env)->ReleaseByteArrayElements(b->env, b->array, b->data, keep ? 0 : JNI_ABORT);
        b->data = NULL;
    }
}

#define FRAME(handle) ((claywind_frame *)(intptr_t)(handle))
#define TEXT(b) ((const uint8_t *)(b).data), ((size_t)(b).length)

/* What claywind_run's user pointer is: the thread's JNI environment and the Kotlin callback. */
typedef struct host {
    JNIEnv *env;
    jobject callback;
    jmethodID frame;
    int failed;
} host;

static void frame_function(claywind_frame *frame, void *user) {
    host *h = user;
    if (h->failed) {
        return;
    }
    (*h->env)->CallVoidMethod(h->env, h->callback, h->frame, (jlong)(intptr_t)frame);
    if ((*h->env)->ExceptionCheck(h->env)) {
        (*h->env)->ExceptionDescribe(h->env);
        (*h->env)->ExceptionClear(h->env);
        h->failed = 1;
    }
}

/* 0 when CeeSafe's runtime runs (now or before), else ceesafe_init's error. */
JNIEXPORT jint JNICALL Java_org_claywind_Native_start(JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    ceesafe_config config;
    memset(&config, 0, sizeof config);
    config.size = sizeof config;
    config.flags = CEESAFE_INIT_ENVIRONMENT;
    int32_t result = ceesafe_init(&config);
    return result == CEESAFE_INIT_AGAIN ? 0 : result;
}

JNIEXPORT jint JNICALL Java_org_claywind_Native_version(JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    return claywind_version();
}

/* claywind_run's result, or -2 when the callback threw. */
JNIEXPORT jint JNICALL Java_org_claywind_Native_run(JNIEnv *env, jclass cls, jbyteArray title, jint width, jint height,
                                                     jobject callback) {
    (void)cls;
    jclass type = (*env)->GetObjectClass(env, callback);
    jmethodID method = (*env)->GetMethodID(env, type, "frame", "(J)V");
    if (method == NULL) {
        return CLAYWIND_NO_WINDOW;
    }
    host h = {env, callback, method, 0};
    bytes t = bytes_of(env, title);
    int32_t result = claywind_run(TEXT(t), width, height, frame_function, &h);
    bytes_done(&t, 0);
    return h.failed ? -2 : result;
}

JNIEXPORT void JNICALL Java_org_claywind_Native_column(JNIEnv *env, jclass cls, jlong f, jfloat gap) {
    (void)env;
    (void)cls;
    claywind_column(FRAME(f), gap);
}

JNIEXPORT void JNICALL Java_org_claywind_Native_row(JNIEnv *env, jclass cls, jlong f, jfloat gap) {
    (void)env;
    (void)cls;
    claywind_row(FRAME(f), gap);
}

JNIEXPORT void JNICALL Java_org_claywind_Native_panel(JNIEnv *env, jclass cls, jlong f) {
    (void)env;
    (void)cls;
    claywind_panel(FRAME(f));
}

JNIEXPORT void JNICALL Java_org_claywind_Native_spacer(JNIEnv *env, jclass cls, jlong f) {
    (void)env;
    (void)cls;
    claywind_spacer(FRAME(f));
}

JNIEXPORT void JNICALL Java_org_claywind_Native_end(JNIEnv *env, jclass cls, jlong f) {
    (void)env;
    (void)cls;
    claywind_end(FRAME(f));
}

/* kind: 0 a label, 1 a caption, 2 a heading. */
JNIEXPORT void JNICALL Java_org_claywind_Native_text(JNIEnv *env, jclass cls, jlong f, jint kind, jbyteArray text) {
    (void)cls;
    bytes t = bytes_of(env, text);
    if (kind == 1) {
        claywind_caption(FRAME(f), TEXT(t));
    } else if (kind == 2) {
        claywind_heading(FRAME(f), TEXT(t));
    } else {
        claywind_label(FRAME(f), TEXT(t));
    }
    bytes_done(&t, 0);
}

JNIEXPORT jboolean JNICALL Java_org_claywind_Native_button(JNIEnv *env, jclass cls, jlong f, jbyteArray id,
                                                            jbyteArray label) {
    (void)cls;
    bytes i = bytes_of(env, id);
    bytes l = bytes_of(env, label);
    bool pressed = claywind_button(FRAME(f), TEXT(i), TEXT(l));
    bytes_done(&l, 0);
    bytes_done(&i, 0);
    return pressed ? JNI_TRUE : JNI_FALSE;
}

/* The checkbox's state after the call (bit 0), and whether the user changed it (bit 1). */
JNIEXPORT jint JNICALL Java_org_claywind_Native_checkbox(JNIEnv *env, jclass cls, jlong f, jbyteArray id,
                                                          jbyteArray label, jboolean value) {
    (void)cls;
    bytes i = bytes_of(env, id);
    bytes l = bytes_of(env, label);
    bool on = value == JNI_TRUE;
    bool changed = claywind_checkbox(FRAME(f), TEXT(i), TEXT(l), &on);
    bytes_done(&l, 0);
    bytes_done(&i, 0);
    return (on ? 1 : 0) | (changed ? 2 : 0);
}

/* A text field over buffer[0..length]: the new length when the user changed the text (written into buffer), else
   -1. */
JNIEXPORT jint JNICALL Java_org_claywind_Native_textField(JNIEnv *env, jclass cls, jlong f, jbyteArray id,
                                                           jbyteArray name, jbyteArray buffer, jint length) {
    (void)cls;
    bytes i = bytes_of(env, id);
    bytes n = bytes_of(env, name);
    bytes b = bytes_of(env, buffer);
    size_t len = length < 0 ? 0 : (size_t)length;
    if (len > (size_t)b.length) {
        len = (size_t)b.length;
    }
    bool changed = b.length > 0 &&
                   claywind_text_field(FRAME(f), TEXT(i), TEXT(n), (uint8_t *)b.data, (size_t)b.length, &len);
    bytes_done(&b, changed);
    bytes_done(&n, 0);
    bytes_done(&i, 0);
    return changed ? (jint)len : -1;
}

JNIEXPORT jboolean JNICALL Java_org_claywind_Native_isDark(JNIEnv *env, jclass cls, jlong f) {
    (void)env;
    (void)cls;
    return claywind_is_dark(FRAME(f)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_claywind_Native_setDark(JNIEnv *env, jclass cls, jlong f, jboolean dark) {
    (void)env;
    (void)cls;
    claywind_set_dark(FRAME(f), dark == JNI_TRUE);
}

JNIEXPORT void JNICALL Java_org_claywind_Native_requestFrame(JNIEnv *env, jclass cls, jlong f) {
    (void)env;
    (void)cls;
    claywind_request_frame(FRAME(f));
}
