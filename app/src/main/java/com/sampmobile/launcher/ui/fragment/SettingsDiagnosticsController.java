package com.sampmobile.launcher.ui.fragment;


import com.sampmobile.launcher.utils.I18n;
import android.content.Context;
import android.os.Build;
import android.os.StatFs;
import android.view.View;
import android.widget.TextView;
import android.widget.Toast;

import androidx.fragment.app.FragmentActivity;

import com.sampmobile.launcher.BuildConfig;
import com.sampmobile.launcher.R;
import com.sampmobile.launcher.utils.ClientSettings;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * Manages the Diagnostics & Crash Feedback tab inside {@link SettingsPageFragment}.
 */
final class SettingsDiagnosticsController {
    private final SettingsPageFragment host;
    private TextView tvStatusTitle;
    private TextView tvStatusDesc;

    SettingsDiagnosticsController(SettingsPageFragment host) {
        this.host = host;
    }

    void bindViews(View root) {
        tvStatusTitle = root.findViewById(R.id.tv_diag_status_title);
        tvStatusDesc = root.findViewById(R.id.tv_diag_status_desc);
    }

    void refresh() {
        Context ctx = host.getContext();
        if (ctx == null) return;
        final Context appCtx = ctx.getApplicationContext();
        host.ioExecutor.execute(() -> {
            File baseDir = appCtx.getExternalFilesDir(null);
            if (baseDir == null) return;
            File logcatDir = new File(baseDir, "logcat");
            int logFileCount = 0;
            int crashHits = 0;

            if (logcatDir.exists()) {
                File[] all = logcatDir.listFiles((d, n) -> n.endsWith(".log"));
                if (all != null) {
                    logFileCount = all.length;
                    Arrays.sort(all, (a, b) -> Long.compare(b.lastModified(), a.lastModified()));
                    for (int i = 0; i < Math.min(all.length, 3); i++) {
                        crashHits += extractCrashAndErrorLines(all[i]).size();
                    }
                }
            }

            final int finalLogCount = logFileCount;
            final boolean hasCrash = crashHits > 0;
            FragmentActivity act = host.getActivity();
            if (act != null && host.isAdded()) {
                act.runOnUiThread(() -> {
                    if (!host.isAdded() || host.getView() == null) return;
                    if (hasCrash) {
                        tvStatusTitle.setText(I18n.tr("运行诊断状态：检测到历史异常记录"));
                        tvStatusDesc.setText(String.format(Locale.US,
                                I18n.tr("系统已在后台自动保存 %d 份运行诊断记录。如您刚遇到闪退或卡顿，可直接点击下方「一键复制故障反馈信息」发送给客服或开发者协助排查。"),
                                finalLogCount));
                    } else if (finalLogCount > 0) {
                        tvStatusTitle.setText(I18n.tr("运行诊断状态：正常"));
                        tvStatusDesc.setText(String.format(Locale.US,
                                I18n.tr("客户端运行环境自检通过，当前已归档 %d 份运行记录，近期未发现崩溃异常。"),
                                finalLogCount));
                    } else {
                        tvStatusTitle.setText(I18n.tr("运行诊断状态：就绪"));
                        tvStatusDesc.setText(I18n.tr("当前尚无历史故障记录。进入游戏后系统将在后台自动守护并记录运行状态。"));
                    }
                });
            }
        });
    }

    void copyReport() {
        Context ctx = host.getContext();
        if (ctx == null) return;
        final Context appCtx = ctx.getApplicationContext();
        host.ioExecutor.execute(() -> {
            String report = buildDiagnosticReportText(appCtx);
            FragmentActivity act = host.getActivity();
            if (act != null && host.isAdded()) {
                act.runOnUiThread(() -> {
                    if (!host.isAdded() || host.getContext() == null) return;
                    host.copyTextToClipboard(I18n.tr("SA-MP Mobile 故障反馈"), report);
                    Toast.makeText(host.getContext(), I18n.tr("故障反馈信息已复制到剪贴板，可直接粘贴发送！"), Toast.LENGTH_LONG).show();
                });
            }
        });
    }

    void shareLogFile() {
        Context ctx = host.getContext();
        if (ctx == null) return;
        final Context appCtx = ctx.getApplicationContext();
        host.ioExecutor.execute(() -> {
            try {
                File baseDir = appCtx.getExternalFilesDir(null);
                if (baseDir == null) throw new IllegalStateException(I18n.tr("存储目录不可用"));
                File dir = new File(baseDir, "logcat");
                if (!dir.exists()) dir.mkdirs();
                File reportFile = new File(dir, "SAMP_Feedback_Report.log");
                String report = buildDiagnosticReportText(appCtx);
                try (FileOutputStream fos = new FileOutputStream(reportFile)) {
                    fos.write(report.getBytes(StandardCharsets.UTF_8));
                }
                FragmentActivity act = host.getActivity();
                if (act != null && host.isAdded()) {
                    act.runOnUiThread(() -> {
                        if (!host.isAdded() || host.getContext() == null) return;
                        host.shareFile(reportFile, "text/plain", I18n.tr("导出故障反馈日志文件"));
                    });
                }
            } catch (Exception e) {
                FragmentActivity act = host.getActivity();
                if (act != null && host.isAdded()) {
                    act.runOnUiThread(() -> {
                        if (!host.isAdded() || host.getContext() == null) return;
                        Toast.makeText(host.getContext(), I18n.tr("导出反馈文件失败: ") + e.getMessage(), Toast.LENGTH_SHORT).show();
                    });
                }
            }
        });
    }

