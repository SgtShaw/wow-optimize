using System;
using System.IO;
using System.Diagnostics;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace WowOptimizeLauncher {
    public class App : Application {
        [STAThread]
        public static void Main() {
            App app = new App();
            app.Run(new MainWindow());
        }
    }

    public class SettingItem {
        public string Section;
        public string Key;
        public bool DefaultVal;
        public CheckBox Ctrl;
        public string Tooltip;

        public SettingItem(string section, string key, bool defaultVal, CheckBox ctrl, string tooltip) {
            Section = section;
            Key = key;
            DefaultVal = defaultVal;
            Ctrl = ctrl;
            Tooltip = tooltip;
        }
    }

    public class MainWindow : Window {
        private string iniPath;
        private Dictionary<string, SettingItem> settingsMap;

        // Custom UI Controls
        private StackPanel generalPanel;
        private StackPanel uiLuaPanel;
        private StackPanel combatNetPanel;
        private StackPanel graphicsSoundPanel;
        private TabControl tabs;
        private TextBlock versionText;

        public MainWindow() {
            // Setup Paths
            string exeDir = AppDomain.CurrentDomain.BaseDirectory;
            iniPath = System.IO.Path.Combine(exeDir, "wow_opt.ini");

            // Window Setup
            Title = "WoW-Optimize Launcher";
            Width = 920;
            Height = 650;
            WindowStartupLocation = WindowStartupLocation.CenterScreen;
            Background = new SolidColorBrush(Color.FromRgb(15, 15, 22));
            Foreground = Brushes.White;
            FontFamily = new FontFamily("Segoe UI, Roboto, sans-serif");
            ResizeMode = ResizeMode.NoResize;
            WindowStyle = WindowStyle.None;
            AllowsTransparency = true;

            // Define settings mapping using the SettingItem helper class
            settingsMap = new Dictionary<string, SettingItem>() {
                // General
                { "Precise Sleep Frame Pacing", new SettingItem("General", "SleepPrecision", true, null, "Enforces millisecond-accurate frame-rate sleep pacing to reduce input lag and stabilize frame delivery.") },
                { "Memory Pressure Governor", new SettingItem("General", "MemoryPressure", true, null, "Sheds caches and adjusts texture footprint dynamically under critical 32-bit virtual address (VA) space limits.") },
                { "Heap Compactor", new SettingItem("General", "HeapCompactor", true, null, "Defragments the client heap every 5 seconds to prevent Out-Of-Memory (OOM) crashes during teleports.") },
                { "Lock-Free Heap Defragmenter", new SettingItem("General", "DefragLf", false, null, "Experimental defragmentation on the main thread using lock-free structures. Bypasses standard heap serialization.") },
                { "Mimalloc Allocator Redirection", new SettingItem("General", "Allocators", false, null, "Redirects static CRT allocations to mimalloc. Helps resolve long loading screens and address space fragmentation.") },
                { "D3D9Ex Vulkan DXVK Support", new SettingItem("General", "VulkanDXVK", false, null, "Optimizes DLL hook integration to work cleanly with DXVK (requires placing a d3d9.dll Vulkan wrapper in the game folder).") },
                { "High-Precision Timing Fix", new SettingItem("General", "TimingFix", false, null, "Overrides GetTickCount and timeGetTime to use QPC, preventing micro-stutters and timer drift.") },
                { "Null Pointer CVar Safeguard", new SettingItem("General", "CvarNullGuard", true, null, "Critical safety hooks to prevent client crashes caused by uninitialized global variables and CVars.") },
                { "Frame Rate Limiter Override", new SettingItem("General", "FrameLimiter", false, null, "Overrides WoW's built-in frame limiter with a high-precision spin-wait sleep loop.") },

                // UI & Lua
                { "UI Update Batching", new SettingItem("UI_Lua", "UIFrameBatch", false, null, "Aggregates frame ticks to batch multiple addon OnUpdate calls, lowering CPU usage in intensive UI scenes.") },
                { "Addon Dispatcher Parallelization", new SettingItem("UI_Lua", "AddonDispatcher", false, null, "Dispatches addon script execution tasks to a background thread pool. Boosts frames in raid setups.") },
                { "Fast UI Frame Accessors", new SettingItem("UI_Lua", "UIFrameAccessorFast", false, null, "Bypasses standard Lua stack queries to retrieve UI frame parameters (IsShown, GetAlpha) instantly.") },
                { "Fast FontString Metrics", new SettingItem("UI_Lua", "FontMetricsFast", false, null, "Provides high-speed text width and height measurements, reducing layout calculation time.") },
                { "Lock-Free Font Metrics", new SettingItem("UI_Lua", "FontMetricsLockFree", false, null, "Uses a read-copy-update styled font metrics cache to eliminate font locks during rendering.") },
                { "Coalesced FrameXML Updates", new SettingItem("UI_Lua", "FrameXmlCoalesce", false, null, "Deduplicates multiple layout recalculations in a single frame tick, stopping layout micro-freezes.") },
                { "Addon Tick Governor", new SettingItem("UI_Lua", "AddonTickGovernor", false, null, "Caps excessive addon update execution rates to prevent CPU bottlenecks.") },
                { "Tooltip Cache", new SettingItem("UI_Lua", "TooltipCache", false, null, "Caches formatted tooltips (spells, items) to avoid invoking slow script layout logic repeatedly.") },
                { "Lua File Reading Cache", new SettingItem("UI_Lua", "LuaFileCache", false, null, "Keeps parsed Lua scripts in memory, bypassing disk disk reads and string parsing on UI reloads.") },
                { "FrameScript FNV-1a Dispatcher", new SettingItem("UI_Lua", "FrameScriptDispatch", false, null, "Uses an O(1) hash map lookup for script handlers instead of linear string matching.") },
                { "Lua Number Conversion Fast Path", new SettingItem("UI_Lua", "LuaNumConvFast", false, null, "Inlines common Lua stack value queries (tonumber, gettop, settop) to bypass stack checking overhead.") },
                { "Lua VM Table Get/Set Cache", new SettingItem("UI_Lua", "LuaOpcache", false, null, "Provides a fast lookup cache for VM table indexing, bypassing the full interpreter loop.") },
                { "Coalesced Lua Garbage Collection", new SettingItem("UI_Lua", "LuaGcCoalesce", false, null, "Bundles tiny incremental garbage collection steps to execute during empty frame budgets.") },
                { "Lua VM Bytecode JIT Compiler", new SettingItem("UI_Lua", "LuaJIT", false, null, "Hooks standard call preparation to redirect and execute compiled Lua bytecode JIT stubs.") },

                // Combat & Net
                { "Aggregated Combat Log Parser", new SettingItem("Combat_Net", "CombatLogParser", false, null, "C++ level combat log aggregator that intercepts and summarizes events, bypassing slow Lua parsers.") },
                { "Incremental Combat Log parsing", new SettingItem("Combat_Net", "CombatLogIncremental", false, null, "Splits large combat updates into small steps, preventing massive spikes in large-scale combat.") },
                { "Event Coalescing", new SettingItem("Combat_Net", "EventCoalescer", false, null, "Combines duplicate UI/combat event notifications within the same frame to prevent event spam.") },
                { "Coalesced Network Packets", new SettingItem("Combat_Net", "NetPacketCoalesce", false, null, "Groups incoming game packets before processing to reduce context switching and network thread latency.") },
                { "Unit Aura Coalescing", new SettingItem("Combat_Net", "UnitAuraCoalesce", false, null, "Batches player/target buff updates to minimize frame lag when many auras refresh concurrently.") },
                { "Addon Message Coalescing", new SettingItem("Combat_Net", "NetAddonCoalescer", false, null, "Groups chat/addon network communications to reduce the volume of individual messages.") },
                { "SavedVariables Serializer", new SettingItem("Combat_Net", "SavedVarsSerializer", false, null, "Serializes and writes addon variables incrementally in a lock-free worker thread.") },
                { "SavedVariables Async Writer", new SettingItem("Combat_Net", "SavedVarsAsync", false, null, "Performs SavedVariables file flushing asynchronously in the background to prevent logout freezes.") },
                { "SavedVariables Preloader", new SettingItem("Combat_Net", "SavedVarsPretoken", false, null, "Pre-loads and parses addon configuration files during the early loading screen sequence.") },
                { "UnitAura Fast Path", new SettingItem("Combat_Net", "UnitAuraFast", false, null, "Vectorizes UnitAura query evaluations at the C++ level to speed up unit frames updates.") },
                { "SSE2 Network GUID Unpacking", new SettingItem("Combat_Net", "NetworkGuidSse2", false, null, "Vectorizes the unpacking of network entity GUIDs inside network data streams.") },
                { "GetSpellInfo Cache", new SettingItem("Combat_Net", "GetSpellInfoCache", false, null, "Caches spells details to prevent repeated DBC lookups by complex combat macros and WA addons.") },
                { "Parallel Packet Offloader", new SettingItem("Combat_Net", "PacketOffload", false, null, "Offloads incoming network packet decompression and deserialization to helper cores.") },
                { "Multithreaded Nameplate Renderer", new SettingItem("Combat_Net", "NameplateMT", false, null, "Calculates nameplate positions and layouts on background threads to maximize combat FPS.") },

                // Graphics & Sound
                { "SSE2 Boyer-Moore strstr", new SettingItem("Graphics_Sound", "StrStrSse2", false, null, "Optimizes string sub-searches (such as font names, textures) using vectorized SIMD algorithms.") },
                { "Vectorized String Concatenation", new SettingItem("Graphics_Sound", "StrCatFast", false, null, "Speeds up string appending (such as chat text building) using SSE2 assembly wrappers.") },
                { "FMOD Sound Mixer Optimization", new SettingItem("Graphics_Sound", "SoundMixerOpt", false, null, "Adjusts audio thread schedules and buffer allocations to prevent sound stutters in raids.") },
                { "Parallel Sound Wave Decoding", new SettingItem("Graphics_Sound", "AudioDecodeMt", false, null, "Decodes sound assets in background threads to eliminate latency when playing fresh audio clips.") },
                { "DBC Data Lookup Cache", new SettingItem("Graphics_Sound", "DbcLookupCache", false, null, "Speeds up data reading from internal database files (.dbc) for models, items, and spells.") },
                { "WorldState Coalescing", new SettingItem("Graphics_Sound", "WorldStateCoalesce", false, null, "Batches high-frequency WorldState updates to prevent camera stutter during active battlegrounds.") }
            };

            // Build GUI Layout
            InitializeLayout();

            // Load Settings from INI
            LoadSettings();

            // Check for Updates in the background
            CheckForUpdatesAsync();
        }

        private void InitializeLayout() {
            // Main Window Grid
            Grid rootGrid = new Grid();
            AddChild(rootGrid);

            // Background Card (WotLK image background with fallback solid color)
            Border backgroundBorder = new Border {
                BorderThickness = new Thickness(1),
                BorderBrush = new SolidColorBrush(Color.FromRgb(30, 30, 45)),
                CornerRadius = new CornerRadius(0) // Strict square edges
            };

            // Attempt to load background image:
            // 1. First check if a custom file exists on disk
            // 2. Fallback to loading the embedded resource image
            string exeDir = AppDomain.CurrentDomain.BaseDirectory;
            string bgImagePath = System.IO.Path.Combine(exeDir, "wotlk_background.jpg");
            bool loadedBg = false;

            if (File.Exists(bgImagePath)) {
                try {
                    ImageBrush ib = new ImageBrush();
                    ib.ImageSource = new BitmapImage(new Uri(bgImagePath));
                    ib.Stretch = Stretch.UniformToFill;
                    backgroundBorder.Background = ib;
                    loadedBg = true;
                } catch {
                    // Fail over to resource
                }
            }

            if (!loadedBg) {
                try {
                    System.Reflection.Assembly asm = System.Reflection.Assembly.GetExecutingAssembly();
                    using (Stream stream = asm.GetManifestResourceStream("wotlk_background.jpg")) {
                        if (stream != null) {
                            BitmapImage bitmap = new BitmapImage();
                            bitmap.BeginInit();
                            bitmap.StreamSource = stream;
                            bitmap.CacheOption = BitmapCacheOption.OnLoad; // Allow closing the stream
                            bitmap.EndInit();

                            ImageBrush ib = new ImageBrush();
                            ib.ImageSource = bitmap;
                            ib.Stretch = Stretch.UniformToFill;
                            backgroundBorder.Background = ib;
                            loadedBg = true;
                        }
                    }
                } catch {
                    // Fail over to solid color
                }
            }

            if (!loadedBg) {
                backgroundBorder.Background = new SolidColorBrush(Color.FromRgb(12, 12, 18));
            }
            rootGrid.Children.Add(backgroundBorder);

            // Dark semi-transparent flat overlay to keep text highly legible (no gradient)
            Border overlayBorder = new Border {
                Background = new SolidColorBrush(Color.FromArgb(235, 12, 12, 18)),
                CornerRadius = new CornerRadius(0)
            };
            rootGrid.Children.Add(overlayBorder);

            // Layout columns: Left Panel (Dashboard), Right Panel (Tabs)
            Grid mainGrid = new Grid { Margin = new Thickness(10) };
            mainGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(280) });
            mainGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            rootGrid.Children.Add(mainGrid);

            // --- LEFT PANEL (CONTROL DASHBOARD) ---
            StackPanel leftPanel = new StackPanel {
                Margin = new Thickness(15, 10, 15, 10),
                VerticalAlignment = VerticalAlignment.Stretch
            };
            Grid.SetColumn(leftPanel, 0);
            mainGrid.Children.Add(leftPanel);

            // Title Header
            TextBlock headerText = new TextBlock {
                Text = "WOW OPTIMIZE",
                FontWeight = FontWeights.Bold,
                FontSize = 24,
                Foreground = new SolidColorBrush(Color.FromRgb(0, 229, 255)),
                Margin = new Thickness(0, 10, 0, 0)
            };
            leftPanel.Children.Add(headerText);

            // Developer Signature Subtitle
            TextBlock devText = new TextBlock {
                Text = "by Suprematist",
                FontStyle = FontStyles.Italic,
                FontSize = 11,
                Foreground = new SolidColorBrush(Color.FromRgb(0, 229, 255)),
                Margin = new Thickness(2, 0, 0, 4)
            };
            leftPanel.Children.Add(devText);

            TextBlock subHeaderText = new TextBlock {
                Text = "MOD CONFIGURATOR & LAUNCHER",
                FontWeight = FontWeights.Medium,
                FontSize = 10,
                Foreground = new SolidColorBrush(Color.FromRgb(150, 150, 180)),
                Margin = new Thickness(2, 0, 0, 20)
            };
            leftPanel.Children.Add(subHeaderText);

            // Master Control Buttons
            Button btnEnableAll = CreateStyledButton("ENABLE ALL FEATURES", Color.FromRgb(0, 229, 255), false);
            btnEnableAll.Click += (s, e) => ToggleAll(true);
            leftPanel.Children.Add(btnEnableAll);

            Button btnDisableAll = CreateStyledButton("DISABLE ALL (VANILLA)", Color.FromRgb(255, 23, 68), false);
            btnDisableAll.Click += (s, e) => ToggleAll(false);
            leftPanel.Children.Add(btnDisableAll);

            Button btnDefaults = CreateStyledButton("RESTORE SAFE DEFAULTS", Color.FromRgb(100, 110, 140), false);
            btnDefaults.Click += (s, e) => RestoreDefaults();
            leftPanel.Children.Add(btnDefaults);

            // Profile Buttons
            Button btnSaveProfile = CreateStyledButton("SAVE PROFILE...", Color.FromRgb(0, 229, 255), false);
            btnSaveProfile.Click += (s, e) => SaveProfile();
            leftPanel.Children.Add(btnSaveProfile);

            Button btnLoadProfile = CreateStyledButton("LOAD PROFILE...", Color.FromRgb(0, 229, 255), false);
            btnLoadProfile.Click += (s, e) => LoadProfile();
            leftPanel.Children.Add(btnLoadProfile);

            // Share Preset Configuration Button
            Button btnShareProfile = CreateStyledButton("SHARE WITH DEVELOPER", Color.FromRgb(255, 179, 0), false);
            btnShareProfile.Click += (s, e) => ShareProfileWithDev();
            leftPanel.Children.Add(btnShareProfile);

            // Fill space
            Border separator = new Border {
                Height = 1,
                Background = new SolidColorBrush(Color.FromRgb(30, 30, 45)),
                Margin = new Thickness(0, 10, 0, 10)
            };
            leftPanel.Children.Add(separator);

            // DLL Status Card
            Border statusCard = new Border {
                Background = new SolidColorBrush(Color.FromRgb(18, 18, 28)),
                BorderThickness = new Thickness(1),
                BorderBrush = new SolidColorBrush(Color.FromRgb(35, 35, 50)),
                CornerRadius = new CornerRadius(0), // Flat square corners
                Padding = new Thickness(12),
                Margin = new Thickness(0, 0, 0, 10)
            };
            StackPanel statusPanel = new StackPanel();
            TextBlock statusLabel = new TextBlock {
                Text = "MODULE STATUS:",
                FontSize = 10,
                FontWeight = FontWeights.Bold,
                Foreground = new SolidColorBrush(Color.FromRgb(140, 140, 170)),
                Margin = new Thickness(0, 0, 0, 4)
            };
            statusPanel.Children.Add(statusLabel);

            bool dllActive = File.Exists(System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "version.dll")) &&
                            File.Exists(System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "wow_optimize.dll"));

            TextBlock statusVal = new TextBlock {
                Text = dllActive ? "OPTIMIZER ACTIVE (version.dll)" : "NOT LOADED / MISSING DLLs",
                FontSize = 12,
                FontWeight = FontWeights.Bold,
                Foreground = dllActive ? new SolidColorBrush(Color.FromRgb(0, 230, 118)) : new SolidColorBrush(Color.FromRgb(255, 145, 0))
            };
            statusPanel.Children.Add(statusVal);
            statusCard.Child = statusPanel;
            leftPanel.Children.Add(statusCard);

            // LAUNCH BUTTON
            Button btnLaunch = CreateStyledButton("LAUNCH WOW", Color.FromRgb(0, 229, 255), true);
            btnLaunch.Height = 45;
            btnLaunch.FontSize = 15;
            btnLaunch.FontWeight = FontWeights.Bold;
            btnLaunch.Click += (s, e) => LaunchWow();
            leftPanel.Children.Add(btnLaunch);

            // Exit application button
            Button btnExit = CreateStyledButton("EXIT LAUNCHER", Color.FromRgb(60, 60, 70), false);
            btnExit.Margin = new Thickness(0, 5, 0, 0);
            btnExit.Click += (s, e) => Close();
            leftPanel.Children.Add(btnExit);

            // Footer Version Info Label
            versionText = new TextBlock {
                Text = "v3.15.0-Release",
                FontSize = 9,
                Foreground = new SolidColorBrush(Color.FromRgb(90, 90, 110)),
                Margin = new Thickness(2, 5, 0, 0),
                HorizontalAlignment = HorizontalAlignment.Left
            };
            leftPanel.Children.Add(versionText);


            // --- RIGHT PANEL (SCROLLABLE TAB CONTENT) ---
            Grid rightPanelGrid = new Grid();
            rightPanelGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            rightPanelGrid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
            Grid.SetColumn(rightPanelGrid, 1);
            mainGrid.Children.Add(rightPanelGrid);

            // Top Tip Label (Description Info)
            TextBlock hoverTipText = new TextBlock {
                Text = "Tip: Hover over any optimization feature to view a detailed description of its behavior.",
                FontSize = 11,
                Foreground = new SolidColorBrush(Color.FromRgb(0, 229, 255)),
                Margin = new Thickness(10, 15, 10, 5),
                FontStyle = FontStyles.Italic
            };
            Grid.SetRow(hoverTipText, 0);
            rightPanelGrid.Children.Add(hoverTipText);

            tabs = new TabControl {
                Background = Brushes.Transparent,
                BorderThickness = new Thickness(0),
                Margin = new Thickness(10, 5, 15, 15)
            };
            Grid.SetRow(tabs, 1);
            rightPanelGrid.Children.Add(tabs);

            // Tab Panels
            generalPanel = new StackPanel();
            uiLuaPanel = new StackPanel();
            combatNetPanel = new StackPanel();
            graphicsSoundPanel = new StackPanel();

            tabs.Items.Add(CreateTabItem("GENERAL", generalPanel));
            tabs.Items.Add(CreateTabItem("UI & LUA", uiLuaPanel));
            tabs.Items.Add(CreateTabItem("COMBAT & NET", combatNetPanel));
            tabs.Items.Add(CreateTabItem("GRAPHICS & SOUND", graphicsSoundPanel));

            // Populate Checkboxes into Panels
            foreach (var item in settingsMap) {
                string name = item.Key;
                SettingItem data = item.Value;
                CheckBox chk = CreateStyledCheckBox(name, data.Tooltip);

                // Update setting item to reference its created Control
                data.Ctrl = chk;

                // Add to appropriate Panel
                switch (data.Section) {
                    case "General":
                        generalPanel.Children.Add(chk);
                        break;
                    case "UI_Lua":
                        uiLuaPanel.Children.Add(chk);
                        break;
                    case "Combat_Net":
                        combatNetPanel.Children.Add(chk);
                        break;
                    case "Graphics_Sound":
                        graphicsSoundPanel.Children.Add(chk);
                        break;
                }
            }
        }

        private TabItem CreateTabItem(string header, StackPanel innerPanel) {
            ScrollViewer scroll = new ScrollViewer {
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                Content = innerPanel,
                Margin = new Thickness(0, 15, 0, 0)
            };

            TabItem item = new TabItem {
                Header = header,
                Foreground = Brushes.Gray,
                FontSize = 12,
                FontWeight = FontWeights.Bold,
                BorderThickness = new Thickness(0),
                Background = Brushes.Transparent,
                Content = scroll
            };

            return item;
        }

        private Button CreateStyledButton(string text, Color color, bool highlight) {
            Button btn = new Button {
                Content = text,
                Height = 30,
                Margin = new Thickness(0, 0, 0, 6),
                Background = highlight ? new SolidColorBrush(color) : new SolidColorBrush(Color.FromRgb(20, 20, 28)),
                BorderBrush = new SolidColorBrush(color),
                BorderThickness = new Thickness(1.5),
                Foreground = highlight ? Brushes.Black : new SolidColorBrush(color),
                Cursor = Cursors.Hand,
                FontWeight = FontWeights.SemiBold,
                FontSize = 11
            };

            // Setup Custom Control Template to force strict square corners
            ControlTemplate template = new ControlTemplate(typeof(Button));
            FrameworkElementFactory borderFactory = new FrameworkElementFactory(typeof(Border));
            borderFactory.Name = "border";
            borderFactory.SetValue(Border.BorderThicknessProperty, new Thickness(1.5));
            borderFactory.SetValue(Border.BorderBrushProperty, new SolidColorBrush(color));
            borderFactory.SetValue(Border.CornerRadiusProperty, new CornerRadius(0)); // STRICT SQUARE
            borderFactory.SetValue(Border.BackgroundProperty, btn.Background);

            FrameworkElementFactory contentPresenter = new FrameworkElementFactory(typeof(ContentPresenter));
            contentPresenter.SetValue(ContentPresenter.HorizontalAlignmentProperty, HorizontalAlignment.Center);
            contentPresenter.SetValue(ContentPresenter.VerticalAlignmentProperty, VerticalAlignment.Center);
            borderFactory.AppendChild(contentPresenter);

            template.VisualTree = borderFactory;
            btn.Template = template;

            // Simple Hover Styling via Events (manipulating the template border)
            btn.MouseEnter += (s, e) => {
                btn.ApplyTemplate();
                Border b = btn.Template.FindName("border", btn) as Border;
                if (b != null) {
                    b.Background = new SolidColorBrush(color);
                    btn.Foreground = Brushes.Black;
                }
            };

            btn.MouseLeave += (s, e) => {
                btn.ApplyTemplate();
                Border b = btn.Template.FindName("border", btn) as Border;
                if (b != null) {
                    b.Background = highlight ? new SolidColorBrush(color) : new SolidColorBrush(Color.FromRgb(20, 20, 28));
                    btn.Foreground = highlight ? Brushes.Black : new SolidColorBrush(color);
                }
            };

            return btn;
        }

        private CheckBox CreateStyledCheckBox(string name, string tooltipText) {
            CheckBox chk = new CheckBox {
                Content = name,
                Foreground = Brushes.White,
                FontSize = 13,
                Margin = new Thickness(5, 5, 5, 12),
                Cursor = Cursors.Hand,
                ToolTip = new ToolTip {
                    Content = new TextBlock {
                        Text = tooltipText,
                        MaxWidth = 350,
                        TextWrapping = TextWrapping.Wrap
                    },
                    Background = new SolidColorBrush(Color.FromRgb(20, 20, 30)),
                    Foreground = Brushes.White,
                    BorderBrush = new SolidColorBrush(Color.FromRgb(0, 229, 255)),
                    BorderThickness = new Thickness(1)
                }
            };
            return chk;
        }

        private void ToggleAll(bool enabled) {
            foreach (SettingItem item in settingsMap.Values) {
                if (item.Ctrl != null) {
                    item.Ctrl.IsChecked = enabled;
                }
            }
        }

        private void RestoreDefaults() {
            foreach (SettingItem item in settingsMap.Values) {
                if (item.Ctrl != null) {
                    item.Ctrl.IsChecked = item.DefaultVal;
                }
            }
        }

        private void LoadSettings() {
            LoadSettingsFromPath(iniPath);
        }

        private void LoadSettingsFromPath(string path) {
            if (!File.Exists(path)) {
                RestoreDefaults();
                return;
            }

            try {
                string[] lines = File.ReadAllLines(path);
                Dictionary<string, string> currentSettings = new Dictionary<string, string>();

                foreach (string line in lines) {
                    string trimmed = line.Trim();
                    if (string.IsNullOrEmpty(trimmed) || trimmed.StartsWith(";") || trimmed.StartsWith("["))
                        continue;

                    string[] parts = trimmed.Split('=');
                    if (parts.Length == 2) {
                        currentSettings[parts[0].Trim()] = parts[1].Trim();
                    }
                }

                foreach (string name in new List<string>(settingsMap.Keys)) {
                    SettingItem data = settingsMap[name];
                    string val;
                    if (currentSettings.TryGetValue(data.Key, out val)) {
                        data.Ctrl.IsChecked = (val == "1" || val.ToLower() == "true");
                    } else {
                        data.Ctrl.IsChecked = data.DefaultVal;
                    }
                }
            } catch (Exception ex) {
                MessageBox.Show("Error loading config profile: " + ex.Message, "Load Error", MessageBoxButton.OK, MessageBoxImage.Error);
                RestoreDefaults();
            }
        }

        private void SaveSettings() {
            SaveSettingsToPath(iniPath);
        }

        private void SaveSettingsToPath(string path) {
            try {
                // Ensure directory exists
                string dir = System.IO.Path.GetDirectoryName(path);
                if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir)) {
                    Directory.CreateDirectory(dir);
                }

                // Compile into categories
                Dictionary<string, List<string>> sections = new Dictionary<string, List<string>>() {
                    { "General", new List<string>() },
                    { "UI_Lua", new List<string>() },
                    { "Combat_Net", new List<string>() },
                    { "Graphics_Sound", new List<string>() }
                };

                foreach (SettingItem item in settingsMap.Values) {
                    string val = (item.Ctrl != null && item.Ctrl.IsChecked == true) ? "1" : "0";
                    sections[item.Section].Add(item.Key + "=" + val);
                }

                // Write INI file
                using (StreamWriter sw = new StreamWriter(path, false, Encoding.UTF8)) {
                    sw.WriteLine("; WoW-Optimize Mod Configuration Profile");
                    sw.WriteLine("; Generated by Launcher");
                    sw.WriteLine();

                    foreach (var section in sections) {
                        sw.WriteLine("[" + section.Key + "]");
                        foreach (string line in section.Value) {
                            sw.WriteLine(line);
                        }
                        sw.WriteLine();
                    }
                }
            } catch (Exception ex) {
                MessageBox.Show("Error saving config profile: " + ex.Message, "Save Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void SaveProfile() {
            Microsoft.Win32.SaveFileDialog sfd = new Microsoft.Win32.SaveFileDialog();
            sfd.Filter = "Configuration Profiles (*.ini)|*.ini";
            sfd.FileName = "wow_opt_profile.ini";
            sfd.Title = "Save Configuration Profile";
            if (sfd.ShowDialog() == true) {
                SaveSettingsToPath(sfd.FileName);
                MessageBox.Show("Profile successfully saved to:\n" + sfd.FileName, "Profile Saved", MessageBoxButton.OK, MessageBoxImage.Information);
            }
        }

        private void LoadProfile() {
            Microsoft.Win32.OpenFileDialog ofd = new Microsoft.Win32.OpenFileDialog();
            ofd.Filter = "Configuration Profiles (*.ini)|*.ini";
            ofd.Title = "Load Configuration Profile";
            if (ofd.ShowDialog() == true) {
                LoadSettingsFromPath(ofd.FileName);
                MessageBox.Show("Profile successfully loaded from:\n" + ofd.FileName, "Profile Loaded", MessageBoxButton.OK, MessageBoxImage.Information);
            }
        }

        private void ShareProfileWithDev() {
            try {
                // Compile active settings into a single string
                StringBuilder sb = new StringBuilder();
                sb.AppendLine("; SUGGESTED SAFE PROFILE PRESET");
                sb.AppendLine("; Submit to Suprematist");
                sb.AppendLine();

                Dictionary<string, List<string>> sections = new Dictionary<string, List<string>>() {
                    { "General", new List<string>() },
                    { "UI_Lua", new List<string>() },
                    { "Combat_Net", new List<string>() },
                    { "Graphics_Sound", new List<string>() }
                };

                foreach (SettingItem item in settingsMap.Values) {
                    string val = (item.Ctrl != null && item.Ctrl.IsChecked == true) ? "1" : "0";
                    sections[item.Section].Add(item.Key + "=" + val);
                }

                foreach (var section in sections) {
                    sb.AppendLine("[" + section.Key + "]");
                    foreach (string line in section.Value) {
                        sb.AppendLine(line);
                    }
                    sb.AppendLine();
                }

                // Copy to Clipboard
                Clipboard.SetText(sb.ToString());

                // Inform player
                MessageBox.Show(
                    "Your current profile settings have been copied to the clipboard!\n\n" +
                    "Please paste and share them with the developer (Suprematist) via Discord or GitHub Issues " +
                    "to suggest making this profile safe by default in future updates.",
                    "Profile Copied to Clipboard",
                    MessageBoxButton.OK,
                    MessageBoxImage.Information
                );
            } catch (Exception ex) {
                MessageBox.Show("Failed to copy profile to clipboard: " + ex.Message, "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void CheckForUpdatesAsync() {
            System.Threading.ThreadPool.QueueUserWorkItem((state) => {
                try {
                    using (System.Net.WebClient wc = new System.Net.WebClient()) {
                        wc.Headers.Add("User-Agent", "WoW-Optimize-Launcher");
                        // Fetch latest version from developer release version file
                        string rawVer = wc.DownloadString("https://raw.githubusercontent.com/Suprematist/wow-optimize/main/version.txt");
                        if (!string.IsNullOrEmpty(rawVer)) {
                            string cleanVer = rawVer.Trim();
                            Version latest = new Version(cleanVer);
                            Version current = new Version("3.15.0");

                            if (latest > current) {
                                Dispatcher.BeginInvoke(new Action(() => {
                                    ShowUpdateAlert(cleanVer);
                                }));
                            }
                        }
                    }
                } catch {
                    // Fail silently on network errors
                }
            });
        }

        private void ShowUpdateAlert(string latestVer) {
            if (versionText != null) {
                versionText.Text = "UPDATE AVAILABLE: v" + latestVer;
                versionText.Foreground = new SolidColorBrush(Color.FromRgb(0, 230, 118)); // Bright neon green
                versionText.FontWeight = FontWeights.Bold;
                versionText.Cursor = Cursors.Hand;
                versionText.ToolTip = "Click to open GitHub releases page for upgrade!";
                versionText.MouseLeftButtonDown += (s, e) => {
                    try {
                        Process.Start("https://github.com/Suprematist/wow-optimize/releases");
                    } catch {
                        // ignore
                    }
                };
            }
        }

        private void LaunchWow() {
            // 1. Save Settings
            SaveSettings();

            // 2. Launch wow.exe
            string exeDir = AppDomain.CurrentDomain.BaseDirectory;
            string wowPath = System.IO.Path.Combine(exeDir, "wow.exe");

            if (!File.Exists(wowPath)) {
                MessageBox.Show("Could not find wow.exe in current directory: " + wowPath + "\n\nPlease place the launcher in your World of Warcraft directory.", "Execution Error", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            try {
                ProcessStartInfo psi = new ProcessStartInfo {
                    FileName = wowPath,
                    WorkingDirectory = exeDir
                };
                Process.Start(psi);

                // Exit launcher on launch
                Close();
            } catch (Exception ex) {
                MessageBox.Show("Failed to launch wow.exe: " + ex.Message, "Execution Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        // Dragging window support
        protected override void OnMouseLeftButtonDown(MouseButtonEventArgs e) {
            base.OnMouseLeftButtonDown(e);
            try {
                DragMove();
            } catch {
                // Ignore drag exceptions
            }
        }
    }
}
