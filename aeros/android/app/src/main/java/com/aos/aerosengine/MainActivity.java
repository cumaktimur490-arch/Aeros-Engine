package com.aos.aerosengine;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;
import java.io.InputStream;
import java.io.FileOutputStream;
import java.io.File;

/**
 * Aeros Engine Android Lite — MainActivity для выбора STL файлов
 * Для слабых телефонов — минимальный Java код, основная логика в C++ NativeActivity
 */
public class MainActivity extends Activity {
    private static final String TAG = "AerosEngine";
    private static final int PICK_STL_FILE = 1001;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.i(TAG, "MainActivity onCreate — Lite for weak phones");
        openFilePicker();
    }

    private void openFilePicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        String[] mimeTypes = {"application/sla", "model/stl", "application/octet-stream", "*/*"};
        intent.putExtra(Intent.EXTRA_MIME_TYPES, mimeTypes);
        try {
            startActivityForResult(intent, PICK_STL_FILE);
        } catch (Exception e) {
            Log.e(TAG, "File picker failed", e);
            Toast.makeText(this, "File picker not available, using built-in models", Toast.LENGTH_LONG).show();
            launchNativeActivity(null);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == PICK_STL_FILE && resultCode == RESULT_OK) {
            if (data != null) {
                Uri uri = data.getData();
                Log.i(TAG, "Picked file: " + uri);
                String cachedPath = copyUriToCache(uri);
                if (cachedPath != null) {
                    Toast.makeText(this, "Loaded: " + cachedPath, Toast.LENGTH_SHORT).show();
                    launchNativeActivity(cachedPath);
                } else {
                    Toast.makeText(this, "Failed to copy file", Toast.LENGTH_LONG).show();
                    launchNativeActivity(null);
                }
            }
        } else {
            Log.i(TAG, "File picker cancelled — using built-in");
            launchNativeActivity(null);
        }
    }

    private String copyUriToCache(Uri uri) {
        try {
            InputStream input = getContentResolver().openInputStream(uri);
            if (input == null) return null;
            File cacheDir = getCacheDir();
            File outFile = new File(cacheDir, "picked_model.stl");
            FileOutputStream output = new FileOutputStream(outFile);
            byte[] buffer = new byte[4096];
            int read;
            while ((read = input.read(buffer)) != -1) {
                output.write(buffer, 0, read);
            }
            input.close();
            output.close();
            Log.i(TAG, "Copied to cache: " + outFile.getAbsolutePath() + " size " + outFile.length());
            return outFile.getAbsolutePath();
        } catch (Exception e) {
            Log.e(TAG, "Copy failed", e);
            return null;
        }
    }

    private void launchNativeActivity(String stlPath) {
        Intent intent = new Intent(this, android.app.NativeActivity.class);
        if (stlPath != null) {
            intent.putExtra("stl_path", stlPath);
        }
        intent.putExtra("preset", detectPreset());
        try {
            startActivity(intent);
            finish();
        } catch (Exception e) {
            Log.e(TAG, "Failed to launch NativeActivity", e);
            Toast.makeText(this, "Failed to launch engine: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    private String detectPreset() {
        ActivityManager am = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
        ActivityManager.MemoryInfo memInfo = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(memInfo);
        long totalMemMB = memInfo.totalMem / (1024 * 1024);
        int cores = Runtime.getRuntime().availableProcessors();
        Log.i(TAG, "Device: RAM " + totalMemMB + " MB, cores " + cores + ", lowMemory " + memInfo.lowMemory);
        if (totalMemMB < 2000 || cores <= 2) {
            return "potato";
        } else if (totalMemMB < 3500 || cores <= 4) {
            return "low";
        } else if (totalMemMB < 6000) {
            return "medium";
        } else {
            return "full";
        }
    }
}
