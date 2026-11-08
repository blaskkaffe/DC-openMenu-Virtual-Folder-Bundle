/* Offline checks of the scraper: title matching, ScreenScraper answers, META/INFO entries, PVR conversion.
 * dotnet run -c Release --project src/GDMENUCardManager.ScraperTests */
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using GDMENUCardManager.Core;
using GDMENUCardManager.Core.Scraper;

class P
{
    static int fails;

    // A slice of github.com/libretro-thumbnails/Sega_-_Dreamcast Named_Boxarts (file names without .png)
    static readonly string[] LibretroNames =
    {
        "Crazy Taxi (Europe)",
        "Crazy Taxi (Japan)",
        "Crazy Taxi (USA)",
        "Crazy Taxi 2 (Europe) (En,Ja,Fr,De,Es)",
        "Crazy Taxi 2 (Japan) (En,Ja)",
        "Crazy Taxi 2 (Japan) (Taikenban)",
        "Crazy Taxi 2 (USA)",
        "De La Jet Set Radio (Japan) (En,Ja,Fr,De,Es)",
        "Jet Grind Radio (USA)",
        "Jet Set Radio (Europe) (En,Fr,De,Es)",
        "Jet Set Radio (Japan)",
        "Rez (Europe) (En,Ja,Fr,De,Es,It)",
        "Rez (Japan) (En,Ja,Fr,De,Es,It)",
        "Shenmue (Europe) (En,Fr,De,Es) (Disc 1)",
        "Shenmue (Europe) (En,Fr,De,Es) (Disc 2)",
        "Shenmue (Europe) (En,Fr,De,Es) (Disc 3)",
        "Shenmue (Europe) (En,Fr,De,Es) (Shenmue Passport)",
        "Shenmue (Europe) (En,Fr,De,Es)",
        "Shenmue (USA) (Disc 1)",
        "Shenmue (USA) (Disc 2)",
        "Shenmue (USA) (Disc 3)",
        "Shenmue (USA) (Shenmue Passport)",
        "Shenmue (USA)",
        "Shenmue - Ichishou Yokosuka (Japan) (Disc 1)",
        "Shenmue - Ichishou Yokosuka (Japan) (Disc 2)",
        "Shenmue - Ichishou Yokosuka (Japan) (Disc 3)",
        "Shenmue - Ichishou Yokosuka (Japan) (Shenmue Passport) (Rev C)",
        "Shenmue - Ichishou Yokosuka (Japan) (Shenmue Passport) (Rev D)",
        "Shenmue - Ichishou Yokosuka (Japan)",
        "Shenmue II (Europe) (En,Fr,De,Es) (Disc 1)",
        "Shenmue II (Europe) (En,Fr,De,Es) (Disc 2)",
        "Shenmue II (Europe) (En,Fr,De,Es) (Disc 3)",
        "Shenmue II (Europe) (En,Fr,De,Es) (Disc 4)",
        "Shenmue II (Europe) (En,Fr,De,Es)",
        "Shenmue II (Japan) (Disc 1)",
        "Shenmue II (Japan) (Disc 2)",
        "Shenmue II (Japan) (Disc 3)",
        "Shenmue II (Japan) (Disc 4)",
        "Shenmue II (Japan)",
        "Soulcalibur (Europe) (En,Fr,De,Es)",
        "Soulcalibur (Europe)(En,Fr,De,Es)",
        "Soulcalibur (Japan) (Tentou Taikenban)",
        "Soulcalibur (Japan)",
        "Soulcalibur (USA)",
        "US Shenmue (Japan) (En,Ja) (Disc 1)",
        "US Shenmue (Japan) (En,Ja) (Disc 2)",
        "US Shenmue (Japan) (En,Ja) (Disc 3)",
        "US Shenmue (Japan) (En,Ja) (Shenmue Passport)",
        "Virtua Tennis (USA)",
        "Virtua Tennis - Sega Professional Tennis (Europe) (En,Fr,De,Es)",
        "Virtua Tennis 2 - Sega Professional Tennis (Europe) (En,Fr,De,Es)"
    };

