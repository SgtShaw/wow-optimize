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
        public CheckBox RecentCtrl;
        public string Tooltip;

        public SettingItem(string section, string key, bool defaultVal, CheckBox ctrl, string tooltip) {
            Section = section;
            Key = key;
            DefaultVal = defaultVal;
            Ctrl = ctrl;
            RecentCtrl = null;
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
        private StackPanel recentNewPanel;
        private TabControl tabs;
        private TextBlock versionText;
        private TextBlock activeModulesText;
        private System.Windows.Documents.Run activeCountRun;
        private Grid progressBarGrid;

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
                { "Memory-Mapped MPQ VFS", new SettingItem("General", "MpqMmapVfs", false, null, "Maps all main MPQ files to memory using map views to speed up asset load times and parallelize decompression.") },
                { "Lock-Free Object Manager", new SettingItem("General", "RcuObjMgr", false, null, "Replaces linear linked-list entity loops with atomic pointer mirror arrays to remove object manager locks in raids.") },
                { "Predictive MPQ Prefetcher", new SettingItem("General", "MpqPrefetch", false, null, "Tracks zone transitions and speculatively pre-caches MPQ asset files in background threads before you arrive.") },
                { "Object Visibility Cache", new SettingItem("General", "ObjVisCache", true, null, "Speeds up GUID-to-Object translations in the Client Object Manager using a high-performance thread-local hash cache.") },
                { "Memory-Mapped DBC RAM Cache", new SettingItem("General", "DbcPreload", false, null, "Pre-loads and decompresses all major client database files (.dbc) into RAM at startup for near-instant loading screens.") },
                { "32-bit OOM VRAM Governor", new SettingItem("General", "OomGovernor", false, null, "Dynamically downscales texture mipmaps when the 32-bit client's virtual address space usage approaches critical OOM levels.") },

                // UI & Lua
                { "UI Update Batching", new SettingItem("UI_Lua", "UIFrameBatch", false, null, "Aggregates frame ticks to batch multiple addon OnUpdate calls, lowering CPU usage in intensive UI scenes.") },
                { "Addon Dispatcher Parallelization", new SettingItem("UI_Lua", "AddonDispatcher", false, null, "Dispatches addon script execution tasks to a background thread pool. Boosts frames in raid setups.") },
                { "Fast UI Frame Accessors", new SettingItem("UI_Lua", "UIFrameAccessorFast", false, null, "Bypasses standard Lua stack queries to retrieve UI frame parameters (IsShown, GetAlpha) instantly.") },
                { "Fast FontString Metrics & Glyph Cache", new SettingItem("UI_Lua", "FontMetricsFast", false, null, "Provides high-speed text measurements and caches rasterized font glyph textures to eliminate render-time layout freezes.") },
                { "Lock-Free Font Metrics", new SettingItem("UI_Lua", "FontMetricsLockFree", false, null, "Uses a read-copy-update styled font metrics cache to eliminate font locks during rendering.") },
                { "Coalesced FrameXML Updates", new SettingItem("UI_Lua", "FrameXmlCoalesce", false, null, "Deduplicates multiple layout recalculations in a single frame tick, stopping layout micro-freezes.") },
                { "Addon Tick Governor", new SettingItem("UI_Lua", "AddonTickGovernor", false, null, "Caps excessive addon update execution rates to prevent CPU bottlenecks.") },
                { "Tooltip Cache", new SettingItem("UI_Lua", "TooltipCache", false, null, "Caches formatted tooltips (spells, items) to avoid invoking slow script layout logic repeatedly.") },
                { "Lua File Reading Cache", new SettingItem("UI_Lua", "LuaFileCache", false, null, "Keeps parsed Lua scripts in memory, bypassing disk disk reads and string parsing on UI reloads.") },
                { "FrameScript FNV-1a Dispatcher", new SettingItem("UI_Lua", "FrameScriptDispatch", false, null, "Uses an O(1) hash map lookup for script handlers instead of linear string matching.") },
                { "Lua Number Conversion Fast Path", new SettingItem("UI_Lua", "LuaNumConvFast", false, null, "Inlines common Lua stack value queries (tonumber, gettop, settop) to bypass stack checking overhead.") },
                { "Lua VM Cache & Regex Cache", new SettingItem("UI_Lua", "LuaOpcache", false, null, "Provides a fast lookup cache for VM table indexing and caches compiled regex patterns to speed up string matching/gsub APIs.") },
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
                { "WorldState Coalescing", new SettingItem("Graphics_Sound", "WorldStateCoalesce", false, null, "Batches high-frequency WorldState updates to prevent camera stutter during active battlegrounds.") },
                { "Asynchronous Texture Loader", new SettingItem("Graphics_Sound", "AsyncTexLoader", false, null, "Asynchronously loads and decompresses BLP textures in background worker threads, hot-swapping them on frame boundaries to prevent stutters.") },
                { "Asynchronous Terrain Loader", new SettingItem("Graphics_Sound", "AsyncTerrainLoader", false, null, "Offloads ADT terrain file loading and collision mesh compiling to helper cores.") },
                { "M2 Model LOD Bias Control", new SettingItem("Graphics_Sound", "M2LodBias", false, null, "Dynamically scales 3D model level-of-detail bias depending on active rendering frametimes.") },
                { "Mipmap Bias Governor", new SettingItem("Graphics_Sound", "MipBiasGovernor", false, null, "Adjusts mipmap texture bias dynamically based on virtual memory pressure to prevent allocation spikes.") },
                { "Spatial Culling & Parallel Frustum Culler", new SettingItem("Graphics_Sound", "SpatialCulling", false, null, "Speculatively culls off-screen models and parallelizes frustum plane intersection queries using helper threads.") },
                { "Direct3D 9 Render-Thread Offloading", new SettingItem("Graphics_Sound", "D3d9RenderThread", false, null, "Offloads draw dispatches and state updates to an asynchronous render thread to prevent driver bottlenecks.") },

                // 10 New Features
                { "Dynamic Shadow Quality Auto-Scaler", new SettingItem("Graphics_Sound", "DynamicShadowScaler", false, null, "[NEW] Dynamically scales shadow quality and resolution depending on the active frame rate.") },
                { "Advanced Sound Channels Coalescer", new SettingItem("Graphics_Sound", "SoundCoalescer", false, null, "[NEW] Coalesces rapid duplicated sound plays to prevent channel exhaustion under AOE spam.") },
                { "Aura / Buff Textures Preload Cache", new SettingItem("Combat_Net", "AuraPreloadCache", false, null, "[NEW] Pre-caches spell/aura icons at zone transitions to eliminate micro-freezes during combat.") },
                { "DBC File Query Cache", new SettingItem("Graphics_Sound", "DbcFileCache", false, null, "[NEW] High-performance cache for record retrieval from client DBC files (items, spells, etc.).") },
                { "Font Glyph Outline Cache", new SettingItem("UI_Lua", "FontOutlineCache", false, null, "[NEW] Caches glyph outline bitmaps to accelerate formatted text rendering.") },
                { "LUA GC Budget Governor", new SettingItem("UI_Lua", "LuaGcGovernor", false, null, "[NEW] Runs GC step cycles dynamically, matching the exact frame time budget to prevent spikes.") },
                { "Particle Density Dynamic Scaler", new SettingItem("Graphics_Sound", "ParticleDensityScaler", false, null, "[NEW] Dynamically scales down particle density in heavy raid environments to keep FPS high.") },
                { "Addon Message Rate Limiter", new SettingItem("Combat_Net", "AddonMsgLimiter", false, null, "[NEW] Intercepts and rate-limits outbound addon sync messages to prevent disconnection #36.") },
                { "Hardware Mouse Smoothing & Edge Lock", new SettingItem("General", "MouseCursorSmooth", false, null, "[NEW] Smooths mouse coordinates and locks the cursor inside the window during mouselook.") },
                { "Model Vertex Buffers Pre-Allocator", new SettingItem("General", "VertexBufferPrealloc", false, null, "[NEW] Pre-allocates memory slabs for dynamic vertex updates using memory pools to stop stutters.") },

                // 10 Additional New Features
                { "World Object Render Optimizer", new SettingItem("Graphics_Sound", "WorldObjectOpt", false, null, "[NEW] Caches visibility state of non-interactive far world objects to skip drawing when camera is static.") },
                { "Nameplate Render Distance CVar", new SettingItem("Combat_Net", "NameplateDistanceCvar", false, null, "[NEW] Enables customizable rendering distance bounds on unit nameplates.") },
                { "Combat Log File Async Flusher", new SettingItem("Combat_Net", "CombatLogAsync", false, null, "[NEW] Writes combat log files to disk asynchronously in a background thread to prevent lag.") },
                { "CDataStore Payload Buffering", new SettingItem("Combat_Net", "CDataStoreBuffering", false, null, "[NEW] Caches data buffer pointers to prevent redundant dereferences on packets.") },
                { "Camera Shake Reducer", new SettingItem("General", "CameraShakeOpt", false, null, "[NEW] Filters out excessive camera shakes to stabilize rendering during heavy combat.") },
                { "Combat Floating Text Font Optimizer", new SettingItem("UI_Lua", "CombatTextFont", false, null, "[NEW] Caches fonts and metrics specifically for floating scrolling damage/heal strings.") },
                { "Spell Overlay Preload Cache", new SettingItem("Combat_Net", "SpellOverlayPreload", false, null, "[NEW] Preloads visual spell overlays at zone transitions to stop combat freezes.") },
                { "Addon SavedVariables Backup Engine", new SettingItem("General", "SavedVarsBackup", false, null, "[NEW] Automatically duplicates saved variables into a backup file to prevent loss on crash.") },
                { "Unit Max Power Cache", new SettingItem("UI_Lua", "UnitMaxPowerCache", false, null, "[NEW] Caches maximum unit power limits to bypass Lua-to-C client API query overhead.") },
                { "Mouse Clip Release", new SettingItem("General", "MouseClipRelease", false, null, "[NEW] Releases coordinate boundaries locks automatically when WoW loses window focus.") },
                { "Loading Screen Render Optimizer", new SettingItem("Graphics_Sound", "LoadingScreenOpt", false, null, "[NEW] Bypasses heavy 3D rendering calls during loading screens to improve loading speeds.") },
                { "Combat Log Range Filter", new SettingItem("Combat_Net", "CombatLogFilter", false, null, "[NEW] Discards out-of-range combat log events between distant non-grouped units to reduce CPU load.") },
                { "Overlapping Sound Volume Limiter", new SettingItem("Graphics_Sound", "SoundVolumeLimit", false, null, "[NEW] Limits and clamps volume for overlapping duplicate sound effects to prevent clipping and audio driver lag.") },
                { "UI Layout Recalculation Throttle", new SettingItem("UI_Lua", "UILayoutThrottle", false, null, "[NEW] Prevents frame layout loops and limits redundant recalculation checks per tick.") },
                { "Terrain Height Cache", new SettingItem("Graphics_Sound", "TerrainHeightCache", false, null, "[NEW] Caches terrain elevation queries within the frame to minimize CPU map collisions query time.") },
                { "Model Animation Blend Cache", new SettingItem("Graphics_Sound", "AnimBlendCache", false, null, "[NEW] Caches bone matrix calculations for frequently rendering animated models.") },
                { "Addon SavedVariables Optimizer", new SettingItem("General", "SavedVarsOpt", false, null, "[NEW] Compacts serialization of addon configurations on exit to speed up logout and reduce file size.") },
                { "Static Item Data Prefetcher", new SettingItem("Combat_Net", "ItemDataPrefetch", false, null, "[NEW] Pre-queries item templates in the background to prevent hover-inspect and chat link lag.") },
                { "Movement Interpolation Smoother", new SettingItem("General", "MovementSmoothing", false, null, "[NEW] Smoothes coordinate updates and reduces movement jitter for distant players and creatures.") },
                { "Font Alpha Blending Fastpath", new SettingItem("UI_Lua", "FontAlphaFastpath", false, null, "[NEW] Bypasses alpha blending render states for fully opaque or fully transparent UI and nameplate text.") },

                // 20 new colossal features (Features 31-50)
                { "Network Packet Processing Throttle", new SettingItem("Combat_Net", "PacketProcessingThrottle", false, null, "[NEW] Limits processing rate of non-essential social/guild status packets in combat.") },
                { "Nameplate Occlusion Culler", new SettingItem("Combat_Net", "NameplateCulling", false, null, "[NEW] Culls processing and drawing of nameplates that are behind obstacles or out of range.") },
                { "Texture Smart Unload Delay", new SettingItem("Graphics_Sound", "TextureUnloadDelay", false, null, "[NEW] Delays texture unloading during camera turnarounds to prevent immediate load micro-stutters.") },
                { "M2 Model Software Skinning SIMD", new SettingItem("Graphics_Sound", "M2MatrixSimd", false, null, "[NEW] Speeds up 3D character bone-skinning matrix transforms using SSE2 SIMD intrinsics.") },
                { "Minimap Refresh Rate Governor", new SettingItem("UI_Lua", "MinimapRefreshGovernor", false, null, "[NEW] Caps minimap radar updates frequency to prevent client rendering overload during fast runs.") },
                { "Spell Visual Effects Culler", new SettingItem("Graphics_Sound", "SpellEffectCulling", false, null, "[NEW] Dynamically scales down particle density and minor spell impact effects in large raids.") },
                { "Lua Fast String Compare", new SettingItem("UI_Lua", "LuaStringCompareFast", false, null, "[NEW] Accelerates Lua string comparison using hardware-inlined vector instructions.") },
                { "DBC Data Row Offset Caching", new SettingItem("Graphics_Sound", "DbcRowCaching", false, null, "[NEW] Speeds up database queries by caching resolved row pointer offsets in DBC files.") },
                { "Social Packet String Pooling", new SettingItem("Combat_Net", "NetworkStringDedup", false, null, "[NEW] De-duplicates incoming packet strings to minimize memory allocation overhead.") },
                { "Camera Collision Check Throttle", new SettingItem("General", "CameraCollisionThrottle", false, null, "[NEW] Rate-limits camera collision terrain raycasts when camera is static.") },
                { "FMOD Sound Play Rate Limit", new SettingItem("Graphics_Sound", "SoundFreqCoalesce", false, null, "[NEW] Coalesces rapid duplicated sound plays that share exact pitch/frequency.") },
                { "Aura Status Update Coalescing", new SettingItem("Combat_Net", "AuraUpdateDedup", false, null, "[NEW] Filters out redundant aura status dispatches within short time windows.") },
                { "Static UI Artwork Texture Cache", new SettingItem("UI_Lua", "UiTextureCaching", false, null, "[NEW] Cache loaded UI texture references in memory to avoid repetitive disk loads.") },
                { "Portal Occluder WMO Culler", new SettingItem("Graphics_Sound", "WmoCullingOpt", false, null, "[NEW] Aggressively culls hidden interior structures of world objects (WMOs).") },
                { "Fast Float String Parser", new SettingItem("UI_Lua", "FastFloatParse", false, null, "[NEW] Bypasses slow standard CRT float conversion during UI asset parsing.") },
                { "Proactive Heap Leak Tracker", new SettingItem("General", "HeapAllocationTracker", false, null, "[NEW] Continuously monitors heap usage to flag memory leak sources before OOM.") },
                { "Spell Cooldown Frame Cache", new SettingItem("UI_Lua", "SpellCooldownCache", false, null, "[NEW] Caches cooldown timers status updates to speed up action bar updates.") },
                { "Combat GUID Hex String Pool", new SettingItem("Combat_Net", "GuidStringCache", false, null, "[NEW] Caches formatted hex string GUIDs to speed up combat log parsers.") },
                { "FrameScript Block Recycling", new SettingItem("UI_Lua", "FrameScriptMemOpt", false, null, "[NEW] Recycles script block allocations to bypass heap allocator serialize bottlenecks.") },
                { "Non-Vital Combat Event Screener", new SettingItem("Combat_Net", "CombatEventLimit", false, null, "[NEW] Dynamically filters minor combat events when active client rendering FPS is low.") }
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

            // Active Modules Counter
            activeModulesText = new TextBlock {
                Margin = new Thickness(2, 5, 2, 2),
                FontSize = 11,
                Foreground = new SolidColorBrush(Color.FromRgb(150, 150, 180))
            };
            activeModulesText.Inlines.Add(new System.Windows.Documents.Run("Active modules: "));
            activeCountRun = new System.Windows.Documents.Run("0") {
                Foreground = new SolidColorBrush(Color.FromRgb(0, 229, 255)),
                FontWeight = FontWeights.Bold
            };
            activeModulesText.Inlines.Add(activeCountRun);
            activeModulesText.Inlines.Add(new System.Windows.Documents.Run("/" + settingsMap.Count));
            leftPanel.Children.Add(activeModulesText);

            // Thin square progress bar
            progressBarGrid = new Grid {
                Height = 4,
                Margin = new Thickness(2, 4, 2, 12),
                Background = new SolidColorBrush(Color.FromRgb(30, 30, 45)) // Dark track
            };
            progressBarGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(0, GridUnitType.Star) }); // Filled
            progressBarGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) }); // Unfilled

            Border progressFill = new Border {
                Background = new SolidColorBrush(Color.FromRgb(0, 229, 255)),
                HorizontalAlignment = HorizontalAlignment.Stretch
            };
            Grid.SetColumn(progressFill, 0);
            progressBarGrid.Children.Add(progressFill);
            leftPanel.Children.Add(progressBarGrid);

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
                Text = "v3.16.0-Release",
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
            Button btnEnableGeneral = CreateStyledButton("ENABLE ALL IN GENERAL", Color.FromRgb(0, 229, 255), false);
            btnEnableGeneral.Margin = new Thickness(5, 5, 5, 15);
            btnEnableGeneral.Height = 28;
            btnEnableGeneral.FontSize = 10;
            btnEnableGeneral.Click += (s, e) => ToggleTabFeatures("General", true);
            generalPanel.Children.Add(btnEnableGeneral);

            uiLuaPanel = new StackPanel();
            Button btnEnableUiLua = CreateStyledButton("ENABLE ALL IN UI & LUA", Color.FromRgb(0, 229, 255), false);
            btnEnableUiLua.Margin = new Thickness(5, 5, 5, 15);
            btnEnableUiLua.Height = 28;
            btnEnableUiLua.FontSize = 10;
            btnEnableUiLua.Click += (s, e) => ToggleTabFeatures("UI_Lua", true);
            uiLuaPanel.Children.Add(btnEnableUiLua);

            combatNetPanel = new StackPanel();
            Button btnEnableCombatNet = CreateStyledButton("ENABLE ALL IN COMBAT & NET", Color.FromRgb(0, 229, 255), false);
            btnEnableCombatNet.Margin = new Thickness(5, 5, 5, 15);
            btnEnableCombatNet.Height = 28;
            btnEnableCombatNet.FontSize = 10;
            btnEnableCombatNet.Click += (s, e) => ToggleTabFeatures("Combat_Net", true);
            combatNetPanel.Children.Add(btnEnableCombatNet);

            graphicsSoundPanel = new StackPanel();
            Button btnEnableGraphicsSound = CreateStyledButton("ENABLE ALL IN GRAPHICS & SOUND", Color.FromRgb(0, 229, 255), false);
            btnEnableGraphicsSound.Margin = new Thickness(5, 5, 5, 15);
            btnEnableGraphicsSound.Height = 28;
            btnEnableGraphicsSound.FontSize = 10;
            btnEnableGraphicsSound.Click += (s, e) => ToggleTabFeatures("Graphics_Sound", true);
            graphicsSoundPanel.Children.Add(btnEnableGraphicsSound);

            recentNewPanel = new StackPanel();
            Button btnEnableRecentNew = CreateStyledButton("ENABLE ALL NEW FEATURES", Color.FromRgb(0, 229, 255), false);
            btnEnableRecentNew.Margin = new Thickness(5, 5, 5, 15);
            btnEnableRecentNew.Height = 28;
            btnEnableRecentNew.FontSize = 10;
            btnEnableRecentNew.Click += (s, e) => {
                foreach (var item in settingsMap.Values) {
                    if (item.RecentCtrl != null) {
                        item.RecentCtrl.IsChecked = true;
                    }
                }
            };
            recentNewPanel.Children.Add(btnEnableRecentNew);

            tabs.Items.Add(CreateTabItem("GENERAL", generalPanel));
            tabs.Items.Add(CreateTabItem("UI & LUA", uiLuaPanel));
            tabs.Items.Add(CreateTabItem("COMBAT & NET", combatNetPanel));
            tabs.Items.Add(CreateTabItem("GRAPHICS & SOUND", graphicsSoundPanel));
            tabs.Items.Add(CreateTabItem("RECENT & NEW", recentNewPanel));

            // Populate Checkboxes into Panels
            foreach (var item in settingsMap) {
                string name = item.Key;
                SettingItem data = item.Value;
                CheckBox chk = CreateStyledCheckBox(name, data.Tooltip);

                // Update setting item to reference its created Control
                data.Ctrl = chk;

                chk.Checked += (s, e) => UpdateActiveModulesCount();
                chk.Unchecked += (s, e) => UpdateActiveModulesCount();

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

                // If feature is new, create a duplicate checkbox in the RECENT & NEW tab and sync them
                if (data.Tooltip.StartsWith("[NEW]")) {
                    CheckBox recentChk = CreateStyledCheckBox(name, data.Tooltip);
                    data.RecentCtrl = recentChk;

                    bool isSyncing = false;
                    chk.Checked += (s, ev) => {
                        if (!isSyncing) {
                            isSyncing = true;
                            recentChk.IsChecked = true;
                            isSyncing = false;
                        }
                    };
                    chk.Unchecked += (s, ev) => {
                        if (!isSyncing) {
                            isSyncing = true;
                            recentChk.IsChecked = false;
                            isSyncing = false;
                        }
                    };
                    recentChk.Checked += (s, ev) => {
                        if (!isSyncing) {
                            isSyncing = true;
                            chk.IsChecked = true;
                            isSyncing = false;
                        }
                    };
                    recentChk.Unchecked += (s, ev) => {
                        if (!isSyncing) {
                            isSyncing = true;
                            chk.IsChecked = false;
                            isSyncing = false;
                        }
                    };

                    recentNewPanel.Children.Add(recentChk);
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
            ToolTipService.SetInitialShowDelay(chk, 250);
            ToolTipService.SetShowDuration(chk, 30000);

            // Custom CheckBox template for flat square look
            ControlTemplate template = new ControlTemplate(typeof(CheckBox));
            FrameworkElementFactory gridFactory = new FrameworkElementFactory(typeof(Grid));
            gridFactory.SetValue(Grid.BackgroundProperty, Brushes.Transparent);

            // Columns: Checkbox border (col 0), Content (col 1)
            FrameworkElementFactory col1 = new FrameworkElementFactory(typeof(ColumnDefinition));
            col1.SetValue(ColumnDefinition.WidthProperty, new GridLength(20));
            FrameworkElementFactory col2 = new FrameworkElementFactory(typeof(ColumnDefinition));
            col2.SetValue(ColumnDefinition.WidthProperty, new GridLength(1, GridUnitType.Star));
            gridFactory.AppendChild(col1);
            gridFactory.AppendChild(col2);

            // Outer box border (strictly square)
            FrameworkElementFactory borderFactory = new FrameworkElementFactory(typeof(Border));
            borderFactory.Name = "checkBoxBorder";
            borderFactory.SetValue(Border.WidthProperty, 16.0);
            borderFactory.SetValue(Border.HeightProperty, 16.0);
            borderFactory.SetValue(Border.BorderThicknessProperty, new Thickness(1.5));
            borderFactory.SetValue(Border.BorderBrushProperty, new SolidColorBrush(Color.FromRgb(100, 110, 140)));
            borderFactory.SetValue(Border.BackgroundProperty, new SolidColorBrush(Color.FromRgb(18, 18, 28)));
            borderFactory.SetValue(Border.CornerRadiusProperty, new CornerRadius(0)); // STRICT SQUARE
            borderFactory.SetValue(Border.HorizontalAlignmentProperty, HorizontalAlignment.Left);
            borderFactory.SetValue(Border.VerticalAlignmentProperty, VerticalAlignment.Center);

            // Check indicator (inner solid flat cyan square)
            FrameworkElementFactory checkIndicator = new FrameworkElementFactory(typeof(Border));
            checkIndicator.Name = "checkMark";
            checkIndicator.SetValue(Border.WidthProperty, 8.0);
            checkIndicator.SetValue(Border.HeightProperty, 8.0);
            checkIndicator.SetValue(Border.BackgroundProperty, new SolidColorBrush(Color.FromRgb(0, 229, 255)));
            checkIndicator.SetValue(Border.CornerRadiusProperty, new CornerRadius(0)); // STRICT SQUARE
            checkIndicator.SetValue(Border.VisibilityProperty, Visibility.Collapsed);
            borderFactory.AppendChild(checkIndicator);

            gridFactory.AppendChild(borderFactory);

            // ContentPresenter for checkbox label
            FrameworkElementFactory contentPresenter = new FrameworkElementFactory(typeof(ContentPresenter));
            contentPresenter.SetValue(ContentPresenter.ContentProperty, new TemplateBindingExtension(CheckBox.ContentProperty));
            contentPresenter.SetValue(ContentPresenter.MarginProperty, new Thickness(8, 0, 0, 0));
            contentPresenter.SetValue(ContentPresenter.VerticalAlignmentProperty, VerticalAlignment.Center);
            contentPresenter.SetValue(Grid.ColumnProperty, 1);
            gridFactory.AppendChild(contentPresenter);

            template.VisualTree = gridFactory;

            // Trigger for Checked state
            Trigger checkedTrigger = new Trigger { Property = CheckBox.IsCheckedProperty, Value = true };
            checkedTrigger.Setters.Add(new Setter { TargetName = "checkMark", Property = Border.VisibilityProperty, Value = Visibility.Visible });
            checkedTrigger.Setters.Add(new Setter { TargetName = "checkBoxBorder", Property = Border.BorderBrushProperty, Value = new SolidColorBrush(Color.FromRgb(0, 229, 255)) });
            template.Triggers.Add(checkedTrigger);

            // Trigger for MouseOver state
            Trigger mouseOverTrigger = new Trigger { Property = CheckBox.IsMouseOverProperty, Value = true };
            mouseOverTrigger.Setters.Add(new Setter { TargetName = "checkBoxBorder", Property = Border.BorderBrushProperty, Value = new SolidColorBrush(Color.FromRgb(0, 229, 255)) });
            template.Triggers.Add(mouseOverTrigger);

            chk.Template = template;
            return chk;
        }

        private void ToggleAll(bool enabled) {
            foreach (SettingItem item in settingsMap.Values) {
                if (item.Ctrl != null) {
                    item.Ctrl.IsChecked = enabled;
                }
            }
        }

        private void ToggleTabFeatures(string section, bool enabled) {
            foreach (SettingItem item in settingsMap.Values) {
                if (item.Section == section && item.Ctrl != null) {
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
                UpdateActiveModulesCount();
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
                        string rawVer = wc.DownloadString("https://raw.githubusercontent.com/suprepupre/wow-optimize/main/version.txt");
                        if (!string.IsNullOrEmpty(rawVer)) {
                            string cleanVer = rawVer.Trim();
                            Version latest = new Version(cleanVer);
                            Version current = new Version("3.16.0");

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
                        Process.Start("https://github.com/suprepupre/wow-optimize/releases");
                    } catch {
                        // ignore
                    }
                };
            }
        }

        private void LaunchWow() {
            // 1. Save Settings
            SaveSettings();

            // 2. Locate target executable
            string exeDir = AppDomain.CurrentDomain.BaseDirectory;
            string wowPath = System.IO.Path.Combine(exeDir, "wow.exe");

            if (!File.Exists(wowPath)) {
                // Check common private server names
                string[] alternateNames = { "Ascension.exe", "run.exe", "WoWCircle.exe", "wow-64.exe", "Sirus.exe" };
                foreach (string altName in alternateNames) {
                    string altPath = System.IO.Path.Combine(exeDir, altName);
                    if (File.Exists(altPath)) {
                        wowPath = altPath;
                        break;
                    }
                }
            }

            // Fallback: search for any .exe containing "wow" or "ascension" that isn't the launcher/loader itself
            if (!File.Exists(wowPath)) {
                try {
                    string[] files = Directory.GetFiles(exeDir, "*.exe");
                    foreach (string file in files) {
                        string name = System.IO.Path.GetFileName(file).ToLower();
                        if (name != "wow_optimize_launcher.exe" && name != "wow_loader.exe" && 
                            (name.Contains("wow") || name.Contains("ascension") || name.Contains("circle") || name.Contains("sirus"))) {
                            wowPath = file;
                            break;
                        }
                    }
                } catch {
                    // Ignore directory read errors
                }
            }

            if (!File.Exists(wowPath)) {
                MessageBox.Show("Could not find wow.exe, Ascension.exe, or another valid game executable in the current directory: " + exeDir + "\n\nPlease place the launcher in your World of Warcraft directory.", "Execution Error", MessageBoxButton.OK, MessageBoxImage.Warning);
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
                MessageBox.Show("Failed to launch " + System.IO.Path.GetFileName(wowPath) + ": " + ex.Message, "Execution Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void UpdateActiveModulesCount() {
            if (settingsMap == null) return;
            int activeCount = 0;
            foreach (var item in settingsMap.Values) {
                if (item.Ctrl != null && item.Ctrl.IsChecked == true) {
                    activeCount++;
                }
            }
            if (activeCountRun != null) {
                activeCountRun.Text = activeCount.ToString();
            }
            if (progressBarGrid != null) {
                int totalCount = settingsMap.Count;
                progressBarGrid.ColumnDefinitions[0].Width = new GridLength(activeCount, GridUnitType.Star);
                progressBarGrid.ColumnDefinitions[1].Width = new GridLength(totalCount - activeCount, GridUnitType.Star);
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
