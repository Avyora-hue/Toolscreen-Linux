package com.toolscreen.linux;

import java.awt.BorderLayout;
import java.awt.Dimension;
import java.awt.FlowLayout;
import java.awt.GraphicsEnvironment;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.URLDecoder;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Properties;

import javax.swing.BorderFactory;
import javax.swing.JButton;
import javax.swing.JComboBox;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JOptionPane;
import javax.swing.JPanel;
import javax.swing.SwingUtilities;
import javax.swing.UIManager;

/**
 * Installer for Toolscreen on Linux. Double-click it inside a MultiMC or Prism Launcher instance folder (or run
 * it anywhere and pick an instance). It copies libtoolscreen.so and a launch wrapper into
 * &lt;instance&gt;/toolscreen-linux/ and sets the instance's Wrapper command so the game starts with the library
 * preloaded.
 *
 * Command line: java -jar Toolscreen-linux.jar [--install DIR | --uninstall DIR | --status DIR | --build-recommended DIR | --list]
 * [--force]
 * (--force skips the check that the launcher is closed)
 */
public final class Installer {

    static final String VERSION = "1.4.4-linux.1";
    static final String TITLE = "Toolscreen " + VERSION;
    static final String FOLDER = "toolscreen-linux";
    static final String WRAPPER = "toolscreen-wrapper.sh";
    static final String LIBRARY = "libtoolscreen.so";
    static final Charset UTF8 = Charset.forName("UTF-8");
    static final String ABSENT = "<absent>";

    private Installer() {
    }

    // ------------------------------------------------------------------
    // Entry point
    // ------------------------------------------------------------------
    public static void main(String[] args) {
        try {
            if (args.length > 0) {
                System.exit(runCli(args));
                return;
            }
            if (GraphicsEnvironment.isHeadless()) {
                System.out.println("No display available. Use --install <instance dir>, --uninstall <instance dir>, or --list.");
                System.exit(2);
                return;
            }
            runGui();
        } catch (Exception e) {
            e.printStackTrace();
            if (!GraphicsEnvironment.isHeadless()) {
                JOptionPane.showMessageDialog(null, "Unexpected error: " + e, TITLE, JOptionPane.ERROR_MESSAGE);
            }
            System.exit(1);
        }
    }

    static int runCli(String[] args) throws IOException {
        String cmd = args[0];
        if (cmd.equals("--list")) {
            for (Instance i : findAllInstances()) {
                System.out.println(i.launcher.displayName + "\t" + i.name + "\t" + i.dir + (isInstalled(i.dir) ? "\t[installed]" : ""));
            }
            return 0;
        }
        if (args.length < 2) {
            System.err.println("Missing instance directory.");
            return 2;
        }
        File dir = new File(args[1]).getAbsoluteFile();
        Instance inst = Instance.at(dir);
        if (inst == null) {
            System.err.println("Not an instance folder (no instance.cfg): " + dir);
            return 2;
        }
        if (cmd.equals("--build-recommended")) {
            // Only (re)builds ~/.config/toolscreen/recommended.toml; the instance settings are not touched.
            System.out.println(buildRecommended(inst));
            return 0;
        }
        if (cmd.equals("--status")) {
            System.out.println(isInstalled(inst.dir) ? "installed" : "not installed");
            return 0;
        }
        boolean force = args.length > 2 && args[2].equals("--force");
        String running = force ? null : runningLauncher(inst.launcher);
        if (running != null) {
            System.err.println(inst.launcher.displayName + " is running (pid " + running + "). Close it first: it rewrites "
                    + "instance.cfg when it exits.");
            return 3;
        }
        if (cmd.equals("--install")) {
            System.out.println(install(inst));
            return 0;
        }
        if (cmd.equals("--uninstall")) {
            System.out.println(uninstall(inst));
            return 0;
        }
        System.err.println("Unknown option " + cmd);
        return 2;
    }

    // ------------------------------------------------------------------
    // GUI
    // ------------------------------------------------------------------
    static void runGui() throws Exception {
        try {
            UIManager.setLookAndFeel(UIManager.getSystemLookAndFeelClassName());
        } catch (Exception ignored) {
        }
        final Instance here = detectInstanceFromJarLocation();
        final List<Instance> all = here != null ? new ArrayList<Instance>() : findAllInstances();
        if (here != null) {
            all.add(here);
        }
        SwingUtilities.invokeLater(new Runnable() {
            public void run() {
                showMainWindow(all, here != null);
            }
        });
    }