    static void Check(bool ok, string what)
    {
        if (!ok)
        {
            fails++;
            Console.WriteLine("FAIL " + what);
        }
    }

    static void Eq<T>(T got, T want, string what)
    {
        if (!EqualityComparer<T>.Default.Equals(got, want))
        {
            fails++;
            Console.WriteLine($"FAIL {what}: got <{got}> want <{want}>");
        }
    }

    const string SsAnswer = @"{
 ""header"": {""APIversion"": ""2.0"", ""success"": ""true""},
 ""response"": {
  ""ssuser"": {""id"": ""someone"", ""maxthreads"": ""1"", ""requeststoday"": ""12"", ""maxrequestsperday"": ""20000""},
  ""jeu"": {
   ""id"": ""5037"",
   ""noms"": [
     {""region"": ""ss"", ""text"": ""Jet Set Radio""},
     {""region"": ""us"", ""text"": ""Jet Grind Radio""},
     {""region"": ""eu"", ""text"": ""Jet Set Radio""},
     {""region"": ""jp"", ""text"": ""Jet Set Radio""}
   ],
   ""systeme"": {""id"": ""23"", ""text"": ""Dreamcast""},
   ""editeur"": {""id"": ""3"", ""text"": ""SEGA""},
   ""developpeur"": {""id"": ""1210"", ""text"": ""Smilebit""},
   ""joueurs"": {""text"": ""1""},
   ""note"": {""text"": ""17""},
   ""synopsis"": [
     {""langue"": ""fr"", ""text"": ""Tokyo-to est envahi par des gangs…""},
     {""langue"": ""en"", ""text"": ""Tokyo-to is ruled by gangs. Beat’s crew, the GGs, tag the city&#39;s streets – and the police don’t like it.""}
   ],
   ""dates"": [
     {""region"": ""jp"", ""text"": ""2000-06-29""},
     {""region"": ""us"", ""text"": ""2000-10-30""},
     {""region"": ""eu"", ""text"": ""2000-11-24""}
   ],
   ""genres"": [
     {""id"": ""10"", ""noms"": [{""langue"": ""en"", ""text"": ""Action""}, {""langue"": ""fr"", ""text"": ""Action""}]},
     {""id"": ""2918"", ""noms"": [{""langue"": ""en"", ""text"": ""Sports""}, {""langue"": ""fr"", ""text"": ""Sport""}]}
   ],
   ""medias"": [
     {""type"": ""ss"", ""region"": ""us"", ""url"": ""https://x/mediaJeu.php?media=ss(us)"", ""format"": ""png""},
     {""type"": ""box-2D"", ""region"": ""jp"", ""url"": ""https://x/mediaJeu.php?media=box-2D(jp)"", ""format"": ""png""},
     {""type"": ""box-2D"", ""region"": ""eu"", ""url"": ""https://x/mediaJeu.php?media=box-2D(eu)"", ""format"": ""png""},
     {""type"": ""box-2D"", ""region"": ""us"", ""url"": ""https://x/mediaJeu.php?media=box-2D(us)"", ""format"": ""png""},
     {""type"": ""box-2D-back"", ""region"": ""eu"", ""url"": ""https://x/mediaJeu.php?media=box-2D-back(eu)"", ""format"": ""png""},
     {""type"": ""support-2D"", ""region"": ""jp"", ""url"": ""https://x/mediaJeu.php?media=support-2D(jp)"", ""format"": ""png""}
   ]
  }
 }
}";

    static void Main(string[] args)
    {
        // ---- titles
        Eq(TitleMatcher.Normalize("The House of the Dead 2 (USA)"), "house of the dead 2", "normalize the/brackets");
        Eq(TitleMatcher.Normalize("Shenmue II"), "shenmue 2", "roman numerals");
        Eq(TitleMatcher.Normalize("Pokémon & Friends: Édition"), "pokemon friends edition", "accents and &");
        Eq(TitleMatcher.Similarity("CRAZY TAXI", "Crazy Taxi (USA)"), 1.0, "case");
        Check(TitleMatcher.Similarity("Crazy Taxi", "Crazy Taxi 2 (USA)") < 0.82, "numbers must agree");
        Eq(TitleMatcher.ToAscii("Beat’s “crew” – Tokyo… café"), "Beat's \"crew\" - Tokyo... cafe", "ascii");

        var regionsEu = TitleMatcher.RegionOrder(ScrapeRegion.Auto, "E");
        var regionsUs = TitleMatcher.RegionOrder(ScrapeRegion.Auto, "JUE");
        var regionsJp = TitleMatcher.RegionOrder(ScrapeRegion.Auto, "J");
        Eq(regionsEu[0], "eu", "PAL disc prefers eu");
        Eq(regionsUs[0], "us", "world disc prefers us");
        Eq(regionsJp[0], "jp", "Japanese disc prefers jp");

        // real libretro names
        var names = LibretroNames.ToList();
        Eq(TitleMatcher.BestMatch(names, new[] { "Crazy Taxi" }, regionsUs), "Crazy Taxi (USA)", "crazy taxi us");
        Eq(TitleMatcher.BestMatch(names, new[] { "CRAZY TAXI" }, regionsEu), "Crazy Taxi (Europe)", "crazy taxi eu");
        Eq(TitleMatcher.BestMatch(names, new[] { "Crazy Taxi 2" }, regionsUs), "Crazy Taxi 2 (USA)", "crazy taxi 2");
        Eq(TitleMatcher.BestMatch(names, new[] { "Shenmue" }, regionsUs, 2), "Shenmue (USA) (Disc 2)", "shenmue disc 2");
        Eq(TitleMatcher.BestMatch(names, new[] { "Shenmue" }, regionsEu, 0), "Shenmue (Europe) (En,Fr,De,Es)", "shenmue no disc");
        Eq(TitleMatcher.BestMatch(names, new[] { "Shenmue II" }, regionsEu, 0), "Shenmue II (Europe) (En,Fr,De,Es)", "shenmue II");
        Eq(TitleMatcher.BestMatch(names, new[] { "Jet Set Radio" }, regionsUs), "Jet Set Radio (Europe) (En,Fr,De,Es)", "jsr: us title differs, eu cover");
        Eq(TitleMatcher.BestMatch(names, new[] { "Jet Set Radio", "Jet Grind Radio" }, regionsUs), "Jet Grind Radio (USA)", "jsr with ss us name");
        Eq(TitleMatcher.BestMatch(names, new[] { "Not A Real Dreamcast Game" }, regionsUs), null, "no match");
        Eq(TitleMatcher.BestMatch(names, new[] { "Soul Calibur" }, regionsEu), "Soulcalibur (Europe) (En,Fr,De,Es)", "joined words");
        Eq(TitleMatcher.BestMatch(names, new[] { "Rez" }, regionsUs), "Rez (Europe) (En,Ja,Fr,De,Es,It)", "no US release: Europe next");

        // ---- ScreenScraper answer
        var ss = new ScreenScraperClient(null, new ScraperSettings());
        var g = ss.Parse(SsAnswer, regionsEu, "en");
        Check(g != null, "parse game");
        Eq(g.MatchedName, "Jet Set Radio", "eu name");
        Eq(g.Names["us"], "Jet Grind Radio", "us name");
        Eq(g.FrontUrl, "https://x/mediaJeu.php?media=box-2D(eu)&maxwidth=512&maxheight=512", "eu front");
        Eq(g.BackUrl, "https://x/mediaJeu.php?media=box-2D-back(eu)&maxwidth=512&maxheight=512", "eu back");
        Eq(g.DiscUrl, "https://x/mediaJeu.php?media=support-2D(jp)&maxwidth=512&maxheight=512", "disc falls back to jp");
        Eq(g.Developer, "Smilebit", "developer");
        Eq(g.Publisher, "SEGA", "publisher");
        Eq(g.ReleaseDate, "2000-11-24", "eu date");
        Eq(string.Join("|", g.Genres), "Action|Sports", "genres");
        Eq(g.Rating, 17, "rating");
        Eq(ss.RequestsToday, 12, "requests today");
        Eq(ss.MaxRequestsPerDay, 20000, "quota");
        var gUs = ss.Parse(SsAnswer, regionsUs, "fr");
        Eq(gUs.MatchedName, "Jet Grind Radio", "us name for us disc");
        Eq(gUs.ReleaseDate, "2000-10-30", "us date");
        Check(gUs.Synopsis.StartsWith("Tokyo-to est"), "french synopsis");
        Eq(string.Join("|", gUs.Genres), "Action|Sport", "french genres");
        Check(ss.Parse("{\"response\":{\"jeu\":[]}}", regionsUs, "en") == null, "empty game");

        // search answer: the best of several games
        var search = "{\"response\":{\"jeux\":[{\"noms\":[{\"region\":\"us\",\"text\":\"Crazy Taxi 2\"}]},{\"noms\":[{\"region\":\"us\",\"text\":\"Crazy Taxi\"}],\"medias\":[{\"type\":\"box-2D\",\"region\":\"us\",\"url\":\"https://y/ct.png\"}]}]}}";
        var sg = ss.ParseSearch(search, "CRAZY TAXI", regionsUs, "en");
        Eq(sg?.MatchedName, "Crazy Taxi", "search picks the right game");
        Eq(sg?.FrontUrl, "https://y/ct.png", "search media (not mediaJeu: no maxwidth)");
        Check(ss.ParseSearch("{\"response\":{\"jeux\":[{}]}}", "x", regionsUs, "en") == null, "search with nothing");

        // ---- libretro tree
        var tree = "{\"sha\":\"x\",\"tree\":[{\"path\":\"Named_Boxarts\"},{\"path\":\"Named_Boxarts/Rez (USA).png\"},{\"path\":\"Named_Snaps/Rez (USA).png\"}],\"truncated\":false}";
        Eq(string.Join("|", LibretroThumbnailsClient.ParseTree(tree)), "Rez (USA)", "tree");

        // ---- text into META.DAT / INFO.DAT
        Eq(ScrapeText.MaxPlayers("1-4"), 4, "players 1-4");
        Eq(ScrapeText.MaxPlayers("2"), 2, "players 2");
        Eq(ScrapeText.MaxPlayers(""), 0, "players none");
        Eq(ScrapeText.GenreBits(new[] { "Action", "Sports" }), (ushort)(1 | 8), "genre bits");
        Eq(ScrapeText.GenreBits(new[] { "Shoot'em Up / Vertical" }), (ushort)(1 << 11), "shmup");
        Eq(ScrapeText.GenreBits(new[] { "Shooter / Light gun" }), (ushort)(1 << 4), "lightgun");
        Eq(ScrapeText.GenreBits(new[] { "Role playing game" }), (ushort)(1 << 10), "rpg");
        Eq(ScrapeText.GenreBits(new[] { "Plate-Forme" }), (ushort)(1 << 9), "plateforme fr");
        var longText = string.Join(" ", Enumerable.Repeat("Sentence number one is here.", 30));
        var cut = ScrapeText.CleanSynopsis(longText, 375);
        Check(cut.Length <= 375 && cut.EndsWith("."), "synopsis cut at a sentence: " + cut.Length);
        Eq(ScrapeText.CleanSynopsis("A &amp; B\n\n  C", 375), "A & B C", "html and spaces");

        var meta = ScrapeText.BuildMeta(null, g, false);
        Eq(meta.Length, 384, "meta size");
        Eq(meta[0], (byte)1, "meta players");
        Eq(BitConverter.ToUInt16(meta, 5), (ushort)9, "meta genre");
        var desc = System.Text.Encoding.ASCII.GetString(meta, 8, Array.IndexOf(meta, (byte)0, 8) - 8);
        Eq(desc, "Tokyo-to is ruled by gangs. Beat's crew, the GGs, tag the city's streets - and the police don't like it.", "meta description");
        // existing entry: VMU blocks, accessories and an existing description are kept without Overwrite
        var old = new byte[384];
        old[1] = 13; old[2] = 0x05; old[4] = 1;
        System.Text.Encoding.ASCII.GetBytes("Mine.").CopyTo(old, 8);
        var merged = ScrapeText.BuildMeta(old, g, false);
        Eq(merged[1], (byte)13, "vmu kept");
        Eq(merged[2], (byte)5, "accessories kept");
        Eq(merged[4], (byte)1, "network kept");
        Eq(System.Text.Encoding.ASCII.GetString(merged, 8, 5), "Mine.", "description kept");
        var over = ScrapeText.BuildMeta(old, g, true);
        Check(System.Text.Encoding.ASCII.GetString(over, 8, 8) == "Tokyo-to", "description overwritten");
        Eq(over[1], (byte)13, "vmu kept with overwrite");

        var info = ScrapeText.BuildInfo(g, regionsEu);
        Eq(info.Year, (ushort)2000, "year");
        Eq(info.Month, (byte)11, "month");
        Eq(info.Day, (byte)24, "day");
        Eq(info.Genre, "Action, Sports", "genre text");
        Eq(info.Title, "Jet Set Radio", "title");

        // INFO.DAT round trip, then for the menu's reader
        var dat = new InfoDatManager();
        dat.Set("MK-51058", info);
        dat.Set("T-9504N", new InfoDatEntry { Year = 1999, Developer = "Café Éditeur", Publisher = new string('x', 80) });
        var tmp = Path.Combine(Path.GetTempPath(), "scrapetest_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tmp);
        var path = Path.Combine(tmp, "INFO.DAT");
        dat.Save(path);
        var back = new InfoDatManager();
        back.Load(path);
        Eq(back.EntryCount, 2, "entries");
        Eq(back.Get("MK51058")?.Developer, "Smilebit", "read back developer");
        Eq(back.Get("T9504N")?.Developer, "Cafe Editeur", "ascii on write");
        Eq(back.Get("T9504N")?.Publisher.Length, 59, "cut to the field");
        Eq(back.Get("MK51058")?.Month, (byte)11, "read back month");

        // ---- pictures: shrunk and converted to PVR
        var png = Path.Combine(tmp, "cover.png");
        using (var img = new SixLabors.ImageSharp.Image<SixLabors.ImageSharp.PixelFormats.Bgra32>(400, 300, new SixLabors.ImageSharp.PixelFormats.Bgra32(200, 40, 40, 255)))
            SixLabors.ImageSharp.ImageExtensions.SaveAsPng(img, png);
        Eq(PvrEncoder.EncodeFromFile(png).Length, PvrEncoder.TotalPvrSize, "front 256x256 PVR");
        Eq(PvrEncoder.EncodeIconFromFile(png).Length, PvrEncoder.TotalIconPvrSize, "icon 128x128 PVR");
        var disc = ArtScraper.EncodeDiscLabel(png);
        Eq(disc.Length, PvrEncoder.TotalIconPvrSize, "disc 128x128 PVR");
        Eq(System.Text.Encoding.ASCII.GetString(disc, 16, 4), "PVRT", "PVRT header");
        Check(disc[0x18] != 1, "padded disc label has alpha (not RGB565)");
        Directory.Delete(tmp, true);

        Console.WriteLine(fails == 0 ? "scraper tests: ok" : $"scraper tests: {fails} failed");
        Environment.Exit(fails == 0 ? 0 : 1);
    }
}