    void confirmClearLogs() {
        Context ctx = host.getContext();
        if (ctx == null) return;
        final Context appCtx = ctx.getApplicationContext();
        host.showAlertDialog(I18n.tr("确定要清理所有历史日志缓存文件吗？"), (d, w) -> {
            host.ioExecutor.execute(() -> {
                File baseDir = appCtx.getExternalFilesDir(null);
                File logcatDir = baseDir != null ? new File(baseDir, "logcat") : null;
                if (logcatDir != null && logcatDir.exists()) {
                    File[] files = logcatDir.listFiles();
                    if (files != null) {
                        for (File f : files) f.delete();
                    }
                }
                FragmentActivity act = host.getActivity();
                if (act != null && host.isAdded()) {
                    act.runOnUiThread(() -> {
                        if (!host.isAdded() || host.getContext() == null) return;
                        refresh();
                        Toast.makeText(host.getContext(), I18n.tr("日志缓存已清理"), Toast.LENGTH_SHORT).show();
                    });
                }
            });
            d.dismiss();
        }, (d, w) -> d.dismiss());
    }

    private String buildDiagnosticReportText(Context appCtx) {
        File baseDir = appCtx.getExternalFilesDir(null);
        File logcatDir = baseDir != null ? new File(baseDir, "logcat") : null;
        StringBuilder sb = new StringBuilder();
        sb.append(I18n.tr("========== SA-MP Mobile 故障反馈诊断信息 ==========\n"));
        sb.append(buildDeviceSummaryLine(appCtx)).append("\n\n");

        File latest = findLatestClientLog(logcatDir);
        if (latest != null && latest.exists()) {
            List<String> crashes = extractCrashAndErrorLines(latest);
            if (!crashes.isEmpty()) {
                sb.append(I18n.tr("--- 异常摘要 (")).append(latest.getName()).append(") ---\n");
                for (String line : crashes) {
                    sb.append(line).append("\n");
                }
                sb.append("\n");
            }
            sb.append(I18n.tr("--- 最近一次客户端运行记录 (")).append(latest.getName()).append(") ---\n");
            sb.append(readFileTail(latest, 160));
        } else {
            sb.append(I18n.tr("尚未生成客户端运行日志记录。\n"));
        }
        return sb.toString();
    }

    private File findLatestClientLog(File logcatDir) {
        if (logcatDir == null || !logcatDir.exists()) return null;
        File[] files = logcatDir.listFiles((d, name) -> name.startsWith("client_") && name.endsWith(".log"));
        if (files == null || files.length == 0) return null;
        Arrays.sort(files, (a, b) -> Long.compare(b.lastModified(), a.lastModified()));
        return files[0];
    }

    private String readFileTail(File file, int maxLines) {
        if (file == null || !file.exists()) return "";
        List<String> lines = new ArrayList<>();
        try (BufferedReader br = new BufferedReader(new InputStreamReader(new FileInputStream(file), StandardCharsets.UTF_8))) {
            String line;
            while ((line = br.readLine()) != null) {
                lines.add(line);
                if (lines.size() > maxLines * 2) {
                    lines.subList(0, maxLines).clear();
                }
            }
        } catch (Exception ignored) {
        }
        int start = Math.max(0, lines.size() - maxLines);
        StringBuilder sb = new StringBuilder();
        for (int i = start; i < lines.size(); i++) {
            sb.append(lines.get(i)).append('\n');
        }
        return sb.toString();
    }

    private List<String> extractCrashAndErrorLines(File file) {
        List<String> result = new ArrayList<>();
        try (BufferedReader br = new BufferedReader(new InputStreamReader(new FileInputStream(file), StandardCharsets.UTF_8))) {
            String line;
            int contextAfterHit = 0;
            while ((line = br.readLine()) != null) {
                String lower = line.toLowerCase(Locale.US);
                boolean hit = lower.contains("crashed") || lower.contains("fatal signal")
                        || lower.contains("backtrace") || lower.contains("[error]")
                        || lower.contains("[critical]") || lower.contains("sigsegv")
                        || lower.contains("sigabrt") || lower.contains("last script command")
                        || lower.contains("last rendered object");
                if (hit) {
                    result.add(line);
                    contextAfterHit = 10;
                } else if (contextAfterHit > 0) {
                    result.add(line);
                    contextAfterHit--;
                }
                if (result.size() > 150) break;
            }
        } catch (Exception ignored) {
        }
        return result;
    }

    private String buildDeviceSummaryLine(Context appCtx) {
        boolean is64 = android.os.Process.is64Bit();
        String abi = is64 ? I18n.tr("64位") : I18n.tr("32位");
        File extDir = appCtx.getExternalFilesDir(null);
        long freeMB = 0;
        if (extDir != null && extDir.exists()) {
            StatFs stat = new StatFs(extDir.getAbsolutePath());
            freeMB = (stat.getAvailableBlocksLong() * stat.getBlockSizeLong()) / (1024 * 1024);
        }
        ClientSettings settings = ClientSettings.loadLauncher(appCtx);
        return String.format(Locale.US,
                I18n.tr("版本: v%s (%s) | 设备: %s %s | 系统: Android %s | 剩余空间: %d MB\n")
                + I18n.tr("模组开关: Modloader=%b, AML=%b, JPatch=%b, MonetLoader=%b, CLEO=%b"),
                BuildConfig.VERSION_NAME, abi,
                Build.MANUFACTURER, Build.MODEL, Build.VERSION.RELEASE, freeMB,
                settings.getBoolean("modloader"),
                settings.getBoolean("aml_scripts"),
                settings.getBoolean("jpatch_enabled"),
                settings.getBoolean("monet_scripts"),
                settings.getBoolean("cleo_scripts"));
    }
}
