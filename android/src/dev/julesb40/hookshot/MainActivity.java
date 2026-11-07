package dev.julesb40.hookshot;

import android.app.Activity;
import android.os.Bundle;
import android.os.Build;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Hardware bring-up only: no game, file picker, external storage access or runtime stubs. */
public final class MainActivity extends Activity implements SurfaceHolder.Callback {
    private TextView status, input;
    private boolean loaded;
    private int touches, buttons;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private native String nativeProbe(Surface surface);

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        TextView title = new TextView(this);
        title.setText("Hookshot • Android ARM64 bring-up\nGame runtime not ported — this APK tests graphics and input only.");
        title.setTextSize(18);
        root.addView(title);
        input = new TextView(this);
        input.setText("Touch a button or connect a controller to test input.");
        root.addView(input);
        LinearLayout controls = new LinearLayout(this);
        for (final String name : new String[]{"←", "↑", "↓", "→", "A", "B", "X", "Y", "L", "R", "+"}) {
            Button button = new Button(this);
            button.setText(name);
            button.setOnTouchListener(new android.view.View.OnTouchListener() {
                @Override public boolean onTouch(android.view.View v, MotionEvent e) {
                if (e.getActionMasked() == MotionEvent.ACTION_DOWN) {
                    touches++;
                    input.setText("Touch input: " + name + "  (" + touches + " presses; diagnostic only)");
                }
                return false;
                }
            });
            controls.addView(button, new LinearLayout.LayoutParams(0, -2, 1));
        }
        root.addView(controls);
        SurfaceView surface = new SurfaceView(this);
        surface.getHolder().addCallback(this);
        root.addView(surface, new LinearLayout.LayoutParams(-1, 100));
        status = new TextView(this);
        status.setTextIsSelectable(true);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(status);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(root);
        try {
            System.loadLibrary("hookshot_probe");
            loaded = true;
        } catch (UnsatisfiedLinkError error) {
            status.setText("Native library did not load: " + error);
        }
    }

    @Override public void surfaceCreated(SurfaceHolder holder) { }
    @Override public void surfaceDestroyed(SurfaceHolder holder) { }
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (!loaded) return;
        final Surface surface = holder.getSurface();
        status.setText("Checking Vulkan on the graphics worker…");
        worker.execute(new Runnable() {
            @Override public void run() { probeSurface(surface); }
        });
    }
    private void probeSurface(Surface surface) {
        String report = "Device: " + Build.MANUFACTURER + " " + Build.MODEL +
            "\nAndroid: " + Build.VERSION.RELEASE + " / API " + Build.VERSION.SDK_INT +
            "\nABIs: " + String.join(", ", Build.SUPPORTED_ABIS) + "\n";
        try {
            report += nativeProbe(surface);
        } catch (RuntimeException error) {
            report += "Native probe error: " + error;
        }
        report += "\nNOT TESTED: game launch, NVN replay, graphics correctness, saves, FPS.\n" +
            "No original game files were opened. No storage permission is requested.\n";
        try {
            File directory = new File(getFilesDir(), "reports");
            if (!directory.isDirectory() && !directory.mkdir()) throw new java.io.IOException("Cannot create reports");
            File file = File.createTempFile("probe-", ".txt", directory);
            try (FileOutputStream stream = new FileOutputStream(file)) {
                stream.write(report.getBytes(StandardCharsets.UTF_8));
            }
            report += "Report saved in isolated app storage: " + file.getName();
        } catch (java.io.IOException error) {
            report += "Could not save report: " + error;
        }
        final String result = report;
        runOnUiThread(new Runnable() {
            @Override public void run() { if (!isDestroyed()) status.setText(result); }
        });
    }

    @Override public void onDestroy() {
        worker.shutdown();
        super.onDestroy();
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if ((event.getSource() & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
            (event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
            if (event.getAction() == KeyEvent.ACTION_DOWN) {
                buttons++;
                input.setText("Controller: " + KeyEvent.keyCodeToString(event.getKeyCode()) + " (" + buttons + " presses)");
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }
    @Override public boolean onGenericMotionEvent(MotionEvent event) {
        if ((event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
            input.setText(String.format(Locale.ROOT, "Sticks: %.2f %.2f / %.2f %.2f; triggers %.2f %.2f",
                event.getAxisValue(MotionEvent.AXIS_X), event.getAxisValue(MotionEvent.AXIS_Y),
                event.getAxisValue(MotionEvent.AXIS_Z), event.getAxisValue(MotionEvent.AXIS_RZ),
                event.getAxisValue(MotionEvent.AXIS_LTRIGGER), event.getAxisValue(MotionEvent.AXIS_RTRIGGER)));
            return true;
        }
        return super.onGenericMotionEvent(event);
    }
}
