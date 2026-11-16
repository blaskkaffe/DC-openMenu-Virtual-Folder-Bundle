/* favorites_test: the fork flags byte of the favorites file survives pack / unpack and the VMS wrapper, and a file written before it existed reads
 * as 0. Host build (no console needed), from openMenu/src:
 *   gcc -Wall -Iopenmenu/src -Iopenmenu_shared/include -Iopenmenu_shared/src -o favorites_test openmenu/test/favorites_test.c openmenu/src/netprefs.c && ./favorites_test */
#include "favorites.c" /* the module itself, to reach its static pack / unpack */
#include <stdio.h>
const gd_item* list_find_by_hash(unsigned int h){(void)h;return NULL;}
int main(void){
 uint8_t buf[FAV_PAYLOAD]; int bad=0;
 hash_count=2; hashes[0]=0x11223344; hashes[1]=0x55667788; fork_flags=FORK_FLAG_BEAT;
 int len=fav_pack(buf);
 hash_count=0; fork_flags=0;
 if(fav_unpack(buf,len)!=0||hash_count!=2||hashes[1]!=0x55667788||fork_flags!=FORK_FLAG_BEAT){printf("FAIL roundtrip\n");bad++;}
 /* a file from before the flags: byte 5 is 0 */
 buf[5]=0; fork_flags=9; if(fav_unpack(buf,len)!=0||fork_flags!=0){printf("FAIL old file\n");bad++;}
 /* the VMS wrapper keeps it too */
 static uint8_t file[VMS_DATA+FAV_PAYLOAD]; fork_flags=FORK_FLAG_BEAT; int sz=vms_wrap(file); fork_flags=0; hash_count=0;
 if(vms_unwrap(file,sz)!=0||fork_flags!=FORK_FLAG_BEAT){printf("FAIL vms\n");bad++;}
 /* the Internet settings ride in the tail, also through the VMS wrapper; a file without the tail (an older build's) reads as the defaults */
 np_reset(); np_set_iface(4); np_set_net(NP_NET_DCNET); np_set_game_net(77,NP_NET_DCNOW); np_player_toggle("Ann");
 hash_count=1; hashes[0]=5; fork_flags=0; sz=vms_wrap(file); np_reset(); hash_count=0;
 if(vms_unwrap(file,sz)!=0||np_iface()!=4||np_net()!=NP_NET_DCNET||np_game_net_saved(77)!=NP_NET_DCNOW||!np_player_starred("Ann")||hash_count!=1){printf("FAIL tail\n");bad++;}
 len=fav_pack(buf); np_reset(); np_set_iface(2); fav_unpack(buf,8+4*1); /* the old size: hashes only */
 if(np_iface()!=0||hash_count!=1){printf("FAIL no tail\n");bad++;}
 printf(bad?"%d failed\n":"favorites flags ok\n",bad);return bad!=0;}
