using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace GDMENUCardManager.Core
{
    /// <summary>
    /// CASES.INI on the menu disc: the 3D case each game is shown in by the BIOS-style openMenu, one line per game,
    /// "PRODUCT=style size" (e.g. "T-9003N=clearwhite double"). A game that is not listed gets its case by region, cover and
    /// number of discs.
    /// </summary>
    public class CaseStylesManager
    {
        public const string FileName = "CASES.INI";

        /// <summary>The styles, as written to the file, with the names shown in the window. "" is automatic.</summary>
        public static readonly IReadOnlyList<KeyValuePair<string, string>> Styles = new List<KeyValuePair<string, string>>
        {
            new KeyValuePair<string, string>("", "Automatic (by region and cover)"),
            new KeyValuePair<string, string>("clearblack", "Clear, black tray (US)"),
            new KeyValuePair<string, string>("clearwhite", "Clear, white tray (US)"),
            new KeyValuePair<string, string>("white", "White (Japan)"),
            new KeyValuePair<string, string>("dark", "Black (homebrew, applications)"),
            new KeyValuePair<string, string>("pal", "Blue Dreamcast case (PAL)"),
        };

        /// <summary>The sizes, as written to the file, with the names shown in the window. "" is automatic.</summary>
        public static readonly IReadOnlyList<KeyValuePair<string, string>> Sizes = new List<KeyValuePair<string, string>>
        {
            new KeyValuePair<string, string>("", "Automatic (double for 2+ discs)"),
            new KeyValuePair<string, string>("single", "Single"),
            new KeyValuePair<string, string>("double", "Double"),
        };

        private readonly Dictionary<string, (string Style, string Size)> _entries =
            new Dictionary<string, (string Style, string Size)>(StringComparer.OrdinalIgnoreCase);

        public bool HasUnsavedChanges { get; set; }

        public void Load(string path)
        {
            _entries.Clear();
            HasUnsavedChanges = false;
            if (!File.Exists(path))
                return;
            foreach (var raw in File.ReadAllLines(path))
            {
                var line = raw.Trim();
                if (line.Length == 0 || line[0] == ';' || line[0] == '#' || line[0] == '[')
                    continue;
                int eq = line.IndexOf('=');
                if (eq <= 0)
                    continue;
                var serial = line.Substring(0, eq).Trim();
                string style = "", size = "";
                foreach (var word in line.Substring(eq + 1).Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries))
                {
                    var w = word.ToLowerInvariant();
                    if (w == "single" || w == "double")
                        size = w;
                    else if (Styles.Any(s => s.Key == w && w.Length > 0))
                        style = w;
                }
                if (style.Length > 0 || size.Length > 0)
                    _entries[serial] = (style, size);
            }
        }

        public void Save(string path)
        {
            var sb = new StringBuilder();
            sb.AppendLine("; The 3D case of each game in the BIOS-style openMenu: PRODUCT=style size");
            sb.AppendLine("; styles: clearblack clearwhite white dark pal; sizes: single double (left out: automatic)");
            foreach (var kv in _entries.OrderBy(x => x.Key, StringComparer.OrdinalIgnoreCase))
                sb.AppendLine($"{kv.Key}={string.Join(" ", new[] { kv.Value.Style, kv.Value.Size }.Where(x => x.Length > 0))}");
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
            HasUnsavedChanges = false;
        }

        public (string Style, string Size) Get(string serial)
        {
            if (!string.IsNullOrEmpty(serial) && _entries.TryGetValue(serial, out var v))
                return v;
            return ("", "");
        }

        public void Set(string serial, string style, string size)
        {
            if (string.IsNullOrEmpty(serial))
                return;
            style ??= "";
            size ??= "";
            var old = Get(serial);
            if (old.Style == style && old.Size == size)
                return;
            if (style.Length == 0 && size.Length == 0)
                _entries.Remove(serial);
            else
                _entries[serial] = (style, size);
            HasUnsavedChanges = true;
        }
    }
}
