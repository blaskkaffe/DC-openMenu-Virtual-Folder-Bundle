#include <stdio.h>
#include <stdlib.h>
#include "nj_model.h"
int main(int argc,char**argv){
  static unsigned char d[BIOS_ROM_SIZE]; FILE*f=fopen(argv[1],"rb"); size_t n=fread(d,1,sizeof d,f); fclose(f);
  static bios_rom rom; if(bios_rom_init(&rom,d,n)){puts("rom");return 1;}
  for(int a=2;a<argc;a++){int idx=atoi(argv[a]); nj_object o; uint32_t ad=bios_model_addr(&rom,idx); if(!ad||nj_object_load(&rom,ad,&o)){printf("model %d: none\n",idx);continue;}
   float mn[3]={1e9,1e9,1e9},mx[3]={-1e9,-1e9,-1e9};
   for(int i=0;i<o.count;i++){nj_node*nd=&o.nodes[i]; printf("model %d node %d parent %d pos %.2f %.2f %.2f scl %.2f %.2f %.2f ang %d %d %d mesh %d\n",idx,i,nd->parent,nd->pos.x,nd->pos.y,nd->pos.z,nd->scl.x,nd->scl.y,nd->scl.z,nd->ang[0],nd->ang[1],nd->ang[2],nd->mesh!=0);
     if(nd->mesh) for(int v=0;v<nd->mesh->nverts;v++){ if(!nd->mesh->verts[v].valid)continue; float p[3]={nd->mesh->verts[v].pos.x,nd->mesh->verts[v].pos.y,nd->mesh->verts[v].pos.z}; for(int k=0;k<3;k++){if(p[k]<mn[k])mn[k]=p[k];if(p[k]>mx[k])mx[k]=p[k];}}}
   printf("model %d bbox x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f\n",idx,mn[0],mx[0],mn[1],mx[1],mn[2],mx[2]);}
  return 0;}
