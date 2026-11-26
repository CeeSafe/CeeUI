// SPDX-License-Identifier: MIT
// The app's files for another app (CeeSafe ADR 0268): the content provider through which ShareSheet.share and
// Launcher.openFile hand a file over, as content://<package>.ceesafe.files/<the file's path>, with a grant to read that
// one URI. The packer declares it (exported false, grantUriPermissions true) and compiles it into the APK, where the
// system finds it when the other app reads. It answers only for files inside the app's own folders (files, cache, and
// their external twins), read-only, with the display name and size a receiver asks for. Nothing is copied.

package org.ceesafe;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.webkit.MimeTypeMap;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;

public final class Files extends ContentProvider {
    @Override
    public boolean onCreate() {
        return true;
    }

    /** The file `uri` names, when it is one of the app's own and is there; else null. */
    private File fileOf(Uri uri) {
        Context context = getContext();
        String path = uri.getPath();
        if (context == null || path == null) {
            return null;
        }
        try {
            File file = new File(path).getCanonicalFile();
            if (!file.isFile()) {
                return null;
            }
            File[] roots = {context.getFilesDir(), context.getCacheDir(), context.getExternalFilesDir(null),
                            context.getExternalCacheDir()};
            for (File root : roots) {
                if (root != null && file.getPath().startsWith(root.getCanonicalPath() + File.separator)) {
                    return file;
                }
            }
        } catch (IOException e) {
            return null;
        }
        return null;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!"r".equals(mode)) {
            throw new SecurityException("read only");
        }
        File file = fileOf(uri);
        if (file == null) {
            throw new FileNotFoundException(uri.toString());
        }
        return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public String getType(Uri uri) {
        File file = fileOf(uri);
        if (file == null) {
            return null;
        }
        String name = file.getName();
        int dot = name.lastIndexOf('.');
        String type = dot >= 0 ? MimeTypeMap.getSingleton().getMimeTypeFromExtension(name.substring(dot + 1).toLowerCase())
                               : null;
        return type != null ? type : "application/octet-stream";
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] arguments, String order) {
        File file = fileOf(uri);
        if (file == null) {
            return null;
        }
        String[] columns = projection != null ? projection
                                              : new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE};
        String[] known = new String[columns.length];
        Object[] values = new Object[columns.length];
        int count = 0;
        for (String column : columns) {
            if (OpenableColumns.DISPLAY_NAME.equals(column)) {
                known[count] = column;
                values[count++] = file.getName();
            } else if (OpenableColumns.SIZE.equals(column)) {
                known[count] = column;
                values[count++] = file.length();
            }
        }
        String[] names = new String[count];
        Object[] row = new Object[count];
        System.arraycopy(known, 0, names, 0, count);
        System.arraycopy(values, 0, row, 0, count);
        MatrixCursor cursor = new MatrixCursor(names, 1);
        cursor.addRow(row);
        return cursor;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException("read only");
    }

    @Override
    public int delete(Uri uri, String selection, String[] arguments) {
        throw new UnsupportedOperationException("read only");
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] arguments) {
        throw new UnsupportedOperationException("read only");
    }
}
