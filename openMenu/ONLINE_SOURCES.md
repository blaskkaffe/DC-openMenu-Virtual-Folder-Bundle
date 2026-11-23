# Online sources for the Internet screen

Dreamcast-friendly sites (plain HTTP, simple HTML) to read information from. Noted 2026-11-23; paths marked "?" are not checked.

| Site | What | Used now |
|------|------|----------|
| `dc.dreamcastlive.net` | DreamcastLive for the Dreamcast: `/news.html`, `/dlc.html`; leaderboard pages? | News (`news_fetch.c`) |
| `dc.segaonline.net` | Sega Online, its start page `/` | News (`news_fetch.c`) |
| `www.dreampipe.net/leaderboards/` | Dreampipe's list of boards: Alien Front Online, ChuChu Rocket, Cruis'n USA, Gundam EFF vs Zeon, Internet Game Pack, Monaco Online, Monkey Ball, NBA 2K2, NCAA 2K2, NFL 2K2, Ooga Booga, PSO, Planet Ring, POD Speedzone, Quake III Arena, Samba De Amigo, Sega Swirl, SF Rush 2049, The Next Tetris, Toy Racer, Wetrix+, WSB 2K2. The page has only the game names, the scores are elsewhere? | not yet |
| `bb.dreampipe.net` | Dreampipe's web (BB) | not yet |
| `seganet.dreampipe.net` | Dreampipe's login | not yet |
| `dreamcastlive.net/leaderboards/` | 24 boards (https only): Alien Front Online, ChuChu Rocket, Culdcept II, Daytona USA, Golf Shiyouyo 2, Internet Game Pack, Gundam FvZ, Monaco, NBA/NCAA/NFL 2K2, The Next Tetris, Ooga Booga, PSO Arena, Planet Ring, POD Speedzone, Power Smash Tennis, Propeller Arena, Quake III Arena, Sega Swirl, Sega Tetris, Toy Racer, Vanishing Point, World Series Baseball 2K2 | list of boards in `lb_fetch.c` (6 of them) |
| `shumania.ddns.net` | Vanishing Point boards: `/vp_challenges.html`, `/vp_track_board.html`, `/vp_top100.html`, `/vp_combined_top100.html` | `lb_fetch.c` |
| `www.uefa.dreampipe.net` | Quake III rankings, `/Quake3/rankings.php` | `lb_fetch.c` |

The leaderboard page reader is `backend/lb_parse.c` (headings, table rows and list items as text lines, tested on snapshots of the boards above in `test/lb_parse_test.c`). A new board is a line in the `sources` table of `backend/lb_fetch.c`.
