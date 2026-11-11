/* Identity-keyed added-roster presets. Original memory-card cells stay unchanged. */
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t rp_items[326][3][8];static int rp_loaded;
static uint32_t rp_hash(const void*p,size_t n){const unsigned char*b=p;uint32_t h=2166136261u;for(size_t i=0;i<n;i++)h=(h^b[i])*16777619u;return h;}
static int rp_path(char*p,size_t n,const char*s){const char*root=getenv("BT3_SAVES");if(!root||!*root)root="saves";int k=snprintf(p,n,"%s/roster-presets-v1%s",root,s);return k>0&&(size_t)k<n;}
static void rp_load(void){char path[1100];FILE*f;uint32_t h[3];if(rp_loaded)return;rp_loaded=1;if(!rp_path(path,sizeof(path),".dat"))return;f=fopen(path,"rb");if(!f)return;if(fread(h,sizeof(h),1,f)!=1||h[0]!=0x31505242||h[1]!=sizeof(rp_items)||fread(rp_items,1,sizeof(rp_items),f)!=sizeof(rp_items)||fgetc(f)!=EOF||h[2]!=rp_hash(rp_items,sizeof(rp_items)))memset(rp_items,0,sizeof(rp_items));fclose(f);}
int Port_RosterPresetRead(int cid,int set,uint16_t*out){if(!out||cid<0||cid>=326||set<0||set>=3)return 0;rp_load();memcpy(out,rp_items[cid][set],16);return 1;}
int Port_RosterPresetWrite(int cid,int set,const uint16_t*in){char path[1100],tmp[1100];FILE*f;uint16_t old[8];uint32_t h[3];if(!in||cid<0||cid>=326||set<0||set>=3)return 0;for(int i=0;i<8;i++)if(in[i]>350)return 0;rp_load();memcpy(old,rp_items[cid][set],16);memcpy(rp_items[cid][set],in,16);if(!rp_path(path,sizeof(path),".dat")||!rp_path(tmp,sizeof(tmp),".tmp"))goto fail;f=fopen(tmp,"wb");if(!f)goto fail;h[0]=0x31505242;h[1]=sizeof(rp_items);h[2]=rp_hash(rp_items,sizeof(rp_items));int ok=fwrite(h,sizeof(h),1,f)==1&&fwrite(rp_items,1,sizeof(rp_items),f)==sizeof(rp_items);if(fclose(f))ok=0;if(!ok)goto fail;
#ifdef _WIN32
 if(!MoveFileExA(tmp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))goto fail;
#else
 if(rename(tmp,path))goto fail;
#endif
 return 1;fail:memcpy(rp_items[cid][set],old,16);return 0;}

/* Added characters have their own native-style capacity progress, never a borrowed card cell. */
static uint32_t rp_level[326][2];static int rp_level_loaded;
static int rp_level_path(char*p,size_t n,const char*s){const char*root=getenv("BT3_SAVES");if(!root||!*root)root="saves";int k=snprintf(p,n,"%s/roster-capacity-v1%s",root,s);return k>0&&(size_t)k<n;}
static void rp_level_load(void){char path[1100];uint32_t h[3];FILE*f;if(rp_level_loaded)return;rp_level_loaded=1;if(!rp_level_path(path,sizeof(path),".dat")||!(f=fopen(path,"rb")))return;int ok=fread(h,sizeof(h),1,f)==1&&h[0]==0x314c5242&&h[1]==sizeof(rp_level)&&fread(rp_level,1,sizeof(rp_level),f)==sizeof(rp_level)&&fgetc(f)==EOF&&h[2]==rp_hash(rp_level,sizeof(rp_level));fclose(f);for(int i=0;i<326&&ok;i++)if(rp_level[i][0]>6||rp_level[i][1]>0x7fffffff)ok=0;if(!ok)memset(rp_level,0,sizeof(rp_level));}
int Port_RosterLevelRead(int cid,int*out){if(cid<0||cid>=326||!out)return 0;rp_level_load();out[0]=rp_level[cid][0];out[1]=rp_level[cid][1];return 1;}
int Port_RosterLevelWrite(int cid,int level,int exp){char path[1100],tmp[1100];uint32_t old[2],h[3];FILE*f;if(cid<0||cid>=326||level<0||level>6||exp<0)return 0;rp_level_load();memcpy(old,rp_level[cid],sizeof(old));rp_level[cid][0]=level;rp_level[cid][1]=exp;if(!rp_level_path(path,sizeof(path),".dat")||!rp_level_path(tmp,sizeof(tmp),".tmp")||!(f=fopen(tmp,"wb")))goto fail;h[0]=0x314c5242;h[1]=sizeof(rp_level);h[2]=rp_hash(rp_level,sizeof(rp_level));int ok=fwrite(h,sizeof(h),1,f)==1&&fwrite(rp_level,1,sizeof(rp_level),f)==sizeof(rp_level);if(fclose(f))ok=0;if(!ok)goto fail;
#ifdef _WIN32
 if(!MoveFileExA(tmp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))goto fail;
#else
 if(rename(tmp,path))goto fail;
#endif
 return 1;fail:memcpy(rp_level[cid],old,sizeof(old));return 0;}