    static void showMainWindow(final List<Instance> instances, boolean fromInstanceFolder) {
        final JFrame frame = new JFrame(TITLE);
        frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        JPanel root = new JPanel(new BorderLayout(10, 10));
        root.setBorder(BorderFactory.createEmptyBorder(16, 18, 14, 18));

        JLabel header = new JLabel("<html><span style='font-size:15px'><b>Toolscreen for Linux</b></span><br>"
                + "<span style='color:#888'>Version " + VERSION + " &middot; MultiMC &amp; Prism Launcher</span></html>");
        root.add(header, BorderLayout.NORTH);

        final JLabel info = new JLabel();
        info.setVerticalAlignment(JLabel.TOP);
        info.setPreferredSize(new Dimension(700, 230));

        JPanel center = new JPanel(new BorderLayout(6, 8));
        final JComboBox<Instance> combo = new JComboBox<Instance>(instances.toArray(new Instance[0]));
        if (instances.isEmpty()) {
            info.setText("<html>No MultiMC or Prism Launcher instances were found.<br><br>"
                    + "Copy this .jar into your instance folder (right-click the instance &rarr; Folder) and double-click it there,"
                    + " or run:<br><tt>java -jar " + jarName() + " --install /path/to/instance</tt></html>");
        }
        if (!fromInstanceFolder && !instances.isEmpty()) {
            JPanel pick = new JPanel(new BorderLayout(6, 0));
            pick.add(new JLabel("Instance:"), BorderLayout.WEST);
            pick.add(combo, BorderLayout.CENTER);
            center.add(pick, BorderLayout.NORTH);
        }
        center.add(info, BorderLayout.CENTER);
        root.add(center, BorderLayout.CENTER);

        final JButton installBtn = new JButton("Install");
        final JButton uninstallBtn = new JButton("Uninstall");
        final JButton configBtn = new JButton("Open config folder");
        final JButton closeBtn = new JButton("Close");
        JPanel buttons = new JPanel(new FlowLayout(FlowLayout.RIGHT, 8, 0));
        buttons.add(configBtn);
        buttons.add(uninstallBtn);
        buttons.add(installBtn);
        buttons.add(closeBtn);
        root.add(buttons, BorderLayout.SOUTH);

        final Runnable refresh = new Runnable() {
            public void run() {
                Instance inst = (Instance) combo.getSelectedItem();
                if (inst == null) {
                    installBtn.setEnabled(false);
                    uninstallBtn.setEnabled(false);
                    return;
                }
                boolean installed = isInstalled(inst.dir);
                installBtn.setText(installed ? "Reinstall / Update" : "Install");
                uninstallBtn.setEnabled(installed);
                installBtn.setEnabled(true);
                info.setText("<html><table cellpadding=2>"
                        + "<tr><td style='color:#888'>Instance</td><td><b>" + esc(inst.name) + "</b></td></tr>"
                        + "<tr><td style='color:#888'>Launcher</td><td>" + inst.launcher.displayName + "</td></tr>"
                        + "<tr><td style='color:#888'>Folder</td><td>" + esc(shortPath(inst.dir.getPath())) + "</td></tr>"
                        + "<tr><td style='color:#888'>Status</td><td>" + (installed ? "<span style='color:#3a3'>Installed</span>"
                        : "Not installed") + "</td></tr>"
                        + "<tr><td style='color:#888'>Config</td><td>" + esc(shortPath(configFile().getPath())) + "</td></tr>"
                        + "</table><p style='width:540px; margin-top:8px'>In game, <b>Ctrl+I</b> opens the settings editor; its "
                        + "<b>Defaults</b> tab loads the recommended settings that Install builds for this computer. Hotkeys: "
                        + "<b>Z</b> Thin, <b>J</b> EyeZoom, <b>Left Alt</b> Wide, <b>O</b> OneShot, <b>B</b> low sensitivity, "
                        + "<b>P</b> Ninjabrain Bot panel.</p></html>");
            }
        };
        combo.addActionListener(new java.awt.event.ActionListener() {
            public void actionPerformed(java.awt.event.ActionEvent e) {
                refresh.run();
            }
        });
        installBtn.addActionListener(new java.awt.event.ActionListener() {
            public void actionPerformed(java.awt.event.ActionEvent e) {
                Instance inst = (Instance) combo.getSelectedItem();
                if (inst == null || !ensureLauncherClosed(frame, inst)) {
                    return;
                }
                try {
                    String msg = install(inst);
                    JOptionPane.showMessageDialog(frame, "<html><div style='width:560px'><b>Installed!</b><br><br>" + esc(msg).replace("\n", "<br>")
                            + "<br><br>Launch the instance from " + inst.launcher.displayName + " as usual.</div></html>", TITLE,
                            JOptionPane.INFORMATION_MESSAGE);
                } catch (Exception ex) {
                    JOptionPane.showMessageDialog(frame, "Installation failed:\n" + ex.getMessage(), TITLE, JOptionPane.ERROR_MESSAGE);
                }
                refresh.run();
            }
        });
        uninstallBtn.addActionListener(new java.awt.event.ActionListener() {
            public void actionPerformed(java.awt.event.ActionEvent e) {
                Instance inst = (Instance) combo.getSelectedItem();
                if (inst == null || !ensureLauncherClosed(frame, inst)) {
                    return;
                }
                try {
                    String msg = uninstall(inst);
                    JOptionPane.showMessageDialog(frame, msg, TITLE, JOptionPane.INFORMATION_MESSAGE);
                } catch (Exception ex) {
                    JOptionPane.showMessageDialog(frame, "Uninstall failed:\n" + ex.getMessage(), TITLE, JOptionPane.ERROR_MESSAGE);
                }
                refresh.run();
            }
        });
        configBtn.addActionListener(new java.awt.event.ActionListener() {
            public void actionPerformed(java.awt.event.ActionEvent e) {
                File dir = configFile().getParentFile();
                dir.mkdirs();
                try {
                    new ProcessBuilder("xdg-open", dir.getPath()).start();
                } catch (IOException ex) {
                    JOptionPane.showMessageDialog(frame, "Config folder: " + dir, TITLE, JOptionPane.INFORMATION_MESSAGE);
                }
            }
        });
        closeBtn.addActionListener(new java.awt.event.ActionListener() {
            public void actionPerformed(java.awt.event.ActionEvent e) {
                frame.dispose();
                System.exit(0);
            }
        });

        refresh.run();
        frame.setContentPane(root);
        frame.pack();
        frame.setLocationRelativeTo(null);
        frame.setVisible(true);
    }

    /** Asks the user to close the instance's launcher, which would otherwise overwrite instance.cfg. */
    static boolean ensureLauncherClosed(JFrame parent, Instance inst) {
        while (true) {
            String pid = runningLauncher(inst.launcher);
            if (pid == null) {
                return true;
            }
            Object[] options = {"Close it for me", "Retry", "Cancel"};
            int choice = JOptionPane.showOptionDialog(parent, "<html><b>" + inst.launcher.displayName + " is running.</b><br><br>"
                    + "It keeps instance settings in memory and writes them back when it exits, which would undo the<br>"
                    + "change Toolscreen makes. Close " + inst.launcher.displayName + " (and any running game), then retry.</html>",
                    TITLE, JOptionPane.DEFAULT_OPTION, JOptionPane.WARNING_MESSAGE, null, options, options[1]);
            if (choice == 0) {
                for (String p : pid.split(",")) {
                    try {
                        new ProcessBuilder("kill", "-TERM", p).start().waitFor();
                    } catch (Exception ignored) {
                    }
                }
                sleep(1500);
            } else if (choice != 1) {
                return false;
            }
        }
    }

