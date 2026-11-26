using System;
using System.Collections.Generic;

namespace GDMENUCardManager.Core
{
    /// <summary>The pictures of a game the artwork window edits: the front cover and the rest of the 3D case.</summary>
    public enum ArtSlot
    {
        /// <summary>BOX.DAT (and the icon in ICON.DAT): handled by the artwork window itself, as before.</summary>
        Front,
        /// <summary>BACK.DAT: the back cover, 256x256 (the label is 139 x 120 mm).</summary>
        Back,
        /// <summary>SPINE.DAT: the spine label, 32x256, reading upwards.</summary>
        Spine,
        /// <summary>DISCUSER.DAT: a disc label chosen by hand, 128x128. It wins over the disc's own 0GDTEX.PVR.</summary>
        Disc
    }

    /// <summary>
    /// Staged changes to the back cover, spine label and disc label of one game, the same way the artwork window stages
    /// the front cover: nothing reaches the DAT files until Apply, and the DATs are written by Save Changes.
    /// </summary>
    public class ArtworkSlots
    {
        private readonly Manager _manager;
        private string _serial = string.Empty;
        private readonly Dictionary<ArtSlot, byte[]> _pending = new Dictionary<ArtSlot, byte[]>();
        private readonly HashSet<ArtSlot> _deletes = new HashSet<ArtSlot>();
        private (string Style, string Size)? _case; // the case style and size (CASES.INI), when changed

        public ArtworkSlots(Manager manager)
        {
            _manager = manager;
        }

        /// <summary>Another game: what was staged for the last one is dropped.</summary>
        public void Reset(string serial)
        {
            _serial = serial ?? string.Empty;
            _pending.Clear();
            _deletes.Clear();
            _case = null;
        }

        public bool HasChanges => _pending.Count > 0 || _deletes.Count > 0 || _case != null;

        /// <summary>The game's case style and size ("" automatic), with the staged change.</summary>
        public (string Style, string Size) CurrentCase =>
            _case ?? _manager.CaseStyles?.Get(_serial) ?? ("", "");

        public void StageCase(string style, string size)
        {
            var stored = _manager.CaseStyles?.Get(_serial) ?? ("", "");
            _case = stored.Style == (style ?? "") && stored.Size == (size ?? "") ? null : ((string, string)?)(style ?? "", size ?? "");
        }

        /// <summary>The picture the DAT holds now (PVR), or null.</summary>
        public byte[] Stored(ArtSlot slot)
        {
            switch (slot)
            {
                case ArtSlot.Back: return _manager.BackDat?.GetPvrDataForSerial(_serial);
                case ArtSlot.Spine: return _manager.SpineDat?.GetPvrDataForSerial(_serial);
                case ArtSlot.Disc: return _manager.DiscUserDat?.GetPvrDataForSerial(_serial);
                default: return _manager.BoxDat?.GetPvrDataForSerial(_serial);
            }
        }

        /// <summary>The picture with the staged change (PVR), or null when there is none or it is to be removed.</summary>
        public byte[] Current(ArtSlot slot)
        {
            if (_deletes.Contains(slot))
                return null;
            return _pending.TryGetValue(slot, out var pvr) ? pvr : Stored(slot);
        }

        /// <summary>
        /// The disc label the menu shows when none was chosen by hand: the disc's own (0GDTEX.PVR, when it could be read),
        /// otherwise a scraped one (DISCART.DAT). Null when there is neither (the menu then uses the icon).
        /// </summary>
        public byte[] DefaultDiscLabel(byte[] gdtex)
        {
            if (gdtex != null)
            {
                try
                {
                    return EncodeDiscLabel(gdtex);
                }
                catch
                {
                    // fall through to the scraped one
                }
            }
            return _manager.DiscArtDat?.GetPvrDataForSerial(_serial);
        }

        /// <summary>A picture file in the slot's size and layout.</summary>
        public static byte[] EncodeFile(ArtSlot slot, string imagePath)
        {
            switch (slot)
            {
                case ArtSlot.Back: return PvrEncoder.EncodeFromFile(imagePath);
                case ArtSlot.Spine: return PvrEncoder.EncodeSpineFromFile(imagePath);
                case ArtSlot.Disc: return PvrEncoder.EncodeIconFromFile(imagePath);
                default: throw new ArgumentException("The front cover is encoded by the artwork window", nameof(slot));
            }
        }

        /// <summary>A disc's own label (0GDTEX.PVR, any PVR format) as a 128x128 disc label.</summary>
        public static byte[] EncodeDiscLabel(byte[] gdtex)
        {
            var decoded = new PuyoTools.PvrTexture().GetDecoded(gdtex);
            return PvrEncoder.EncodeIconFromPixels(decoded.Item1, decoded.Item2, decoded.Item3);
        }

        public void Stage(ArtSlot slot, byte[] pvr)
        {
            if (slot == ArtSlot.Front || pvr == null)
                return;
            _pending[slot] = pvr;
            _deletes.Remove(slot);
        }

        public void StageDelete(ArtSlot slot)
        {
            if (slot == ArtSlot.Front)
                return;
            _pending.Remove(slot);
            _deletes.Add(slot);
        }

        /// <summary>Puts the staged pictures into the DATs in memory (written by Save Changes in the main window).</summary>
        public void Apply()
        {
            foreach (var kv in _pending)
            {
                switch (kv.Key)
                {
                    case ArtSlot.Back: _manager.BackDat?.SetArtworkForSerial(_serial, kv.Value); break;
                    case ArtSlot.Spine: _manager.SpineDat?.SetArtworkForSerial(_serial, kv.Value); break;
                    case ArtSlot.Disc: _manager.DiscUserDat?.SetIconForSerial(_serial, kv.Value); break;
                }
            }
            foreach (var slot in _deletes)
            {
                switch (slot)
                {
                    case ArtSlot.Back: _manager.BackDat?.DeleteEntryForSerial(_serial); break;
                    case ArtSlot.Spine: _manager.SpineDat?.DeleteEntryForSerial(_serial); break;
                    case ArtSlot.Disc: _manager.DiscUserDat?.DeleteEntryForSerial(_serial); break;
                }
            }
            if (_case != null)
                _manager.CaseStyles?.Set(_serial, _case.Value.Style, _case.Value.Size);
            _pending.Clear();
            _deletes.Clear();
            _case = null;
        }

        /// <summary>The line under the preview: what the slot is and where it goes.</summary>
        public static string Hint(ArtSlot slot, bool chosen, bool ownLabel)
        {
            switch (slot)
            {
                case ArtSlot.Back:
                    return "Back cover (BACK.DAT). The picture fills the back label (139 x 120 mm).";
                case ArtSlot.Spine:
                    return "Spine label (SPINE.DAT). A tall picture that reads upwards; a wide one is turned.";
                case ArtSlot.Disc:
                    if (chosen)
                        return "Disc label chosen here. It is used instead of the disc's own label.";
                    return ownLabel
                        ? "Showing the disc's own label (0GDTEX.PVR from the image). Browse to use another picture."
                        : "No disc label chosen. Browse for a picture, or Import From Disc when the image has one.";
                default:
                    return "Front cover (BOX.DAT, and the icon in ICON.DAT).";
            }
        }
    }
}
