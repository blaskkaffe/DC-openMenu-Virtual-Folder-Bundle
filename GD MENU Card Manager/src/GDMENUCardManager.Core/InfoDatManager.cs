using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace GDMENUCardManager.Core
{
    /// <summary>One game's extra information in INFO.DAT.</summary>
    public sealed class InfoDatEntry
    {
        public ushort Year { get; set; }
        public byte Month { get; set; }
        public byte Day { get; set; }
        public string Developer { get; set; } = string.Empty;
        public string Publisher { get; set; } = string.Empty;
        public string Genre { get; set; } = string.Empty;
        public string Players { get; set; } = string.Empty;
        /// <summary>0..20 (ScreenScraper's note), 0 = unknown.</summary>
        public byte Rating { get; set; }
        public string Title { get; set; } = string.Empty;

        public bool IsEmpty =>
            Year == 0 && string.IsNullOrEmpty(Developer) && string.IsNullOrEmpty(Publisher) &&
            string.IsNullOrEmpty(Genre) && string.IsNullOrEmpty(Players) && Rating == 0 && string.IsNullOrEmpty(Title);
    }

    /// <summary>
    /// INFO.DAT: what META.DAT has no room for (release date, developer, publisher, genre as text, players, rating,
    /// the full title), for the BIOS-style openMenu's information screen and its "Year" sort. Same container as
    /// META.DAT ("DAT\x01", entry size, count; 16 byte index entries of a 10 character serial and a file number), so
    /// the menu's DAT reader reads it as it reads the others. Derek's openMenu does not read it.
    ///
    /// Entry (256 bytes, strings ASCII and NUL-terminated):
    ///   0x00 u16 year, 0x02 u8 month, 0x03 u8 day (0 = unknown)
    ///   0x04 developer[60], 0x40 publisher[60], 0x7C genre[60], 0xB8 players[8]
    ///   0xC0 u8 rating (0..20), 0xC1..0xC3 reserved, 0xC4 title[60]
    /// </summary>
    public sealed class InfoDatManager
    {
        public const uint EntrySize = 0x100;
        public const int HeaderSize = 16;
        public const int EntryIndexSize = 16;
        public const int NameFieldLength = 10;

        private const int OffDeveloper = 0x04, OffPublisher = 0x40, OffGenre = 0x7C, OffPlayers = 0xB8;
        private const int OffRating = 0xC0, OffTitle = 0xC4;
        private const int TextLength = 60, PlayersLength = 8;

        private readonly Dictionary<string, byte[]> _entries = new(StringComparer.OrdinalIgnoreCase);

        public bool HasUnsavedChanges { get; set; }
        public string LoadError { get; private set; } = string.Empty;
        public int EntryCount => _entries.Count;

        public static string NormalizeSerial(string serial) => MetaDatManager.NormalizeSerial(serial);

        public void Load(string path)
        {
            _entries.Clear();
            LoadError = string.Empty;
            HasUnsavedChanges = false;
            try
            {
                if (!File.Exists(path))
                    return;
                var data = File.ReadAllBytes(path);
                if (data.Length < HeaderSize || data[0] != 'D' || data[1] != 'A' || data[2] != 'T' || data[3] != 1)
                {
                    LoadError = "Not a DAT file";
                    return;
                }
                uint entrySize = BitConverter.ToUInt32(data, 4);
                uint count = BitConverter.ToUInt32(data, 8);
                if (entrySize != EntrySize)
                {
                    LoadError = $"Unexpected entry size 0x{entrySize:X}";
                    return;
                }
                for (int i = 0; i < count; i++)
                {
                    int at = HeaderSize + i * EntryIndexSize;
                    if (at + EntryIndexSize > data.Length)
                        break;
                    var name = Encoding.ASCII.GetString(data, at, NameFieldLength).TrimEnd('\0').Trim();
                    uint fileNumber = BitConverter.ToUInt32(data, at + 12);
                    long offset = (long)fileNumber * EntrySize;
                    if (name.Length == 0 || offset + EntrySize > data.Length)
                        continue;
                    var raw = new byte[EntrySize];
                    Array.Copy(data, offset, raw, 0, EntrySize);
                    _entries[name] = raw;
                }
            }
            catch (Exception ex)
            {
                LoadError = ex.Message;
            }
        }

        public InfoDatEntry Get(string serial)
        {
            if (!_entries.TryGetValue(NormalizeSerial(serial), out var raw))
                return null;
            return new InfoDatEntry
            {
                Year = BitConverter.ToUInt16(raw, 0),
                Month = raw[2],
                Day = raw[3],
                Developer = ReadString(raw, OffDeveloper, TextLength),
                Publisher = ReadString(raw, OffPublisher, TextLength),
                Genre = ReadString(raw, OffGenre, TextLength),
                Players = ReadString(raw, OffPlayers, PlayersLength),
                Rating = raw[OffRating],
                Title = ReadString(raw, OffTitle, TextLength),
            };
        }

        public byte[] GetRaw(string serial) =>
            _entries.TryGetValue(NormalizeSerial(serial), out var raw) ? (byte[])raw.Clone() : null;

        public void SetRaw(string serial, byte[] raw)
        {
            var key = NormalizeSerial(serial);
            if (string.IsNullOrEmpty(key))
                return;
            if (raw == null)
                _entries.Remove(key);
            else if (raw.Length == EntrySize)
                _entries[key] = (byte[])raw.Clone();
            HasUnsavedChanges = true;
        }

        public void Set(string serial, InfoDatEntry e)
        {
            var raw = new byte[EntrySize];
            BitConverter.GetBytes(e.Year).CopyTo(raw, 0);
            raw[2] = e.Month;
            raw[3] = e.Day;
            WriteString(raw, OffDeveloper, TextLength, e.Developer);
            WriteString(raw, OffPublisher, TextLength, e.Publisher);
            WriteString(raw, OffGenre, TextLength, e.Genre);
            WriteString(raw, OffPlayers, PlayersLength, e.Players);
            raw[OffRating] = Math.Min(e.Rating, (byte)20);
            WriteString(raw, OffTitle, TextLength, e.Title);
            SetRaw(serial, raw);
        }

        /// <summary>Writes the file; with no entries the file is removed (the menu then has no INFO.DAT).</summary>
        public void Save(string path)
        {
            if (_entries.Count == 0)
            {
                if (File.Exists(path))
                {
                    Helper.TryMakeWritable(path);
                    File.Delete(path);
                }
                HasUnsavedChanges = false;
                return;
            }
            if (File.Exists(path))
                Helper.TryMakeWritable(path);
            var names = _entries.Keys.OrderBy(k => k, StringComparer.Ordinal).ToList();
            long indexEnd = HeaderSize + (long)names.Count * EntryIndexSize;
            uint first = (uint)Math.Max(1, (indexEnd + EntrySize - 1) / EntrySize);
            using var fs = new FileStream(path, FileMode.Create, FileAccess.Write);
            using var w = new BinaryWriter(fs);
            w.Write(new[] { (byte)'D', (byte)'A', (byte)'T', (byte)1 });
            w.Write(EntrySize);
            w.Write((uint)names.Count);
            w.Write(0u);
            for (int i = 0; i < names.Count; i++)
            {
                var name = new byte[NameFieldLength];
                var ascii = Encoding.ASCII.GetBytes(names[i]);
                Array.Copy(ascii, name, Math.Min(ascii.Length, NameFieldLength));
                w.Write(name);
                w.Write((ushort)0);
                w.Write(first + (uint)i);
            }
            w.Write(new byte[first * EntrySize - fs.Position]);
            foreach (var n in names)
                w.Write(_entries[n]);
            HasUnsavedChanges = false;
        }

        private static string ReadString(byte[] raw, int offset, int length)
        {
            int end = offset;
            while (end < offset + length && raw[end] != 0)
                end++;
            return Encoding.ASCII.GetString(raw, offset, end - offset);
        }

        private static void WriteString(byte[] raw, int offset, int length, string value)
        {
            var ascii = Encoding.ASCII.GetBytes(Scraper.TitleMatcher.ToAscii(value ?? string.Empty));
            Array.Copy(ascii, 0, raw, offset, Math.Min(ascii.Length, length - 1)); // always NUL-terminated
        }
    }
}