    // ------------------------------------------------------------------
    // Install / uninstall
    // ------------------------------------------------------------------
    static boolean isInstalled(File instDir) {
        File cfg = new File(instDir, "instance.cfg");
        if (!new File(new File(instDir, FOLDER), LIBRARY).isFile() || !cfg.isFile()) {
            return false;
        }
        try {
            IniDoc ini = IniDoc.load(cfg);
            return ini.get("WrapperCommand", "").contains(FOLDER + "/" + WRAPPER);
        } catch (IOException e) {
            return false;
        }
    }

    static String install(Instance inst) throws IOException {
        File folder = new File(inst.dir, FOLDER);
        if (!folder.isDirectory() && !folder.mkdirs()) {
            throw new IOException("Cannot create " + folder);
        }
        File lib = new File(folder, LIBRARY);
        File wrapper = new File(folder, WRAPPER);
        extract("/native/" + LIBRARY, lib);
        extract("/native/" + WRAPPER, wrapper);
        extract("/native/README.txt", new File(folder, "README.txt"));
        if (!wrapper.setExecutable(true, false) || !lib.setReadable(true, false)) {
            throw new IOException("Cannot make " + wrapper + " executable");
        }

        File cfgFile = new File(inst.dir, "instance.cfg");
        File backup = new File(inst.dir, "instance.cfg.toolscreen-backup");
        if (!backup.exists()) {
            copy(cfgFile, backup);
        }
        IniDoc ini = IniDoc.load(cfgFile);
        Properties state = loadState(folder);
        List<String> notes = new ArrayList<String>();

        boolean override = "true".equalsIgnoreCase(ini.get("OverrideCommands", "false"));
        if (!override && !state.containsKey("overrideWasFalse")) {
            // Turning the per-instance override on would drop the launcher-wide commands, so carry them over.
            IniDoc global = inst.launcher.globalConfig();
            for (String key : new String[] {"PreLaunchCommand", "WrapperCommand", "PostExitCommand"}) {
                state.setProperty("orig." + key, ini.has(key) ? ini.get(key, "") : ABSENT);
                String v = global != null ? global.get(key, "") : "";
                ini.set(key, v);
                if (!v.isEmpty()) {
                    notes.add("Copied the launcher-wide " + key + " into the instance.");
                }
            }
            ini.set("OverrideCommands", "true");
            state.setProperty("overrideWasFalse", "true");
        }

        // A pre-launch command from the Windows Toolscreen installer cannot work on Linux.
        String pre = ini.get("PreLaunchCommand", "");
        if (pre.contains("-jar") && pre.toLowerCase().contains("toolscreen")) {
            state.setProperty("removedPreLaunch", pre);
            ini.set("PreLaunchCommand", "");
            notes.add("Removed the Windows Toolscreen pre-launch command.");
        }

        String existing = stripOurWrapper(ini.get("WrapperCommand", ""));
        String ours = quoteArg(wrapper.getAbsolutePath());
        ini.set("WrapperCommand", existing.isEmpty() ? ours : ours + " " + existing);
        if (!existing.isEmpty()) {
            notes.add("Kept your existing wrapper command: " + existing);
        }
        ini.save(cfgFile);
        saveState(folder, state);
        importWindowsConfigIfPresent(notes);
        try {
            notes.add(buildRecommended(inst));
        } catch (IOException e) {
            notes.add("Could not build the recommended settings: " + e.getMessage());
        }

        StringBuilder sb = new StringBuilder();
        sb.append("Toolscreen is set up for ").append(inst.name).append(" (").append(inst.launcher.displayName).append(").");
        for (String n : notes) {
            sb.append("\n").append(n);
        }
        sb.append("\nConfig file: ").append(shortPath(configFile().getPath()));
        return sb.toString();
    }

    static String uninstall(Instance inst) throws IOException {
        File folder = new File(inst.dir, FOLDER);
        File cfgFile = new File(inst.dir, "instance.cfg");
        Properties state = loadState(folder);
        IniDoc ini = IniDoc.load(cfgFile);
        ini.set("WrapperCommand", stripOurWrapper(ini.get("WrapperCommand", "")));
        if ("true".equals(state.getProperty("overrideWasFalse"))) {
            // Restore the instance to how it was, unless the commands were edited since installing.
            IniDoc global = inst.launcher.globalConfig();
            boolean sameAsGlobal = true;
            for (String key : new String[] {"PreLaunchCommand", "WrapperCommand", "PostExitCommand"}) {
                String g = global != null ? global.get(key, "") : "";
                if (!ini.get(key, "").equals(g)) {
                    sameAsGlobal = false;
                }
            }
            if (sameAsGlobal) {
                ini.set("OverrideCommands", "false");
                for (String key : new String[] {"PreLaunchCommand", "WrapperCommand", "PostExitCommand"}) {
                    String orig = state.getProperty("orig." + key);
                    if (ABSENT.equals(orig)) {
                        ini.remove(key);
                    } else if (orig != null) {
                        ini.set(key, orig);
                    }
                }
            }
        } else if (state.getProperty("removedPreLaunch") != null && ini.get("PreLaunchCommand", "").isEmpty()) {
            ini.set("PreLaunchCommand", state.getProperty("removedPreLaunch"));
        }
        ini.save(cfgFile);
        deleteRecursive(folder);
        return "Toolscreen was removed from " + inst.name + ".\nYour config in " + shortPath(configFile().getParent()) + " was kept.";
    }

    /** Removes our wrapper (first argument) from a wrapper command line, leaving any chained wrapper. */
    static String stripOurWrapper(String cmd) {
        List<String> args = splitArgs(cmd);
        if (!args.isEmpty() && args.get(0).endsWith(FOLDER + "/" + WRAPPER)) {
            args.remove(0);
            StringBuilder sb = new StringBuilder();
            for (String a : args) {
                if (sb.length() > 0) {
                    sb.append(' ');
                }
                sb.append(quoteArg(a));
            }
            return sb.toString();
        }
        return cmd.trim();
    }

    static String quoteArg(String s) {
        for (char c : s.toCharArray()) {
            if (Character.isWhitespace(c) || c == '"' || c == '\'' || c == '\\') {
                return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
            }
        }
        return s;
    }

