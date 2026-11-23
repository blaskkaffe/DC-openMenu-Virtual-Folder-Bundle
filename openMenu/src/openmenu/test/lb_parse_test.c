/* Host test: gcc -I src/openmenu/src src/openmenu/src/backend/lb_parse.c src/openmenu/test/lb_parse_test.c */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "backend/lb_parse.h"

int
main(void) {
    static lb_line_t l[LB_LINES_MAX];
    /* Quake III Arena: one table */
    const char* q3 =
        "<html><head><style>td{x}</style></head><body><h1>Quake 3 Arena Player Rankings</h1><p>Top 50</p>"
        "<table><thead><tr><th>#</th><th>Name</th><th>Wins</th><th>F/D Ratio</th></tr></thead><tbody>\n"
        "<tr><td>1</td><td>*TacT*</td><td>2</td><td>0.79</td></tr>\n<tr><td>2</td><td>Xiden</td><td>1</td><td>2.00</td></tr></tbody></table></body></html>";
    int n = lb_parse_html(q3, l, LB_LINES_MAX);
    assert(n == 4);
    assert(l[0].kind == LB_HEAD && !strcmp(l[0].text, "Quake 3 Arena Player Rankings"));
    assert(l[1].kind == LB_COLS && !strcmp(l[1].text, "#  Name  Wins  F/D Ratio"));
    assert(l[2].kind == LB_ROW && !strcmp(l[2].text, "1  *TacT*  2  0.79"));
    assert(!strcmp(l[3].text, "2  Xiden  1  2.00"));
    /* Vanishing Point: the Flag column is a number of a flag picture, left out */
    const char* vp =
        "<h1>Top 100 Global Rankers</h1><table><thead><tr><th>Place</th><th>Flag</th><th>Racer</th><th>Points</th></tr></thead><tbody>"
        "<tr><td>1</td><td>18</td><td>TruenoGT</td><td>1886</td></tr><tr><td>2</td><td>27</td><td>Sqigley</td><td>1866</td></tr></tbody></table>"
        "<h2>Red Rock (fwd)</h2><table><tr><th>Place</th><th>Racer</th><th>Time</th></tr><tr><td>1</td><td>Sqigley</td><td>0'49&quot;63</td></tr></table>";
    n = lb_parse_html(vp, l, LB_LINES_MAX);
    assert(n == 7);
    assert(!strcmp(l[1].text, "Place  Racer  Points"));
    assert(!strcmp(l[2].text, "1  TruenoGT  1886") && !strcmp(l[3].text, "2  Sqigley  1866"));
    assert(l[4].kind == LB_HEAD && !strcmp(l[4].text, "Red Rock (fwd)"));
    assert(!strcmp(l[6].text, "1  Sqigley  0'49\"63")); /* the second table has no Flag column: nothing is dropped */
    /* PSO: headings and lists, with the prime and double prime marks of the times */
    const char* pso =
        "<ul class=\"menu\"><li><a href=\"/\">Home</a></li></ul><h2>Challenge Mode (Stage 1)</h2><ol>"
        "<li><a href=\"x.jpg\">12\xE2\x80\xB2 42\xE2\x80\xB3</a><br>Team: cmode DOPE, Afro Thunder<br>Date: 8/22/2026</li>"
        "<li><a>19&#8242; 12&#8243;</a><br>Team: Teal\xE2\x80\x99" "c<br>Date: 5/6/2021</li></ol>";
    n = lb_parse_html(pso, l, LB_LINES_MAX);
    assert(n == 4); /* the menu's item is a line too: a page's own lists cannot be told apart */
    assert(l[1].kind == LB_HEAD && !strcmp(l[1].text, "Challenge Mode (Stage 1)"));
    assert(!strcmp(l[2].text, "12' 42\" Team: cmode DOPE, Afro Thunder Date: 8/22/2026"));
    assert(!strcmp(l[3].text, "19' 12\" Team: Teal'c Date: 5/6/2021"));
    /* limits and junk */
    assert(lb_parse_html(vp, l, 3) == 3);
    assert(lb_parse_html("", l, 3) == 0);
    assert(lb_parse_html("<table><tr><td>a<td>b</table><script>var x='<h1>no</h1>';</script>1 < 2", l, 3) == 1);
    puts("lb_parse ok");
    return 0;
}