    /** Shell-like splitting matching the launchers' Commandline::splitArgs (double/single quotes, backslash). */
    static List<String> splitArgs(String s) {
        List<String> out = new ArrayList<String>();
        StringBuilder cur = new StringBuilder();
        boolean inArg = false;
        char quote = 0;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (quote != 0) {
                if (c == '\\' && quote == '"' && i + 1 < s.length()) {
                    cur.append(s.charAt(++i));
                } else if (c == quote) {
                    quote = 0;
                } else {
                    cur.append(c);
                }
            } else if (c == '"' || c == '\'') {
                quote = c;
                inArg = true;
            } else if (c == '\\' && i + 1 < s.length()) {
                cur.append(s.charAt(++i));
                inArg = true;
            } else if (Character.isWhitespace(c)) {
                if (inArg) {
                    out.add(cur.toString());
                    cur.setLength(0);
                    inArg = false;
                }
            } else {
                cur.append(c);
                inArg = true;
            }
        }
        if (inArg) {
            out.add(cur.toString());
        }
        return out;
    }

    /** If no Linux config exists yet but a Windows Toolscreen config is found in a Wine prefix, copy it. */
    static void importWindowsConfigIfPresent(List<String> notes) {
        File target = configFile();
        if (target.exists()) {
            return;
        }
        File users = new File(System.getProperty("user.home"), ".wine/drive_c/users");
        File[] dirs = users.listFiles();
        if (dirs == null) {
            return;
        }
        for (File u : dirs) {
            for (String sub : new String[] {".config/toolscreen/config.toml", ".config/Toolscreen/config.toml"}) {
                File f = new File(u, sub);
                if (f.isFile()) {
                    try {
                        target.getParentFile().mkdirs();
                        copy(f, target);
                        notes.add("Imported your Windows Toolscreen config from " + f.getPath());
                    } catch (IOException ignored) {
                    }
                    return;
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Recommended settings, built once per computer at install time and loaded from the in-game Defaults tab.
    // ------------------------------------------------------------------
    static File recommendedFile() {
        return new File(configFile().getParentFile(), "recommended.toml");
    }

    /** Minecraft's options.txt from the instance's game folder (minecraft/ or .minecraft/). */
    static Map<String, String> readMinecraftOptions(File instDir) {
        Map<String, String> out = new LinkedHashMap<String, String>();
        for (String sub : new String[] {"minecraft/options.txt", ".minecraft/options.txt"}) {
            File f = new File(instDir, sub);
            if (!f.isFile()) {
                continue;
            }
            for (String line : readSmall(f).split("\r?\n")) {
                int c = line.indexOf(':');
                if (c > 0) {
                    out.put(line.substring(0, c), line.substring(c + 1));
                }
            }
            break;
        }
        return out;
    }

    /** Minecraft's automatic GUI scale for a window size (same loop as the game). */
    static int autoGuiScale(int w, int h) {
        int i = 1;
        while (i < w && i < h && w / (i + 1) >= 320 && h / (i + 1) >= 240) {
            i++;
        }
        return i;
    }

    /** Perfect Travel measuring FOV for a window size (MCSR wiki table), or 0 when the size is not listed. */
    static int perfectTravelFov(int w, int h, boolean fullscreen) {
        if (w == 1920 && h == 1080) {
            return fullscreen ? 31 : 30;  // windowed 1080p monitors give a 1920x1027 game window
        }
        if (w == 2560 && h == 1440) {
            return fullscreen ? 31 : 30;  // windowed: 2560x1387
        }
        return 0;
    }

    static String tomlString(String s) {
        return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    static String resourceText(String name) throws IOException {
        InputStream in = Installer.class.getResourceAsStream(name);
        if (in == null) {
            throw new IOException("missing resource " + name);
        }
        return new String(readAll(in), UTF8);
    }

    /** Builds ~/.config/toolscreen/recommended.toml for this computer and returns a one-line summary. */
    static String buildRecommended(Instance inst) throws IOException {
        Map<String, String> mc = readMinecraftOptions(inst.dir);
        int screenW = 1920;
        int screenH = 1080;
        String forced = System.getProperty("toolscreen.screen");  // testing: -Dtoolscreen.screen=2560x1440
        if (forced != null && forced.matches("\\d+x\\d+")) {
            screenW = Integer.parseInt(forced.split("x")[0]);
            screenH = Integer.parseInt(forced.split("x")[1]);
        } else if (!GraphicsEnvironment.isHeadless()) {
            java.awt.DisplayMode dm = GraphicsEnvironment.getLocalGraphicsEnvironment().getDefaultScreenDevice().getDisplayMode();
            screenW = dm.getWidth();
            screenH = dm.getHeight();
        }
        String sensText = mc.containsKey("mouseSensitivity") ? mc.get("mouseSensitivity").trim() : "0.5";
        double sens = Double.parseDouble(sensText);
        int guiScaleSetting = mc.containsKey("guiScale") ? Integer.parseInt(mc.get("guiScale").trim()) : 0;
        int gui = guiScaleSetting > 0 ? Math.min(guiScaleSetting, autoGuiScale(screenW, screenH)) : autoGuiScale(screenW, screenH);
        boolean fullscreen = "true".equals(mc.containsKey("fullscreen") ? mc.get("fullscreen").trim() : "true");
        int tallHeight = 16384;
        // Measuring FOV (not the gameplay FOV in options.txt). With EyeZoom and Ninjabrain Bot's "Tall resolution"
        // adjustment it must be 30: Ninjabrain Bot computes a pixel's angle with tan(15 deg), half of 30.
        int eyezoomFov = 30;
        // Perfect Travel (subpixel adjustment without EyeZoom) depends on the window size.
        int ptFov = perfectTravelFov(screenW, screenH, fullscreen);
        // Ninjabrain Bot README: crosshair correction only for 1440p fullscreen at GUI scale 3 (0.026) or 6/auto (0.104).
        String crosshair = "0";
        if (screenW == 2560 && screenH == 1440 && fullscreen) {
            if (gui == 3) {
                crosshair = "0.026";
            } else if (gui == 6) {
                crosshair = "0.104";
            }
        }

        // Toolscreen: the official defaults, fitted to this screen, plus the Linux-port extras.
        String t = resourceText("/defaults/default.toml");
        t = t.replace("stretch = { enabled = true, height = 1440, width = 2560, x = 0, y = 0 }",
                "stretch = { enabled = true, height = " + screenH + ", width = " + screenW + ", x = 0, y = 0 }");
        t = t.replace("fontPath = 'c:\\Windows\\Fonts\\Arial.ttf'", "fontPath = ''");
        t = t.replace("guiHotkey = [162, 73]", "guiHotkey = [162, 73]\nninjabrainOverlayHotkey = [ 80 ]  # P shows/hides the Ninjabrain Bot panel");
        int nb = t.indexOf("[ninjabrainOverlay]");
        int en = t.indexOf("enabled = false", nb);
        if (nb >= 0 && en > nb) {
            t = t.substring(0, en) + "enabled = true\nshowOnlyAfterMeasurement = true\nhideInMenus = true" + t.substring(en + "enabled = false".length());
            // Top right, so the F3 coordinates in the top left stay visible.
            int rel = t.indexOf("relativeTo = 'topLeftScreen'", nb);
            int next = t.indexOf("\n[", nb + 1);
            if (rel > nb && (next < 0 || rel < next)) {
                t = t.substring(0, rel) + "relativeTo = 'topRightScreen'" + t.substring(rel + "relativeTo = 'topLeftScreen'".length());
            }
        }
        t = t.replace(", { type = 'Image', id = 'Ninjabrain Bot' }", "");
        // B toggles a low mouse sensitivity for measuring: the multiplier that matches Minecraft's slider at 0%.
        // Minecraft's speed factor is (s*0.6+0.2)^3*8; Toolscreen scales in whole counts, so boat eye still works.
        double cur = Math.pow(sens * 0.6 + 0.2, 3) * 8;
        double low = Math.pow(0.2, 3) * 8;
        String lowMult = String.format(java.util.Locale.ROOT, "%.4f", cur > 0 ? low / cur : 1.0);
        t = t.trim() + "\n\n# B toggles a low mouse sensitivity for measuring eyes (feels like Minecraft's slider at 0%).\n"
                + "# Minecraft's own sensitivity is not changed. Not while F3 is held, so F3+B still shows hitboxes.\n"
                + "[[sensitivityHotkey]]\nkeys = [ 66 ]\nsensitivity = " + lowMult + "\nseparateXY = false\nsensitivityX = 1.0\n"
                + "sensitivityY = 1.0\ntoggle = true\ndebounce = 100\n    [sensitivityHotkey.conditions]\n    exclusions = [ 114 ]\n"
                + "    gameState = []\n";
        // OneShot mode, with the dragon health bar mirror sized for this GUI scale (bar: 182x5 GUI pixels, 12 from the top).
        String oneshot = resourceText("/defaults/oneshot.toml")
                .replace("captureWidth = 364", "captureWidth = " + (182 * gui))
                .replace("captureHeight = 10", "captureHeight = " + (5 * gui))
                .replace("relativeTo = 'topCenterViewport', x = 0, y = 24", "relativeTo = 'topCenterViewport', x = 0, y = " + (12 * gui));

        // Ninjabrain Bot, as its README recommends for adjusting measured angles at a tall (EyeZoom) resolution.
        StringBuilder nbPrefs = new StringBuilder();
        nbPrefs.append("[recommendedNinjabrainBot]\n");
        nbPrefs.append("# Written to ~/.java/.userPrefs/ninjabrainbot/prefs.xml by the Defaults tab (Ninjabrain Bot closed).\n");
        nbPrefs.append("sensitivity = ").append(tomlString(String.format(java.util.Locale.ROOT, "%.10f", sens))).append("  # Minecraft's mouseSensitivity\n");
        nbPrefs.append("angle_adjustment_type = '1'  # Tall resolution: each adjustment is one pixel\n");
        nbPrefs.append("resolution_height = '").append(tallHeight).append("'  # EyeZoom height\n");
        nbPrefs.append("angle_adjustment_display_type = '1'  # show the number of adjustments\n");
        nbPrefs.append("hotkey_increment_code = '65549'  # = adds one pixel to the last angle\n");
        nbPrefs.append("hotkey_increment_modifier = '0'\n");
        nbPrefs.append("hotkey_decrement_code = '65548'  # - removes one pixel\n");
        nbPrefs.append("hotkey_decrement_modifier = '0'\n");
        nbPrefs.append("enable_http_server = 'true'  # the live panel in Toolscreen\n");
        nbPrefs.append("show_angle_errors = 'true'\n");
        nbPrefs.append("sigma = '0.007'  # README: 0.005-0.010 for adjusted measuring; a calibrated value is kept\n");
        nbPrefs.append("crosshair_correction = '").append(crosshair).append("'  # README: only 1440p fullscreen at GUI scale 3 or 6 needs one\n");
        nbPrefs.append("use_precise_angle = 'true'  # boat measurements\n");
        nbPrefs.append("default_boat_type = '1'  # Blue boat (boat-eye guides: green needs an advanced setup)\n");
        nbPrefs.append("boat_error = '0.03'  # allowable boat angle error (boat-eye guide)\n");
        nbPrefs.append("sigma_boat = '0.007'  # standard deviation for boat throws (boat-eye guide)\n");
        nbPrefs.append("use_adv_statistics = 'true'  # advanced stronghold statistics\n");

        StringBuilder info = new StringBuilder();
        info.append("\n[recommendedInfo]\n");
        info.append("generatedBy = ").append(tomlString("Toolscreen for Linux installer " + VERSION)).append("\n");
        info.append("generatedOn = ").append(tomlString(new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm").format(new java.util.Date()))).append("\n");
        info.append("instance = ").append(tomlString(inst.name)).append("\n");
        info.append("screen = ").append(tomlString(screenW + "x" + screenH)).append("\n");
        info.append("minecraftSensitivity = ").append(tomlString(sensText)).append("\n");
        info.append("guiScale = ").append(gui).append("\n");
        info.append("fullscreen = ").append(fullscreen).append("\n");
        info.append("measuringFovEyeZoom = ").append(eyezoomFov).append("  # FOV to measure eyes at with EyeZoom\n");
        info.append("measuringFovPerfectTravel = ").append(ptFov).append("  # without EyeZoom; 0 = not listed for this resolution\n\n");

        String doc = "# Recommended Toolscreen settings for this computer, built by the installer on "
                + new java.text.SimpleDateFormat("yyyy-MM-dd").format(new java.util.Date())
                + ".\n# Load them in game: Ctrl+I > Defaults. Re-run the installer to rebuild them.\n\n"
                + t.trim() + "\n\n" + oneshot.trim() + "\n" + info + nbPrefs;
        File out = recommendedFile();
        out.getParentFile().mkdirs();
        try (OutputStream o = new FileOutputStream(out)) {
            o.write(doc.getBytes(UTF8));
        }
        String summary = "Built recommended settings (" + screenW + "x" + screenH + ", GUI scale " + gui + ", sensitivity " + sensText
                + ", measure at FOV " + eyezoomFov + ") in " + shortPath(out.getPath());
        if (sens < 0.01) {
            summary += ". WARNING: Minecraft's mouse sensitivity is at 0 (lowered for measuring?). Set it back to your normal "
                    + "value and run the installer again, or the recommended sensitivity settings will be wrong";
        }
        File cfg = configFile();
        if (!cfg.exists()) {
            try (OutputStream o = new FileOutputStream(cfg)) {
                o.write(doc.getBytes(UTF8));
            }
            summary += "; also used as your starting config";
        }
        return summary;
    }

    static File configFile() {
        String xdg = System.getenv("XDG_CONFIG_HOME");
        File base = (xdg != null && !xdg.isEmpty()) ? new File(xdg) : new File(System.getProperty("user.home"), ".config");
        return new File(new File(base, "toolscreen"), "config.toml");
    }

    static Properties loadState(File folder) {
        Properties p = new Properties();
        File f = new File(folder, "install.properties");
        if (f.isFile()) {
            try (InputStream in = new FileInputStream(f)) {
                p.load(in);
            } catch (IOException ignored) {
            }
        }
        return p;
    }

    static void saveState(File folder, Properties p) throws IOException {
        try (OutputStream out = new FileOutputStream(new File(folder, "install.properties"))) {
            p.store(out, "Toolscreen for Linux install state");
        }
    }

    // ------------------------------------------------------------------
    // Instance and launcher discovery
    // ------------------------------------------------------------------
    enum LauncherKind {
        // Prism and PolyMC first: their data folders can keep a multimc.cfg left over from migrating.
        PRISM("Prism Launcher", "prismlauncher.cfg"), POLYMC("PolyMC", "polymc.cfg"), MULTIMC("MultiMC", "multimc.cfg"),
        UNKNOWN("Launcher", null);

        final String displayName;
        final String cfgName;

        LauncherKind(String displayName, String cfgName) {
            this.displayName = displayName;
            this.cfgName = cfgName;
        }
    }

    static final class Launcher {
        final LauncherKind kind;
        final File dataDir;
        final boolean flatpak;
        final String displayName;

        Launcher(LauncherKind kind, File dataDir) {
            this.kind = kind;
            this.dataDir = dataDir;
            this.flatpak = dataDir != null && dataDir.getPath().contains("/.var/app/");
            this.displayName = kind.displayName + (flatpak ? " (Flatpak)" : "");
        }

        IniDoc globalConfig() {
            if (kind.cfgName == null || dataDir == null) {
                return null;
            }
            File f = new File(dataDir, kind.cfgName);
            try {
                return f.isFile() ? IniDoc.load(f) : null;
            } catch (IOException e) {
                return null;
            }
        }

        static Launcher forInstanceDir(File instDir) {
            // <data dir>/<InstanceDir>/<instance>; InstanceDir may also be an absolute path elsewhere.
            File p = instDir.getParentFile();
            for (int depth = 0; p != null && depth < 3; depth++, p = p.getParentFile()) {
                for (LauncherKind k : LauncherKind.values()) {
                    if (k.cfgName != null && new File(p, k.cfgName).isFile()) {
                        return new Launcher(k, p);
                    }
                }
            }
            for (Launcher l : knownLaunchers()) {
                IniDoc g = l.globalConfig();
                if (g == null) {
                    continue;
                }
                File root = resolve(l.dataDir, g.get("InstanceDir", "instances"));
                if (root.equals(instDir.getParentFile())) {
                    return l;
                }
            }
            return new Launcher(LauncherKind.UNKNOWN, null);
        }
    }

    static final class Instance {
        final File dir;
        final String name;
        final Launcher launcher;

        Instance(File dir, String name, Launcher launcher) {
            this.dir = dir;
            this.name = name;
            this.launcher = launcher;
        }

        static Instance at(File dir) {
            File cfg = new File(dir, "instance.cfg");
            if (!cfg.isFile()) {
                return null;
            }
            String name = dir.getName();
            try {
                name = IniDoc.load(cfg).get("name", name);
            } catch (IOException ignored) {
            }
            return new Instance(dir, name, Launcher.forInstanceDir(dir));
        }

        @Override
        public String toString() {
            return name + "  —  " + launcher.displayName + (isInstalled(dir) ? "  (installed)" : "");
        }
    }

    static File resolve(File base, String path) {
        File f = new File(path);
        return f.isAbsolute() ? f : new File(base, path);
    }

    static List<Launcher> knownLaunchers() {
        String home = System.getProperty("user.home");
        String xdgData = System.getenv("XDG_DATA_HOME");
        if (xdgData == null || xdgData.isEmpty()) {
            xdgData = home + "/.local/share";
        }
        List<Launcher> out = new ArrayList<Launcher>();
        Object[][] candidates = {
            {LauncherKind.PRISM, xdgData + "/PrismLauncher"},
            {LauncherKind.PRISM, home + "/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher"},
            {LauncherKind.MULTIMC, xdgData + "/multimc"},
            {LauncherKind.MULTIMC, xdgData + "/MultiMC"},
            {LauncherKind.MULTIMC, home + "/MultiMC"},
            {LauncherKind.MULTIMC, home + "/.multimc"},
            {LauncherKind.POLYMC, xdgData + "/PolyMC"},
            {LauncherKind.POLYMC, home + "/.var/app/org.polymc.PolyMC/data/PolyMC"},
        };
        for (Object[] c : candidates) {
            LauncherKind k = (LauncherKind) c[0];
            File dir = new File((String) c[1]);
            if (new File(dir, k.cfgName).isFile() || new File(dir, "instances").isDirectory()) {
                out.add(new Launcher(k, dir));
            }
        }
        return out;
    }

    static List<Instance> findAllInstances() {
        List<Instance> out = new ArrayList<Instance>();
        for (Launcher l : knownLaunchers()) {
            IniDoc g = l.globalConfig();
            File root = resolve(l.dataDir, g != null ? g.get("InstanceDir", "instances") : "instances");
            File[] dirs = root.listFiles();
            if (dirs == null) {
                continue;
            }
            java.util.Arrays.sort(dirs);
            for (File d : dirs) {
                if (d.isDirectory() && !d.getName().startsWith("_") && new File(d, "instance.cfg").isFile()) {
                    String name = d.getName();
                    try {
                        name = IniDoc.load(new File(d, "instance.cfg")).get("name", name);
                    } catch (IOException ignored) {
                    }
                    out.add(new Instance(d, name, l));
                }
            }
        }
        return out;
    }

    static Instance detectInstanceFromJarLocation() {
        List<File> candidates = new ArrayList<File>();
        File jar = jarFile();
        if (jar != null) {
            candidates.add(jar.getParentFile());
        }
        candidates.add(new File(System.getProperty("user.dir")));
        for (File c : candidates) {
            // Accept the instance folder itself or its game directory (.minecraft / minecraft).
            for (File d = c; d != null; d = d.getParentFile()) {
                Instance inst = Instance.at(d);
                if (inst != null) {
                    return inst;
                }
                if (!d.getName().equals(".minecraft") && !d.getName().equals("minecraft") && d != c) {
                    break;
                }
            }
        }
        return null;
    }

    static File jarFile() {
        try {
            String path = Installer.class.getProtectionDomain().getCodeSource().getLocation().getPath();
            return new File(URLDecoder.decode(path, "UTF-8")).getAbsoluteFile();
        } catch (Exception e) {
            return null;
        }
    }

    static String jarName() {
        File j = jarFile();
        return j != null ? j.getName() : "Toolscreen.jar";
    }

    /** Returns comma-separated PIDs of a running launcher of the given kind, or null. */
    static String runningLauncher(Launcher launcher) {
        File[] procs = new File("/proc").listFiles();
        if (procs == null) {
            return null;
        }
        StringBuilder pids = new StringBuilder();
        long self = selfPid();
        for (File p : procs) {
            String name = p.getName();
            if (!name.matches("\\d+") || Long.parseLong(name) == self) {
                continue;
            }
            String comm = readSmall(new File(p, "comm")).trim();
            String cmdline = readSmall(new File(p, "cmdline")).replace('\0', ' ');
            boolean match;
            switch (launcher.kind) {
                case PRISM:
                    match = comm.equalsIgnoreCase("prismlauncher") || (comm.equals("bwrap") && cmdline.contains("org.prismlauncher"));
                    break;
                case MULTIMC:
                    match = comm.equals("MultiMC");
                    break;
                case POLYMC:
                    match = comm.equalsIgnoreCase("polymc");
                    break;
                default:
                    match = comm.equalsIgnoreCase("prismlauncher") || comm.equals("MultiMC") || comm.equalsIgnoreCase("polymc");
            }
            if (match) {
                if (pids.length() > 0) {
                    pids.append(',');
                }
                pids.append(name);
            }
        }
        return pids.length() > 0 ? pids.toString() : null;
    }

    static long selfPid() {
        try {
            return Long.parseLong(new File("/proc/self").getCanonicalFile().getName());
        } catch (Exception e) {
            return -1;
        }
    }

    // ------------------------------------------------------------------
    // instance.cfg handling: Prism writes QSettings INI ([General] section, Qt escaping); MultiMC writes a
    // section-less key=value file with its own escaping. Lines are edited in place to keep everything else.
    // ------------------------------------------------------------------
    static final class IniDoc {
        final List<String> lines = new ArrayList<String>();
        boolean qsettings;

        static IniDoc load(File f) throws IOException {
            IniDoc d = new IniDoc();
            String text = new String(readAll(new FileInputStream(f)), UTF8);
            for (String l : text.split("\r?\n", -1)) {
                d.lines.add(l);
            }
            if (!d.lines.isEmpty() && d.lines.get(d.lines.size() - 1).isEmpty()) {
                d.lines.remove(d.lines.size() - 1);
            }
            for (String l : d.lines) {
                String t = l.trim();
                if (t.startsWith("[") || t.startsWith("ConfigVersion=")) {
                    d.qsettings = true;
                    break;
                }
            }
            return d;
        }

        /** Index range [start, end) of the top-level (General) keys. */
        private int[] generalRange() {
            if (!qsettings) {
                return new int[] {0, lines.size()};
            }
            int start = -1;
            int end = lines.size();
            for (int i = 0; i < lines.size(); i++) {
                String t = lines.get(i).trim();
                if (t.startsWith("[") && t.endsWith("]")) {
                    if (start >= 0) {
                        end = i;
                        break;
                    }
                    if (t.equals("[General]")) {
                        start = i + 1;
                    }
                }
            }
            if (start < 0) {
                // No [General] header: keys before the first section are top-level.
                start = 0;
                for (int i = 0; i < lines.size(); i++) {
                    if (lines.get(i).trim().startsWith("[")) {
                        end = i;
                        break;
                    }
                }
            }
            return new int[] {start, end};
        }

        private int find(String key) {
            int[] r = generalRange();
            for (int i = r[0]; i < r[1]; i++) {
                String l = lines.get(i);
                int eq = l.indexOf('=');
                if (eq > 0 && l.substring(0, eq).trim().equals(key)) {
                    return i;
                }
            }
            return -1;
        }

        boolean has(String key) {
            return find(key) >= 0;
        }

        void remove(String key) {
            int i = find(key);
            if (i >= 0) {
                lines.remove(i);
            }
        }

        String get(String key, String def) {
            int i = find(key);
            if (i < 0) {
                return def;
            }
            String raw = lines.get(i).substring(lines.get(i).indexOf('=') + 1);
            return qsettings ? qtUnescape(raw.trim()) : mmcUnescape(raw);
        }

        void set(String key, String value) {
            String encoded = qsettings ? qtEscape(value) : mmcEscape(value);
            int i = find(key);
            String line = key + "=" + encoded;
            if (i >= 0) {
                lines.set(i, line);
                return;
            }
            int[] r = generalRange();
            int insertAt = r[1];
            while (insertAt > r[0] && lines.get(insertAt - 1).trim().isEmpty()) {
                insertAt--;
            }
            if (qsettings && r[0] == 0 && lines.isEmpty()) {
                lines.add("[General]");
                insertAt = 1;
            }
            lines.add(insertAt, line);
        }

        void save(File f) throws IOException {
            StringBuilder sb = new StringBuilder();
            for (String l : lines) {
                sb.append(l).append('\n');
            }
            File tmp = new File(f.getPath() + ".toolscreen-tmp");
            try (OutputStream out = new FileOutputStream(tmp)) {
                out.write(sb.toString().getBytes(UTF8));
            }
            if (!tmp.renameTo(f)) {
                throw new IOException("Cannot write " + f);
            }
        }

        static String mmcEscape(String s) {
            StringBuilder sb = new StringBuilder();
            for (char c : s.toCharArray()) {
                if (c == '\n') {
                    sb.append("\\n");
                } else if (c == '\t') {
                    sb.append("\\t");
                } else if (c == '\\') {
                    sb.append("\\\\");
                } else if (c == '#') {
                    sb.append("\\#");
                } else {
                    sb.append(c);
                }
            }
            return sb.toString();
        }

        static String mmcUnescape(String s) {
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < s.length(); i++) {
                char c = s.charAt(i);
                if (c == '\\' && i + 1 < s.length()) {
                    char n = s.charAt(++i);
                    sb.append(n == 'n' ? '\n' : n == 't' ? '\t' : n);
                } else {
                    sb.append(c);
                }
            }
            return sb.toString();
        }

        /** QSettings::iniEscapedString for plain strings. */
        static String qtEscape(String s) {
            StringBuilder sb = new StringBuilder();
            boolean needsQuotes = false;
            for (char c : s.toCharArray()) {
                if (c == ';' || c == ',' || c == '=') {
                    needsQuotes = true;
                }
                switch (c) {
                    case '"':
                    case '\\':
                        sb.append('\\').append(c);
                        break;
                    case '\n':
                        sb.append("\\n");
                        break;
                    case '\t':
                        sb.append("\\t");
                        break;
                    case '\r':
                        sb.append("\\r");
                        break;
                    default:
                        sb.append(c);
                }
            }
            String out = sb.toString();
            if (out.startsWith("@")) {
                out = "@" + out;
            }
            if (needsQuotes || out.startsWith(" ") || out.endsWith(" ")) {
                out = "\"" + out + "\"";
            }
            return out;
        }

        static String qtUnescape(String s) {
            StringBuilder sb = new StringBuilder();
            boolean inQuotes = false;
            for (int i = 0; i < s.length(); i++) {
                char c = s.charAt(i);
                if (c == '"') {
                    inQuotes = !inQuotes;
                } else if (c == '\\' && i + 1 < s.length()) {
                    char n = s.charAt(++i);
                    switch (n) {
                        case 'n':
                            sb.append('\n');
                            break;
                        case 't':
                            sb.append('\t');
                            break;
                        case 'r':
                            sb.append('\r');
                            break;
                        case 'x': {
                            int j = i + 1;
                            while (j < s.length() && j < i + 5 && Character.digit(s.charAt(j), 16) >= 0) {
                                j++;
                            }
                            if (j > i + 1) {
                                sb.append((char) Integer.parseInt(s.substring(i + 1, j), 16));
                                i = j - 1;
                            }
                            break;
                        }
                        default:
                            sb.append(n);
                    }
                } else {
                    sb.append(c);
                }
            }
            String out = sb.toString();
            if (out.startsWith("@@")) {
                out = out.substring(1);
            }
            return out;
        }
    }

    // ------------------------------------------------------------------
    // Small IO helpers
    // ------------------------------------------------------------------
    static void extract(String resource, File dest) throws IOException {
        InputStream in = Installer.class.getResourceAsStream(resource);
        if (in == null) {
            throw new IOException("Missing resource " + resource + " in the installer jar");
        }
        File tmp = new File(dest.getPath() + ".tmp");
        try (OutputStream out = new FileOutputStream(tmp)) {
            out.write(readAll(in));
        }
        // Rename over the old file so a running game keeps its mapped copy intact.
        if (!tmp.renameTo(dest)) {
            throw new IOException("Cannot write " + dest);
        }
    }

    static void copy(File src, File dest) throws IOException {
        try (OutputStream out = new FileOutputStream(dest)) {
            out.write(readAll(new FileInputStream(src)));
        }
    }

    static byte[] readAll(InputStream in) throws IOException {
        try {
            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) {
                bos.write(buf, 0, n);
            }
            return bos.toByteArray();
        } finally {
            in.close();
        }
    }

    static String readSmall(File f) {
        try {
            return new String(readAll(new FileInputStream(f)), UTF8);
        } catch (IOException e) {
            return "";
        }
    }

    static void deleteRecursive(File f) {
        File[] kids = f.listFiles();
        if (kids != null) {
            for (File k : kids) {
                deleteRecursive(k);
            }
        }
        f.delete();
    }

    static void sleep(long ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException ignored) {
        }
    }

    /** A path for display: the home folder as ~, and very long paths shortened in the middle. */
    static String shortPath(String p) {
        String home = System.getProperty("user.home");
        if (home != null && p.startsWith(home + "/")) {
            p = "~" + p.substring(home.length());
        }
        if (p.length() > 80) {
            p = p.substring(0, 36) + "…" + p.substring(p.length() - 40);
        }
        return p;
    }

    static String esc(String s) {
        return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;");
    }
}
